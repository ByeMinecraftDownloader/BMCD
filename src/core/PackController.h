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
    Q_PROPERTY(QString minecraftVersion READ minecraftVersion NOTIFY packChanged)
    Q_PROPERTY(QString loaderType READ loaderType NOTIFY packChanged)
    Q_PROPERTY(QString loaderVersion READ loaderVersion NOTIFY packChanged)
    Q_PROPERTY(QString packName READ packName NOTIFY packChanged)
    Q_PROPERTY(int fileCount READ fileCount NOTIFY packChanged)
    // True when this plan carries the vanilla client/libraries/assets
    // (a plain version prepared here), false for mods-only packs.
    Q_PROPERTY(bool hasGameFiles READ hasGameFiles NOTIFY packChanged)
    Q_PROPERTY(qint64 totalBytes READ totalBytes NOTIFY packChanged)
    Q_PROPERTY(QString sourceRoot READ sourceRoot NOTIFY packChanged)
    Q_PROPERTY(QString hashLabel READ hashLabel NOTIFY packChanged)
    Q_PROPERTY(bool paused READ paused NOTIFY taskChanged)
    Q_PROPERTY(QVariantList downloadFiles READ downloadFiles NOTIFY downloadFilesChanged)
    Q_PROPERTY(QString targetDir READ targetDir CONSTANT)
    Q_PROPERTY(QString deployDir READ deployDir WRITE setDeployDir NOTIFY deployDirChanged)
    Q_PROPERTY(bool cooked READ cooked NOTIFY taskChanged)
    // True when this plan was adopted from a version already downloaded by
    // VersionDetail; such a plan is verified, not cooked again.
    Q_PROPERTY(bool adopted READ adopted NOTIFY packChanged)
    Q_PROPERTY(QString lastOutput READ lastOutput NOTIFY taskChanged)
    Q_PROPERTY(int conflictCount READ conflictCount NOTIFY preflightChanged)
    Q_PROPERTY(bool dryRunOk READ dryRunOk NOTIFY preflightChanged)
    Q_PROPERTY(QStringList missingFiles READ missingFiles NOTIFY preflightChanged)
    Q_PROPERTY(int licenceDenied READ licenceDenied NOTIFY preflightChanged)
    Q_PROPERTY(int licenceNeedsConfirm READ licenceNeedsConfirm NOTIFY preflightChanged)
    // Grouped authorisation: one row per parent mod instead of per file.
    Q_PROPERTY(QVariantList licenceGroups READ licenceGroups NOTIFY preflightChanged)
    Q_PROPERTY(int licenceDeniedGroups READ licenceDeniedGroups NOTIFY preflightChanged)
    Q_PROPERTY(int licenceNeedsConfirmGroups READ licenceNeedsConfirmGroups NOTIFY preflightChanged)
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
    QString minecraftVersion() const { return m_pack.minecraftVersion; }
    QString loaderType() const { return m_pack.loaderType; }
    QString loaderVersion() const { return m_pack.loaderVersion; }
    QString packName() const { return m_pack.name; }
    int fileCount() const { return m_pack.fileCount(); }
    bool hasGameFiles() const;
    qint64 totalBytes() const { return m_pack.totalBytes(); }
    QString sourceRoot() const { return m_pack.target; }
    QString hashLabel() const;
    bool paused() const { return m_paused; }
    QVariantList downloadFiles() const { return m_downloadFiles; }
    QString targetDir() const;
    QString deployDir() const;
    void setDeployDir(const QString &dir);
    bool cooked() const { return m_cooked; }
    bool adopted() const { return m_adopted; }
    QString lastOutput() const { return m_lastOutput; }
    int conflictCount() const { return m_conflictCount; }
    bool dryRunOk() const { return m_dryRunOk; }
    QStringList missingFiles() const { return m_missingFiles; }
    int licenceDenied() const { return m_licenceDenied; }
    int licenceNeedsConfirm() const { return m_licenceNeedsConfirm; }
    QVariantList licenceGroups() const { return m_licenceGroups; }
    int licenceDeniedGroups() const { return m_licenceDeniedGroups; }
    int licenceNeedsConfirmGroups() const { return m_licenceNeedsConfirmGroups; }
    QStringList deniedFiles() const { return m_deniedFiles; }
    bool busy() const { return m_busy; }
    int taskDone() const { return m_taskDone; }
    int taskTotal() const { return m_taskTotal; }
    QString taskStep() const { return m_taskStep; }
    QString lastResult() const { return m_lastResult; }

    Q_INVOKABLE void prepare(const QString &versionId, const QString &versionJsonUrl);
    // Import the plan for a plain version whose files were downloaded by
    // VersionDetail. The cook result lives in VersionDetail's targetDir, not
    // in this controller's private cookingDir.
    Q_INVOKABLE void prepareDownloaded(const QString &versionId,
                                       const QString &versionJsonUrl,
                                       const QString &sourceRoot);
    Q_INVOKABLE void importPack(const QString &archivePath);
    Q_INVOKABLE bool adoptPack(const QString &json);
    Q_INVOKABLE void clear();
    Q_INVOKABLE void confirm(const QString &path);
    Q_INVOKABLE void confirmGroup(const QString &groupId);
    Q_INVOKABLE void preflight();
    Q_INVOKABLE void serve();
    Q_INVOKABLE void serveTo(const QString &targetDir);
    Q_INVOKABLE void exportPack(const QString &format);
    Q_INVOKABLE void exportPackTo(const QString &format, const QString &outputPath);
    Q_INVOKABLE void pauseDownload();
    Q_INVOKABLE void resumeDownload();
    Q_INVOKABLE void cancelDownload();
    Q_INVOKABLE void startDownload();

signals:
    void stateChanged();
    void packChanged();
    void preflightChanged();
    void busyChanged();
    void taskChanged();
    void message(const QString &text);
    void downloadFilesChanged();
    void deployDirChanged();

private:
    QString cookingDir() const;
    void buildPack(const QJsonObject &versionJson, const QJsonObject &assetIndexJson);
    void fetchVersionJsonAt(const QStringList &urls, int index);
    void fetchAssetIndexAt(const QStringList &urls, int index, const QJsonObject &versionJson);
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
    QVariantList m_licenceGroups;
    int m_licenceDeniedGroups = 0;
    int m_licenceNeedsConfirmGroups = 0;
    QStringList m_deniedFiles;
    QStringList m_confirmed;

    bool m_busy = false;
    bool m_paused = false;
    int m_taskDone = 0;
    int m_taskTotal = 0;
    QString m_taskStep;
    QVariantList m_downloadFiles;
    bool m_filesDirty = false;
    QString m_lastResult;
    QString m_lastOutput;
    QString m_deployDir;
    bool m_cooked = false;
    bool m_adopted = false;
    QString m_pendingRoot;
    bool m_pendingCooked = false;
    bool m_adoptCooked = false;
};
