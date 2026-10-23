#include "LoaderInstaller.h"

#include "AppSettings.h"
#include "ProfileMerger.h"
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
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSysInfo>
#include <QXmlStreamReader>

namespace {

QString sha1Hex(const QByteArray &data)
{
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha1).toHex());
}

QString firstSha1(const QByteArray &body)
{
    static const QRegularExpression re(QStringLiteral("\\b[0-9a-fA-F]{40}\\b"));
    const QString text = QString::fromLatin1(body).trimmed();
    const auto match = re.match(text);
    return match.hasMatch() ? match.captured(0).toLower() : QString();
}

bool libraryAllowed(const QJsonObject &library)
{
    const QJsonArray rules = library.value(QStringLiteral("rules")).toArray();
    if (rules.isEmpty())
        return true;
    const QString os = QSysInfo::productType() == QStringLiteral("windows")
        ? QStringLiteral("windows")
        : (QSysInfo::productType() == QStringLiteral("osx") ? QStringLiteral("osx")
                                                            : QStringLiteral("linux"));
    const QString arch = QSysInfo::currentCpuArchitecture();
    auto matches = [&](const QJsonObject &rule) {
        const QJsonObject osRule = rule.value(QStringLiteral("os")).toObject();
        if (!osRule.isEmpty()) {
            const QString name = osRule.value(QStringLiteral("name")).toString();
            if (!name.isEmpty() && name != os)
                return false;
            const QString wantedArch = osRule.value(QStringLiteral("arch")).toString();
            if (!wantedArch.isEmpty()) {
                if (wantedArch == QStringLiteral("x86") && arch != QStringLiteral("i386")
                    && arch != QStringLiteral("x86"))
                    return false;
                if (wantedArch == QStringLiteral("x86_64") && arch != QStringLiteral("x86_64")
                    && arch != QStringLiteral("amd64"))
                    return false;
                if (wantedArch != QStringLiteral("x86") && wantedArch != QStringLiteral("x86_64")
                    && wantedArch != arch)
                    return false;
            }
            const QString version = osRule.value(QStringLiteral("version")).toString();
            if (!version.isEmpty() && QRegularExpression(version).match(QSysInfo::productVersion()).hasMatch() == false)
                return false;
        }
        const QJsonObject features = rule.value(QStringLiteral("features")).toObject();
        if (!features.isEmpty())
            return false; // no optional feature is enabled by this installer
        return true;
    };
    bool allow = false;
    for (const QJsonValue &value : rules) {
        const QJsonObject rule = value.toObject();
        if (matches(rule))
            allow = rule.value(QStringLiteral("action")).toString() == QStringLiteral("allow");
    }
    return allow;
}

QString mavenPath(const QString &name)
{
    QString value = name;
    const int at = value.indexOf(QLatin1Char('@'));
    if (at >= 0)
        value.truncate(at);
    const QStringList parts = value.split(QLatin1Char(':'));
    if (parts.size() < 3)
        return {};
    QString file = parts[1] + QLatin1Char('-') + parts[2];
    if (parts.size() >= 4 && !parts[3].isEmpty())
        file += QLatin1Char('-') + parts[3];
    file += QStringLiteral(".jar");
    QString group = parts[0];
    group.replace(QLatin1Char('.'), QLatin1Char('/'));
    return group + QLatin1Char('/') + parts[1] + QLatin1Char('/') + parts[2] + QLatin1Char('/')
        + file;
}

bool isSafeVersion(const QString &value)
{
    static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._+-]*$"));
    return re.match(value).hasMatch();
}

QString quoteArg(const QString &arg)
{
    if (!arg.contains(QLatin1Char(' ')) && !arg.contains(QLatin1Char('\t')))
        return arg;
    return QLatin1Char('"') + QString(arg).replace(QLatin1Char('"'), QStringLiteral("\\\"")) + QLatin1Char('"');
}

} // namespace

LoaderInstaller::LoaderInstaller(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
{
    m_process.setProcessChannelMode(platform::Process::MergedChannels);
    m_processTimeout.setSingleShot(true);
    m_processTimeout.setInterval(10 * 60 * 1000);
    connect(&m_processTimeout, &QTimer::timeout, this, [this] {
        if (m_process.state() != platform::Process::NotRunning) {
            writeLog(QStringLiteral("[timeout] installer did not finish in 10 minutes"));
            m_process.kill();
            m_process.waitForFinished(2000);
            fail(QStringLiteral("安装器超时（10 分钟）"));
        }
    });
    connect(&m_process, &platform::Process::readyRead, this, &LoaderInstaller::processOutput);
    connect(&m_process, QOverload<int, platform::Process::ExitStatus>::of(&platform::Process::finished),
            this, &LoaderInstaller::processFinished);
    connect(&m_process, &platform::Process::errorOccurred, this, [this](platform::Process::ProcessError error) {
        if (error == platform::Process::FailedToStart)
            fail(QStringLiteral("无法启动 Java 安装器：%1").arg(m_process.errorString()));
    });
    m_catalogTimeout.setSingleShot(true);
    m_catalogTimeout.setInterval(15000);
    connect(&m_catalogTimeout, &QTimer::timeout, this, [this] {
        if (!m_catalogLoading)
            return;
        abortCatalog();
        m_catalogLoading = false;
        m_catalogError = QStringLiteral("读取加载器版本超时，请手动填写版本号");
        emit catalogChanged();
    });
}

LoaderInstaller::~LoaderInstaller()
{
    m_generation++;
    abortPending();
    abortCatalog();
    m_processTimeout.stop();
    if (m_process.state() != platform::Process::NotRunning) {
        m_process.kill();
        m_process.waitForFinished(1000);
    }
}

bool LoaderInstaller::busy() const
{
    return m_phase != Phase::Idle && m_phase != Phase::Done && m_phase != Phase::Error;
}

QVariantList LoaderInstaller::parseLoaderVersions(const QString &loaderType,
                                                  const QString &minecraftVersion,
                                                  const QByteArray &body, QString *error)
{
    const QString type = loaderType.toLower().trimmed();
    const QString mc = minecraftVersion.trimmed();
    QVariantList result;
    auto add = [&result](const QString &version, bool stable) {
        if (version.isEmpty())
            return;
        for (const QVariant &existing : result) {
            if (existing.toMap().value(QStringLiteral("version")).toString() == version)
                return;
        }
        QVariantMap item;
        item.insert(QStringLiteral("version"), version);
        item.insert(QStringLiteral("stable"), stable);
        item.insert(QStringLiteral("label"), stable
                    ? QStringLiteral("%1 · stable").arg(version) : version);
        result.append(item);
    };

    if (type == QStringLiteral("fabric") || type == QStringLiteral("quilt")) {
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
            if (error)
                *error = QStringLiteral("官方加载器版本清单不是有效 JSON");
            return {};
        }
        for (const QJsonValue &value : document.array()) {
            const QJsonObject loader = value.toObject().value(QStringLiteral("loader")).toObject();
            add(loader.value(QStringLiteral("version")).toString(),
                loader.value(QStringLiteral("stable")).toBool());
        }
        if (result.isEmpty() && error)
            *error = QStringLiteral("官方没有为 Minecraft %1 提供 %2 版本").arg(mc, type);
        return result;
    }

    if (type == QStringLiteral("forge")) {
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            if (error)
                *error = QStringLiteral("Forge promotions 清单不是有效 JSON");
            return {};
        }
        const QJsonObject promos = document.object().value(QStringLiteral("promos")).toObject();
        const QString recommended = promos.value(mc + QStringLiteral("-recommended")).toString();
        const QString latest = promos.value(mc + QStringLiteral("-latest")).toString();
        add(recommended, true);
        add(latest, false);
        if (result.isEmpty() && error)
            *error = QStringLiteral("官方没有为 Minecraft %1 提供 Forge 推荐版本").arg(mc);
        return result;
    }

    if (type == QStringLiteral("neoforge")) {
        QString prefix;
        if (mc == QStringLiteral("1.20.1")) {
            prefix = QStringLiteral("47.");
        } else if (mc.startsWith(QStringLiteral("1.20."))) {
            prefix = QStringLiteral("20.") + mc.mid(5) + QLatin1Char('.');
        } else if (mc == QStringLiteral("1.21")) {
            prefix = QStringLiteral("21.0.");
        } else if (mc.startsWith(QStringLiteral("1.21."))) {
            prefix = QStringLiteral("21.") + mc.mid(5) + QLatin1Char('.');
        }
        QXmlStreamReader xml(body);
        QStringList versions;
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isStartElement() && xml.name() == QStringLiteral("version")) {
                const QString value = xml.readElementText().trimmed();
                if (!prefix.isEmpty() && value.startsWith(prefix))
                    versions.append(value);
            }
        }
        for (auto it = versions.crbegin(); it != versions.crend(); ++it)
            add(*it, false);
        if (result.isEmpty() && error) {
            *error = prefix.isEmpty()
                    ? QStringLiteral("无法从 NeoForge 版本号推断 Minecraft %1").arg(mc)
                    : QStringLiteral("官方没有为 Minecraft %1 提供 NeoForge 版本").arg(mc);
        }
        return result;
    }

    if (error)
        *error = QStringLiteral("不支持的加载器类型：%1").arg(loaderType);
    return {};
}

void LoaderInstaller::abortCatalog()
{
    ++m_catalogGeneration;
    m_catalogTimeout.stop();
    if (m_catalogReply) {
        m_catalogReply->disconnect(this);
        m_catalogReply->abort();
        m_catalogReply->deleteLater();
        m_catalogReply = nullptr;
    }
}

void LoaderInstaller::finishCatalog(const QByteArray &body)
{
    if (!m_catalogLoading)
        return;
    QString parseError;
    m_catalog = parseLoaderVersions(m_catalogType, m_catalogMinecraft, body, &parseError);
    m_catalogLoading = false;
    m_catalogError = parseError;
    emit catalogChanged();
}

void LoaderInstaller::fetchLoaderVersions(const QString &loaderType,
                                          const QString &minecraftVersion)
{
    abortCatalog();
    m_catalog.clear();
    m_catalogError.clear();
    const QString type = loaderType.toLower().trimmed();
    const QString mc = minecraftVersion.trimmed();
    if (!isSupportedLoader(type)) {
        m_catalogLoading = false;
        m_catalogError = QStringLiteral("不支持的加载器类型：%1").arg(loaderType);
        emit catalogChanged();
        return;
    }
    if (!isSafeVersion(mc)) {
        m_catalogLoading = false;
        m_catalogError = QStringLiteral("先选择一个 Minecraft 版本");
        emit catalogChanged();
        return;
    }

    QUrl url;
    if (type == QStringLiteral("fabric"))
        url = QUrl(QStringLiteral("https://meta.fabricmc.net/v2/versions/loader/") + mc);
    else if (type == QStringLiteral("quilt"))
        url = QUrl(QStringLiteral("https://meta.quiltmc.org/v3/versions/loader/") + mc);
    else if (type == QStringLiteral("forge"))
        url = QUrl(QStringLiteral("https://files.minecraftforge.net/net/minecraftforge/forge/promotions_slim.json"));
    else if (type == QStringLiteral("neoforge"))
        url = QUrl(QStringLiteral("https://maven.neoforged.net/releases/net/neoforged/neoforge/maven-metadata.xml"));
    if (!isAllowedUrl(url)) {
        m_catalogLoading = false;
        m_catalogError = QStringLiteral("加载器版本来源不在官方 HTTPS 白名单内");
        emit catalogChanged();
        return;
    }

    // Keep catalogue fields separate: an install may start while this reply is
    // in flight, and finishCatalog() must not parse against the install request.
    m_catalogType = type;
    m_catalogMinecraft = mc;
    m_catalogLoading = true;
    emit catalogChanged();
    if (!m_catalogNetwork)
        m_catalogNetwork = new QNetworkAccessManager(this);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("BMCD/1.0"));
    QNetworkReply *reply = m_catalogNetwork->get(request);
    m_catalogReply = reply;
    const int generation = ++m_catalogGeneration;
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation]() {
        if (generation != m_catalogGeneration || reply != m_catalogReply)
            return;
        m_catalogReply = nullptr;
        m_catalogTimeout.stop();
        const QByteArray body = reply->readAll();
        const QString networkError = reply->error() == QNetworkReply::NoError
                ? QString() : reply->errorString();
        reply->deleteLater();
        if (!networkError.isEmpty()) {
            m_catalogLoading = false;
            m_catalogError = QStringLiteral("读取加载器版本失败：%1").arg(networkError);
            emit catalogChanged();
            return;
        }
        finishCatalog(body);
    });
    m_catalogTimeout.start();
}


bool LoaderInstaller::isSupportedLoader(const QString &type)
{
    const QString value = type.toLower();
    return value == QStringLiteral("fabric") || value == QStringLiteral("quilt")
        || value == QStringLiteral("forge") || value == QStringLiteral("neoforge");
}

bool LoaderInstaller::isAllowedUrl(const QUrl &url)
{
    if (!url.isValid() || url.scheme() != QStringLiteral("https"))
        return false;
    static const QStringList hosts = {
        QStringLiteral("meta.fabricmc.net"),
        QStringLiteral("maven.fabricmc.net"),
        QStringLiteral("meta.quiltmc.org"),
        QStringLiteral("maven.quiltmc.org"),
        QStringLiteral("maven.minecraftforge.net"),
        QStringLiteral("files.minecraftforge.net"),
        QStringLiteral("maven.neoforged.net"),
    };
    return hosts.contains(url.host().toLower());
}

void LoaderInstaller::setHooksForTests(const Hooks &hooks)
{
    if (busy())
        return;
    m_hooks = hooks;
}

void LoaderInstaller::setCacheDirForTests(const QString &dir)
{
    if (busy())
        return;
    m_cacheDirForTests = dir;
}

QString LoaderInstaller::cacheDir() const
{
    if (!m_cacheDirForTests.isEmpty())
        return m_cacheDirForTests;
    return platform::cacheDir();
}

QString LoaderInstaller::logDir() const
{
    return cacheDir() + QStringLiteral("/logs");
}

void LoaderInstaller::install(const Request &request)
{
    if (busy()) {
        fail(QStringLiteral("安装器正在运行，请先等待或取消"));
        return;
    }

    reset();
    m_request = request;
    m_request.loaderType = m_request.loaderType.toLower().trimmed();
    m_request.minecraftVersion = m_request.minecraftVersion.trimmed();
    m_request.loaderVersion = m_request.loaderVersion.trimmed();

    if (!isSupportedLoader(m_request.loaderType)) {
        fail(QStringLiteral("不支持的加载器类型：%1").arg(request.loaderType));
        return;
    }
    if (!isSafeVersion(m_request.minecraftVersion) || !isSafeVersion(m_request.loaderVersion)) {
        fail(QStringLiteral("版本号包含非法字符"));
        return;
    }
    if (m_request.gameDir.isEmpty() || !QDir::isAbsolutePath(m_request.gameDir)) {
        fail(QStringLiteral("游戏目录必须是绝对路径"));
        return;
    }
    if (!QDir().mkpath(m_request.gameDir)) {
        fail(QStringLiteral("无法创建游戏目录：%1").arg(m_request.gameDir));
        return;
    }

    const QStringList before = versionIds();
    m_versionsBefore = QSet<QString>(before.begin(), before.end());
    m_startedAt = QDateTime::currentMSecsSinceEpoch();
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    m_logPath = logDir() + QStringLiteral("/loader-%1-%2.log").arg(stamp, m_request.loaderType);
    QDir().mkpath(logDir());
    writeLog(QStringLiteral("BMCD loader install: %1 %2 for Minecraft %3")
                 .arg(m_request.loaderType, m_request.loaderVersion, m_request.minecraftVersion));
    writeLog(QStringLiteral("gameDir: %1").arg(m_request.gameDir));

    setPhase(Phase::Resolving);
    setMessage(QStringLiteral("正在解析官方安装器…"));
    QTimer::singleShot(0, this, &LoaderInstaller::resolve);
}

void LoaderInstaller::retry()
{
    if (m_request.gameDir.isEmpty()) {
        fail(QStringLiteral("没有可重试的安装任务"));
        return;
    }
    install(m_request);
}

void LoaderInstaller::cancel()
{
    if (!busy())
        return;
    m_generation++;
    abortPending();
    m_processTimeout.stop();
    if (m_process.state() != platform::Process::NotRunning) {
        m_process.kill();
        m_process.waitForFinished(1500);
    }
    writeLog(QStringLiteral("[cancelled]"));
    m_phase = Phase::Idle;
    m_state = QStringLiteral("idle");
    m_progress = 0.0;
    m_message = QStringLiteral("已取消");
    emit changed();
    emit finished(false);
}

void LoaderInstaller::reset()
{
    m_generation++;
    abortPending();
    m_processTimeout.stop();
    if (m_process.state() != platform::Process::NotRunning) {
        m_process.kill();
        m_process.waitForFinished(1000);
    }
    m_phase = Phase::Idle;
    m_state = QStringLiteral("idle");
    m_progress = 0.0;
    m_message.clear();
    m_error.clear();
    m_logPath.clear();
    m_versionId.clear();
    m_checksum.clear();
    m_installerBytes.clear();
    m_resolved = {};
    m_arguments.clear();
    m_processOutput.clear();
    m_versionsBefore.clear();
}

void LoaderInstaller::setPhase(Phase phase)
{
    m_phase = phase;
    QString state;
    switch (phase) {
    case Phase::Idle: state = QStringLiteral("idle"); break;
    case Phase::Resolving: state = QStringLiteral("resolving"); break;
    case Phase::Downloading: state = QStringLiteral("downloading"); break;
    case Phase::Verifying: state = QStringLiteral("verifying"); break;
    case Phase::Installing: state = QStringLiteral("installing"); break;
    case Phase::Merging: state = QStringLiteral("merging"); break;
    case Phase::Done: state = QStringLiteral("done"); break;
    case Phase::Error: state = QStringLiteral("error"); break;
    }
    if (m_state != state) {
        m_state = state;
        emit stateChanged(m_state);
    }
    emit changed();
}

void LoaderInstaller::setProgress(double progress)
{
    progress = qBound(0.0, progress, 1.0);
    if (qFuzzyCompare(m_progress, progress))
        return;
    m_progress = progress;
    emit changed();
}

void LoaderInstaller::setMessage(const QString &message)
{
    if (m_message == message)
        return;
    m_message = message;
    emit changed();
}

void LoaderInstaller::fail(const QString &error)
{
    if (m_phase == Phase::Error || m_phase == Phase::Done)
        return;
    m_generation++;
    abortPending();
    m_processTimeout.stop();
    if (m_process.state() != platform::Process::NotRunning) {
        m_process.kill();
        m_process.waitForFinished(1000);
    }
    m_phase = Phase::Error;
    m_state = QStringLiteral("error");
    m_error = error;
    m_message = error;
    writeLog(QStringLiteral("[error] ") + error);
    emit changed();
    emit finished(false);
}

void LoaderInstaller::resolve()
{
    if (m_phase != Phase::Resolving)
        return;

    const QString mc = m_request.minecraftVersion;
    const QString loader = m_request.loaderVersion;

    if (m_request.loaderType == QStringLiteral("fabric")
        || m_request.loaderType == QStringLiteral("quilt")) {
        m_resolved.useMeta = true;
        m_resolved.metaUrl = QUrl(m_request.loaderType == QStringLiteral("fabric")
                                      ? QStringLiteral("https://meta.fabricmc.net/v2/versions/installer")
                                      : QStringLiteral("https://meta.quiltmc.org/v3/versions/installer"));
        resolveMeta();
        return;
    }

    if (!isAllowedUrl(QUrl(QStringLiteral("https://maven.minecraftforge.net")))
        || !isAllowedUrl(QUrl(QStringLiteral("https://maven.neoforged.net")))) {
        fail(QStringLiteral("官方 Maven 白名单配置无效"));
        return;
    }

    if (m_request.loaderType == QStringLiteral("forge")) {
        const QString path = QStringLiteral("/net/minecraftforge/forge/%1-%2/forge-%1-%2-installer.jar")
                                 .arg(mc, loader);
        m_resolved.installerUrl = QUrl(QStringLiteral("https://maven.minecraftforge.net") + path);
        m_resolved.versionIdHint = QStringLiteral("%1-forge-%2").arg(mc, loader);
        m_resolved.installerName = m_resolved.installerUrl.fileName();
    } else {
        const QString path = QStringLiteral("/releases/net/neoforged/neoforge/%1/neoforge-%1-installer.jar")
                                 .arg(loader);
        m_resolved.installerUrl = QUrl(QStringLiteral("https://maven.neoforged.net") + path);
        m_resolved.versionIdHint = QStringLiteral("neoforge-%1").arg(loader);
        m_resolved.installerName = m_resolved.installerUrl.fileName();
    }
    m_resolved.checksumUrl = QUrl(m_resolved.installerUrl.toString() + QStringLiteral(".sha1"));
    resolveChecksum();
}

void LoaderInstaller::resolveMeta()
{
    const QUrl url = m_resolved.metaUrl;
    setMessage(QStringLiteral("正在读取官方安装器元数据…"));
    fetch(url, [this, url](bool ok, const QByteArray &body, const QString &error) {
        if (m_phase != Phase::Resolving)
            return;
        if (!ok) {
            fail(QStringLiteral("读取安装器元数据失败：%1").arg(error));
            return;
        }
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
            fail(QStringLiteral("官方安装器元数据格式无效"));
            return;
        }
        const QJsonArray array = doc.array();
        QString installerUrl;
        QString checksum;
        for (const QJsonValue &value : array) {
            if (!value.isObject())
                continue;
            const QJsonObject object = value.toObject();
            const QString candidate = object.value(QStringLiteral("url")).toString();
            if (candidate.isEmpty())
                continue;
            const bool stable = object.value(QStringLiteral("stable")).toBool(true);
            installerUrl = candidate;
            checksum = object.value(QStringLiteral("sha1")).toString();
            if (checksum.isEmpty())
                checksum = object.value(QStringLiteral("hashes")).toObject()
                               .value(QStringLiteral("sha1")).toString();
            if (stable)
                break;
        }
        if (installerUrl.isEmpty()) {
            fail(QStringLiteral("官方元数据中没有可用的安装器"));
            return;
        }
        m_resolved.installerUrl = QUrl(installerUrl);
        if (!isAllowedUrl(m_resolved.installerUrl)) {
            fail(QStringLiteral("安装器地址不在官方白名单内：%1").arg(installerUrl));
            return;
        }
        m_resolved.installerName = m_resolved.installerUrl.fileName();
        if (m_request.loaderType == QStringLiteral("fabric"))
            m_resolved.versionIdHint = QStringLiteral("fabric-loader-%1-%2")
                                           .arg(m_request.loaderVersion, m_request.minecraftVersion);
        else
            m_resolved.versionIdHint = QStringLiteral("quilt-loader-%1-%2")
                                           .arg(m_request.loaderVersion, m_request.minecraftVersion);
        if (!checksum.isEmpty()) {
            m_checksum = checksum.toLower();
            setPhase(Phase::Downloading);
            downloadInstaller();
        } else {
            m_resolved.checksumUrl = QUrl(m_resolved.installerUrl.toString() + QStringLiteral(".sha1"));
            resolveChecksum();
        }
    });
}

void LoaderInstaller::resolveChecksum()
{
    setMessage(QStringLiteral("正在读取官方校验值…"));
    fetch(m_resolved.checksumUrl, [this](bool ok, const QByteArray &body, const QString &error) {
        if (m_phase != Phase::Resolving)
            return;
        if (!ok) {
            fail(QStringLiteral("读取安装器 SHA-1 失败：%1").arg(error));
            return;
        }
        m_checksum = firstSha1(body);
        if (m_checksum.isEmpty()) {
            fail(QStringLiteral("官方校验值格式无效"));
            return;
        }
        setPhase(Phase::Downloading);
        downloadInstaller();
    });
}

void LoaderInstaller::downloadInstaller()
{
    setMessage(QStringLiteral("正在下载安装器…"));
    setProgress(0.35);
    fetch(m_resolved.installerUrl, [this](bool ok, const QByteArray &body, const QString &error) {
        if (m_phase != Phase::Downloading)
            return;
        if (!ok) {
            fail(QStringLiteral("下载安装器失败：%1").arg(error));
            return;
        }
        if (body.isEmpty()) {
            fail(QStringLiteral("下载到的安装器为空"));
            return;
        }
        m_installerBytes = body;
        setProgress(0.68);
        setPhase(Phase::Verifying);
        verifyInstaller();
    });
}

bool LoaderInstaller::verifyChecksum(const QByteArray &body, const QString &expected,
                                     QString *error) const
{
    const QString actual = sha1Hex(body);
    if (actual.compare(expected, Qt::CaseInsensitive) != 0) {
        if (error)
            *error = QStringLiteral("SHA-1 不匹配：期望 %1，实际 %2").arg(expected, actual);
        return false;
    }
    return true;
}

void LoaderInstaller::verifyInstaller()
{
    setMessage(QStringLiteral("正在校验安装器…"));
    QString error;
    if (!verifyChecksum(m_installerBytes, m_checksum, &error)) {
        fail(error);
        return;
    }
    const QString dir = cacheDir() + QStringLiteral("/loader");
    if (!QDir().mkpath(dir)) {
        fail(QStringLiteral("无法创建安装器缓存目录：%1").arg(dir));
        return;
    }
    QString name = m_resolved.installerName;
    if (name.isEmpty())
        name = QStringLiteral("loader-installer.jar");
    const QString path = dir + QLatin1Char('/') + name;
    if (!platform::writeFileAtomic(path, m_installerBytes)) {
        fail(QStringLiteral("无法写入安装器缓存：%1").arg(path));
        return;
    }
    m_resolved.installerPath = path;
    writeLog(QStringLiteral("installer verified: %1").arg(path));
    setPhase(Phase::Installing);
    setProgress(0.72);
    runInstaller();
}

bool LoaderInstaller::resolveJava(QString *path, QString *error) const
{
    auto executable = [](const QString &value) {
        const QFileInfo info(value);
        return info.isFile() && info.isExecutable();
    };

    QString candidate = m_request.javaPath.trimmed();
    if (candidate.isEmpty())
        candidate = AppSettings::configuredJavaPath();
    if (!candidate.isEmpty()) {
        if (!executable(candidate)) {
            if (error)
                *error = QStringLiteral("指定的 Java 不可执行：%1").arg(candidate);
            return false;
        }
        *path = candidate;
        return true;
    }

    const QString javaHome = qEnvironmentVariable("JAVA_HOME");
    if (!javaHome.isEmpty()) {
        candidate = javaHome + QStringLiteral("/bin/java");
#ifdef Q_OS_WIN
        candidate += QStringLiteral(".exe");
#endif
        if (executable(candidate)) {
            *path = candidate;
            return true;
        }
    }

    candidate = QStandardPaths::findExecutable(QStringLiteral("java"));
    if (!candidate.isEmpty() && executable(candidate)) {
        *path = candidate;
        return true;
    }

    if (error)
        *error = QStringLiteral("找不到 Java；请安装 Java 或在设置中选择 java 可执行文件");
    return false;
}

void LoaderInstaller::runInstaller()
{
    setMessage(QStringLiteral("正在运行官方安装器…"));
    QString java;
    QString error;
    if (!resolveJava(&java, &error)) {
        fail(error);
        return;
    }

    m_arguments.clear();
    m_arguments << QStringLiteral("-jar") << m_resolved.installerPath;
    if (m_request.loaderType == QStringLiteral("fabric")) {
        m_arguments << QStringLiteral("client")
                    << QStringLiteral("-dir") << m_request.gameDir
                    << QStringLiteral("-mcversion") << m_request.minecraftVersion
                    << QStringLiteral("-loader") << m_request.loaderVersion
                    << QStringLiteral("-noprofile");
    } else if (m_request.loaderType == QStringLiteral("quilt")) {
        // Quilt's CLI is not the Fabric one: it takes the Minecraft and loader
        // versions positionally and wants --install-dir=<quoted path>.
        m_arguments << QStringLiteral("install") << QStringLiteral("client")
                    << m_request.minecraftVersion << m_request.loaderVersion
                    << QStringLiteral("--install-dir=\"%1\"").arg(m_request.gameDir)
                    << QStringLiteral("--no-profile");
    } else {
        m_arguments << QStringLiteral("--installClient") << m_request.gameDir;
    }

    QString command = quoteArg(java);
    for (const QString &arg : m_arguments)
        command += QLatin1Char(' ') + quoteArg(arg);
    writeLog(QStringLiteral("$ ") + command);

    if (m_hooks.run) {
        QByteArray output;
        QString hookError;
        const int code = m_hooks.run(java, m_arguments, m_request.gameDir, &output, &hookError);
        if (!output.isEmpty()) {
            m_processOutput += output;
            writeLog(QString::fromLocal8Bit(output));
        }
        if (code != 0) {
            fail(QStringLiteral("安装器退出码 %1：%2")
                     .arg(code)
                     .arg(hookError.isEmpty() ? QString::fromLocal8Bit(output.right(1000))
                                              : hookError));
            return;
        }
        processFinished(0, platform::Process::NormalExit);
        return;
    }

    m_process.setProgram(java);
    m_process.setArguments(m_arguments);
    m_process.setWorkingDirectory(m_request.gameDir);
    m_processOutput.clear();
    m_process.start();
    m_processTimeout.start();
    if (!m_process.waitForStarted(10000)) {
        fail(QStringLiteral("无法启动 Java 安装器：%1").arg(m_process.errorString()));
        return;
    }
}

void LoaderInstaller::processOutput()
{
    const QByteArray data = m_process.readAll();
    if (data.isEmpty())
        return;
    if (m_processOutput.size() < 64 * 1024)
        m_processOutput += data.left(64 * 1024 - m_processOutput.size());
    writeLog(QString::fromLocal8Bit(data));
}

void LoaderInstaller::processFinished(int exitCode, platform::Process::ExitStatus status)
{
    m_processTimeout.stop();
    if (m_phase != Phase::Installing)
        return;
    if (status != platform::Process::NormalExit || exitCode != 0) {
        QString tail = QString::fromLocal8Bit(m_processOutput.right(2000)).trimmed();
        fail(QStringLiteral("安装器失败（退出码 %1）%2")
                 .arg(exitCode)
                 .arg(tail.isEmpty() ? QString() : QStringLiteral("：") + tail));
        return;
    }
    writeLog(QStringLiteral("installer finished successfully"));
    setPhase(Phase::Merging);
    setProgress(0.88);
    mergeProfile();
}

QJsonObject LoaderInstaller::versionJson(const QString &versionId, QString *error) const
{
    const QString path = m_request.gameDir + QStringLiteral("/versions/") + versionId
        + QLatin1Char('/') + versionId + QStringLiteral(".json");
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("找不到版本 JSON：%1").arg(path);
        return {};
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error)
            *error = QStringLiteral("版本 JSON 无效：%1").arg(path);
        return {};
    }
    return doc.object();
}

QStringList LoaderInstaller::versionIds() const
{
    QStringList result;
    const QDir root(m_request.gameDir + QStringLiteral("/versions"));
    if (!root.exists())
        return result;
    const QStringList dirs = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &dir : dirs) {
        if (QFileInfo::exists(root.filePath(dir + QLatin1Char('/') + dir + QStringLiteral(".json"))))
            result.append(dir);
    }
    return result;
}

QString LoaderInstaller::discoverVersion(QString *error)
{
    const QDir root(m_request.gameDir + QStringLiteral("/versions"));
    const QFileInfoList dirs = root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time);
    QString hintPath;
    if (!m_resolved.versionIdHint.isEmpty())
        hintPath = root.filePath(m_resolved.versionIdHint + QLatin1Char('/')
                                 + m_resolved.versionIdHint + QStringLiteral(".json"));

    for (const QFileInfo &dirInfo : dirs) {
        const QString id = dirInfo.fileName();
        if (id == m_request.minecraftVersion)
            continue;
        const QFileInfo jsonInfo(root.filePath(id + QLatin1Char('/') + id + QStringLiteral(".json")));
        if (!jsonInfo.exists())
            continue;
        const bool fresh = !m_versionsBefore.contains(id)
            || jsonInfo.lastModified().toMSecsSinceEpoch() >= m_startedAt - 3000;
        if (!fresh)
            continue;
        QString parseError;
        const QJsonObject object = versionJson(id, &parseError);
        if (object.isEmpty())
            continue;
        const QString inherits = object.value(QStringLiteral("inheritsFrom")).toString();
        const bool matches = id.contains(m_request.loaderVersion, Qt::CaseInsensitive)
            || inherits == m_request.minecraftVersion
            || id.contains(m_request.minecraftVersion, Qt::CaseInsensitive);
        if (matches)
            return id;
    }

    if (!hintPath.isEmpty() && QFileInfo::exists(hintPath))
        return m_resolved.versionIdHint;

    if (error)
        *error = QStringLiteral("安装器运行成功，但没有找到新的版本 JSON（versions/%1/%1.json）")
                     .arg(m_resolved.versionIdHint.isEmpty() ? QStringLiteral("<loader>")
                                                             : m_resolved.versionIdHint);
    return {};
}

bool LoaderInstaller::verifyInstalledVersion(const QString &versionId, QString *error) const
{
    const QJsonObject object = versionJson(versionId, error);
    if (object.isEmpty())
        return false;
    if (object.value(QStringLiteral("id")).toString() != versionId) {
        if (error)
            *error = QStringLiteral("版本 JSON 的 id 与目录名不一致：%1").arg(versionId);
        return false;
    }
    if (object.value(QStringLiteral("mainClass")).toString().isEmpty()) {
        if (error)
            *error = QStringLiteral("版本 JSON 缺少 mainClass：%1").arg(versionId);
        return false;
    }
    const QJsonArray libraries = object.value(QStringLiteral("libraries")).toArray();
    int checked = 0;
    for (const QJsonValue &value : libraries) {
        const QJsonObject library = value.toObject();
        if (!libraryAllowed(library))
            continue;
        QString path = library.value(QStringLiteral("downloads")).toObject()
                           .value(QStringLiteral("artifact")).toObject()
                           .value(QStringLiteral("path")).toString();
        if (path.isEmpty())
            path = mavenPath(library.value(QStringLiteral("name")).toString());
        if (path.isEmpty())
            continue;
        if (path.contains(QStringLiteral("..")) || QDir::isAbsolutePath(path)) {
            if (error)
                *error = QStringLiteral("版本 JSON 包含不安全的库路径：%1").arg(path);
            return false;
        }
        const QString file = m_request.gameDir + QStringLiteral("/libraries/") + path;
        if (!QFileInfo::exists(file)) {
            if (error)
                *error = QStringLiteral("加载器库文件缺失：%1").arg(file);
            return false;
        }
        ++checked;
    }
    if (checked == 0) {
        if (error)
            *error = QStringLiteral("版本 JSON 没有列出任何可校验的加载器库");
        return false;
    }
    return true;
}

void LoaderInstaller::mergeProfile()
{
    setMessage(QStringLiteral("正在检查安装结果…"));
    QString error;
    const QString versionId = discoverVersion(&error);
    if (versionId.isEmpty()) {
        fail(error);
        return;
    }
    if (!verifyInstalledVersion(versionId, &error)) {
        fail(error);
        return;
    }
    m_versionId = versionId;
    writeLog(QStringLiteral("version discovered: %1").arg(versionId));

    ProfileSpec spec;
    spec.gameDir = m_request.gameDir;
    spec.minecraftVersion = m_request.minecraftVersion;
    spec.loaderType = m_request.loaderType;
    spec.loaderVersion = m_request.loaderVersion;
    spec.lastVersionId = versionId;
    spec.name = m_request.profileName;
    spec.javaArgs = m_request.javaArgs;
    const ProfileMergeResult merged = ProfileMerger::merge(spec);
    if (!merged.ok) {
        fail(QStringLiteral("写入 launcher_profiles.json 失败：%1").arg(merged.error));
        return;
    }
    writeLog(QStringLiteral("profile %1 (%2) %3")
                 .arg(merged.id, merged.name,
                      merged.created ? QStringLiteral("created") : QStringLiteral("updated")));
    setPhase(Phase::Done);
    setProgress(1.0);
    setMessage(QStringLiteral("%1 安装完成").arg(versionId));
    emit changed();
    emit finished(true);
}

void LoaderInstaller::fetch(const QUrl &url,
                            const std::function<void(bool, QByteArray, const QString &)> &done)
{
    if (!isAllowedUrl(url)) {
        QTimer::singleShot(0, this, [done, url] {
            done(false, {}, QStringLiteral("非官方地址：%1").arg(url.toString()));
        });
        return;
    }

    const int generation = m_generation;
    if (m_hooks.fetch) {
        QTimer::singleShot(0, this, [this, generation, url, done] {
            if (generation != m_generation)
                return;
            QString error;
            const QByteArray body = m_hooks.fetch(url, &error);
            if (generation != m_generation)
                return;
            done(body.isEmpty() && !error.isEmpty() ? false : true, body, error);
        });
        return;
    }

    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(120000);
    QNetworkReply *reply = m_network->get(request);
    m_reply = reply;
    connect(reply, &QNetworkReply::downloadProgress, this,
            [this](qint64 received, qint64 total) {
                if (m_phase == Phase::Downloading && total > 0)
                    setProgress(0.35 + 0.33 * double(received) / double(total));
            });
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation, done] {
        if (generation != m_generation || reply != m_reply) {
            reply->deleteLater();
            return;
        }
        m_reply = nullptr;
        const QByteArray body = reply->readAll();
        const QNetworkReply::NetworkError code = reply->error();
        const QString error = reply->errorString();
        const QUrl finalUrl = reply->url();
        reply->deleteLater();
        if (code != QNetworkReply::NoError) {
            done(false, {}, error);
            return;
        }
        if (!isAllowedUrl(finalUrl)) {
            done(false, {}, QStringLiteral("重定向到非官方地址：%1").arg(finalUrl.toString()));
            return;
        }
        done(true, body, {});
    });
}

void LoaderInstaller::abortPending()
{
    if (!m_reply)
        return;
    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    disconnect(reply, nullptr, this, nullptr);
    reply->abort();
    reply->deleteLater();
}

void LoaderInstaller::writeLog(const QString &line)
{
    if (m_logPath.isEmpty())
        return;
    QFile file(m_logPath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        file.write(line.toUtf8());
        if (!line.endsWith(QLatin1Char('\n')))
            file.write("\n");
    }
    emit log(line);
}
