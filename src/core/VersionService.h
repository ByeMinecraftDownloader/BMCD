#pragma once

#include <QDateTime>
#include <QObject>
#include <QString>
#include <qqmlintegration.h>

class QNetworkAccessManager;
class QNetworkReply;
class VersionListModel;

// Owns the Mojang version manifest: fetches it, caches it on disk and keeps a
// VersionListModel in sync. Registered as a QML singleton.
class VersionService : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(VersionListModel *versions READ versions CONSTANT)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(bool offline READ offline NOTIFY offlineChanged)
    Q_PROPERTY(QString latestRelease READ latestRelease NOTIFY latestChanged)
    Q_PROPERTY(QString latestSnapshot READ latestSnapshot NOTIFY latestChanged)
    Q_PROPERTY(QDateTime fetchedAt READ fetchedAt NOTIFY fetchedAtChanged)

public:
    explicit VersionService(QObject *parent = nullptr);
    ~VersionService() override;

    VersionListModel *versions() const { return m_versions; }
    bool loading() const { return m_loading; }
    QString error() const { return m_error; }
    bool offline() const { return m_offline; }
    QString latestRelease() const { return m_latestRelease; }
    QString latestSnapshot() const { return m_latestSnapshot; }
    QDateTime fetchedAt() const { return m_fetchedAt; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE QString urlFor(const QString &versionId) const;

signals:
    void loadingChanged();
    void errorChanged();
    void offlineChanged();
    void latestChanged();
    void fetchedAtChanged();

private:
    bool loadFromCache();
    void startRequest();
    bool parseManifest(const QByteArray &payload);
    QString cacheFilePath() const;
    void setLoading(bool loading);
    void setError(const QString &error);

    VersionListModel *m_versions = nullptr;
    QNetworkAccessManager *m_network = nullptr;
    QNetworkReply *m_reply = nullptr;
    bool m_loading = false;
    bool m_offline = false;
    QString m_error;
    QString m_latestRelease;
    QString m_latestSnapshot;
    QDateTime m_fetchedAt;
};
