#pragma once

#include <QList>
#include <QObject>
#include "platform/Process.h"
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

// Finds a usable Java executable without bundling a runtime. The UI only needs
// one candidate: explicit MC_JAVA / saved setting first, then JAVA_HOME, PATH
// and the usual per-platform JDK directories. Probing is asynchronous because a
// broken java shim can otherwise stall the first frame.
class JavaLocator : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString path READ path NOTIFY changed)
    Q_PROPERTY(QString version READ version NOTIFY changed)
    Q_PROPERTY(int major READ major NOTIFY changed)
    Q_PROPERTY(QString source READ source NOTIFY changed)
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool checking READ checking NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)

public:
    explicit JavaLocator(QObject *parent = nullptr);
    ~JavaLocator() override;

    QString path() const { return m_path; }
    QString version() const { return m_version; }
    int major() const { return m_major; }
    QString source() const { return m_source; }
    bool available() const { return m_major > 0 && !m_path.isEmpty(); }
    bool checking() const { return m_checking; }
    QString error() const { return m_error; }

    Q_INVOKABLE void detect();
    Q_INVOKABLE void detectAgain();

    // Exposed for the unit tests and for callers that already have a `java
    // -version` transcript (for example a launcher log).
    static QString parseVersion(const QString &output);
    static int parseMajor(const QString &output);

    // Tests use this to keep auto-detection deterministic; production always
    // scans the platform directories.
    void setScanSystemDirsForTests(bool enabled);
    void setSystemRootsForTests(const QStringList &roots);

signals:
    void changed();

private:
    struct Candidate
    {
        QString path;
        QString source;
        bool explicitlyConfigured = false;
        bool preferFirst = false;
    };

    void detectInternal(bool force);
    void collectCandidates();
    void runNext();
    void processFinished(int exitCode, platform::Process::ExitStatus status);
    void processFailed(platform::Process::ProcessError error);
    void processTimedOut();
    void select(const Candidate &candidate);
    void finishWithError(const QString &error);
    void finish();
    void addCandidate(const QString &path, const QString &source, bool explicitPath,
                      bool preferFirst, QSet<QString> &seen);

    QList<Candidate> m_candidates;
    int m_index = 0;
    bool m_checking = false;
    bool m_runningCandidate = false;
    bool m_scanSystemDirs = true;
    bool m_checked = false;
    QStringList m_systemRootsForTests;
    platform::Process m_process;
    QTimer m_timeout;

    Candidate m_best;
    int m_bestMajor = 0;
    QString m_path;
    QString m_version;
    int m_major = 0;
    QString m_source;
    QString m_error;
};
