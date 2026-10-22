#pragma once

#include "Plan.h"

#include <QElapsedTimer>
#include <QFile>
#include <QHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QTimer>
#include <QVector>

class QNetworkReply;

// Downloads a TaskPlan into a target directory:
//   * a handful of transfers in flight at once,
//   * resumes from "<file>.part" with a Range request,
//   * verifies the strongest available hash (SHA-256 > SHA-512 > SHA-1)
//     before renaming into place,
//   * skips files that are already there and still match their hash.
class DownloadTask : public QObject
{
    Q_OBJECT

public:
    DownloadTask(TaskPlan plan, QString root, int concurrency, QObject *parent = nullptr);
    ~DownloadTask() override;

    void start();
    void pause();
    void cancel();

    // Hash every file that is already on disk and report the result without
    // touching the network or writing missing files.
    void setVerifyOnly(bool verifyOnly) { m_verifyOnly = verifyOnly; }
    bool verifyOnly() const { return m_verifyOnly; }

    QString root() const { return m_root; }
    const TaskPlan &plan() const { return m_plan; }

    // For tests: cap how many files are actually transferred / retried.
    void setLimit(int limit) { m_limit = limit; }
    void setMaxAttemptsForTests(int attempts) { m_maxAttempts = attempts; }

signals:
    void progress(qint64 doneBytes, qint64 totalBytes, int doneFiles, int totalFiles, double bytesPerSecond);
    void log(const QString &message);
    void fileProgress(const QString &path, qint64 doneBytes, qint64 totalBytes, int state);
    void finished(int skipped, int downloaded);
    void failed(const QString &message);
    // verifyOnly() result: every planned file was hashed; missing counts the
    // files that were absent or did not match.
    void verified(int cached, int missing, qint64 doneBytes, qint64 totalBytes);

private:
    struct Transfer
    {
        int index = -1;
        QFile *file = nullptr;
        QCryptographicHash *hasher = nullptr;
        QString path;
        qint64 totalBytes = 0;
        qint64 offset = 0;
        qint64 counted = 0; // bytes credited to the progress bar for this attempt
        int attempts = 0;
        int sourceIndex = 0;
        int sourceCount = 1;
    };

    void pump();
    bool startOne(int index);
    bool advanceSource(const Transfer &transfer);
    bool verifyExisting(const PlanFile &file, const QString &absPath) const;
    void cleanupTransfer(QNetworkReply *reply, Transfer &transfer);
    void failOne(int index, const QString &reason);
    void emitProgress();
    void finishIfIdle();
    void abortActive();

    QString absolutePath(const PlanFile &file) const;

    TaskPlan m_plan;
    QString m_root;
    int m_concurrency = 6;
    int m_limit = -1;
    int m_maxAttempts = 3;

    QNetworkAccessManager m_nam;
    QHash<QNetworkReply *, Transfer> m_active;
    QHash<int, int> m_sourceIndex;
    QHash<int, int> m_attempts;
    QVector<int> m_pending;
    bool m_paused = false;
    bool m_cancelled = false;
    bool m_done = false;
    bool m_verifyOnly = false;

    qint64 m_doneBytes = 0;
    qint64 m_totalBytes = 0;
    int m_doneFiles = 0;
    int m_totalFiles = 0;
    int m_skipped = 0;
    int m_downloaded = 0;
    int m_failures = 0;
    int m_missing = 0;

    QTimer m_tick;
    QElapsedTimer m_clock;
    qint64 m_lastBytes = 0;
    qint64 m_lastMs = 0;
    double m_speed = 0.0;
};
