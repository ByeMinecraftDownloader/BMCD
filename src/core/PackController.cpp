#include "core/PackController.h"

#include "AppSettings.h"

#include "core/Deployer.h"
#include "core/DownloadSources.h"
#include "core/DownloadTask.h"
#include "core/LicenseGate.h"
#include "core/PackImporter.h"
#include "core/PlanParser.h"
#include "core/Renderers.h"
#include "platform/Paths.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QCryptographicHash>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QtConcurrent>

#include <chrono>

PackController::PackController(QObject *parent)
    : QObject(parent)
{
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(30000);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        setState(QStringLiteral("error"), QStringLiteral("请求超时"));
    });
}

QString PackController::targetDir() const
{
    return AppSettings::resolvedTargetDir();
}

QString PackController::deployDir() const
{
    return m_deployDir.isEmpty() ? targetDir() : m_deployDir;
}

void PackController::setDeployDir(const QString &dir)
{
    if (dir.isEmpty() || dir == m_deployDir)
        return;
    m_deployDir = dir;
    emit deployDirChanged();
    if (m_pack.fileCount() > 0 && !m_busy)
        preflight();
}

QString PackController::cookingDir() const
{
    QString id = m_pack.versionId.isEmpty() ? m_pack.name : m_pack.versionId;
    id.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]")), QStringLiteral("_"));
    if (id.isEmpty())
        id = QStringLiteral("plan");
    const QString salt = QString::fromLatin1(
            QCryptographicHash::hash(m_pack.name.toUtf8(), QCryptographicHash::Sha1).toHex().left(8));
    return QDir(AppSettings::resolvedDownloadRoot()).filePath(id + QStringLiteral("-") + salt);
}

QString PackController::hashLabel() const
{
    bool sha256 = false;
    bool sha512 = false;
    bool sha1 = false;
    for (const PackFile &file : m_pack.files) {
        if (!file.selected || !file.enabled)
            continue;
        sha256 = sha256 || !file.sha256.isEmpty();
        sha512 = sha512 || !file.sha512.isEmpty();
        sha1 = sha1 || !file.sha1.isEmpty();
    }
    if (sha256)
        return QStringLiteral("SHA-256");
    if (sha512)
        return QStringLiteral("SHA-512");
    if (sha1)
        return QStringLiteral("SHA-1");
    return QStringLiteral("—");
}

void PackController::setState(const QString &state, const QString &error)
{
    m_state = state;
    m_error = error;
    emit stateChanged();
}

void PackController::setBusy(bool busy)
{
    m_busy = busy;
    emit busyChanged();
}

void PackController::stopDownload()
{
    DownloadTask *task = m_download;
    if (!task)
        return;
    // Detach before cancel(): cancel() emits failed() synchronously, and the
    // failure handler must not delete the task a second time.
    m_download = nullptr;
    task->cancel();
    task->deleteLater();
    m_paused = false;
    setBusy(false);
}

void PackController::clear()
{
    ++m_generation;
    stopDownload();
    m_pack = Pack{};
    m_adopted = false;
    m_adoptCooked = false;
    m_confirmed.clear();
    m_conflictCount = 0;
    m_dryRunOk = false;
    m_missingFiles.clear();
    m_licenceDenied = 0;
    m_licenceNeedsConfirm = 0;
    m_licenceGroups.clear();
    m_licenceDeniedGroups = 0;
    m_licenceNeedsConfirmGroups = 0;
    m_deniedFiles.clear();
    m_lastResult.clear();
    m_lastOutput.clear();
    m_cooked = false;
    m_taskDone = m_taskTotal = 0;
    m_taskStep.clear();
    setState(QStringLiteral("idle"));
    emit packChanged();
    emit preflightChanged();
    emit taskChanged();
}

void PackController::confirm(const QString &path)
{
    if (!m_confirmed.contains(path))
        m_confirmed.append(path);
    recomputeGate();
}

void PackController::confirmGroup(const QString &groupId)
{
    if (groupId.isEmpty() || m_confirmed.contains(groupId))
        return;
    // Keep the group id (the gate understands it) and the concrete paths:
    // Deployer uses path confirmations for optional files.
    const license::GateResult gate = license::evaluate(m_pack);
    for (const license::GateGroup &group : gate.groups) {
        if (group.id != groupId)
            continue;
        for (const QString &path : group.paths) {
            if (!m_confirmed.contains(path))
                m_confirmed.append(path);
        }
        break;
    }
    m_confirmed.append(groupId);
    recomputeGate();
}

void PackController::prepareDownloaded(const QString &versionId, const QString &versionJsonUrl,
                                       const QString &sourceRoot)
{
    if (sourceRoot.isEmpty()) {
        prepare(versionId, versionJsonUrl);
        return;
    }
    m_pendingRoot = sourceRoot;
    m_pendingCooked = true;
    prepare(versionId, versionJsonUrl);
}

void PackController::prepare(const QString &versionId, const QString &versionJsonUrl)
{
    if (versionId.isEmpty() || versionJsonUrl.isEmpty()) {
        setState(QStringLiteral("error"), QStringLiteral("缺少版本或清单地址"));
        return;
    }
    const int generation = ++m_generation;
    stopDownload();
    const QString pendingRoot = m_pendingRoot;
    const bool pendingCooked = m_pendingCooked;
    m_pendingRoot.clear();
    m_pendingCooked = false;
    m_adoptCooked = pendingCooked;
    m_adopted = pendingCooked;
    m_cooked = false;
    m_pack = Pack{};
    m_pack.versionId = versionId;
    m_pack.versionId = versionId;
    m_pack.minecraftVersion = versionId;
    m_pack.name = QStringLiteral("Minecraft %1").arg(versionId);
    m_pack.loaderType = QStringLiteral("vanilla");
    m_pack.target = pendingRoot.isEmpty() ? cookingDir() : pendingRoot;
    m_pack.generator = QStringLiteral("0.6.0");
    m_confirmed.clear();
    m_missingFiles.clear();
    m_conflictCount = 0;
    m_dryRunOk = false;
    m_licenceDenied = m_licenceNeedsConfirm = 0;
    m_licenceGroups.clear();
    m_licenceDeniedGroups = m_licenceNeedsConfirmGroups = 0;
    m_deniedFiles.clear();
    emit packChanged();
    emit preflightChanged();

    setState(QStringLiteral("loading"));

    fetchVersionJsonAt(DownloadSources::candidates(versionJsonUrl, QStringLiteral("version-json"),
                                                    versionId),
                       0);
}

void PackController::fetchVersionJsonAt(const QStringList &urls, int index)
{
    if (index >= urls.size()) {
        setState(QStringLiteral("error"), QStringLiteral("获取版本 json 失败：没有可用的下载源"));
        return;
    }

    const int generation = m_generation;
    QNetworkRequest request{QUrl(urls.at(index))};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(std::chrono::milliseconds(20000));
    QNetworkReply *reply = m_nam.get(request);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, generation, urls, index] {
                reply->deleteLater();
                if (generation != m_generation)
                    return;
                if (reply->error() != QNetworkReply::NoError) {
                    if (index + 1 < urls.size()) {
                        fetchVersionJsonAt(urls, index + 1);
                        return;
                    }
                    setState(QStringLiteral("error"),
                             QStringLiteral("获取版本 json 失败：%1").arg(reply->errorString()));
                    return;
                }
                const QJsonObject json = QJsonDocument::fromJson(reply->readAll()).object();
                if (json.isEmpty()) {
                    setState(QStringLiteral("error"), QStringLiteral("版本 json 为空"));
                    return;
                }
                const QString id = json.value(QLatin1String("id")).toString();
                if (!id.isEmpty()) {
                    m_pack.versionId = id;
                    m_pack.minecraftVersion = id;
                    m_pack.name = QStringLiteral("Minecraft %1").arg(id);
                }
                const QString assetIndexUrl =
                        json.value(QLatin1String("assetIndex")).toObject().value(QLatin1String("url")).toString();
                if (assetIndexUrl.isEmpty()) {
                    buildPack(json, QJsonObject{});
                    return;
                }
                fetchAssetIndexAt(DownloadSources::candidates(assetIndexUrl,
                                                              QStringLiteral("asset-index"),
                                                              m_pack.versionId),
                                  0, json);
            });
}

void PackController::fetchAssetIndexAt(const QStringList &urls, int index,
                                       const QJsonObject &versionJson)
{
    if (index >= urls.size()) {
        setState(QStringLiteral("error"), QStringLiteral("获取资源索引失败：没有可用的下载源"));
        return;
    }

    const int generation = m_generation;
    QNetworkRequest request{QUrl(urls.at(index))};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(std::chrono::milliseconds(60000));
    QNetworkReply *reply = m_nam.get(request);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, generation, urls, index, versionJson] {
                reply->deleteLater();
                if (generation != m_generation)
                    return;
                if (reply->error() != QNetworkReply::NoError) {
                    if (index + 1 < urls.size()) {
                        fetchAssetIndexAt(urls, index + 1, versionJson);
                        return;
                    }
                    setState(QStringLiteral("error"),
                             QStringLiteral("获取资源索引失败：%1").arg(reply->errorString()));
                    return;
                }
                const QJsonObject indexJson = QJsonDocument::fromJson(reply->readAll()).object();
                buildPack(versionJson, indexJson);
            });
}

bool PackController::hasGameFiles() const
{
    for (const PackFile &file : m_pack.files) {
        if (file.kind == QLatin1String("client") || file.kind == QLatin1String("lib")
            || file.kind == QLatin1String("asset"))
            return true;
    }
    return false;
}

void PackController::buildPack(const QJsonObject &versionJson, const QJsonObject &assetIndexJson)
{
    TaskPlan plan;
    QString error;
    QByteArray rawVersionJson = QJsonDocument(versionJson).toJson(QJsonDocument::Compact);
    if (m_adopted) {
        // Use the bytes that VersionDetail actually wrote; hashing a compact
        // re-serialisation makes an already-downloaded version json fail its
        // SHA-1 check during preflight.
        const QString versionPath = QDir(m_pack.target)
                .filePath(QStringLiteral("versions/%1/%1.json").arg(m_pack.versionId));
        QFile file(versionPath);
        if (file.open(QIODevice::ReadOnly))
            rawVersionJson = file.readAll();
    }
    if (!PlanParser::build(versionJson, rawVersionJson,
                           QStringLiteral("local"), assetIndexJson, &plan, &error)) {
        setState(QStringLiteral("error"), error);
        return;
    }

    m_pack.files.clear();
    for (const PlanFile &file : plan.files) {
        PackFile entry;
        entry.path = file.path;
        entry.url = file.url;
        entry.sha1 = file.sha1;
        entry.sha256 = file.sha256;
        entry.sha512 = file.sha512;
        entry.size = file.size;
        entry.kind = file.kind == QLatin1String("client") ? QStringLiteral("client")
                    : file.kind == QLatin1String("library") ? QStringLiteral("lib")
                                                           : QStringLiteral("asset");
        // Vanilla files stay launcher-side; the self-contained gate only
        // inspects content files, so these do not block the export.
        m_pack.files.append(entry);
    }
    m_pack.description = QStringLiteral("原版 %1（%2 个文件）").arg(m_pack.minecraftVersion).arg(plan.files.size());
    emit packChanged();
    recomputeGate();
    preflight();
    // Only claim the dish is cooked when the caller confirmed VersionDetail
    // finished and the preflight found every planned file at the adopted root.
    m_cooked = m_adoptCooked && m_missingFiles.isEmpty();
    emit taskChanged();
    setState(QStringLiteral("ready"));
}

void PackController::recomputeGate()
{
    const license::GateResult gate = license::evaluate(m_pack, m_confirmed);
    m_licenceDenied = int(gate.denied.size());
    m_licenceNeedsConfirm = int(gate.needsConfirm.size());
    m_deniedFiles = gate.denied;
    m_licenceDeniedGroups = int(gate.deniedGroups.size());
    m_licenceNeedsConfirmGroups = int(gate.needsConfirmGroups.size());

    m_licenceGroups.clear();
    for (const license::GateGroup &group : gate.groups) {
        if (group.pendingCount == 0)
            continue;
        m_licenceGroups.append(QVariantMap{
            {QStringLiteral("id"), group.id},
            {QStringLiteral("kind"), group.kind},
            {QStringLiteral("name"), group.name},
            {QStringLiteral("licenseId"), group.licenseId},
            {QStringLiteral("licenseName"), group.licenseName},
            {QStringLiteral("sourceUrl"), group.sourceUrl},
            {QStringLiteral("author"), group.author},
            {QStringLiteral("verdict"), license::verdictName(group.verdict)},
            {QStringLiteral("fileCount"), group.fileCount},
            {QStringLiteral("pendingCount"), group.pendingCount},
        });
    }
    emit preflightChanged();
}

void PackController::preflight()
{
    if (m_pack.fileCount() == 0)
        return;
    DeployOptions options;
    options.targetDir = deployDir();
    options.sourceRoot = m_pack.target;
    options.dryRun = true;
    options.overwrite = true;

    const DeployReport report = deploy(m_pack, options, m_confirmed);
    m_conflictCount = int(report.conflicts.size());
    m_dryRunOk = report.ok;
    m_missingFiles = report.failures;
    // Deployer sets report.error to "N 个文件未通过预检" when there are
    // failures; appending that summary made the UI count one file too many.
    if (m_missingFiles.isEmpty() && !report.error.isEmpty())
        m_missingFiles.append(report.error);
    recomputeGate();
    emit preflightChanged();
}

void PackController::reportProgress(int done, int total, const QString &step)
{
    QMetaObject::invokeMethod(
            this,
            [this, done, total, step] {
                m_taskDone = done;
                m_taskTotal = total;
                m_taskStep = step;
                emit taskChanged();
            },
            Qt::QueuedConnection);
}

void PackController::serve()
{
    serveTo(deployDir());
}

void PackController::serveTo(const QString &targetDir)
{
    if (m_busy || m_pack.fileCount() == 0 || !m_cooked) {
        setState(QStringLiteral("error"), QStringLiteral("先完成烹饪再出餐"));
        return;
    }
    const QString target = targetDir.isEmpty() ? deployDir() : targetDir;
    setBusy(true);
    m_taskDone = 0;
    m_taskTotal = m_pack.fileCount();
    m_taskStep.clear();
    m_lastResult.clear();
    m_lastOutput.clear();
    emit taskChanged();
    setState(QStringLiteral("working"));

    const Pack pack = m_pack;
    const QStringList confirmed = m_confirmed;
    auto *watcher = new QFutureWatcher<DeployReport>(this);
    connect(watcher, &QFutureWatcher<DeployReport>::finished, this, [this, watcher, target] {
        const DeployReport report = watcher->result();
        watcher->deleteLater();
        setBusy(false);
        if (!report.ok) {
            setState(QStringLiteral("error"), report.error);
            m_lastResult = report.error;
            emit taskChanged();
            return;
        }
        m_lastOutput = target;
        m_lastResult = QStringLiteral("已出餐 %1 个文件到 %2").arg(report.deployed).arg(target);
        emit message(m_lastResult);
        setState(QStringLiteral("done"));
        emit taskChanged();
    });

    watcher->setFuture(QtConcurrent::run([this, pack, target, confirmed] {
        DeployOptions options;
        options.targetDir = target;
        options.sourceRoot = pack.target;
        options.progress = [this](int done, int total, const QString &step) {
            reportProgress(done, total, step);
        };
        return deploy(pack, options, confirmed);
    }));
}

void PackController::importPack(const QString &archivePath)
{
    if (m_busy || archivePath.isEmpty())
        return;
    const int generation = ++m_generation;
    stopDownload();
    setBusy(true);
    m_taskDone = 0;
    m_taskTotal = 1;
    m_taskStep = QStringLiteral("读取整合包清单");
    m_lastResult.clear();
    emit taskChanged();
    setState(QStringLiteral("working"));

    auto *watcher = new QFutureWatcher<ImportResult>(this);
    connect(watcher, &QFutureWatcher<ImportResult>::finished, this, [this, watcher, generation] {
        const ImportResult result = watcher->result();
        watcher->deleteLater();
        if (generation != m_generation)
            return;
        setBusy(false);
        if (!result.ok) {
            setState(QStringLiteral("error"), result.error);
            m_lastResult = result.error;
            emit taskChanged();
            return;
        }

        m_pack = result.pack;
        m_cooked = false;
        m_lastOutput.clear();
        m_confirmed.clear();
        m_conflictCount = 0;
        m_missingFiles.clear();
        emit packChanged();
        recomputeGate();
        preflight();

        QString summary = QStringLiteral("已导入 %1（%2，%3 个文件")
                                  .arg(m_pack.name, result.format)
                                  .arg(m_pack.fileCount());
        if (result.overrideCount > 0)
            summary += QStringLiteral("，含 %1 个内置文件").arg(result.overrideCount);
        summary += QStringLiteral("）");
        if (!result.unresolved.isEmpty())
            summary += QStringLiteral("；%1 个文件无法解析下载地址").arg(result.unresolved.size());
        m_lastResult = summary;
        emit message(summary);
        emit taskChanged();

        if (m_pack.files.isEmpty()) {
            // Overrides-only pack: nothing to download, straight to the serve screen.
            preflight();
            setState(QStringLiteral("ready"));
            emit taskChanged();
            return;
        }
        setState(QStringLiteral("ready"));
        m_lastResult = QStringLiteral("已导入 %1，确认后开始烹饪").arg(m_pack.name);
        emit message(m_lastResult);
        emit taskChanged();
    });
    watcher->setFuture(QtConcurrent::run([archivePath] { return importPackArchive(archivePath); }));
}

bool PackController::adoptPack(const QString &json)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setState(QStringLiteral("error"), QStringLiteral("模组计划不是有效的 JSON"));
        return false;
    }
    Pack pack = Pack::fromJson(document.object());
    QString error;
    if (!pack.isValid(&error)) {
        setState(QStringLiteral("error"), QStringLiteral("模组计划无效：%1").arg(error));
        return false;
    }
    if (m_busy)
        stopDownload();
    m_pack = pack;
    m_adopted = false;
    m_adoptCooked = false;
    m_cooked = false;
    m_lastOutput.clear();
    m_pack.target = cookingDir();
    m_pack.generator = QStringLiteral("0.1.0");
    m_confirmed.clear();
    m_conflictCount = 0;
    m_missingFiles.clear();
    m_dryRunOk = false;
    m_licenceDenied = m_licenceNeedsConfirm = 0;
    m_licenceGroups.clear();
    m_licenceDeniedGroups = m_licenceNeedsConfirmGroups = 0;
    m_deniedFiles.clear();
    emit packChanged();
    recomputeGate();
    preflight();
    m_lastResult = QStringLiteral("已生成 %1（%2 个文件）").arg(m_pack.name).arg(m_pack.fileCount());
    emit taskChanged();
    setState(QStringLiteral("ready"));
    return true;
}

void PackController::startDownload()
{
    TaskPlan plan;
    plan.id = m_pack.versionId;
    plan.type = QStringLiteral("pack");
    for (const PackFile &file : m_pack.files) {
        if (!file.selected || !file.enabled)
            continue;
        PlanFile entry;
        entry.path = file.path;
        entry.url = file.url;
        entry.sha1 = file.sha1;
        entry.sha256 = file.sha256;
        entry.sha512 = file.sha512;
        entry.size = file.size;
        entry.kind = file.kind;
        plan.files.append(entry);
        plan.totalBytes += file.size;
    }
    if (plan.files.isEmpty()) {
        if (m_pack.overrides.isEmpty())
            return;
        // Overrides-only packs have nothing to transfer, but Cook still needs
        // a terminal state; otherwise "开始烹饪" appears to do nothing.
        m_taskDone = 0;
        m_taskTotal = 0;
        m_taskStep = QStringLiteral("无需下载");
        m_downloadFiles.clear();
        emit downloadFilesChanged();
        m_cooked = true;
        setBusy(false);
        setState(QStringLiteral("done"));
        m_lastResult = QStringLiteral("没有需要下载的文件，已就绪");
        emit message(m_lastResult);
        emit taskChanged();
        return;
    }

    if (m_download)
        stopDownload();
    m_taskDone = 0;
    m_taskTotal = plan.files.size();
    m_taskStep = QStringLiteral("下载到临时目录");
    m_downloadFiles.clear();
    for (const PlanFile &file : plan.files) {
        m_downloadFiles.append(QVariantMap{{QStringLiteral("path"), file.path},
                                           {QStringLiteral("kind"), file.kind},
                                           {QStringLiteral("done"), qint64(0)},
                                           {QStringLiteral("total"), file.size},
                                           {QStringLiteral("state"), 0}});
    }
    m_filesDirty = false;
    emit downloadFilesChanged();
    m_paused = false;
    setBusy(true);
    setState(QStringLiteral("working"));
    emit taskChanged();

    auto *download = new DownloadTask(plan, m_pack.target, AppSettings::concurrency(), this);
    m_download = download;
    connect(download, &DownloadTask::fileProgress, this,
            [this](const QString &path, qint64 done, qint64 total, int fileState) {
                for (int i = 0; i < m_downloadFiles.size(); ++i) {
                    QVariantMap entry = m_downloadFiles.at(i).toMap();
                    if (entry.value(QStringLiteral("path")).toString() != path)
                        continue;
                    entry.insert(QStringLiteral("done"), done);
                    entry.insert(QStringLiteral("total"), total);
                    entry.insert(QStringLiteral("state"), fileState);
                    m_downloadFiles[i] = entry;
                    m_filesDirty = true;
                    break;
                }
            });
    connect(download, &DownloadTask::progress, this,
            [this](qint64, qint64, int doneFiles, int totalFiles, double) {
                m_taskDone = doneFiles;
                m_taskTotal = totalFiles;
                emit taskChanged();
                if (m_filesDirty) {
                    m_filesDirty = false;
                    emit downloadFilesChanged();
                }
            });
    connect(download, &DownloadTask::finished, this, [this, download](int skipped, int downloaded) {
        if (download != m_download)
            return;
        m_download = nullptr;
        download->deleteLater();
        setBusy(false);
        m_paused = false;
        m_cooked = true;
        m_lastOutput.clear();
        preflight();
        setState(QStringLiteral("done"));
        m_lastResult = QStringLiteral("烹饪完成：下载 %1，跳过 %2").arg(downloaded).arg(skipped);
        emit message(m_lastResult);
        emit taskChanged();
        emit downloadFilesChanged();
    });
    connect(download, &DownloadTask::failed, this, [this, download](const QString &message) {
        if (download != m_download)
            return;
        m_download = nullptr;
        download->deleteLater();
        setBusy(false);
        m_paused = false;
        setState(QStringLiteral("error"), QStringLiteral("下载失败：%1").arg(message));
        m_lastResult = m_error;
        emit taskChanged();
    });
    download->start();
}

void PackController::pauseDownload()
{
    if (!m_download || m_state != QLatin1String("working"))
        return;
    m_download->pause();
    m_paused = true;
    setState(QStringLiteral("paused"));
    m_lastResult = QStringLiteral("下载已暂停");
    emit taskChanged();
}

void PackController::resumeDownload()
{
    if (!m_download || !m_paused)
        return;
    m_paused = false;
    setState(QStringLiteral("working"));
    m_download->start();
    emit taskChanged();
}

void PackController::cancelDownload()
{
    const QString dir = m_pack.target;
    if (m_download)
        stopDownload();

    // Cancel is a discard, not a pause: remove the temporary download tree so
    // the next cooking run starts clean. Never touch a target outside the
    // application cache.
    if (!dir.isEmpty() && QDir(dir).exists()) {
        const QString cache = QDir(AppSettings::resolvedDownloadRoot()).absolutePath();
        const QString absolute = QDir(dir).absolutePath();
        if (absolute == cache || absolute.startsWith(cache + QLatin1Char('/')))
            QDir(absolute).removeRecursively();
    }

    setBusy(false);
    m_cooked = false;
    m_lastOutput.clear();
    m_taskStep = QStringLiteral("已取消");
    setState(QStringLiteral("ready"));
    m_lastResult = QStringLiteral("已取消下载");
    preflight();
    emit taskChanged();
}

void PackController::exportPack(const QString &format)
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    const QString safeName = QStringLiteral("%1-%2.%3")
                                     .arg(m_pack.name, QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmm")))
                                     .arg(format == QLatin1String("self") ? QStringLiteral("zip") : format);
    exportPackTo(format, QDir(dir.isEmpty() ? QDir::homePath() : dir).filePath(safeName));
}

void PackController::exportPackTo(const QString &format, const QString &outputPath)
{
    if (m_busy || m_pack.fileCount() == 0 || !m_cooked) {
        setState(QStringLiteral("error"), QStringLiteral("先完成烹饪再出餐"));
        return;
    }
    if (outputPath.isEmpty()) {
        exportPack(format);
        return;
    }
    const QString output = outputPath;
    QDir().mkpath(QFileInfo(output).absolutePath());

    setBusy(true);
    m_taskDone = 0;
    m_taskTotal = m_pack.fileCount();
    m_taskStep.clear();
    m_lastOutput.clear();
    emit taskChanged();
    setState(QStringLiteral("working"));

    const Pack pack = m_pack;
    const QStringList confirmed = m_confirmed;
    auto *watcher = new QFutureWatcher<RenderResult>(this);
    connect(watcher, &QFutureWatcher<RenderResult>::finished, this, [this, watcher, output] {
        const RenderResult result = watcher->result();
        watcher->deleteLater();
        setBusy(false);
        if (!result.ok) {
            setState(QStringLiteral("error"), result.error);
            m_lastResult = result.error;
            emit taskChanged();
            return;
        }
        m_lastOutput = output;
        m_lastResult = QStringLiteral("已导出到 %1").arg(output);
        emit message(m_lastResult);
        setState(QStringLiteral("done"));
        emit taskChanged();
    });

    watcher->setFuture(QtConcurrent::run([this, pack, output, format, confirmed] {
        RenderOptions options;
        options.outputPath = output;
        options.confirmedPaths = confirmed;
        options.progress = [this](int done, int total, const QString &step) {
            reportProgress(done, total, step);
        };
        if (format == QLatin1String("self")) {
            const license::GateResult gate = license::evaluate(pack, confirmed);
            return renderSelfContained(pack, options, gate);
        }
        if (format == QLatin1String("ftb"))
            return renderFtb(pack, options);
        return renderMrpack(pack, options);
    }));
}
