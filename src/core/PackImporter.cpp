#include "core/PackImporter.h"

#include "core/ZipReader.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

namespace {

// CurseForge packs rely on the API to turn project/file ids into CDN URLs.
// Only used when CURSEFORGE_API_KEY is present; the key never leaves the machine
// except towards api.curseforge.com.
QString resolveCurseForgeUrl(const QString &projectId, const QString &fileId)
{
    const QString key = qEnvironmentVariable("CURSEFORGE_API_KEY");
    if (key.isEmpty() || projectId.isEmpty() || fileId.isEmpty())
        return {};
    QNetworkAccessManager nam;
    QNetworkRequest request(QUrl(QStringLiteral("https://api.curseforge.com/v1/mods/%1/files/%2")
                                         .arg(projectId, fileId)));
    request.setRawHeader("x-api-key", key.toUtf8());
    request.setRawHeader("Accept", "application/json");
    QNetworkReply *reply = nam.get(request);
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(15000);
    if (!reply->isFinished())
        loop.exec();
    QString url;
    if (reply->isFinished() && reply->error() == QNetworkReply::NoError) {
        const QJsonObject data = QJsonDocument::fromJson(reply->readAll()).object()
                                         .value(QStringLiteral("data")).toObject();
        url = data.value(QStringLiteral("downloadUrl")).toString();
    } else {
        qWarning("CurseForge lookup failed for %s/%s: %s", qPrintable(projectId),
                 qPrintable(fileId), qPrintable(reply->errorString()));
    }
    reply->deleteLater();
    return url;
}

QString kindForPath(const QString &path)
{
    if (path.startsWith(QLatin1String("mods/")))
        return QStringLiteral("mod");
    if (path.startsWith(QLatin1String("config/")) || path.startsWith(QLatin1String("defaultconfigs/"))
        || path.startsWith(QLatin1String("kubejs/")) || path.startsWith(QLatin1String("scripts/")))
        return QStringLiteral("config");
    if (path.startsWith(QLatin1String("resourcepacks/")))
        return QStringLiteral("resource");
    if (path.startsWith(QLatin1String("shaderpacks/")))
        return QStringLiteral("shader");
    if (path.startsWith(QLatin1String("datapacks/")))
        return QStringLiteral("script");
    return QStringLiteral("config");
}

// "fabric-loader-0.16.9", "forge-47.2.0", "neoforge-21.1.80", "quilt-..."
void applyLoaderId(Pack *pack, const QString &id)
{
    QString type;
    QString version;
    if (id.startsWith(QLatin1String("fabric-loader-"))) {
        type = QStringLiteral("fabric");
        version = id.mid(14);
    } else if (id.startsWith(QLatin1String("quilt-loader-"))) {
        type = QStringLiteral("quilt");
        version = id.mid(13);
    } else if (id.startsWith(QLatin1String("neoforge-"))) {
        type = QStringLiteral("neoforge");
        version = id.mid(9);
    } else if (id.startsWith(QLatin1String("forge-"))) {
        type = QStringLiteral("forge");
        version = id.mid(6);
    } else if (id.startsWith(QLatin1String("fabric"))) {
        type = QStringLiteral("fabric");
    } else if (id.startsWith(QLatin1String("quilt"))) {
        type = QStringLiteral("quilt");
    } else if (id.startsWith(QLatin1String("neoforge"))) {
        type = QStringLiteral("neoforge");
    } else if (id.startsWith(QLatin1String("forge"))) {
        type = QStringLiteral("forge");
    }
    if (!type.isEmpty()) {
        pack->loaderType = type;
        pack->loaderVersion = version;
    }
}

bool boolish(const QJsonValue &value, bool fallback)
{
    if (value.isBool())
        return value.toBool();
    if (value.isDouble())
        return value.toInt() != 0;
    return fallback;
}

void applyEnvironment(PackFile *file, const QJsonObject &environment, ImportResult *result)
{
    if (environment.isEmpty())
        return;
    const QString client = environment.value(QStringLiteral("client")).toString();
    const QString server = environment.value(QStringLiteral("server")).toString();
    file->clientSide = client != QLatin1String("unsupported");
    file->serverSide = server != QLatin1String("unsupported");
    if (client == QLatin1String("optional") || server == QLatin1String("optional"))
        file->optional = true;
    if (!file->clientSide)
        result->unsupported.append(file->path + QStringLiteral("（仅服务端）"));
}

QString firstDownload(const QJsonArray &downloads)
{
    for (const QJsonValue &value : downloads) {
        const QString url = value.toString();
        if (!url.startsWith(QLatin1String("https://")) && !url.startsWith(QLatin1String("http://")))
            continue;
        // FTB's API returns unescaped spaces in paths (e.g. "FTB Academy-1.1.0").
        const QUrl parsed(url);
        return parsed.isValid() ? parsed.toString(QUrl::FullyEncoded) : url;
    }
    return {};
}

QString hashByAlgo(const QJsonValue &hashes, const QString &algo, const QString &numericAlgo)
{
    if (hashes.isObject()) {
        const QJsonObject object = hashes.toObject();
        const QJsonValue direct = object.value(algo);
        if (direct.isString())
            return direct.toString();
        return {};
    }
    for (const QJsonValue &value : hashes.toArray()) {
        const QJsonObject hash = value.toObject();
        const QString candidate = hash.value(QStringLiteral("algo")).toString();
        if (candidate == algo || (hash.value(QStringLiteral("algo")).isDouble()
                                  && QString::number(hash.value(QStringLiteral("algo")).toInt())
                                         == numericAlgo))
            return hash.value(QStringLiteral("value")).toString();
    }
    return {};
}

QString sha1FromHashes(const QJsonValue &hashes)
{
    return hashByAlgo(hashes, QStringLiteral("sha1"), QStringLiteral("1"));
}

QString sha256FromHashes(const QJsonValue &hashes)
{
    return hashByAlgo(hashes, QStringLiteral("sha256"), QStringLiteral("3"));
}

QString sha512FromHashes(const QJsonValue &hashes)
{
    return hashByAlgo(hashes, QStringLiteral("sha512"), QStringLiteral("2"));
}

QString sha1FromCurseHashes(const QJsonArray &hashes)
{
    for (const QJsonValue &value : hashes) {
        const QJsonObject hash = value.toObject();
        if (hash.value(QStringLiteral("algo")).toInt() == 1)
            return hash.value(QStringLiteral("value")).toString();
    }
    return {};
}

bool collectOverrides(ZipReader *zip, const QString &prefix, const QString &staging, Pack *pack,
                      ImportResult *result)
{
    QStringList extracted;
    QString error;
    if (!zip->extractPrefix(prefix, staging, &extracted, &error, true)) {
        result->error = error;
        return false;
    }
    result->overrideCount += extracted.size();
    pack->overrides.append(extracted);
    pack->overrides.removeDuplicates();
    return true;
}

} // namespace

QString importStagingDir(const QString &archivePath)
{
    const QFileInfo info(archivePath);
    const QByteArray fingerprint = (info.absoluteFilePath() + QLatin1Char('|')
                                    + QString::number(info.size()) + QLatin1Char('|')
                                    + QString::number(info.lastModified().toSecsSinceEpoch()))
                                           .toUtf8();
    const QString digest = QString::fromLatin1(
        QCryptographicHash::hash(fingerprint, QCryptographicHash::Sha1).toHex().left(16));
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return base + QStringLiteral("/imports/") + digest;
}

ImportResult importPackArchive(const QString &archivePath)
{
    ImportResult result;
    const QFileInfo info(archivePath);
    if (!info.isFile() || !info.isReadable()) {
        result.error = QStringLiteral("找不到压缩包：%1").arg(archivePath);
        return result;
    }

    ZipReader zip(archivePath);
    QString error;
    if (!zip.open(&error)) {
        result.error = error;
        return result;
    }

    const QString staging = importStagingDir(archivePath);
    if (!QDir().mkpath(staging)) {
        result.error = QStringLiteral("无法创建导入目录：%1").arg(staging);
        return result;
    }

    QJsonObject manifest;
    if (zip.contains(QStringLiteral("modrinth.index.json"))) {
        result.format = QStringLiteral("modrinth");
        if (!zip.readJson(QStringLiteral("modrinth.index.json"), &manifest, &error)) {
            result.error = error;
            return result;
        }
    } else if (zip.contains(QStringLiteral("manifest.json"))) {
        if (!zip.readJson(QStringLiteral("manifest.json"), &manifest, &error)) {
            result.error = error;
            return result;
        }
        if (manifest.contains(QStringLiteral("targets")))
            result.format = QStringLiteral("ftb");
        else
            result.format = QStringLiteral("curseforge");
    } else {
        result.error = QStringLiteral("不是受支持的整合包：缺少 modrinth.index.json 或 manifest.json");
        return result;
    }

    Pack pack;
    pack.target = staging;
    pack.overridesDir = staging;
    pack.generator = result.format;

    if (result.format == QLatin1String("modrinth")) {
        pack.format = manifest.value(QStringLiteral("formatVersion")).toInt(1);
        pack.name = manifest.value(QStringLiteral("name")).toString(info.completeBaseName());
        pack.versionId = manifest.value(QStringLiteral("versionId")).toString();
        pack.description = manifest.value(QStringLiteral("summary")).toString();
        const QJsonObject dependencies = manifest.value(QStringLiteral("dependencies")).toObject();
        pack.minecraftVersion = dependencies.value(QStringLiteral("minecraft")).toString();
        for (auto it = dependencies.begin(); it != dependencies.end(); ++it) {
            if (it.key() != QLatin1String("minecraft"))
                applyLoaderId(&pack, it.key() + QLatin1Char('-') + it.value().toString());
        }

        for (const QJsonValue &value : manifest.value(QStringLiteral("files")).toArray()) {
            const QJsonObject object = value.toObject();
            PackFile file;
            file.path = object.value(QStringLiteral("path")).toString();
            file.size = qint64(object.value(QStringLiteral("fileSize")).toDouble());
            file.sha1 = sha1FromHashes(object.value(QStringLiteral("hashes")));
            file.sha256 = sha256FromHashes(object.value(QStringLiteral("hashes")));
            file.sha512 = sha512FromHashes(object.value(QStringLiteral("hashes")));
            file.url = firstDownload(object.value(QStringLiteral("downloads")).toArray());
            file.kind = kindForPath(file.path);
            applyEnvironment(&file, object.value(QStringLiteral("env")).toObject(), &result);
            if (!isSafeRelativePath(file.path)) {
                result.unsupported.append(QStringLiteral("%1（不安全路径）").arg(file.path));
                continue;
            }
            if (file.url.isEmpty()) {
                result.unresolved.append(file.path);
                continue;
            }
            pack.files.append(file);
        }

        if (!collectOverrides(&zip, QStringLiteral("overrides/"), staging, &pack, &result))
            return result;
        if (!collectOverrides(&zip, QStringLiteral("client-overrides/"), staging, &pack, &result))
            return result;
    } else if (result.format == QLatin1String("curseforge")) {
        pack.format = 1;
        pack.name = manifest.value(QStringLiteral("name")).toString(info.completeBaseName());
        pack.versionId = manifest.value(QStringLiteral("version")).toString();
        pack.description = manifest.value(QStringLiteral("author")).toString();
        const QJsonObject minecraft = manifest.value(QStringLiteral("minecraft")).toObject();
        pack.minecraftVersion = minecraft.value(QStringLiteral("version")).toString();
        const QJsonArray modLoaders = minecraft.value(QStringLiteral("modLoaders")).toArray();
        for (const QJsonValue &value : modLoaders) {
            const QJsonObject loader = value.toObject();
            if (boolish(loader.value(QStringLiteral("primary")), modLoaders.size() == 1)) {
                applyLoaderId(&pack, loader.value(QStringLiteral("id")).toString());
                break;
            }
        }

        for (const QJsonValue &value : manifest.value(QStringLiteral("files")).toArray()) {
            const QJsonObject object = value.toObject();
            PackFile file;
            file.path = object.value(QStringLiteral("path")).toString();
            file.projectId = QString::number(qint64(object.value(QStringLiteral("projectID")).toDouble()));
            file.fileId = QString::number(qint64(object.value(QStringLiteral("fileID")).toDouble()));
            file.url = object.value(QStringLiteral("url")).toString();
            file.sha1 = sha1FromCurseHashes(object.value(QStringLiteral("hashes")).toArray());
            file.kind = kindForPath(file.path);
            file.optional = !boolish(object.value(QStringLiteral("required")), true);
            if (!isSafeRelativePath(file.path)) {
                result.unsupported.append(QStringLiteral("%1（不安全路径）").arg(file.path));
                continue;
            }
            if (file.url.isEmpty())
                file.url = resolveCurseForgeUrl(file.projectId, file.fileId);
            if (file.url.isEmpty()) {
                result.unresolved.append(file.path
                                         + QStringLiteral("（CurseForge 项目 %1 / 文件 %2）")
                                               .arg(file.projectId, file.fileId));
                continue;
            }
            pack.files.append(file);
        }

        if (!collectOverrides(&zip, QStringLiteral("overrides/"), staging, &pack, &result))
            return result;
    } else { // ftb
        pack.format = 1;
        pack.name = manifest.value(QStringLiteral("name")).toString(info.completeBaseName());
        pack.versionId = manifest.value(QStringLiteral("version")).toString();
        pack.description = manifest.value(QStringLiteral("author")).toString();
        for (const QJsonValue &value : manifest.value(QStringLiteral("targets")).toArray()) {
            const QJsonObject target = value.toObject();
            const QString type = target.value(QStringLiteral("type")).toString();
            if (type == QLatin1String("game")) {
                pack.minecraftVersion = target.value(QStringLiteral("version")).toString();
            } else if (type == QLatin1String("modloader")) {
                applyLoaderId(&pack, target.value(QStringLiteral("name")).toString()
                                       + QLatin1Char('-')
                                       + target.value(QStringLiteral("version")).toString());
            }
        }

        for (const QJsonValue &value : manifest.value(QStringLiteral("files")).toArray()) {
            const QJsonObject object = value.toObject();
            PackFile file;
            file.path = object.value(QStringLiteral("path")).toString();
            file.size = qint64(object.value(QStringLiteral("fileSize")).toDouble());
            file.url = firstDownload(object.value(QStringLiteral("downloads")).toArray());
            file.sha1 = sha1FromHashes(object.value(QStringLiteral("hashes")));
            file.sha256 = sha256FromHashes(object.value(QStringLiteral("hashes")));
            file.sha512 = sha512FromHashes(object.value(QStringLiteral("hashes")));
            file.kind = kindForPath(file.path);
            applyEnvironment(&file, object.value(QStringLiteral("env")).toObject(), &result);
            if (!isSafeRelativePath(file.path)) {
                result.unsupported.append(QStringLiteral("%1（不安全路径）").arg(file.path));
                continue;
            }
            if (file.url.isEmpty()) {
                result.unresolved.append(file.path);
                continue;
            }
            pack.files.append(file);
        }

        if (!collectOverrides(&zip, QStringLiteral("overrides/"), staging, &pack, &result))
            return result;
    }

    if (pack.loaderType.isEmpty())
        pack.loaderType = QStringLiteral("vanilla");
    if (pack.name.isEmpty())
        pack.name = info.completeBaseName();

    // A human-readable snapshot lets `bmcd --pack <staging>/import.json`
    // continue from the extracted tree without re-reading the archive.
    QFile snapshot(QDir(staging).filePath(QStringLiteral("import.json")));
    if (snapshot.open(QIODevice::WriteOnly | QIODevice::Truncate))
        snapshot.write(QJsonDocument(pack.toJson()).toJson(QJsonDocument::Indented));

    result.pack = pack;
    result.stagingDir = staging;
    result.ok = true;
    return result;
}
