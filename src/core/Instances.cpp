#include "core/Instances.h"

#include "platform/Paths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace {

void readMmcPack(const QString &instancePath, LocalInstance *instance)
{
    QFile file(QDir(instancePath).filePath(QStringLiteral("mmc-pack.json")));
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject())
        return;
    for (const QJsonValue &value : document.object().value(QStringLiteral("components")).toArray()) {
        const QJsonObject component = value.toObject();
        const QString uid = component.value(QStringLiteral("uid")).toString();
        const QString version = component.value(QStringLiteral("version")).toString();
        if (uid == QLatin1String("net.minecraft")) {
            instance->minecraftVersion = version;
        } else if (uid.contains(QLatin1String("fabric-loader"))) {
            instance->loaderType = QStringLiteral("fabric");
            instance->loaderVersion = version;
        } else if (uid.contains(QLatin1String("quilt-loader"))) {
            instance->loaderType = QStringLiteral("quilt");
            instance->loaderVersion = version;
        } else if (uid == QLatin1String("net.minecraftforge")) {
            instance->loaderType = QStringLiteral("forge");
            instance->loaderVersion = version;
        } else if (uid == QLatin1String("net.neoforged")) {
            instance->loaderType = QStringLiteral("neoforge");
            instance->loaderVersion = version;
        }
    }
    if (instance->loaderType.isEmpty())
        instance->loaderType = QStringLiteral("vanilla");
}

void inspectDirectory(const QDir &dir, QVector<LocalInstance> *out)
{
    LocalInstance instance;
    instance.name = dir.dirName();
    instance.path = dir.absolutePath();
    instance.source = QStringLiteral("directory");

    const bool hasMods = QFileInfo::exists(dir.filePath(QStringLiteral("mods")));
    const bool hasConfig = QFileInfo::exists(dir.filePath(QStringLiteral("config")));
    const bool hasMmc = QFileInfo::exists(dir.filePath(QStringLiteral("mmc-pack.json")));
    QString bmcdManifest = dir.filePath(QStringLiteral("bmcd-pack.json"));
    if (!QFileInfo::exists(bmcdManifest))
        bmcdManifest = dir.filePath(QStringLiteral(".bmcd-deployment.json"));
    const bool hasBmcd = QFileInfo::exists(bmcdManifest);
    if (!hasMods && !hasConfig && !hasMmc && !hasBmcd)
        return;

    QFile packFile(bmcdManifest);
    if (hasBmcd && packFile.open(QIODevice::ReadOnly)) {
        QJsonObject manifest = QJsonDocument::fromJson(packFile.readAll()).object();
        if (manifest.contains(QStringLiteral("pack")))
            manifest = manifest.value(QStringLiteral("pack")).toObject();
        const Pack pack = Pack::fromJson(manifest);
        if (!pack.name.isEmpty())
            instance.name = pack.name;
        instance.minecraftVersion = pack.minecraftVersion;
        instance.loaderType = pack.loaderType;
        instance.loaderVersion = pack.loaderVersion;
        instance.source = QStringLiteral("bmcd");
    } else if (hasMmc) {
        readMmcPack(dir.absolutePath(), &instance);
        instance.source = QStringLiteral("multimc");
        // MultiMC keeps the human name in instance.cfg.
        QFile cfg(dir.filePath(QStringLiteral("instance.cfg")));
        if (cfg.open(QIODevice::ReadOnly)) {
            const QString text = QString::fromUtf8(cfg.readAll());
            for (const QString &line : text.split(QLatin1Char('\n'))) {
                if (line.startsWith(QLatin1String("name=")) && line.size() > 5) {
                    instance.name = line.mid(5).trimmed();
                    break;
                }
            }
        }
    }

    const QDir mods(dir.filePath(QStringLiteral("mods")));
    if (mods.exists()) {
        const QFileInfoList jars = mods.entryInfoList({QStringLiteral("*.jar")}, QDir::Files);
        instance.modCount = int(jars.size());
    }
    out->append(instance);
}

} // namespace

QVector<LocalInstance> scanInstances(const QString &root)
{
    QVector<LocalInstance> result;
    const QDir base(root);
    if (!base.exists() || root.isEmpty())
        return result;

    // The root itself is an instance when it directly holds mods/ or mmc-pack.json.
    inspectDirectory(base, &result);

    const QFileInfoList entries = base.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable);
    for (const QFileInfo &entry : entries) {
        if (entry.fileName().startsWith(QLatin1Char('.')))
            continue;
        inspectDirectory(QDir(entry.absoluteFilePath()), &result);
    }
    return result;
}

QStringList defaultInstanceRoots()
{
    QStringList roots;
    const QString home = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    roots.append(platform::defaultGameDir());
    roots.append(home + QStringLiteral("/.local/share/multimc/instances"));
    roots.append(home + QStringLiteral("/.multimc/instances"));
    roots.append(home + QStringLiteral("/.local/share/PrismLauncher/instances"));
    roots.append(home + QStringLiteral("/.var/app/org.prismlauncher.PrismLauncher/instances"));
    roots.removeDuplicates();
    return roots;
}

QString sanitizedInstanceName(const QString &packName)
{
    QString name = packName;
    name.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|\\x00-\\x1f]")), QStringLiteral("-"));
    name = name.trimmed();
    while (name.endsWith(QLatin1Char('.')) || name.endsWith(QLatin1Char(' ')))
        name.chop(1);
    if (name.isEmpty())
        name = QStringLiteral("imported-pack");
    return name.left(64);
}
