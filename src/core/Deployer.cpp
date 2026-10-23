#include "core/Deployer.h"

#include "platform/FileOps.h"
#include "platform/Paths.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStorageInfo>

#include <algorithm>
#include <utility>

namespace {

QString digestOf(const QString &path, QCryptographicHash::Algorithm algorithm)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(algorithm);
    if (!hash.addData(&file))
        return {};
    return QString::fromLatin1(hash.result().toHex());
}

QString sha1Of(const QString &path)
{
    return digestOf(path, QCryptographicHash::Sha1);
}

bool matchesStrongestHash(const PackFile &file, const QString &path)
{
    if (!file.sha256.isEmpty())
        return digestOf(path, QCryptographicHash::Sha256) == file.sha256;
    if (!file.sha512.isEmpty())
        return digestOf(path, QCryptographicHash::Sha512) == file.sha512;
    if (!file.sha1.isEmpty())
        return digestOf(path, QCryptographicHash::Sha1) == file.sha1;
    return true;
}

QString uniqueStamp()
{
    return QString::number(QDateTime::currentSecsSinceEpoch());
}

} // namespace

QString sha1OfFile(const QString &path)
{
    return sha1Of(path);
}

VerifyReport verifyDeployment(const Pack &pack, const QString &targetDir)
{
    VerifyReport report;
    const QDir root(targetDir);
    for (const PackFile &file : pack.files) {
        const QString path = root.filePath(file.path);
        if (!QFileInfo::exists(path)) {
            ++report.missing;
            report.problems.append(QStringLiteral("缺少 %1").arg(file.path));
            continue;
        }
        const QString actual = sha1Of(path);
        if (!file.sha1.isEmpty() && actual != file.sha1) {
            ++report.corrupt;
            report.problems.append(QStringLiteral("校验失败 %1").arg(file.path));
        } else {
            ++report.present;
        }
    }
    for (const QString &relative : pack.overrides) {
        const QString path = root.filePath(relative);
        if (!QFileInfo::exists(path)) {
            ++report.missing;
            report.problems.append(QStringLiteral("缺少 %1").arg(relative));
            continue;
        }
        if (!pack.overridesDir.isEmpty()) {
            const QString expected = sha1Of(QDir(pack.overridesDir).filePath(relative));
            if (!expected.isEmpty() && sha1Of(path) != expected) {
                ++report.corrupt;
                report.problems.append(QStringLiteral("校验失败 %1").arg(relative));
                continue;
            }
        }
        ++report.present;
    }
    report.ok = report.problems.isEmpty();
    return report;
}

DeployReport deploy(const Pack &pack, const DeployOptions &options, const QStringList &confirmed)
{
    DeployReport report;
    report.stagingDir = options.targetDir + QStringLiteral("/.bmcd-staging-") + uniqueStamp();
    report.backupDir = options.targetDir + QStringLiteral("/.bmcd-backup-") + uniqueStamp();

    QString invalid;
    if (!pack.isValid(&invalid)) {
        report.error = invalid;
        return report;
    }
    if (options.targetDir.isEmpty()) {
        report.error = QStringLiteral("未指定目标目录");
        return report;
    }

    const QString sourceRoot = options.sourceRoot.isEmpty() ? pack.target : options.sourceRoot;
    if (sourceRoot.isEmpty() && !options.dryRun) {
        report.error = QStringLiteral("未指定来源目录");
        return report;
    }

    const QSet<QString> confirmedSet(confirmed.begin(), confirmed.end());

    // Imported packs keep their bundled files in a separate tree; hash them up
    // front so the same staging/backup/rollback path applies to both kinds.
    QVector<PackFile> overrides;
    QSet<QString> overridePaths;
    if (!pack.overrides.isEmpty()) {
        if (pack.overridesDir.isEmpty()) {
            report.error = QStringLiteral("导入包缺少 overrides 目录");
            return report;
        }
        for (const QString &relative : pack.overrides) {
            if (overridePaths.contains(relative)) {
                report.error = QStringLiteral("重复的 overrides 路径：%1").arg(relative);
                return report;
            }
            const QString source = QDir(pack.overridesDir).filePath(relative);
            const QFileInfo info(source);
            if (!info.isFile()) {
                ++report.failed;
                report.failures.append(QStringLiteral("整合包内置文件缺失 %1").arg(relative));
                continue;
            }
            PackFile file;
            file.path = relative;
            file.url = QStringLiteral("local:overrides");
            file.kind = QStringLiteral("config");
            file.size = info.size();
            file.sha1 = sha1Of(source);
            overrides.append(file);
            overridePaths.insert(relative);
        }
    }

    // 1. preflight: cache integrity, conflicts, disk space. Nothing is written yet.
    QVector<PackFile> todo;
    QHash<QString, QString> sourceOf;
    qint64 requiredBytes = 0;

    const auto preflight = [&](const PackFile &file, const QString &source) {
        const QFileInfo info(source);
        if (!info.isFile()) {
            ++report.failed;
            report.failures.append(QStringLiteral("缓存缺失 %1").arg(file.path));
            return;
        }
        if (info.size() != file.size || (!file.sha1.isEmpty() && sha1Of(source) != file.sha1)) {
            ++report.failed;
            report.failures.append(QStringLiteral("缓存校验失败 %1（重新下载该文件）").arg(file.path));
            return;
        }
        const QString destination = QDir(options.targetDir).filePath(file.path);
        if (QFileInfo::exists(destination)) {
            const bool identical = !file.sha1.isEmpty() && sha1Of(destination) == file.sha1;
            if (identical) {
                ++report.skipped;
                report.bytes += file.size;
                return;
            }
            report.conflicts.append(file.path);
            if (!options.overwrite) {
                ++report.failed;
                report.failures.append(QStringLiteral("目标已存在：%1").arg(file.path));
                return;
            }
        }
        todo.append(file);
        sourceOf.insert(file.path, source);
        requiredBytes += file.size;
    };

    for (const PackFile &file : pack.files) {
        if (!file.selected || !file.enabled)
            continue; // only the current recipe is downloaded, deployed and exported
        if (overridePaths.contains(file.path))
            continue; // the bundled override wins over the downloadable copy
        if (file.optional && !file.clientSide && !confirmedSet.contains(file.path))
            continue;
        if (file.optional && options.skipOptional && !confirmedSet.contains(file.path))
            continue;
        preflight(file, QDir(sourceRoot).filePath(file.path));
    }
    for (const PackFile &file : std::as_const(overrides)) {
        if (options.skipOptional && file.optional && !confirmedSet.contains(file.path))
            continue;
        preflight(file, QDir(pack.overridesDir).filePath(file.path));
    }

    if (report.failed > 0) {
        report.error = QStringLiteral("%1 个文件未通过预检").arg(report.failed);
        return report;
    }
    if (options.dryRun) {
        report.ok = true;
        report.error.clear();
        return report;
    }

    const QStorageInfo storage(options.targetDir);
    if (storage.isValid() && storage.bytesAvailable() >= 0
        && storage.bytesAvailable() < requiredBytes + (16LL << 20)) {
        report.error = QStringLiteral("空间不足：需要 %1 MB，可用 %2 MB")
                               .arg(requiredBytes / (1024 * 1024))
                               .arg(storage.bytesAvailable() / (1024 * 1024));
        return report;
    }

    // 2. stage: copy every file into a sibling directory on the same device.
    if (!QDir().mkpath(report.stagingDir)) {
        report.error = QStringLiteral("无法创建暂存目录");
        return report;
    }
    QStringList staged;
    int done = 0;
    for (const PackFile &file : std::as_const(todo)) {
        const QString stagedPath = QDir(report.stagingDir).filePath(file.path);
        QDir().mkpath(QFileInfo(stagedPath).absolutePath());
        if (!platform::copyFile(sourceOf.value(file.path), stagedPath)
            || !matchesStrongestHash(file, stagedPath)) {
            report.error = QStringLiteral("暂存失败：%1").arg(file.path);
            QDir(report.stagingDir).removeRecursively();
            return report;
        }
        staged.append(file.path);
        if (options.progress)
            options.progress(++done, int(todo.size()), file.path);
    }

    // 3. commit: back up replaced files, then move the staged tree into place.
    QDir().mkpath(report.backupDir);
    QStringList backedUp;
    bool commitFailed = false;
    for (const QString &relative : std::as_const(staged)) {
        const QString destination = QDir(options.targetDir).filePath(relative);
        const QString stagedPath = QDir(report.stagingDir).filePath(relative);
        if (QFileInfo::exists(destination)) {
            if (!options.backup) {
                commitFailed = true;
                report.failures.append(QStringLiteral("拒绝覆盖（未启用备份）：%1").arg(relative));
                break;
            }
            const QString backupPath = QDir(report.backupDir).filePath(relative);
            QDir().mkpath(QFileInfo(backupPath).absolutePath());
            if (!platform::replaceFile(destination, backupPath)) {
                commitFailed = true;
                report.failures.append(QStringLiteral("备份失败：%1").arg(relative));
                break;
            }
            backedUp.append(relative);
        }
        QDir().mkpath(QFileInfo(destination).absolutePath());
        if (!platform::replaceFile(stagedPath, destination)) {
            commitFailed = true;
            report.failures.append(QStringLiteral("写入失败：%1").arg(relative));
            break;
        }
        ++report.deployed;
        const auto it = std::find_if(todo.cbegin(), todo.cend(),
                                     [&relative](const PackFile &f) { return f.path == relative; });
        if (it != todo.cend())
            report.bytes += it->size;
    }

    if (commitFailed) {
        for (const QString &relative : std::as_const(backedUp)) {
            const QString backupPath = QDir(report.backupDir).filePath(relative);
            const QString destination = QDir(options.targetDir).filePath(relative);
            if (platform::replaceFile(backupPath, destination))
                report.rolledBack.append(relative);
        }
        QDir(report.stagingDir).removeRecursively();
        report.error = report.failures.value(0);
        return report;
    }

    // 4. receipt: make the deployment auditable and replayable.
    QJsonArray deployedFiles;
    for (const QString &relative : std::as_const(staged)) {
        const auto it = std::find_if(todo.cbegin(), todo.cend(),
                                     [&relative](const PackFile &f) { return f.path == relative; });
        if (it == todo.cend())
            continue;
        deployedFiles.append(QJsonObject{{QStringLiteral("path"), it->path},
                                        {QStringLiteral("sha1"), it->sha1},
                                        {QStringLiteral("size"), double(it->size)}});
    }
    const QJsonObject receipt{
        {QStringLiteral("format"), 1},
        {QStringLiteral("pack"), pack.toJson()},
        {QStringLiteral("deployedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        {QStringLiteral("files"), deployedFiles},
        {QStringLiteral("backupDir"), report.backupDir},
    };
    platform::writeFileAtomic(QDir(options.targetDir).filePath(QStringLiteral(".bmcd-deployment.json")),
                              QJsonDocument(receipt).toJson(QJsonDocument::Indented));

    QDir(report.stagingDir).removeRecursively();
    report.ok = true;
    return report;
}
