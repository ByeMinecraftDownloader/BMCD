#include "platform/FileOps.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QThread>

#include <cstdio>

#ifdef Q_OS_WIN
#  include <qt_windows.h>
#endif

namespace platform {
namespace {

constexpr int kRetries = 8;
constexpr int kBackoffMs = 80;

bool moveRetrying(const QString &source, const QString &target)
{
    for (int attempt = 0; attempt < kRetries; ++attempt) {
#ifdef Q_OS_WIN
        const std::wstring sourceW = QDir::toNativeSeparators(source).toStdWString();
        const std::wstring targetW = QDir::toNativeSeparators(target).toStdWString();
        if (MoveFileExW(sourceW.c_str(), targetW.c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED))
            return true;
#else
        if (::rename(QFile::encodeName(source).constData(), QFile::encodeName(target).constData()) == 0)
            return true;
        // Cross-device rename fails with EXDEV: copy, then unlink.
        if (QFile::copy(source, target)) {
            QFile::remove(source);
            return true;
        }
#endif
        QThread::msleep(kBackoffMs * (attempt + 1));
    }
    return false;
}

bool copyRetrying(const QString &source, const QString &target)
{
    for (int attempt = 0; attempt < kRetries; ++attempt) {
        if (QFile::copy(source, target))
            return true;
        QThread::msleep(kBackoffMs * (attempt + 1));
    }
    return false;
}

} // namespace

bool replaceFile(const QString &source, const QString &target)
{
    if (!QFileInfo::exists(source))
        return false;
    QDir().mkpath(QFileInfo(target).absolutePath());
    if (moveRetrying(source, target))
        return true;
    return QFile::copy(source, target) && QFile::remove(source);
}

bool copyFile(const QString &source, const QString &target)
{
    if (!QFileInfo::exists(source))
        return false;
    QDir().mkpath(QFileInfo(target).absolutePath());
    return copyRetrying(source, target);
}

bool writeFileAtomic(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    if (file.write(data) != data.size()) {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}

bool removeIfExists(const QString &path)
{
    if (!QFileInfo::exists(path))
        return true;
    for (int attempt = 0; attempt < kRetries; ++attempt) {
        if (QFile::remove(path))
            return true;
        QThread::msleep(kBackoffMs * (attempt + 1));
    }
    return false;
}

QString nativePath(const QString &path)
{
#ifdef Q_OS_WIN
    if (path.size() >= 240 && QDir::isAbsolutePath(path) && !path.startsWith(QStringLiteral("\\\\?\\"))) {
        QString native = QDir::toNativeSeparators(path);
        if (native.startsWith(QStringLiteral("\\\\")))
            return QStringLiteral("\\\\?\\UNC\\") + native.mid(2);
        return QStringLiteral("\\\\?\\") + native;
    }
    return QDir::toNativeSeparators(path);
#else
    return path;
#endif
}

} // namespace platform
