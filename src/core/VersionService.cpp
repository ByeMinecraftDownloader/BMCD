#include "core/VersionService.h"

#include "core/DownloadSources.h"
#include "core/VersionListModel.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>

namespace {
constexpr const char *kManifestUrl = "https://piston-meta.mojang.com/mc/game/version_manifest_v2.json";
constexpr qint64 kCacheTtlSeconds = 6 * 60 * 60;
} // namespace

VersionService::VersionService(QObject *parent)
    : QObject(parent)
    , m_versions(new VersionListModel(this))
    , m_network(new QNetworkAccessManager(this))
{
    // A fresh cache is good enough for the first paint; the refresh button and
    // the TTL keep it honest instead of hammering piston-meta on every launch.
    if (!loadFromCache())
        startRequest();
}

VersionService::~VersionService() = default;

void VersionService::setLoading(bool loading)
{
    if (m_loading == loading)
        return;
    m_loading = loading;
    emit loadingChanged();
}

void VersionService::setError(const QString &error)
{
    if (m_error == error)
        return;
    m_error = error;
    emit errorChanged();
}

QString VersionService::cacheFilePath() const
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (dir.isEmpty())
        dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return dir + QStringLiteral("/version_manifest_v2.json");
}

bool VersionService::loadFromCache()
{
    const QString path = cacheFilePath();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    const QByteArray payload = file.readAll();
    const QFileInfo info(path);
    if (!parseManifest(payload))
        return false;

    m_fetchedAt = info.lastModified();
    emit fetchedAtChanged();

    return info.lastModified().secsTo(QDateTime::currentDateTime()) < kCacheTtlSeconds;
}

void VersionService::refresh()
{
    startRequest();
}

QString VersionService::urlFor(const QString &versionId) const
{
    return m_versions ? m_versions->urlFor(versionId) : QString();
}

void VersionService::startRequest()
{
    m_manifestCandidates = DownloadSources::candidates(QString::fromLatin1(kManifestUrl),
                                                        QStringLiteral("manifest"));
    m_manifestIndex = 0;
    requestManifest();
}

void VersionService::requestManifest()
{
    if (m_reply) {
        // abort() emits finished() synchronously, and the finished handler
        // clears m_reply; keep our own pointer so the deleteLater() below can
        // never run on a null/dangling reply.
        QNetworkReply *previous = m_reply;
        m_reply = nullptr;
        previous->abort();
        previous->deleteLater();
    }

    const QString url = m_manifestCandidates.value(m_manifestIndex,
                                                   QString::fromLatin1(kManifestUrl));
    QNetworkRequest request{QUrl(url)};
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("BMCD/0.2"));
    request.setTransferTimeout(15000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    setLoading(true);
    setError({});

    m_reply = m_network->get(request);
    connect(m_reply, &QNetworkReply::finished, this, [this] {
        QNetworkReply *reply = m_reply;
        m_reply = nullptr;
        if (!reply)
            return;
        reply->deleteLater();

        setLoading(false);

        if (reply->error() != QNetworkReply::NoError && ++m_manifestIndex < m_manifestCandidates.size()) {
            requestManifest();
            return;
        }

        if (reply->error() != QNetworkReply::NoError) {
            const QString message = reply->errorString();
            // Field diagnostics: Qt swallows qWarning on some setups, so allow an
            // explicit log file instead of guessing why the manifest is missing.
            const QString logPath = qEnvironmentVariable("MC_NET_LOG");
            if (!logPath.isEmpty()) {
                QFile log(logPath);
                if (log.open(QIODevice::Append | QIODevice::Text))
                    log.write(QStringLiteral("manifest error: %1\n").arg(message).toUtf8());
            }
            setError(message);
            if (!m_offline) {
                m_offline = true;
                emit offlineChanged();
            }
            return;
        }

        const QByteArray payload = reply->readAll();
        if (!parseManifest(payload)) {
            setError(tr("版本清单解析失败"));
            return;
        }

        m_fetchedAt = QDateTime::currentDateTime();
        emit fetchedAtChanged();

        if (m_offline) {
            m_offline = false;
            emit offlineChanged();
        }

        QDir().mkpath(QFileInfo(cacheFilePath()).absolutePath());
        QFile cache(cacheFilePath());
        if (!cache.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            qWarning().noquote() << "cannot write manifest cache:" << cache.errorString();
            return;
        }
        cache.write(payload);
    });
}

bool VersionService::parseManifest(const QByteArray &payload)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return false;

    const QJsonObject root = document.object();
    const QJsonObject latest = root.value(QStringLiteral("latest")).toObject();
    const QString latestRelease = latest.value(QStringLiteral("release")).toString();
    const QString latestSnapshot = latest.value(QStringLiteral("snapshot")).toString();

    const QJsonArray versions = root.value(QStringLiteral("versions")).toArray();
    if (versions.isEmpty())
        return false;

    QList<VersionEntry> entries;
    entries.reserve(versions.size());
    for (const QJsonValue &value : versions) {
        const QJsonObject object = value.toObject();
        VersionEntry entry;
        entry.id = object.value(QStringLiteral("id")).toString();
        if (entry.id.isEmpty())
            continue;
        entry.type = object.value(QStringLiteral("type")).toString();
        entry.url = object.value(QStringLiteral("url")).toString();
        entry.releaseTime = QDateTime::fromString(object.value(QStringLiteral("releaseTime")).toString(),
                                                  Qt::ISODate);
        entry.latest = entry.id == latestRelease || entry.id == latestSnapshot;
        entries.append(entry);
    }

    if (entries.isEmpty())
        return false;

    m_versions->reset(std::move(entries));

    if (m_latestRelease != latestRelease || m_latestSnapshot != latestSnapshot) {
        m_latestRelease = latestRelease;
        m_latestSnapshot = latestSnapshot;
        emit latestChanged();
    }

    return true;
}
