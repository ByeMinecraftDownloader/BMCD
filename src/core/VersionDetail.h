#pragma once

#include "Plan.h"

#include <QDateTime>
#include <QNetworkAccessManager>
#include <QObject>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

class DownloadTask;

// Everything the UI needs to know about the version the user picked: what it
// costs to download, and — once started — how far along the download is.
class VersionDetail : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString versionId READ versionId NOTIFY planChanged)
    Q_PROPERTY(QString type READ type NOTIFY planChanged)
    Q_PROPERTY(QDateTime releaseTime READ releaseTime NOTIFY planChanged)
    Q_PROPERTY(int javaMajor READ javaMajor NOTIFY planChanged)
    Q_PROPERTY(qint64 totalBytes READ totalBytes NOTIFY planChanged)
    Q_PROPERTY(int fileCount READ fileCount NOTIFY planChanged)
    Q_PROPERTY(int assetCount READ assetCount NOTIFY planChanged)
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)

    Q_PROPERTY(QString downloadState READ downloadState NOTIFY downloadChanged)
    Q_PROPERTY(int doneFiles READ doneFiles NOTIFY downloadChanged)
    Q_PROPERTY(int downloadTotalFiles READ downloadTotalFiles NOTIFY downloadChanged)
    Q_PROPERTY(qint64 doneBytes READ doneBytes NOTIFY downloadChanged)
    Q_PROPERTY(qint64 downloadTotalBytes READ downloadTotalBytes NOTIFY downloadChanged)
    Q_PROPERTY(double speed READ speed NOTIFY downloadChanged)
    Q_PROPERTY(QString downloadError READ downloadError NOTIFY downloadChanged)
    Q_PROPERTY(bool cacheChecking READ cacheChecking NOTIFY downloadChanged)
    Q_PROPERTY(bool cacheChecked READ cacheChecked NOTIFY downloadChanged)
    Q_PROPERTY(QString targetDir READ targetDir CONSTANT)
    Q_PROPERTY(QString hashLabel READ hashLabel CONSTANT)

public:
    explicit VersionDetail(QObject *parent = nullptr);
    ~VersionDetail() override;

    QString versionId() const { return m_id; }
    QString type() const { return m_plan.type; }
    QDateTime releaseTime() const { return m_plan.releaseTime; }
    int javaMajor() const { return m_plan.javaMajor; }
    qint64 totalBytes() const { return m_plan.totalBytes; }
    int fileCount() const { return m_plan.files.size(); }
    int assetCount() const { return m_plan.assetCount; }
    QString state() const { return m_state; }
    QString error() const { return m_error; }

    QString downloadState() const { return m_downloadState; }
    int doneFiles() const { return m_doneFiles; }
    int downloadTotalFiles() const { return m_downloadTotalFiles; }
    qint64 doneBytes() const { return m_doneBytes; }
    qint64 downloadTotalBytes() const { return m_downloadTotalBytes; }
    double speed() const { return m_speed; }
    QString downloadError() const { return m_downloadError; }
    bool cacheChecking() const { return m_cacheChecking; }
    bool cacheChecked() const { return m_cacheChecked; }
    QString targetDir() const;
    QString hashLabel() const { return QStringLiteral("SHA-1"); }

    Q_INVOKABLE void select(const QString &versionId, const QString &versionJsonUrl);
    Q_INVOKABLE void clear();
    Q_INVOKABLE void startDownload();
    Q_INVOKABLE void verifyCache();
    Q_INVOKABLE void pauseDownload();
    Q_INVOKABLE void cancelDownload();

signals:
    void planChanged();
    void stateChanged();
    void downloadChanged();
    void message(const QString &text);

private:
    void fetchVersionJson(const QString &url);
    void fetchVersionJsonAt(const QStringList &urls, int index);
    void fetchAssetIndex(const QJsonObject &versionJson);
    void fetchAssetIndexAt(const QStringList &urls, int index, const QJsonObject &versionJson);
    void finishPlan(const QJsonObject &versionJson, const QJsonObject &assetIndexJson);
    void setState(const QString &state, const QString &error = QString());
    void stopTask();

    QNetworkAccessManager m_nam;
    QTimer m_timeout;
    int m_generation = 0;

    QString m_id;
    QString m_jsonUrl;
    QByteArray m_rawVersionJson;
    TaskPlan m_plan;
    QString m_state = QStringLiteral("idle");
    QString m_error;

    DownloadTask *m_task = nullptr;
    QString m_downloadState = QStringLiteral("idle");
    int m_doneFiles = 0;
    int m_downloadTotalFiles = 0;
    qint64 m_doneBytes = 0;
    qint64 m_downloadTotalBytes = 0;
    double m_speed = 0.0;
    QString m_downloadError;
    bool m_cacheChecking = false;
    bool m_cacheChecked = false;
};
