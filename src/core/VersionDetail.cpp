#include "VersionDetail.h"

#include "AppSettings.h"
#include "DownloadSources.h"
#include "DownloadTask.h"
#include "PlanParser.h"

#include "platform/Console.h"
#include "platform/Paths.h"

#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>

#include <chrono>

VersionDetail::VersionDetail(QObject *parent)
    : QObject(parent)
{
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(20000);
    connect(&m_timeout, &QTimer::timeout, this, [this]() {
        if (m_state == QLatin1String("loading")) {
            ++m_generation;
            setState(QStringLiteral("error"), QStringLiteral("请求超时，检查网络后重试"));
        }
    });
}

VersionDetail::~VersionDetail()
{
    if (m_task) {
        m_task->cancel();
        m_task->deleteLater();
    }
}

QString VersionDetail::targetDir() const
{
    const QString id = m_id.isEmpty() ? QStringLiteral("unknown") : m_id;
    return QDir(AppSettings::resolvedDownloadRoot()).filePath(QStringLiteral("versions/") + id);
}

void VersionDetail::setState(const QString &state, const QString &error)
{
    m_state = state;
    m_error = error;
    emit stateChanged();
}

void VersionDetail::select(const QString &versionId, const QString &versionJsonUrl)
{
    // Do not delete the old version's cache when the user simply picks
    // another version; only cancelling an active download removes its files.
    stopTask();

    ++m_generation;
    m_id = versionId;
    m_jsonUrl = versionJsonUrl;
    m_plan = TaskPlan{};
    emit planChanged();
    setState(QStringLiteral("loading"));

    m_doneBytes = 0;
    m_downloadTotalBytes = 0;
    m_doneFiles = 0;
    m_downloadTotalFiles = 0;
    m_speed = 0.0;
    m_downloadError.clear();
    m_downloadState = QStringLiteral("idle");
    m_cacheChecking = false;
    m_cacheChecked = false;
    emit downloadChanged();

    fetchVersionJson(versionJsonUrl);
}

void VersionDetail::clear()
{
    stopTask();
    ++m_generation;
    m_id.clear();
    m_plan = TaskPlan{};
    m_error.clear();
    m_state = QStringLiteral("idle");
    m_downloadState = QStringLiteral("idle");
    m_doneBytes = 0;
    m_doneFiles = 0;
    m_speed = 0.0;
    m_cacheChecking = false;
    m_cacheChecked = false;
    m_downloadError.clear();
    emit planChanged();
    emit stateChanged();
    emit downloadChanged();
}

void VersionDetail::fetchVersionJson(const QString &url)
{
    fetchVersionJsonAt(DownloadSources::candidates(url, QStringLiteral("version-json"), m_id), 0);
}

void VersionDetail::fetchVersionJsonAt(const QStringList &urls, int index)
{
    if (index >= urls.size()) {
        setState(QStringLiteral("error"), QStringLiteral("获取版本信息失败：没有可用的下载源"));
        return;
    }

    const int generation = m_generation;
    QNetworkRequest request{QUrl(urls.at(index))};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(std::chrono::milliseconds(20000));
    request.setHeader(QNetworkRequest::UserAgentHeader, platform::userAgent());

    QNetworkReply *reply = m_nam.get(request);
    m_timeout.start();

    connect(reply, &QNetworkReply::finished, this, [this, reply, generation, urls, index]() {
        reply->deleteLater();
        if (generation != m_generation)
            return;
        if (reply->error() != QNetworkReply::NoError) {
            if (index + 1 < urls.size()) {
                fetchVersionJsonAt(urls, index + 1);
                return;
            }
            setState(QStringLiteral("error"), QStringLiteral("获取版本信息失败：%1").arg(reply->errorString()));
            return;
        }
        m_rawVersionJson = reply->readAll();
        const QJsonObject versionJson = QJsonDocument::fromJson(m_rawVersionJson).object();
        if (versionJson.isEmpty()) {
            setState(QStringLiteral("error"), QStringLiteral("版本信息为空"));
            return;
        }
        fetchAssetIndex(versionJson);
    });
}

void VersionDetail::fetchAssetIndex(const QJsonObject &versionJson)
{
    const QJsonObject assetIndex = versionJson.value(QLatin1String("assetIndex")).toObject();
    const QString url = assetIndex.value(QLatin1String("url")).toString();
    if (url.isEmpty()) {
        finishPlan(versionJson, QJsonObject{});
        return;
    }
    fetchAssetIndexAt(DownloadSources::candidates(url, QStringLiteral("asset-index"), m_id), 0,
                      versionJson);
}

void VersionDetail::fetchAssetIndexAt(const QStringList &urls, int index,
                                      const QJsonObject &versionJson)
{
    if (index >= urls.size()) {
        setState(QStringLiteral("error"), QStringLiteral("获取资源索引失败：没有可用的下载源"));
        return;
    }

    const int generation = m_generation;
    QNetworkRequest request{QUrl(urls.at(index))};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(std::chrono::milliseconds(20000));
    request.setHeader(QNetworkRequest::UserAgentHeader, platform::userAgent());

    QNetworkReply *reply = m_nam.get(request);
    m_timeout.start();

    connect(reply, &QNetworkReply::finished, this, [this, reply, generation, versionJson, urls, index]() {
        reply->deleteLater();
        if (generation != m_generation)
            return;
        if (reply->error() != QNetworkReply::NoError) {
            if (index + 1 < urls.size()) {
                fetchAssetIndexAt(urls, index + 1, versionJson);
                return;
            }
            setState(QStringLiteral("error"), QStringLiteral("获取资源索引失败：%1").arg(reply->errorString()));
            return;
        }
        const QJsonObject assetIndexJson = QJsonDocument::fromJson(reply->readAll()).object();
        if (assetIndexJson.value(QLatin1String("objects")).toObject().isEmpty()) {
            setState(QStringLiteral("error"), QStringLiteral("资源索引为空"));
            return;
        }
        finishPlan(versionJson, assetIndexJson);
    });
}

void VersionDetail::finishPlan(const QJsonObject &versionJson, const QJsonObject &assetIndexJson)
{
    m_timeout.stop();

    TaskPlan plan;
    QString error;
    if (!PlanParser::build(versionJson, m_rawVersionJson, m_jsonUrl, assetIndexJson, &plan, &error)) {
        setState(QStringLiteral("error"), error);
        return;
    }

    m_plan = plan;
    emit planChanged();
    setState(QStringLiteral("ready"));
}

void VersionDetail::startDownload()
{
    if (!m_plan.isValid())
        return;
    if (m_task && m_downloadState == QLatin1String("running"))
        return;

    stopTask();

    m_downloadError.clear();
    m_cacheChecking = false;
    m_cacheChecked = false;
    m_doneBytes = 0;
    m_doneFiles = 0;
    m_downloadTotalBytes = m_plan.totalBytes;
    m_downloadTotalFiles = m_plan.files.size();
    m_speed = 0.0;
    m_downloadState = QStringLiteral("running");
    emit downloadChanged();

    m_task = new DownloadTask(m_plan, targetDir(), AppSettings::concurrency(), this);

    connect(m_task, &DownloadTask::progress, this,
            [this](qint64 doneBytes, qint64 totalBytes, int doneFiles, int totalFiles, double speed) {
                m_doneBytes = doneBytes;
                m_downloadTotalBytes = totalBytes;
                m_doneFiles = doneFiles;
                m_downloadTotalFiles = totalFiles;
                m_speed = speed;
                emit downloadChanged();
            });

    connect(m_task, &DownloadTask::log, this, [this](const QString &text) {
        emit message(text);
    });

    connect(m_task, &DownloadTask::finished, this, [this](int skipped, int downloaded) {
        m_downloadState = QStringLiteral("done");
        m_cacheChecked = true;
        m_speed = 0.0;
        m_doneFiles = m_downloadTotalFiles;
        m_doneBytes = m_downloadTotalBytes;
        emit downloadChanged();
        emit message(QStringLiteral("完成：下载 %1 个文件，跳过 %2 个已存在").arg(downloaded).arg(skipped));
    });

    connect(m_task, &DownloadTask::failed, this, [this](const QString &reason) {
        m_downloadState = QStringLiteral("failed");
        m_cacheChecked = false;
        m_downloadError = reason;
        m_speed = 0.0;
        emit downloadChanged();
    });

    m_task->start();
}

void VersionDetail::stopTask()
{
    if (!m_task)
        return;
    m_task->disconnect(this);
    m_task->cancel();
    m_task->deleteLater();
    m_task = nullptr;
    m_cacheChecking = false;
    m_speed = 0.0;
}

void VersionDetail::verifyCache()
{
    if (!m_plan.isValid() || m_cacheChecked || m_cacheChecking || m_task)
        return;

    m_downloadError.clear();
    m_doneBytes = 0;
    m_doneFiles = 0;
    m_downloadTotalBytes = m_plan.totalBytes;
    m_downloadTotalFiles = m_plan.files.size();
    m_speed = 0.0;
    m_cacheChecking = true;
    emit downloadChanged();

    m_task = new DownloadTask(m_plan, targetDir(), AppSettings::concurrency(), this);
    m_task->setVerifyOnly(true);

    connect(m_task, &DownloadTask::progress, this,
            [this](qint64 doneBytes, qint64 totalBytes, int doneFiles, int totalFiles, double speed) {
                m_doneBytes = doneBytes;
                m_downloadTotalBytes = totalBytes;
                m_doneFiles = doneFiles;
                m_downloadTotalFiles = totalFiles;
                m_speed = speed;
                emit downloadChanged();
            });

    connect(m_task, &DownloadTask::verified, this,
            [this](int cached, int missing, qint64 doneBytes, qint64 totalBytes) {
                m_cacheChecking = false;
                m_cacheChecked = true;
                m_doneBytes = doneBytes;
                m_doneFiles = cached;
                m_downloadTotalBytes = totalBytes;
                m_downloadTotalFiles = m_plan.files.size();
                m_speed = 0.0;
                if (missing == 0) {
                    m_downloadState = QStringLiteral("done");
                    m_doneFiles = m_downloadTotalFiles;
                    m_doneBytes = m_downloadTotalBytes;
                    emit message(QStringLiteral("缓存校验完成：%1 个文件已就绪").arg(cached));
                } else {
                    m_downloadState = QStringLiteral("idle");
                    emit message(QStringLiteral("缓存校验：%1 个文件需要下载").arg(missing));
                }
                DownloadTask *task = m_task;
                m_task = nullptr;
                if (task)
                    task->deleteLater();
                emit downloadChanged();
            });

    connect(m_task, &DownloadTask::failed, this, [this](const QString &reason) {
        m_cacheChecking = false;
        m_cacheChecked = true;
        m_downloadState = QStringLiteral("idle");
        m_downloadError = reason;
        m_speed = 0.0;
        DownloadTask *task = m_task;
        m_task = nullptr;
        if (task)
            task->deleteLater();
        emit downloadChanged();
    });

    m_task->start();
}

void VersionDetail::pauseDownload()
{
    if (!m_task || m_downloadState != QLatin1String("running"))
        return;
    m_task->pause();
    m_downloadState = QStringLiteral("paused");
    m_speed = 0.0;
    emit downloadChanged();
}

void VersionDetail::cancelDownload()
{
    if (!m_task || m_downloadState == QLatin1String("done")
        || m_downloadState == QLatin1String("idle"))
        return;
    const QString dir = targetDir();
    stopTask();
    if (!dir.isEmpty() && QDir(dir).exists()) {
        const QString cache = QDir(AppSettings::resolvedDownloadRoot()).absolutePath();
        const QString absolute = QDir(dir).absolutePath();
        if (absolute == cache || absolute.startsWith(cache + QLatin1Char('/')))
            QDir(absolute).removeRecursively();
    }
    m_downloadState = QStringLiteral("idle");
    m_doneBytes = 0;
    m_doneFiles = 0;
    m_cacheChecked = false;
    m_speed = 0.0;
    emit downloadChanged();
}
