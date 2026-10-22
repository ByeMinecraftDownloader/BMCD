#include "DownloadTask.h"

#include "DownloadSources.h"

#include "platform/FileOps.h"
#include "platform/Console.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QNetworkReply>
#include <QNetworkRequest>

#include <algorithm>
#include <chrono>

namespace {

constexpr int kChunk = 64 * 1024;
constexpr int kVerifyBatch = 32;

struct HashSpec
{
    QCryptographicHash::Algorithm algorithm = QCryptographicHash::Sha1;
    QString expected;
    const char *label = "SHA-1";
};

HashSpec hashSpecFor(const PlanFile &file)
{
    if (!file.sha256.isEmpty())
        return {QCryptographicHash::Sha256, file.sha256.toLower(), "SHA-256"};
    if (!file.sha512.isEmpty())
        return {QCryptographicHash::Sha512, file.sha512.toLower(), "SHA-512"};
    return {QCryptographicHash::Sha1, file.sha1.toLower(), "SHA-1"};
}

} // namespace

DownloadTask::DownloadTask(TaskPlan plan, QString root, int concurrency, QObject *parent)
    : QObject(parent)
    , m_plan(std::move(plan))
    , m_root(std::move(root))
    , m_concurrency(std::max(1, concurrency))
{
    m_totalBytes = m_plan.totalBytes;
    m_totalFiles = m_plan.files.size();

    m_tick.setInterval(500);
    connect(&m_tick, &QTimer::timeout, this, &DownloadTask::emitProgress);
}

DownloadTask::~DownloadTask()
{
    abortActive();
}

QString DownloadTask::absolutePath(const PlanFile &file) const
{
    return m_root + QLatin1Char('/') + file.path;
}

void DownloadTask::start()
{
    m_paused = false;
    m_cancelled = false;
    m_done = false;
    m_doneBytes = 0;
    m_doneFiles = 0;
    m_skipped = 0;
    m_downloaded = 0;
    m_failures = 0;
    m_missing = 0;
    m_clock.start();
    m_lastMs = 0;
    m_lastBytes = 0;

    m_pending.clear();
    m_sourceIndex.clear();
    m_attempts.clear();
    for (int i = 0; i < m_plan.files.size(); ++i)
        m_pending.append(i);
    if (m_limit >= 0 && m_limit < m_pending.size()) {
        m_pending.resize(m_limit);
        // Limited runs (tests) must report a coherent 100% at the end.
        m_totalBytes = 0;
        for (int index : m_pending)
            m_totalBytes += m_plan.files.at(index).size;
        m_totalFiles = m_pending.size();
    }

    m_tick.start();
    emitProgress();
    pump();
    finishIfIdle();
}

void DownloadTask::pause()
{
    if (m_done)
        return;
    m_paused = true;
    abortActive();
    emit log(QStringLiteral("paused"));
    finishIfIdle();
}

void DownloadTask::cancel()
{
    m_cancelled = true;
    m_paused = true;
    abortActive();
    m_tick.stop();
    if (!m_done) {
        m_done = true;
        emit log(QStringLiteral("cancelled"));
        emit failed(QStringLiteral("已取消"));
    }
}

void DownloadTask::abortActive()
{
    const auto replies = m_active.keys();
    for (QNetworkReply *reply : replies) {
        auto it = m_active.find(reply);
        if (it == m_active.end())
            continue;
        Transfer transfer = it.value();
        // Keep the partial file so the next start resumes from it.
        cleanupTransfer(reply, transfer);
    }
}

void DownloadTask::cleanupTransfer(QNetworkReply *reply, Transfer &transfer)
{
    m_active.remove(reply);
    if (transfer.file) {
        transfer.file->flush();
        transfer.file->close();
        delete transfer.file;
        transfer.file = nullptr;
    }
    delete transfer.hasher;
    transfer.hasher = nullptr;
    reply->abort();
    reply->deleteLater();
}

void DownloadTask::pump()
{
    if (m_paused || m_cancelled || m_done)
        return;

    // Verifying already-downloaded files hashes them synchronously.  A
    // version may have thousands of cached assets, so do that work in small
    // batches and yield between batches; otherwise the GUI looks frozen at
    // 0% until every cached file has been hashed.
    int skipped = 0;
    while (m_active.size() < m_concurrency && !m_pending.isEmpty()) {
        const int index = m_pending.takeFirst();
        if (startOne(index) && ++skipped >= kVerifyBatch) {
            emitProgress();
            QTimer::singleShot(0, this, &DownloadTask::pump);
            return;
        }
    }
    if (skipped > 0)
        emitProgress();
    finishIfIdle();
}

bool DownloadTask::verifyExisting(const PlanFile &file, const QString &absPath) const
{
    QFileInfo info(absPath);
    if (!info.exists() || !info.isFile())
        return false;
    if (file.size > 0 && info.size() != file.size)
        return false;
    const HashSpec spec = hashSpecFor(file);
    if (spec.expected.isEmpty() || file.size == 0)
        return false; // nothing to prove integrity with: re-download

    QFile check(absPath);
    if (!check.open(QIODevice::ReadOnly))
        return false;

    QCryptographicHash hasher(spec.algorithm);
    if (!hasher.addData(&check))
        return false;
    return QString::fromLatin1(hasher.result().toHex()) == spec.expected;
}

bool DownloadTask::startOne(int index)
{
    const PlanFile &file = m_plan.files.at(index);
    const QString absPath = absolutePath(file);

    // already downloaded and intact
    if (verifyExisting(file, absPath)) {
        m_attempts.remove(index);
        ++m_doneFiles;
        ++m_skipped;
        m_doneBytes += file.size;
        emit fileProgress(file.path, file.size, file.size, 2);
        return true;
    }
    if (m_verifyOnly) {
        ++m_missing;
        return true;
    }

    const QStringList sources = DownloadSources::candidates(file.url, file.kind, m_plan.id);
    const int sourceIndex = m_sourceIndex.value(index, 0);
    if (sources.isEmpty() || sourceIndex >= sources.size()) {
        failOne(index, QStringLiteral("没有可用的下载源"));
        return false;
    }
    if (QFileInfo::exists(absPath) && !platform::removeIfExists(absPath)) {
        failOne(index, QStringLiteral("无法覆盖已存在的文件 %1（可能被占用）").arg(file.path));
        return false;
    }

    QDir().mkpath(QFileInfo(absPath).absolutePath());

    const QString partPath = absPath + QStringLiteral(".part");
    QFileInfo partInfo(partPath);
    qint64 offset = partInfo.exists() ? partInfo.size() : 0;
    if (file.size > 0 && offset > file.size) {
        QFile::remove(partPath);
        offset = 0;
    }

    auto *partFile = new QFile(partPath);
    if (!partFile->open(QIODevice::ReadWrite | QIODevice::Append)) {
        delete partFile;
        failOne(index, QStringLiteral("无法写入 %1").arg(partPath));
        return false;
    }

    auto *hasher = new QCryptographicHash(hashSpecFor(file).algorithm);
    if (offset > 0) {
        // resume: seed the hasher with what is already on disk
        QFile existing(partPath);
        if (existing.open(QIODevice::ReadOnly)) {
            QByteArray buffer;
            while (!(buffer = existing.read(kChunk)).isEmpty())
                hasher->addData(buffer);
        }
    }

    QNetworkRequest request(QUrl(sources.at(sourceIndex)));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(std::chrono::milliseconds(30000));
    request.setHeader(QNetworkRequest::UserAgentHeader, platform::userAgent());
    if (offset > 0)
        request.setRawHeader("Range", "bytes=" + QByteArray::number(offset) + "-");

    QNetworkReply *reply = m_nam.get(request);

    Transfer transfer;
    transfer.index = index;
    transfer.file = partFile;
    transfer.hasher = hasher;
    transfer.path = file.path;
    transfer.totalBytes = file.size;
    transfer.offset = offset;
    transfer.counted = 0;
    transfer.sourceIndex = sourceIndex;
    transfer.sourceCount = sources.size();
    transfer.attempts = m_attempts.value(index, 0);
    // Bytes already on disk count toward the bar, otherwise a resumed file
    // makes the progress jump backwards when it finishes.
    m_doneBytes += offset;
    m_active.insert(reply, transfer);
    emit fileProgress(file.path, offset, file.size, 1);

    connect(reply, &QNetworkReply::readyRead, this, [this, reply]() {
        auto it = m_active.find(reply);
        if (it == m_active.end())
            return;
        const QByteArray data = reply->readAll();
        if (data.isEmpty())
            return;
        if (it->file->write(data) != data.size()) {
            const int index = it->index;
            Transfer broken = *it;
            cleanupTransfer(reply, broken);
            failOne(index, QStringLiteral("写入失败"));
            return;
        }
        it->hasher->addData(data);
        it->counted += data.size();
        m_doneBytes += data.size();
        emit fileProgress(it->path, it->file->pos(), it->totalBytes, 1);
        emitProgress();
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        auto it = m_active.find(reply);
        if (it == m_active.end()) {
            reply->deleteLater();
            return;
        }
        Transfer transfer = *it;
        m_active.remove(reply);
        reply->deleteLater();

        const PlanFile &file = m_plan.files.at(transfer.index);
        transfer.file->flush();

        if (m_cancelled || m_paused) {
            transfer.file->close();
            delete transfer.file;
            delete transfer.hasher;
            return; // the .part file stays for the next run
        }

        const QVariant status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
        const bool rejectedRange = transfer.offset > 0 && status.isValid() && status.toInt() == 200;
        if (rejectedRange) {
            // Server ignored Range: start this file over from byte zero.
            transfer.file->close();
            delete transfer.file;
            delete transfer.hasher;
            m_doneBytes = std::max<qint64>(0, m_doneBytes - transfer.offset - transfer.counted);
            QFile::remove(absolutePath(file) + QStringLiteral(".part"));
            if (advanceSource(transfer)) {
                m_pending.prepend(transfer.index);
                pump();
                return;
            }
            ++transfer.attempts;
            m_attempts[transfer.index] = transfer.attempts;
            if (transfer.attempts >= m_maxAttempts) {
                failOne(transfer.index, QStringLiteral("服务器不支持断点续传"));
            } else {
                m_pending.prepend(transfer.index);
                pump();
            }
            return;
        }

        if (reply->error() != QNetworkReply::NoError) {
            transfer.file->close();
            delete transfer.file;
            delete transfer.hasher;
            m_doneBytes = std::max<qint64>(0, m_doneBytes - transfer.offset - transfer.counted);
            if (advanceSource(transfer)) {
                m_pending.prepend(transfer.index);
                pump();
                return;
            }
            ++transfer.attempts;
            m_attempts[transfer.index] = transfer.attempts;
            if (transfer.attempts >= m_maxAttempts) {
                failOne(transfer.index, reply->errorString());
            } else {
                m_pending.prepend(transfer.index);
                QTimer::singleShot(300 * transfer.attempts, this, &DownloadTask::pump);
            }
            return;
        }

        const qint64 written = transfer.file->size();
        transfer.file->close();
        delete transfer.file;
        transfer.file = nullptr;

        bool ok = true;
        QString error;
        if (file.size > 0 && written != file.size) {
            ok = false;
            error = QStringLiteral("大小不符（预期 %1，实际 %2）").arg(file.size).arg(written);
        } else {
            const HashSpec spec = hashSpecFor(file);
            if (!spec.expected.isEmpty()) {
                const QByteArray actual = transfer.hasher->result().toHex();
                if (QString::fromLatin1(actual) != spec.expected) {
                    ok = false;
                    error = QStringLiteral("%1 校验失败").arg(QLatin1String(spec.label));
                }
            }
        }
        delete transfer.hasher;

        if (!ok) {
            m_doneBytes = std::max<qint64>(0, m_doneBytes - transfer.offset - transfer.counted);
            QFile::remove(absolutePath(file) + QStringLiteral(".part"));
            if (advanceSource(transfer)) {
                emit log(QStringLiteral("%1: %2，切换下载源").arg(file.path, error));
                m_pending.prepend(transfer.index);
                pump();
                return;
            }
            ++transfer.attempts;
            m_attempts[transfer.index] = transfer.attempts;
            if (transfer.attempts >= m_maxAttempts) {
                failOne(transfer.index, error);
            } else {
                m_pending.prepend(transfer.index);
                emit log(QStringLiteral("%1: %2，重试").arg(file.path, error));
                pump();
            }
            return;
        }

        // Replace, not rename: on Windows rename fails whenever the target is
        // open (antivirus, another launcher running the same jar), and a plain
        // rename error used to abort the whole queue.
        if (!platform::replaceFile(absolutePath(file) + QStringLiteral(".part"), absolutePath(file))) {
            m_doneBytes = std::max<qint64>(0, m_doneBytes - transfer.offset - transfer.counted);
            failOne(transfer.index, QStringLiteral("无法写入目标文件 %1（文件被占用或磁盘只读）").arg(file.path));
            return;
        }

        m_attempts.remove(transfer.index);
        ++m_doneFiles;
        ++m_downloaded;
        m_doneBytes += file.size - transfer.offset - transfer.counted;
        emit fileProgress(file.path, file.size, file.size, 2);
        emitProgress();
        pump();
    });
    return false;
}

bool DownloadTask::advanceSource(const Transfer &transfer)
{
    const int next = transfer.sourceIndex + 1;
    if (next >= transfer.sourceCount)
        return false;

    m_sourceIndex[transfer.index] = next;
    m_attempts[transfer.index] = 0;
    QFile::remove(absolutePath(m_plan.files.at(transfer.index)) + QStringLiteral(".part"));
    return true;
}

void DownloadTask::failOne(int index, const QString &reason)
{
    ++m_failures;
    m_attempts.remove(index);
    const PlanFile &failed = m_plan.files.at(index);
    emit fileProgress(failed.path, 0, failed.size, 3);
    emit log(QStringLiteral("失败 %1: %2").arg(failed.path, reason));
    // Count it as done so the queue keeps moving; the failure is reported at the end.
    m_doneFiles = std::min(m_doneFiles + 1, m_totalFiles);
    pump();
    finishIfIdle();
}

void DownloadTask::emitProgress()
{
    const qint64 ms = m_clock.isValid() ? m_clock.elapsed() : 0;
    if (ms - m_lastMs >= 400) {
        const double deltaBytes = double(m_doneBytes - m_lastBytes);
        const double deltaMs = double(ms - m_lastMs);
        if (deltaMs > 0)
            m_speed = deltaBytes * 1000.0 / deltaMs;
        m_lastBytes = m_doneBytes;
        m_lastMs = ms;
    }
    emit progress(m_doneBytes, m_totalBytes, m_doneFiles, m_totalFiles, m_speed);
}

void DownloadTask::finishIfIdle()
{
    if (m_done || m_cancelled || m_paused)
        return;
    if (!m_active.isEmpty() || !m_pending.isEmpty())
        return;

    m_done = true;
    m_tick.stop();
    emitProgress();

    if (m_verifyOnly) {
        emit verified(m_skipped, m_missing, m_doneBytes, m_totalBytes);
        return;
    }
    if (m_failures > 0) {
        emit failed(QStringLiteral("%1 个文件下载失败").arg(m_failures));
        return;
    }
    emit finished(m_skipped, m_downloaded);
}
