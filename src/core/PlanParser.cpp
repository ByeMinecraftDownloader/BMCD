#include "PlanParser.h"

#include <QCryptographicHash>

#include <QJsonArray>
#include <QJsonValue>
#include <QSet>
#include <QSysInfo>

namespace {

QString readString(const QJsonObject &obj, const char *key)
{
    return obj.value(QLatin1String(key)).toString();
}

qint64 readSize(const QJsonObject &obj)
{
    const QJsonValue v = obj.value(QLatin1String("size"));
    return v.isDouble() ? static_cast<qint64>(v.toDouble()) : 0;
}

bool osMatches(const QJsonValue &osValue)
{
    if (!osValue.isObject())
        return true;

    const QJsonObject os = osValue.toObject();
    if (os.contains(QLatin1String("name"))) {
        if (readString(os, "name") != PlanParser::hostOsName())
            return false;
    }
    if (os.contains(QLatin1String("arch"))) {
        const QString arch = readString(os, "arch");
        if (arch == QLatin1String("x86"))
            return false; // 32-bit only artifact, we are 64-bit
#if defined(Q_PROCESSOR_ARM_64)
        if (arch != QLatin1String("arm64"))
            return false;
#else
        if (arch == QLatin1String("arm64"))
            return false;
#endif
    }
    return true;
}

// Minecraft rule evaluation: no rules means allowed, otherwise the last
// matching rule wins and the default is "deny".
bool rulesAllow(const QJsonValue &rulesValue)
{
    if (!rulesValue.isArray())
        return true;

    const QJsonArray rules = rulesValue.toArray();
    if (rules.isEmpty())
        return true;

    bool allowed = false;
    for (const QJsonValue &ruleValue : rules) {
        const QJsonObject rule = ruleValue.toObject();
        if (!osMatches(rule.value(QLatin1String("os"))))
            continue;
        // Rules guarded by game features (demo mode, custom resolution) never
        // apply to a plain download plan.
        if (rule.contains(QLatin1String("features")))
            continue;
        allowed = readString(rule, "action") == QLatin1String("allow");
    }
    return allowed;
}

void appendFile(TaskPlan *plan, QSet<QString> *seen, PlanFile file)
{
    if (file.path.isEmpty() || file.url.isEmpty())
        return;
    if (seen->contains(file.path))
        return;
    seen->insert(file.path);
    plan->totalBytes += file.size;
    plan->files.append(std::move(file));
}

QString nativeClassifier(const QJsonObject &library)
{
    const QJsonValue nativesValue = library.value(QLatin1String("natives"));
    if (!nativesValue.isObject())
        return {};
    const QJsonObject natives = nativesValue.toObject();
    const QString osName = PlanParser::hostOsName();
    const QString key = osName == QLatin1String("windows") ? QStringLiteral("windows")
                      : osName == QLatin1String("osx")     ? QStringLiteral("osx")
                                                           : QStringLiteral("linux");
    QString classifier = readString(natives, key.toUtf8().constData());
    if (classifier.isEmpty())
        return {};
    classifier.replace(QLatin1String("${arch}"), QSysInfo::currentCpuArchitecture().startsWith(QLatin1String("arm")) ? QStringLiteral("arm64") : QStringLiteral("64"));
    return classifier;
}

} // namespace

QString PlanParser::hostOsName()
{
#if defined(Q_OS_WIN)
    return QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("osx");
#else
    return QStringLiteral("linux");
#endif
}

QString PlanParser::assetUrl(const QString &hash)
{
    return QStringLiteral("https://resources.download.minecraft.net/") + hash.left(2) + QLatin1Char('/') + hash;
}

QString PlanParser::assetPath(const QString &hash)
{
    return QStringLiteral("assets/objects/") + hash.left(2) + QLatin1Char('/') + hash;
}

bool PlanParser::build(const QJsonObject &versionJson,
                       const QByteArray &rawVersionJson,
                       const QString &versionJsonUrl,
                       const QJsonObject &assetIndexJson,
                       TaskPlan *plan,
                       QString *error)
{
    if (!build(versionJson, assetIndexJson, plan, error))
        return false;
    if (rawVersionJson.isEmpty() || versionJsonUrl.isEmpty())
        return true;

    PlanFile file;
    file.path = QStringLiteral("versions/%1/%1.json").arg(plan->id);
    file.url = versionJsonUrl;
    file.sha1 = QString::fromLatin1(QCryptographicHash::hash(rawVersionJson,
                                                             QCryptographicHash::Sha1).toHex());
    file.size = rawVersionJson.size();
    file.kind = QStringLiteral("version-json");

    plan->files.prepend(file);
    plan->totalBytes += file.size;
    return true;
}

bool PlanParser::build(const QJsonObject &versionJson,
                       const QJsonObject &assetIndexJson,
                       TaskPlan *plan,
                       QString *error)
{
    *plan = TaskPlan{};

    const QString inheritsFrom = readString(versionJson, "inheritsFrom");
    if (!inheritsFrom.isEmpty()) {
        *error = QStringLiteral("暂不支持继承版本（inheritsFrom=%1）").arg(inheritsFrom);
        return false;
    }

    plan->id = readString(versionJson, "id");
    plan->type = readString(versionJson, "type");
    plan->releaseTime = QDateTime::fromString(readString(versionJson, "releaseTime"), Qt::ISODate);

    const QJsonObject javaVersion = versionJson.value(QLatin1String("javaVersion")).toObject();
    plan->javaMajor = javaVersion.value(QLatin1String("majorVersion")).toInt();

    if (plan->id.isEmpty()) {
        *error = QStringLiteral("版本 json 缺少 id 字段");
        return false;
    }

    QSet<QString> seen;

    // client jar
    const QJsonObject downloads = versionJson.value(QLatin1String("downloads")).toObject();
    const QJsonObject client = downloads.value(QLatin1String("client")).toObject();
    if (!client.isEmpty()) {
        PlanFile file;
        file.path = QStringLiteral("versions/%1/%1.jar").arg(plan->id);
        file.url = readString(client, "url");
        file.sha1 = readString(client, "sha1");
        file.size = readSize(client);
        file.kind = QStringLiteral("client");
        appendFile(plan, &seen, file);
    }

    // asset index file itself
    const QJsonObject assetIndex = versionJson.value(QLatin1String("assetIndex")).toObject();
    plan->assetIndexId = readString(assetIndex, "id");
    if (!assetIndex.isEmpty()) {
        PlanFile file;
        file.path = QStringLiteral("assets/indexes/%1.json").arg(plan->assetIndexId.isEmpty() ? plan->id : plan->assetIndexId);
        file.url = readString(assetIndex, "url");
        file.sha1 = readString(assetIndex, "sha1");
        file.size = readSize(assetIndex);
        file.kind = QStringLiteral("asset-index");
        appendFile(plan, &seen, file);
    }

    // libraries (+ per-OS natives)
    const QJsonArray libraries = versionJson.value(QLatin1String("libraries")).toArray();
    for (const QJsonValue &libraryValue : libraries) {
        const QJsonObject library = libraryValue.toObject();
        if (!rulesAllow(library.value(QLatin1String("rules"))))
            continue;

        const QJsonObject libraryDownloads = library.value(QLatin1String("downloads")).toObject();

        const QJsonObject artifact = libraryDownloads.value(QLatin1String("artifact")).toObject();
        if (!artifact.isEmpty()) {
            PlanFile file;
            file.path = QStringLiteral("libraries/") + readString(artifact, "path");
            file.url = readString(artifact, "url");
            file.sha1 = readString(artifact, "sha1");
            file.size = readSize(artifact);
            file.kind = QStringLiteral("library");
            appendFile(plan, &seen, file);
        }

        const QString classifier = nativeClassifier(library);
        if (!classifier.isEmpty()) {
            const QJsonObject natives = libraryDownloads.value(QLatin1String("classifiers")).toObject();
            const QJsonObject native = natives.value(classifier).toObject();
            if (!native.isEmpty()) {
                PlanFile file;
                file.path = QStringLiteral("libraries/") + readString(native, "path");
                file.url = readString(native, "url");
                file.sha1 = readString(native, "sha1");
                file.size = readSize(native);
                file.kind = QStringLiteral("library");
                appendFile(plan, &seen, file);
            }
        }
    }

    // asset objects
    const QJsonObject objects = assetIndexJson.value(QLatin1String("objects")).toObject();
    for (auto it = objects.begin(); it != objects.end(); ++it) {
        const QJsonObject object = it.value().toObject();
        const QString hash = readString(object, "hash");
        if (hash.size() < 2)
            continue;

        PlanFile file;
        file.path = assetPath(hash);
        file.url = assetUrl(hash);
        file.sha1 = hash;
        file.size = readSize(object);
        file.kind = QStringLiteral("asset");
        const int before = plan->files.size();
        appendFile(plan, &seen, file);
        if (plan->files.size() != before)
            ++plan->assetCount;
    }

    if (plan->files.isEmpty()) {
        *error = QStringLiteral("版本 json 里没有可下载的内容");
        return false;
    }
    return true;
}
