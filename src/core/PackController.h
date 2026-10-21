#pragma once

#include "Pack.h"

#include <QNetworkAccessManager>
#include <QObject>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

class DownloadTask;

// Bridges the pack model to QML (the serve screen). Owns the "make a pack from
// a version, then export it or deploy it" flow and keeps every blocking file
// operation off the GUI thread.
class PackController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString state READ state NOTIFY stateChanged)          // idle|loading|ready|working|done|error
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(QString versionId READ versionId NOTIFY packChanged)
    Q_PROPERTY(QString packName READ packName NOTIFY packChanged)
    Q_PROPERTY(int fileCount READ fileCount NOTIFY packChanged)
    Q_PROPERTY(qint64 totalBytes READ totalBytes NOTIFY packChanged)
    Q_PROPERTY(QString sourceRoot READ sourceRoot NOTIFY packChanged)
    Q_PROPERTY(QString targetDir READ targetDir CONSTANT)
    Q_PROPERTY(int conflictCount READ conflictCount NOTIFY preflightChanged)
    Q_PROPERTY(bool dryRunOk READ dryRunOk NOTIFY preflightChanged)
    Q_PROPERTY(QStringList missingFiles READ missingFiles NOTIFY preflightChanged)
    Q_PROPERTY(int licenceDenied READ licenceDenied NOTIFY preflightChanged)
    Q_PROPERTY(int licenceNeedsConfirm READ licenceNeedsConfirm NOTIFY preflightChanged)
    Q_PROPERTY(QStringList deniedFiles READ deniedFiles NOTIFY preflightChanged)

    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(int taskDone READ taskDone NOTIFY taskChanged)
    Q_PROPERTY(int taskTotal READ taskTotal NOTIFY taskChanged)
    Q_PROPERTY(QString taskStep READ taskStep NOTIFY taskChanged)
    Q_PROPERTY(QString lastResult READ lastResult NOTIFY taskChanged)

public:
    explicit PackController(QObject *parent = nullptr);

    QString state() const { return m_state; }
    QString error() const { return m_error; }
    QString versionId() const { return m_pack.versionId; }
    QString packName() const { return m_pack.name; }
    int fileCount() const { return m_pack.fileCount(); }
    qint64 totalBytes() const { return m_pack.totalBytes(); }
    QString sourceRoot() const { return m_pack.target; }
    QString targetDir() const;
    int conflictCount() const { return m_conflictCount; }
    bool dryRunOk() const { return m_dryRunOk; }
    QStringList missingFiles() const { return m_missingFiles; }
    int licenceDenied() const { return m_licenceDenied; }
    int licenceNeedsConfirm() const { return m_licenceNeedsConfirm; }
    QStringList deniedFiles() const { return m_deniedFiles; }
    bool busy() const { return m_busy; }
    int taskDone() const { return m_taskDone; }
    int taskTotal() const { return m_taskTotal; }
    QString taskStep() const { return m_taskStep; }
    QString lastResult() const { return m_lastResult; }

    Q_INVOKABLE void prepare(const QString &versionId, const QString &versionJsonUrl);
    Q_INVOKABLE void importPack(const QString &archivePath);
    Q_INVOKABLE void clear();
    Q_INVOKABLE void confirm(const QString &path);
    Q_INVOKABLE void preflight();
    Q_INVOKABLE void serve();
    Q_INVOKABLE void exportPack(const QString &format);

signals:
    void stateChanged();
    void packChanged();
    void preflightChanged();
    void busyChanged();
    void taskChanged();
    void message(const QString &text);

private:
    void buildPack(const QJsonObject &versionJson, const QJsonObject &assetIndexJson);
    void startDownload();
    void stopDownload();
    void setState(const QString &state, const QString &error = QString());
    void setBusy(bool busy);
    void recomputeGate();
    void reportProgress(int done, int total, const QString &step);

    QNetworkAccessManager m_nam;
    QTimer m_timeout;
    DownloadTask *m_download = nullptr;
    int m_generation = 0;

    Pack m_pack;
    QString m_state = QStringLiteral("idle");
    QString m_error;

    int m_conflictCount = 0;
    bool m_dryRunOk = false;
    QStringList m_missingFiles;
    int m_licenceDenied = 0;
    int m_licenceNeedsConfirm = 0;
    QStringList m_deniedFiles;
    QStringList m_confirmed;

    bool m_busy = false;
    int m_taskDone = 0;
    int m_taskTotal = 0;
    QString m_taskStep;
    QString m_lastResult;
};
