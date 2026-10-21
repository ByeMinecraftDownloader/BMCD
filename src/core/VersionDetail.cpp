#include "VersionDetail.h"

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

namespace {

constexpr int kConcurrency = 6;

QString resolveTargetDir()
{
    const QString override = qEnvironmentVariable("MC_TARGET");
    if (!override.isEmpty())
        return override;
    return platform::defaultGameDir();
}

} // namespace

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
    return resolveTargetDir();
}

void VersionDetail::setState(const QString &state, const QString &error)
{
    m_state = state;
    m_error = error;
    emit stateChanged();
}

void VersionDetail::select(const QString &versionId, const QString &versionJsonUrl)
{
    if (m_task)
        cancelDownload();

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
    emit downloadChanged();

    fetchVersionJson(versionJsonUrl);
}

void VersionDetail::clear()
{
    ++m_generation;
    m_id.clear();
    m_plan = TaskPlan{};
    m_error.clear();
    m_state = QStringLiteral("idle");
    emit planChanged();
    emit stateChanged();
}

void VersionDetail::fetchVersionJson(const QString &url)
{
    const int generation = m_generation;
    QNetworkRequest request{QUrl(url)};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(std::chrono::milliseconds(20000));
    request.setHeader(QNetworkRequest::UserAgentHeader, platform::userAgent());

    QNetworkReply *reply = m_nam.get(request);
    m_timeout.start();

    connect(reply, &QNetworkReply::finished, this, [this, reply, generation]() {
        reply->deleteLater();
        if (generation != m_generation)
            return;
        if (reply->error() != QNetworkReply::NoError) {
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

    const int generation = m_generation;
    QNetworkRequest request{QUrl(url)};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(std::chrono::milliseconds(20000));
    request.setHeader(QNetworkRequest::UserAgentHeader, platform::userAgent());

    QNetworkReply *reply = m_nam.get(request);
    m_timeout.start();

    connect(reply, &QNetworkReply::finished, this, [this, reply, generation, versionJson]() {
        reply->deleteLater();
        if (generation != m_generation)
            return;
        if (reply->error() != QNetworkReply::NoError) {
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

    if (m_task) {
        m_task->disconnect(this);
        m_task->cancel();
        m_task->deleteLater();
        m_task = nullptr;
    }

    m_downloadError.clear();
    m_doneBytes = 0;
    m_doneFiles = 0;
    m_downloadTotalBytes = m_plan.totalBytes;
    m_downloadTotalFiles = m_plan.files.size();
    m_speed = 0.0;
    m_downloadState = QStringLiteral("running");
    emit downloadChanged();

    m_task = new DownloadTask(m_plan, resolveTargetDir(), kConcurrency, this);

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
        m_speed = 0.0;
        m_doneFiles = m_downloadTotalFiles;
        m_doneBytes = m_downloadTotalBytes;
        emit downloadChanged();
        emit message(QStringLiteral("完成：下载 %1 个文件，跳过 %2 个已存在").arg(downloaded).arg(skipped));
    });

    connect(m_task, &DownloadTask::failed, this, [this](const QString &reason) {
        m_downloadState = QStringLiteral("failed");
        m_downloadError = reason;
        m_speed = 0.0;
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
    if (!m_task)
        return;
    m_task->disconnect(this);
    m_task->cancel();
    m_task->deleteLater();
    m_task = nullptr;
    m_downloadState = QStringLiteral("idle");
    m_doneBytes = 0;
    m_doneFiles = 0;
    m_speed = 0.0;
    emit downloadChanged();
}
