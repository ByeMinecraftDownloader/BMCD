#include "platform/Paths.h"

#include <QDir>
#include <QProcessEnvironment>
#include <QStandardPaths>

namespace platform {

QString defaultGameDir()
{
#if defined(Q_OS_WIN)
    const QString appData = QProcessEnvironment::systemEnvironment().value(QStringLiteral("APPDATA"));
    if (!appData.isEmpty())
        return appData + QStringLiteral("/.minecraft");
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/.minecraft");
#elif defined(Q_OS_MACOS)
    return QDir::homePath() + QStringLiteral("/Library/Application Support/minecraft");
#else
    return QDir::homePath() + QStringLiteral("/.minecraft");
#endif
}

QString cacheDir()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    return dir.isEmpty() ? QDir::homePath() + QStringLiteral("/.cache/bmcd") : dir;
}

QString downloadsDir()
{
    return cacheDir() + QStringLiteral("/downloads");
}

QString configDir()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    return dir.isEmpty() ? QDir::homePath() + QStringLiteral("/.config/bmcd") : dir;
}

} // namespace platform
