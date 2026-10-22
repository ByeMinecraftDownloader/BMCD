#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include "platform/Process.h"
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QTimer>
#include <QVariantList>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

// Installs a mod loader into a game directory and registers it with the
// launcher's launcher_profiles.json.
//
// The state machine is deliberately linear:
//   idle -> resolving -> downloading -> verifying -> installing -> merging
//        -> done / error
//
// Only official endpoints are used. The installer jar is downloaded from the
// official loader Maven/meta host, its SHA-1 comes from the same official
// source, and it is started with an explicit argv (never through a shell).
// Fabric/Quilt use their installers' client command; Forge/NeoForge use the
// installer's --installClient argument.
class LoaderInstaller : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString logPath READ logPath NOTIFY changed)
    Q_PROPERTY(QString versionId READ versionId NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    // Loader versions that the official meta advertises for one game version.
    // This is a small, read-only catalogue; installing never depends on it.
    Q_PROPERTY(QVariantList catalog READ catalog NOTIFY catalogChanged)
    Q_PROPERTY(bool catalogLoading READ catalogLoading NOTIFY catalogChanged)
    Q_PROPERTY(QString catalogError READ catalogError NOTIFY catalogChanged)

public:
    struct Request
    {
        QString gameDir;
        QString minecraftVersion;
        QString loaderType;      // fabric | quilt | forge | neoforge
        QString loaderVersion;
        QString javaPath;        // empty: detect from settings / PATH
        QString profileName;
        QString javaArgs;
    };

    struct Hooks
    {
        // Returns the response body, or an empty body and *error on failure.
        std::function<QByteArray(const QUrl &, QString *)> fetch;
        // Runs the installer; returns the exit code. Used by tests to avoid
        // requiring a real Java runtime and network.
        std::function<int(const QString &program, const QStringList &arguments,
                          const QString &workingDir, QByteArray *output, QString *error)> run;
    };

    explicit LoaderInstaller(QObject *parent = nullptr);
    ~LoaderInstaller() override;

    QString state() const { return m_state; }
    double progress() const { return m_progress; }
    QString message() const { return m_message; }
    QString error() const { return m_error; }
    QString logPath() const { return m_logPath; }
    QString versionId() const { return m_versionId; }
    bool busy() const;
    QVariantList catalog() const { return m_catalog; }
    bool catalogLoading() const { return m_catalogLoading; }
    QString catalogError() const { return m_catalogError; }

    Q_INVOKABLE void fetchLoaderVersions(const QString &loaderType, const QString &minecraftVersion);
    static QVariantList parseLoaderVersions(const QString &loaderType, const QString &minecraftVersion,
                                            const QByteArray &body, QString *error);

    Q_INVOKABLE void install(const Request &request);
    Q_INVOKABLE void retry();
    Q_INVOKABLE void cancel();

    const Request &request() const { return m_request; }
    QStringList installerArguments() const { return m_arguments; }
    static bool isAllowedUrl(const QUrl &url);
    static bool isSupportedLoader(const QString &type);

    void setHooksForTests(const Hooks &hooks);
    void setCacheDirForTests(const QString &dir);

signals:
    void changed();
    void log(const QString &line);
    void finished(bool ok);
    void stateChanged(const QString &state);
    void catalogChanged();

private:
    enum class Phase { Idle, Resolving, Downloading, Verifying, Installing, Merging, Done, Error };

    struct Resolved
    {
        QUrl installerUrl;
        QUrl checksumUrl;
        QUrl metaUrl;
        QString installerName;
        QString installerPath;
        QString versionIdHint;
        QStringList installerArgs;
        bool useMeta = false;
    };

    void setPhase(Phase phase);
    void setProgress(double progress);
    void setMessage(const QString &message);
    void fail(const QString &error);
    void reset();
    void resolve();
    void resolveMeta();
    void resolveChecksum();
    void downloadInstaller();
    void verifyInstaller();
    void runInstaller();
    void mergeProfile();
    void finish();
    void fetch(const QUrl &url, const std::function<void(bool, QByteArray, const QString &)> &done);
    void abortPending();
    void startProcess();
    void processOutput();
    void processFinished(int exitCode, platform::Process::ExitStatus status);
    void writeLog(const QString &line);
    void abortCatalog();
    void finishCatalog(const QByteArray &body);
    bool resolveJava(QString *path, QString *error) const;
    bool installJarPath(QString *path) const;
    bool verifyChecksum(const QByteArray &body, const QString &expected, QString *error) const;
    QString discoverVersion(QString *error);
    bool verifyInstalledVersion(const QString &versionId, QString *error) const;
    QJsonObject versionJson(const QString &versionId, QString *error) const;
    QStringList versionIds() const;
    QString cacheDir() const;
    QString logDir() const;

    Request m_request;
    Hooks m_hooks;
    QString m_cacheDirForTests;
    Phase m_phase = Phase::Idle;
    QString m_state = QStringLiteral("idle");
    double m_progress = 0.0;
    QString m_message;
    QString m_error;
    QString m_logPath;
    QString m_versionId;
    QString m_checksum;
    QByteArray m_installerBytes;
    Resolved m_resolved;
    QStringList m_arguments;
    QNetworkAccessManager *m_network = nullptr;
    QNetworkReply *m_reply = nullptr;
    QNetworkAccessManager *m_catalogNetwork = nullptr;
    QNetworkReply *m_catalogReply = nullptr;
    QTimer m_catalogTimeout;
    int m_catalogGeneration = 0;
    QVariantList m_catalog;
    QString m_catalogType;
    QString m_catalogMinecraft;
    bool m_catalogLoading = false;
    QString m_catalogError;
    platform::Process m_process;
    QTimer m_processTimeout;
    QByteArray m_processOutput;
    int m_generation = 0;
    QSet<QString> m_versionsBefore;
    qint64 m_startedAt = 0;
};
