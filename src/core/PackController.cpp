#include "core/PackController.h"

#include "core/Deployer.h"
#include "core/DownloadTask.h"
#include "core/LicenseGate.h"
#include "core/PackImporter.h"
#include "core/PlanParser.h"
#include "core/Renderers.h"
#include "platform/Paths.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
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
    const QString env = qEnvironmentVariable("MC_TARGET");
    return env.isEmpty() ? platform::defaultGameDir() : env;
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
    if (!m_download)
        return;
    m_download->cancel();
    m_download->deleteLater();
    m_download = nullptr;
    setBusy(false);
}

void PackController::clear()
{
    ++m_generation;
    stopDownload();
    m_pack = Pack{};
    m_confirmed.clear();
    m_conflictCount = 0;
    m_dryRunOk = false;
    m_missingFiles.clear();
    m_licenceDenied = 0;
    m_licenceNeedsConfirm = 0;
    m_deniedFiles.clear();
    m_lastResult.clear();
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

void PackController::prepare(const QString &versionId, const QString &versionJsonUrl)
{
    if (versionId.isEmpty() || versionJsonUrl.isEmpty()) {
        setState(QStringLiteral("error"), QStringLiteral("缺少版本或清单地址"));
        return;
    }
    const int generation = ++m_generation;
    stopDownload();
    m_pack = Pack{};
    m_pack.versionId = versionId;
    m_pack.versionId = versionId;
    m_pack.minecraftVersion = versionId;
    m_pack.name = QStringLiteral("Minecraft %1").arg(versionId);
    m_pack.loaderType = QStringLiteral("vanilla");
    m_pack.target = targetDir();
    m_pack.generator = QStringLiteral("0.6.0");
    m_confirmed.clear();
    m_missingFiles.clear();
    m_conflictCount = 0;
    m_dryRunOk = false;
    m_licenceDenied = m_licenceNeedsConfirm = 0;
    m_deniedFiles.clear();
    emit packChanged();
    emit preflightChanged();

    setState(QStringLiteral("loading"));

    QNetworkRequest request{QUrl(versionJsonUrl)};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(std::chrono::milliseconds(20000));
    QNetworkReply *reply = m_nam.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation, versionJsonUrl] {
        reply->deleteLater();
        if (generation != m_generation)
            return;
        if (reply->error() != QNetworkReply::NoError) {
            setState(QStringLiteral("error"), QStringLiteral("获取版本 json 失败：%1").arg(reply->errorString()));
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

        QNetworkRequest indexRequest{QUrl(assetIndexUrl)};
        indexRequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                                 QNetworkRequest::NoLessSafeRedirectPolicy);
        indexRequest.setTransferTimeout(std::chrono::milliseconds(60000));
        QNetworkReply *indexReply = m_nam.get(indexRequest);
        connect(indexReply, &QNetworkReply::finished, this, [this, indexReply, generation, json] {
            indexReply->deleteLater();
            if (generation != m_generation)
                return;
            if (indexReply->error() != QNetworkReply::NoError) {
                setState(QStringLiteral("error"),
                         QStringLiteral("获取资源索引失败：%1").arg(indexReply->errorString()));
                return;
            }
            const QJsonObject indexJson = QJsonDocument::fromJson(indexReply->readAll()).object();
            buildPack(json, indexJson);
        });
    });
}

void PackController::buildPack(const QJsonObject &versionJson, const QJsonObject &assetIndexJson)
{
    TaskPlan plan;
    QString error;
    if (!PlanParser::build(versionJson, QJsonDocument(versionJson).toJson(QJsonDocument::Compact),
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
        entry.size = file.size;
        entry.kind = file.kind == QLatin1String("client") ? QStringLiteral("client")
                    : file.kind == QLatin1String("library") ? QStringLiteral("lib")
                                                           : QStringLiteral("asset");
        // Vanilla Minecraft files have no per-file project licence: mark them
        // as Mojang assets, which the self-contained gate treats as unknown.
        m_pack.files.append(entry);
    }
    m_pack.description = QStringLiteral("原版 %1（%2 个文件）").arg(m_pack.minecraftVersion).arg(plan.files.size());
    emit packChanged();
    recomputeGate();
    preflight();
    setState(QStringLiteral("ready"));
}

void PackController::recomputeGate()
{
    const license::GateResult gate = license::evaluate(m_pack, m_confirmed);
    m_licenceDenied = int(gate.denied.size());
    m_licenceNeedsConfirm = int(gate.needsConfirm.size());
    m_deniedFiles = gate.denied;
    emit preflightChanged();
}

void PackController::preflight()
{
    if (m_pack.fileCount() == 0)
        return;
    DeployOptions options;
    options.targetDir = targetDir();
    options.sourceRoot = m_pack.target;
    options.dryRun = true;
    options.overwrite = true;

    const DeployReport report = deploy(m_pack, options, m_confirmed);
    m_conflictCount = int(report.conflicts.size());
    m_dryRunOk = report.ok;
    m_missingFiles = report.failures;
    if (!report.error.isEmpty())
        m_missingFiles.append(report.error);
    recomputeGate();
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
    if (m_busy || m_pack.fileCount() == 0)
        return;
    setBusy(true);
    m_taskDone = 0;
    m_taskTotal = m_pack.fileCount();
    m_taskStep.clear();
    m_lastResult.clear();
    emit taskChanged();
    setState(QStringLiteral("working"));

    const Pack pack = m_pack;
    const QString target = targetDir();
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
        m_lastResult = QStringLiteral("已导出 %1 个文件到 %2").arg(report.deployed).arg(target);
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
        startDownload();
    });
    watcher->setFuture(QtConcurrent::run([archivePath] { return importPackArchive(archivePath); }));
}

void PackController::startDownload()
{
    TaskPlan plan;
    plan.id = m_pack.versionId;
    plan.type = QStringLiteral("pack");
    for (const PackFile &file : m_pack.files) {
        PlanFile entry;
        entry.path = file.path;
        entry.url = file.url;
        entry.sha1 = file.sha1;
        entry.size = file.size;
        entry.kind = file.kind;
        plan.files.append(entry);
        plan.totalBytes += file.size;
    }
    if (plan.files.isEmpty())
        return;

    if (m_download)
        stopDownload();
    m_taskDone = 0;
    m_taskTotal = plan.files.size();
    m_taskStep = QStringLiteral("下载整合包文件");
    setBusy(true);
    setState(QStringLiteral("working"));
    emit taskChanged();

    auto *download = new DownloadTask(plan, m_pack.target, 6, this);
    m_download = download;
    connect(download, &DownloadTask::progress, this,
            [this](qint64, qint64, int doneFiles, int totalFiles, double) {
                m_taskDone = doneFiles;
                m_taskTotal = totalFiles;
                emit taskChanged();
            });
    connect(download, &DownloadTask::finished, this, [this, download](int skipped, int downloaded) {
        if (download != m_download)
            return;
        m_download = nullptr;
        download->deleteLater();
        setBusy(false);
        preflight();
        setState(QStringLiteral("ready"));
        m_lastResult = QStringLiteral("整合包文件就绪：下载 %1，跳过 %2").arg(downloaded).arg(skipped);
        emit message(m_lastResult);
        emit taskChanged();
    });
    connect(download, &DownloadTask::failed, this, [this, download](const QString &message) {
        if (download != m_download)
            return;
        m_download = nullptr;
        download->deleteLater();
        setBusy(false);
        setState(QStringLiteral("error"), QStringLiteral("下载失败：%1").arg(message));
        m_lastResult = m_error;
        emit taskChanged();
    });
    download->start();
}

void PackController::exportPack(const QString &format)
{
    if (m_busy || m_pack.fileCount() == 0)
        return;

    const QString dir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    const QString safeName = QStringLiteral("%1-%2.%3")
                                     .arg(m_pack.name, QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmm")))
                                     .arg(format == QLatin1String("self") ? QStringLiteral("zip") : format);
    const QString output = QDir(dir.isEmpty() ? QDir::homePath() : dir).filePath(safeName);

    setBusy(true);
    m_taskDone = 0;
    m_taskTotal = m_pack.fileCount();
    m_taskStep.clear();
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
