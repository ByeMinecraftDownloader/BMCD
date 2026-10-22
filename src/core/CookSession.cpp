#include "CookSession.h"

#include "JavaLocator.h"
#include "LoaderInstaller.h"
#include "PackController.h"
#include "ProfileMerger.h"
#include "VersionDetail.h"

#include <QDir>
#include <QFileInfo>
#include <QQmlEngine>
#include <QTimer>

namespace {

QString normalisedLoader(const QString &value)
{
    const QString type = value.trimmed().toLower();
    if (type == QStringLiteral("fabric") || type == QStringLiteral("quilt")
        || type == QStringLiteral("forge") || type == QStringLiteral("neoforge"))
        return type;
    return QStringLiteral("vanilla");
}

} // namespace

CookSession::CookSession(QObject *parent)
    : QObject(parent)
{
    // QML singletons are constructed before the engine exposes them through
    // qmlEngine(this); attach on the next event-loop turn and again from
    // start()/refreshLoaderVersions().
    QTimer::singleShot(0, this, [this]() { ensureAttached(); });
}

bool CookSession::ensureAttached()
{
    if (m_attached)
        return m_version && m_pack;
    QQmlEngine *engine = qmlEngine(this);
    if (!engine)
        return false;

    m_version = engine->singletonInstance<VersionDetail *>(QStringLiteral("BMCD"),
                                                           QStringLiteral("VersionDetail"));
    m_pack = engine->singletonInstance<PackController *>(QStringLiteral("BMCD"),
                                                         QStringLiteral("PackController"));
    m_java = engine->singletonInstance<JavaLocator *>(QStringLiteral("BMCD"),
                                                      QStringLiteral("JavaLocator"));
    if (!m_version || !m_pack)
        return false;

    m_loader = new LoaderInstaller(this);
    connect(m_version, &VersionDetail::downloadChanged, this, &CookSession::onVersionChanged);
    connect(m_version, &VersionDetail::stateChanged, this, &CookSession::onVersionChanged);
    connect(m_version, &VersionDetail::planChanged, this, &CookSession::onVersionChanged);
    connect(m_pack, &PackController::stateChanged, this, &CookSession::onPackChanged);
    connect(m_pack, &PackController::taskChanged, this, &CookSession::onPackChanged);
    connect(m_pack, &PackController::packChanged, this, &CookSession::onPackChanged);
    connect(m_loader, &LoaderInstaller::changed, this, &CookSession::onLoaderChanged);
    connect(m_loader, &LoaderInstaller::finished, this, &CookSession::onLoaderFinished);
    connect(m_loader, &LoaderInstaller::catalogChanged, this, [this]() {
        if (m_loaderVersion.isEmpty()) {
            const QVariantList entries = m_loader ? m_loader->catalog() : QVariantList{};
            for (const QVariant &entry : entries) {
                const QVariantMap map = entry.toMap();
                if (map.value(QStringLiteral("stable")).toBool()) {
                    m_loaderVersion = map.value(QStringLiteral("version")).toString();
                    break;
                }
            }
            if (m_loaderVersion.isEmpty() && !entries.isEmpty())
                m_loaderVersion = entries.first().toMap().value(QStringLiteral("version")).toString();
        }
        emit changed();
    });
    if (m_java) {
        connect(m_java, &JavaLocator::changed, this, [this]() { emit changed(); });
        if (!m_java->available() && !m_java->checking())
            m_java->detect();
    }
    m_attached = true;
    emit changed();
    return true;
}

bool CookSession::hasPlan() const
{
    if (m_version && m_version->state() == QStringLiteral("ready"))
        return true;
    return m_pack && m_pack->fileCount() > 0;
}

QString CookSession::planName() const
{
    if (m_pack && m_pack->fileCount() > 0)
        return m_pack->packName();
    return m_version ? m_version->versionId() : QString();
}

QString CookSession::minecraftVersion() const
{
    // A pack plan is authoritative: its body was resolved from this exact
    // version, and the loader profile must match it even while VersionDetail is
    // showing another selection.
    if (m_pack && m_pack->fileCount() > 0 && !m_pack->minecraftVersion().isEmpty())
        return m_pack->minecraftVersion();
    if (m_version && !m_version->versionId().isEmpty())
        return m_version->versionId();
    return m_pack ? m_pack->minecraftVersion() : QString();
}

QString CookSession::hashLabel() const
{
    if (m_pack && m_pack->fileCount() > 0)
        return m_pack->hashLabel();
    return m_version ? m_version->hashLabel() : QStringLiteral("SHA-1");
}

bool CookSession::hasLoader() const
{
    return m_loaderType != QStringLiteral("vanilla") && !m_loaderType.isEmpty();
}

QString CookSession::requiredLoader() const
{
    if (!m_pack || m_pack->fileCount() == 0)
        return QString();
    const QString type = normalisedLoader(m_pack->loaderType());
    return type == QStringLiteral("vanilla") ? QString() : type;
}

bool CookSession::loaderSelectedForPlan() const
{
    const QString required = requiredLoader();
    if (!required.isEmpty() && (!hasLoader() || m_loaderType != required))
        return false;
    return !hasLoader() || !loaderVersion().isEmpty();
}

bool CookSession::loaderReady() const
{
    return loaderSelectedForPlan() && (!hasLoader() || m_loaderInstalled);
}

bool CookSession::javaReady() const
{
    return m_java && m_java->available();
}

QString CookSession::javaVersion() const
{
    return m_java ? m_java->version() : QString();
}

QString CookSession::loaderProfileId() const
{
    if (!hasLoader() || loaderVersion().isEmpty() || minecraftVersion().isEmpty())
        return QString();
    return ProfileMerger::profileId(minecraftVersion(), m_loaderType, loaderVersion());
}

QString CookSession::loaderProfileName() const
{
    if (!hasLoader() || minecraftVersion().isEmpty())
        return QString();
    return ProfileMerger::defaultName(minecraftVersion(), m_loaderType);
}

QString CookSession::loaderLogPath() const
{
    return m_loader ? m_loader->logPath() : QString();
}

QVariantList CookSession::loaderVersions() const
{
    return m_loader ? m_loader->catalog() : QVariantList{};
}

bool CookSession::loaderVersionsLoading() const
{
    return m_loader && m_loader->catalogLoading();
}

QString CookSession::loaderVersionsError() const
{
    return m_loader ? m_loader->catalogError() : QString();
}

void CookSession::setLoaderType(const QString &type)
{
    ensureAttached();
    const QString clean = normalisedLoader(type);
    if (clean == m_loaderType)
        return;
    m_loaderType = clean;
    m_loaderVersion.clear();
    m_loaderVersionId.clear();
    m_loaderInstalled = false;
    emit changed();
    if (hasLoader())
        refreshLoaderVersions();
}

void CookSession::setLoaderVersion(const QString &version)
{
    const QString clean = version.trimmed();
    if (clean == m_loaderVersion)
        return;
    m_loaderVersion = clean;
    m_loaderInstalled = false;
    emit changed();
}

void CookSession::refreshLoaderVersions()
{
    ensureAttached();
    if (!m_loader)
        return;
    if (!hasLoader()) {
        emit changed();
        return;
    }
    m_loader->fetchLoaderVersions(m_loaderType, minecraftVersion());
    emit changed();
}

bool CookSession::gameFromPack() const
{
    return m_pack && m_pack->fileCount() > 0 && m_pack->hasGameFiles();
}

bool CookSession::gameDone() const
{
    if (!hasPlan())
        return false;
    if (gameFromPack())
        return true;
    if (!m_version || m_version->state() != QStringLiteral("ready"))
        return false;
    return m_version->downloadState() == QStringLiteral("done");
}

bool CookSession::loaderDone() const
{
    return !hasLoader() || m_loaderInstalled;
}

bool CookSession::modsDone() const
{
    if (!m_pack || m_pack->fileCount() == 0)
        return true;
    return m_pack->state() == QStringLiteral("done") && m_pack->cooked();
}

double CookSession::gameProgress() const
{
    if (gameFromPack() || gameDone())
        return 1.0;
    if (!m_version)
        return 0.0;
    const qint64 total = m_version->downloadTotalBytes() > 0
            ? m_version->downloadTotalBytes() : m_version->totalBytes();
    if (total > 0)
        return qBound(0.0, double(m_version->doneBytes()) / double(total), 1.0);
    const int files = m_version->downloadTotalFiles() > 0
            ? m_version->downloadTotalFiles() : m_version->fileCount();
    return files > 0 ? qBound(0.0, double(m_version->doneFiles()) / double(files), 1.0) : 0.0;
}

double CookSession::packProgress() const
{
    if (!m_pack || m_pack->fileCount() == 0 || modsDone())
        return 1.0;
    if (m_pack->taskTotal() > 0)
        return qBound(0.0, double(m_pack->taskDone()) / double(m_pack->taskTotal()), 1.0);
    return 0.0;
}

double CookSession::stepProgress(const Step &step) const
{
    if (step.state == QStringLiteral("done") || step.state == QStringLiteral("skipped"))
        return 1.0;
    if (step.state == QStringLiteral("error"))
        return 0.0;
    if (step.key == QStringLiteral("game"))
        return gameProgress();
    if (step.key == QStringLiteral("loader"))
        return m_loader ? qBound(0.0, m_loader->progress(), 1.0) : 0.0;
    if (step.key == QStringLiteral("mods"))
        return packProgress();
    return 0.0;
}

double CookSession::progress() const
{
    if (m_state == QStringLiteral("done"))
        return 1.0;
    if (m_steps.isEmpty())
        return 0.0;
    double sum = 0.0;
    for (const Step &entry : m_steps)
        sum += stepProgress(entry);
    return qBound(0.0, sum / double(m_steps.size()), 1.0);
}

QVariantList CookSession::steps() const
{
    QVariantList result;
    for (const Step &entry : m_steps) {
        QVariantMap map;
        map.insert(QStringLiteral("key"), entry.key);
        map.insert(QStringLiteral("title"), entry.title);
        map.insert(QStringLiteral("state"), entry.state);
        map.insert(QStringLiteral("detail"), entry.detail);
        map.insert(QStringLiteral("progress"), stepProgress(entry));
        result.append(map);
    }
    return result;
}

CookSession::Step *CookSession::step(const QString &key)
{
    for (Step &entry : m_steps) {
        if (entry.key == key)
            return &entry;
    }
    return nullptr;
}

const CookSession::Step *CookSession::step(const QString &key) const
{
    for (const Step &entry : m_steps) {
        if (entry.key == key)
            return &entry;
    }
    return nullptr;
}

QString CookSession::currentStep() const
{
    for (const Step &entry : m_steps) {
        if (entry.state == QStringLiteral("running") || entry.state == QStringLiteral("pending")
            || entry.state == QStringLiteral("error"))
            return entry.key;
    }
    return QString();
}

QString CookSession::currentStepTitle() const
{
    const QString key = currentStep();
    const Step *entry = key.isEmpty() ? nullptr : step(key);
    return entry ? entry->title : QString();
}

bool CookSession::canPause() const
{
    if (m_state != QStringLiteral("running"))
        return false;
    const QString key = currentStep();
    return !key.isEmpty() && key != QStringLiteral("loader");
}

void CookSession::resetSteps()
{
    m_steps.clear();
    Step game;
    game.key = QStringLiteral("game");
    game.title = QStringLiteral("游戏本体");
    game.state = QStringLiteral("pending");
    m_steps.append(game);
    if (hasLoader()) {
        Step loader;
        loader.key = QStringLiteral("loader");
        loader.title = QStringLiteral("加载器");
        loader.state = QStringLiteral("pending");
        m_steps.append(loader);
    }
    if (m_pack && m_pack->fileCount() > 0) {
        Step mods;
        mods.key = QStringLiteral("mods");
        mods.title = QStringLiteral("模组");
        mods.state = QStringLiteral("pending");
        m_steps.append(mods);
    }
}

void CookSession::buildSteps()
{
    resetSteps();
    if (gameFromPack())
        markStep(QStringLiteral("game"), QStringLiteral("skipped"), QStringLiteral("由整合包提供"));
    else if (gameDone())
        markStep(QStringLiteral("game"), QStringLiteral("done"), QStringLiteral("已校验"));
    if (modsDone() && m_pack && m_pack->fileCount() > 0)
        markStep(QStringLiteral("mods"), QStringLiteral("done"), QStringLiteral("已就绪"));
    if (!hasLoader())
        return;
    if (m_loaderInstalled)
        markStep(QStringLiteral("loader"), QStringLiteral("done"), loaderProfileName());
}

void CookSession::markStep(const QString &key, const QString &state, const QString &detail)
{
    Step *entry = step(key);
    if (!entry)
        return;
    entry->state = state;
    if (!detail.isEmpty())
        entry->detail = detail;
}

void CookSession::prepare()
{
    ensureAttached();
    if (m_state != QStringLiteral("idle"))
        return;
    refreshInstalledLoader();
    buildSteps();
    emit changed();
}

void CookSession::start()
{
    ensureAttached();
    if (m_state == QStringLiteral("running") || m_state == QStringLiteral("paused")
        || m_state == QStringLiteral("done"))
        return;
    if (!hasPlan()) {
        fail(QStringLiteral("锅里还是空的：先回点菜页选一个版本。"), QString());
        return;
    }
    if (!loaderSelectedForPlan()) {
        if (!requiredLoader().isEmpty()
            && (!hasLoader() || m_loaderType != requiredLoader())) {
            fail(QStringLiteral("这个整合包需要 %1 加载器：回点菜页把加载器切到 %1。")
                         .arg(requiredLoader()),
                 QString());
        } else {
            fail(QStringLiteral("还没有选择加载器版本。"), QString());
        }
        return;
    }

    m_error.clear();
    m_failedStep.clear();
    m_gameStarted = false;
    m_loaderStarted = false;
    m_modsStarted = false;
    m_loaderInstalled = false;
    m_loaderVersionId.clear();
    refreshInstalledLoader();
    m_state = QStringLiteral("running");
    buildSteps();

    if (!gameFromPack() && !gameDone()) {
        markStep(QStringLiteral("game"), QStringLiteral("running"), QStringLiteral("下载并校验"));
        m_gameStarted = true;
        if (m_version)
            m_version->startDownload();
    }
    emit changed();
    advance();
}

void CookSession::advance()
{
    if (m_state != QStringLiteral("running"))
        return;

    if (!gameDone()) {
        if (!m_gameStarted && !gameFromPack()) {
            markStep(QStringLiteral("game"), QStringLiteral("running"), QStringLiteral("下载并校验"));
            m_gameStarted = true;
            if (m_version)
                m_version->startDownload();
            emit changed();
            return;
        }
        if (!gameDone() && m_version
            && m_version->downloadState() == QStringLiteral("failed")) {
            fail(m_version->downloadError().isEmpty() ? QStringLiteral("游戏本体下载失败")
                                                      : m_version->downloadError(),
                 QStringLiteral("game"));
            return;
        }
        emit changed();
        return;
    }

    if (hasLoader() && !loaderDone()) {
        if (m_loaderStarted && m_loader && m_loader->state() == QStringLiteral("error")) {
            fail(m_loader->error().isEmpty() ? QStringLiteral("加载器安装失败") : m_loader->error(),
                 QStringLiteral("loader"));
            return;
        }
        if (!m_loaderStarted)
            beginLoader();
        emit changed();
        return;
    }

    if (!modsDone()) {
        if (!m_modsStarted) {
            m_modsStarted = true;
            markStep(QStringLiteral("mods"), QStringLiteral("running"), QStringLiteral("下载模组"));
            if (m_pack)
                m_pack->startDownload();
        }
        if (m_pack && m_pack->state() == QStringLiteral("error")) {
            fail(m_pack->error().isEmpty() ? QStringLiteral("模组下载失败") : m_pack->error(),
                 QStringLiteral("mods"));
            return;
        }
        emit changed();
        return;
    }

    finish();
}

void CookSession::refreshInstalledLoader()
{
    if (!hasLoader() || loaderVersion().isEmpty() || minecraftVersion().isEmpty()) {
        m_loaderInstalled = false;
        return;
    }

    // BMCD installs loaders under the same stable directory names the official
    // installers use. A version JSON with the expected id is the cheapest
    // durable proof that an earlier cook already installed this exact loader;
    // if it is gone the loader step runs again.
    QString versionDir;
    if (m_loaderType == QStringLiteral("fabric") || m_loaderType == QStringLiteral("quilt")) {
        versionDir = QStringLiteral("%1-loader-%2-%3").arg(m_loaderType, loaderVersion(),
                                                           minecraftVersion());
    } else if (m_loaderType == QStringLiteral("forge")) {
        versionDir = QStringLiteral("%1-forge-%2").arg(minecraftVersion(), loaderVersion());
    } else if (m_loaderType == QStringLiteral("neoforge")) {
        versionDir = QStringLiteral("neoforge-%1").arg(loaderVersion());
    }
    m_loaderInstalled = !versionDir.isEmpty() && m_pack
            && QFileInfo::exists(m_pack->targetDir() + QStringLiteral("/versions/") + versionDir
                                 + QLatin1Char('/') + versionDir + QStringLiteral(".json"));
}

void CookSession::beginLoader()
{
    if (!m_loader || !m_pack || !m_version) {
        fail(QStringLiteral("加载器组件没有准备好。"), QStringLiteral("loader"));
        return;
    }
    if (!m_java || !m_java->available()) {
        fail(QStringLiteral("找不到可用的 Java：去设置页选择 Java，或点“重新检测”。"),
             QStringLiteral("loader"));
        return;
    }
    if (loaderVersion().isEmpty()) {
        fail(QStringLiteral("还没有选择加载器版本。"), QStringLiteral("loader"));
        return;
    }
    if (minecraftVersion().isEmpty()) {
        fail(QStringLiteral("缺少 Minecraft 版本号，无法安装加载器。"), QStringLiteral("loader"));
        return;
    }

    LoaderInstaller::Request request;
    request.gameDir = m_pack->targetDir();
    request.minecraftVersion = minecraftVersion();
    request.loaderType = m_loaderType;
    request.loaderVersion = loaderVersion();
    request.javaPath = m_java->path();
    request.profileName = loaderProfileName();
    m_loaderStarted = true;
    markStep(QStringLiteral("loader"), QStringLiteral("running"),
             QStringLiteral("安装 %1 %2").arg(m_loaderType, loaderVersion()));
    emit changed();
    m_loader->install(request);
}

void CookSession::finish()
{
    m_state = QStringLiteral("done");
    m_error.clear();
    m_failedStep.clear();
    if (!step(QStringLiteral("game")) || step(QStringLiteral("game"))->state != QStringLiteral("skipped"))
        markStep(QStringLiteral("game"), QStringLiteral("done"));
    if (hasLoader())
        markStep(QStringLiteral("loader"), QStringLiteral("done"), loaderProfileName());
    if (m_pack && m_pack->fileCount() > 0)
        markStep(QStringLiteral("mods"), QStringLiteral("done"));
    emit changed();
}

void CookSession::fail(const QString &message, const QString &stepKey)
{
    m_state = QStringLiteral("error");
    m_error = message;
    m_failedStep = stepKey;
    if (!stepKey.isEmpty())
        markStep(stepKey, QStringLiteral("error"), message);
    emit changed();
}

void CookSession::retry()
{
    if (m_state != QStringLiteral("error"))
        return;
    const QString key = m_failedStep;
    m_error.clear();
    m_failedStep.clear();
    m_state = QStringLiteral("running");
    if (key.isEmpty()) {
        start();
        return;
    }
    markStep(key, QStringLiteral("pending"), QStringLiteral("重试"));
    if (key == QStringLiteral("game")) {
        m_gameStarted = false;
    } else if (key == QStringLiteral("loader")) {
        m_loaderStarted = false;
        m_loaderInstalled = false;
        if (m_loader)
            m_loader->cancel();
    } else if (key == QStringLiteral("mods")) {
        m_modsStarted = false;
    }
    emit changed();
    advance();
}

void CookSession::pause()
{
    if (!canPause())
        return;
    if (!gameDone() && m_version)
        m_version->pauseDownload();
    else if (!modsDone() && m_pack)
        m_pack->pauseDownload();
    m_state = QStringLiteral("paused");
    emit changed();
}

void CookSession::resume()
{
    if (m_state != QStringLiteral("paused"))
        return;
    m_state = QStringLiteral("running");
    if (!gameDone() && m_version)
        m_version->startDownload();
    else if (!modsDone() && m_pack)
        m_pack->resumeDownload();
    emit changed();
    advance();
}

void CookSession::cancel()
{
    if (m_version)
        m_version->cancelDownload();
    if (m_pack)
        m_pack->cancelDownload();
    if (m_loader)
        m_loader->cancel();
    m_state = QStringLiteral("idle");
    m_error.clear();
    m_failedStep.clear();
    m_gameStarted = false;
    m_loaderStarted = false;
    m_modsStarted = false;
    m_loaderInstalled = false;
    m_loaderVersionId.clear();
    buildSteps();
    emit changed();
}

void CookSession::onVersionChanged()
{
    if (m_state == QStringLiteral("running")) {
        if (!gameDone() && m_version
            && m_version->downloadState() == QStringLiteral("failed")) {
            fail(m_version->downloadError().isEmpty() ? QStringLiteral("游戏本体下载失败")
                                                      : m_version->downloadError(),
                 QStringLiteral("game"));
            return;
        }
        if (gameDone())
            markStep(QStringLiteral("game"), QStringLiteral("done"), QStringLiteral("已校验"));
        advance();
        emit changed();
        return;
    }
    emit changed();
}

void CookSession::onPackChanged()
{
    // Do not copy the plan loader into the user's selection: the order screen
    // must ask before changing it. requiredLoader() exposes the mismatch.
    refreshInstalledLoader();
    if (m_state == QStringLiteral("running") && m_modsStarted) {
        if (m_pack && m_pack->state() == QStringLiteral("error")) {
            fail(m_pack->error().isEmpty() ? QStringLiteral("模组下载失败") : m_pack->error(),
                 QStringLiteral("mods"));
            return;
        }
        if (modsDone())
            markStep(QStringLiteral("mods"), QStringLiteral("done"), QStringLiteral("已就绪"));
        advance();
        emit changed();
        return;
    }
    emit changed();
}

void CookSession::onLoaderChanged()
{
    if (m_state == QStringLiteral("running") && m_loaderStarted && m_loader) {
        if (m_loader->state() == QStringLiteral("error")) {
            fail(m_loader->error().isEmpty() ? QStringLiteral("加载器安装失败") : m_loader->error(),
                 QStringLiteral("loader"));
            return;
        }
        if (m_loader->state() == QStringLiteral("done") && !m_loaderInstalled) {
            m_loaderInstalled = true;
            m_loaderVersionId = m_loader->versionId();
            markStep(QStringLiteral("loader"), QStringLiteral("done"), loaderProfileName());
        }
    }
    emit changed();
}

void CookSession::onLoaderFinished(bool ok)
{
    // cancel() may interrupt the installer; a late failure must not resurrect
    // an error over an already idle session.
    if (m_state != QStringLiteral("running"))
        return;
    if (!ok) {
        fail(m_loader && !m_loader->error().isEmpty() ? m_loader->error()
                                                       : QStringLiteral("加载器安装失败"),
             QStringLiteral("loader"));
        return;
    }
    m_loaderInstalled = true;
    m_loaderVersionId = m_loader->versionId();
    markStep(QStringLiteral("loader"), QStringLiteral("done"), loaderProfileName());
    emit changed();
    advance();
}
