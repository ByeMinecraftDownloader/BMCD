#include "ProfileMerger.h"

#include "platform/FileOps.h"
#include "platform/Paths.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

namespace {

QString cleanSegment(const QString &text, const QString &fallback)
{
    QString result;
    for (const QChar ch : text.trimmed()) {
        if (ch.isLetterOrNumber() || ch == QLatin1Char('.') || ch == QLatin1Char('_')
            || ch == QLatin1Char('+') || ch == QLatin1Char('-')) {
            result.append(ch);
        } else {
            result.append(QLatin1Char('-'));
        }
    }
    while (result.contains(QLatin1String("--")))
        result.replace(QLatin1String("--"), QLatin1String("-"));
    while (result.contains(QLatin1String("..")))
        result.replace(QLatin1String(".."), QLatin1String("."));
    while (result.startsWith(QLatin1Char('-')) || result.startsWith(QLatin1Char('.')))
        result.remove(0, 1);
    while (result.endsWith(QLatin1Char('-')) || result.endsWith(QLatin1Char('.')))
        result.chop(1);
    return result.isEmpty() ? fallback : result;
}

QString loaderLabel(const QString &loaderType)
{
    const QString loader = loaderType.toLower();
    if (loader == QLatin1String("fabric"))
        return QStringLiteral("Fabric");
    if (loader == QLatin1String("quilt"))
        return QStringLiteral("Quilt");
    if (loader == QLatin1String("forge"))
        return QStringLiteral("Forge");
    if (loader == QLatin1String("neoforge"))
        return QStringLiteral("NeoForge");
    return QStringLiteral("Vanilla");
}

QString timestamp()
{
    return QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyy-MM-dd'T'HH:mm:ss'Z'"));
}

bool validVersionId(const QString &id)
{
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._+-]*$"));
    return pattern.match(id).hasMatch();
}

ProfileMergeResult failure(const QString &message)
{
    ProfileMergeResult result;
    result.error = message;
    return result;
}

} // namespace

QString ProfileMerger::profileId(const QString &minecraftVersion,
                                 const QString &loaderType,
                                 const QString &loaderVersion)
{
    const QString mc = cleanSegment(minecraftVersion, QStringLiteral("unknown"));
    const QString loader = cleanSegment(loaderType.toLower(), QStringLiteral("vanilla"));
    const QString version = cleanSegment(loaderVersion, QStringLiteral("unknown"));
    return QStringLiteral("bmcd-%1-%2-%3").arg(mc, loader, version);
}

QString ProfileMerger::defaultName(const QString &minecraftVersion,
                                   const QString &loaderType)
{
    const QString mc = minecraftVersion.trimmed().isEmpty()
            ? QStringLiteral("Minecraft") : minecraftVersion.trimmed();
    const QString loader = loaderType.toLower();
    if (loader.isEmpty() || loader == QLatin1String("vanilla"))
        return QStringLiteral("BMCD %1").arg(mc);
    return QStringLiteral("BMCD %1 %2").arg(loaderLabel(loader), mc);
}

ProfileMergeResult ProfileMerger::merge(const ProfileSpec &spec)
{
    ProfileMergeResult result;
    if (spec.gameDir.trimmed().isEmpty() || !QDir::isAbsolutePath(spec.gameDir.trimmed()))
        return failure(QStringLiteral("游戏目录必须是绝对路径"));
    if (spec.lastVersionId.trimmed().isEmpty())
        return failure(QStringLiteral("缺少 lastVersionId，无法创建启动配置"));
    if (!validVersionId(spec.lastVersionId.trimmed()))
        return failure(QStringLiteral("lastVersionId 含有非法字符"));

    const QString gameDir = QDir::cleanPath(spec.gameDir.trimmed());
    const QString path = QDir(gameDir).filePath(QStringLiteral("launcher_profiles.json"));
    const QString backupPath = path + QStringLiteral(".bmcd-backup");
    const QString id = profileId(spec.minecraftVersion, spec.loaderType, spec.loaderVersion);
    if (id.size() > 128)
        return failure(QStringLiteral("profile id 过长"));

    QJsonObject root;
    QByteArray previous;
    bool hadFile = false;
    QFile file(path);
    if (file.exists()) {
        hadFile = true;
        if (!file.open(QIODevice::ReadOnly))
            return failure(QStringLiteral("无法读取 %1：%2").arg(path, file.errorString()));
        previous = file.readAll();
        file.close();

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(previous, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject())
            return failure(QStringLiteral("launcher_profiles.json 不是有效 JSON：%1")
                                   .arg(parseError.errorString()));
        root = document.object();
    }

    QJsonValue profilesValue = root.value(QStringLiteral("profiles"));
    if (!profilesValue.isUndefined() && !profilesValue.isObject())
        return failure(QStringLiteral("launcher_profiles.json 的 profiles 字段不是对象"));
    QJsonObject profiles = profilesValue.toObject();

    const QJsonObject existing = profiles.value(id).toObject();
    const QString lastVersionId = spec.lastVersionId.trimmed();
    QJsonObject profile = existing;
    bool created = existing.isEmpty();
    bool changed = false;

    if (created) {
        profile.insert(QStringLiteral("name"),
                       spec.name.trimmed().isEmpty()
                       ? defaultName(spec.minecraftVersion, spec.loaderType)
                       : spec.name.trimmed());
        profile.insert(QStringLiteral("type"), QStringLiteral("custom"));
        profile.insert(QStringLiteral("created"), timestamp());
        profile.insert(QStringLiteral("lastUsed"), timestamp());
        profile.insert(QStringLiteral("lastVersionId"), lastVersionId);
        profile.insert(QStringLiteral("gameDir"), gameDir);
        if (!spec.javaArgs.trimmed().isEmpty())
            profile.insert(QStringLiteral("javaArgs"), spec.javaArgs.trimmed());
        changed = true;
    } else {
        // Repeated installs must not rewrite user edits (name, icon, gameDir,
        // javaArgs, ...). Only the version this profile points at moves.
        if (profile.value(QStringLiteral("lastVersionId")).toString() != lastVersionId) {
            profile.insert(QStringLiteral("lastVersionId"), lastVersionId);
            changed = true;
        }
        if (profile.value(QStringLiteral("name")).toString().isEmpty()) {
            profile.insert(QStringLiteral("name"),
                           spec.name.trimmed().isEmpty()
                           ? defaultName(spec.minecraftVersion, spec.loaderType)
                           : spec.name.trimmed());
            changed = true;
        }
    }

    result.ok = true;
    result.created = created;
    result.changed = changed;
    result.id = id;
    result.name = profile.value(QStringLiteral("name")).toString();
    result.path = path;

    if (!changed) {
        result.backupPath = QFileInfo::exists(backupPath) ? backupPath : QString();
        return result;
    }

    profiles.insert(id, profile);
    root.insert(QStringLiteral("profiles"), profiles);
    if (!root.contains(QStringLiteral("version")))
        root.insert(QStringLiteral("version"), 3);

    if (hadFile) {
        if (!platform::writeFileAtomic(backupPath, previous))
            return failure(QStringLiteral("无法备份 launcher_profiles.json"));
        result.backupPath = backupPath;
    }

    QByteArray output = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (!output.endsWith('\n'))
        output.append('\n');
    if (!platform::writeFileAtomic(path, output))
        return failure(QStringLiteral("无法写入 %1").arg(path));

    return result;
}
