#include "core/AppSettings.h"
#include "core/DownloadSources.h"
#include "core/DownloadTask.h"
#include "platform/Paths.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QUrl>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>

class TinyHttpServer : public QTcpServer
{
public:
    explicit TinyHttpServer(QByteArray body, QObject *parent = nullptr)
        : QTcpServer(parent), m_body(std::move(body))
    {
    }

protected:
    void incomingConnection(qintptr socketDescriptor) override
    {
        auto *socket = new QTcpSocket(this);
        socket->setSocketDescriptor(socketDescriptor);
        connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
            if (!socket->property("answered").toBool()
                && socket->readAll().contains("\r\n\r\n")) {
                socket->setProperty("answered", true);
                const QByteArray header = "HTTP/1.1 200 OK\r\nContent-Length: "
                        + QByteArray::number(m_body.size()) + "\r\nConnection: close\r\n\r\n";
                socket->write(header);
                socket->write(m_body);
                socket->disconnectFromHost();
            }
        });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }

private:
    QByteArray m_body;
};

namespace {

class ScopedEnv
{
public:
    ScopedEnv(const char *name, const QByteArray &value)
        : m_name(name)
        , m_had(qEnvironmentVariableIsSet(name))
        , m_old(qgetenv(name))
    {
        qputenv(name, value);
    }
    ~ScopedEnv()
    {
        if (m_had)
            qputenv(m_name, m_old);
        else
            qunsetenv(m_name);
    }

private:
    QByteArray m_name;
    bool m_had = false;
    QByteArray m_old;
};

} // namespace

class DownloadTest : public QObject
{
    Q_OBJECT

private slots:
    void mapsBmclApiSources();
    void mapsCustomModCdn();
    void honoursFailoverSwitch();
    void rejectsInvalidSourceBase();
    void resolvesTempDownloadRoot();
    void rejectsRelativeTempDownloadRoot();
    void decodesFolderUrlWithoutEscapingSpaces();
    void stopsAfterRetryBudget();
    void prefersSha256();
    void fallsBackToSha512();
    void fallsBackToSha1();
    void rejectsWrongSha256();
    void reportsCachedFilesProgressively();
    void verifiesCacheWithoutDownloading();
};

static QByteArray digest(const QByteArray &data, QCryptographicHash::Algorithm algorithm)
{
    return QCryptographicHash::hash(data, algorithm).toHex();
}

static void runCase(const QByteArray &body, const QString &sha1, const QString &sha256,
                    const QString &sha512, bool expectOk)
{
    TinyHttpServer server(body);
    QVERIFY(server.listen(QHostAddress::LocalHost));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    TaskPlan plan;
    PlanFile file;
    file.path = QStringLiteral("mods/test.jar");
    file.size = body.size();
    file.url = QStringLiteral("http://127.0.0.1:%1/test.jar").arg(server.serverPort());
    file.sha1 = sha1;
    file.sha256 = sha256;
    file.sha512 = sha512;
    plan.files.append(file);
    plan.totalBytes = body.size();

    DownloadTask task(plan, dir.path(), 2);
    task.setMaxAttemptsForTests(1);
    QSignalSpy finished(&task, &DownloadTask::finished);
    QSignalSpy failed(&task, &DownloadTask::failed);
    QSignalSpy fileProgress(&task, &DownloadTask::fileProgress);
    task.start();

    QTRY_VERIFY_WITH_TIMEOUT(finished.count() + failed.count() == 1, 8000);
    const QString path = dir.filePath(file.path);
    if (expectOk) {
        QCOMPARE(finished.count(), 1);
        QFile downloaded(path);
        QVERIFY(downloaded.open(QIODevice::ReadOnly));
        QCOMPARE(downloaded.readAll(), body);
        bool sawDone = false;
        for (const QList<QVariant> &args : fileProgress) {
            if (args.at(0).toString() == file.path && args.at(3).toInt() == 2
                && args.at(1).toLongLong() == body.size()) {
                sawDone = true;
            }
        }
        QVERIFY(sawDone);
    } else {
        QCOMPARE(failed.count(), 1);
        QVERIFY(!QFile::exists(path));
    }
}

void DownloadTest::reportsCachedFilesProgressively()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    TaskPlan plan;
    const QByteArray body = QByteArrayLiteral("cached payload");
    const QString sha1 = QString::fromLatin1(digest(body, QCryptographicHash::Sha1));
    for (int i = 0; i < 100; ++i) {
        PlanFile file;
        file.path = QStringLiteral("files/%1.bin").arg(i);
        file.size = body.size();
        file.sha1 = sha1;
        file.url = QStringLiteral("http://127.0.0.1:9/never");
        plan.files.append(file);
        plan.totalBytes += file.size;

        QDir().mkpath(QFileInfo(dir.filePath(file.path)).absolutePath());
        QFile cached(dir.filePath(file.path));
        QVERIFY(cached.open(QIODevice::WriteOnly));
        QCOMPARE(cached.write(body), qint64(body.size()));
    }

    DownloadTask task(plan, dir.path(), 4);
    QSignalSpy progress(&task, &DownloadTask::progress);
    QSignalSpy finished(&task, &DownloadTask::finished);
    task.start();
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 8000);

    // The old synchronous skip loop emitted 0% and then 100% after hashing
    // every cached file.  At least one intermediate file count proves the
    // verification batches yield and update the UI.
    bool sawIntermediate = false;
    for (const QList<QVariant> &args : progress) {
        const int doneFiles = args.at(2).toInt();
        if (doneFiles > 0 && doneFiles < 100)
            sawIntermediate = true;
    }
    QVERIFY(sawIntermediate);
}

void DownloadTest::verifiesCacheWithoutDownloading()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QByteArray body = QByteArrayLiteral("verify-only payload");
    const QString sha1 = QString::fromLatin1(digest(body, QCryptographicHash::Sha1));
    TaskPlan plan;
    PlanFile cachedFile;
    cachedFile.path = QStringLiteral("files/cached.bin");
    cachedFile.size = body.size();
    cachedFile.sha1 = sha1;
    cachedFile.url = QStringLiteral("http://127.0.0.1:9/never");
    plan.files.append(cachedFile);
    PlanFile missingFile;
    missingFile.path = QStringLiteral("files/missing.bin");
    missingFile.size = body.size();
    missingFile.sha1 = sha1;
    missingFile.url = QStringLiteral("http://127.0.0.1:9/never");
    plan.files.append(missingFile);
    plan.totalBytes = body.size() * 2;

    QDir().mkpath(QFileInfo(dir.filePath(cachedFile.path)).absolutePath());
    QFile cached(dir.filePath(cachedFile.path));
    QVERIFY(cached.open(QIODevice::WriteOnly));
    QCOMPARE(cached.write(body), qint64(body.size()));
    cached.close();

    DownloadTask task(plan, dir.path(), 2);
    task.setVerifyOnly(true);
    QSignalSpy verified(&task, &DownloadTask::verified);
    QSignalSpy failed(&task, &DownloadTask::failed);
    task.start();

    QTRY_COMPARE_WITH_TIMEOUT(verified.count(), 1, 8000);
    QCOMPARE(failed.count(), 0);
    const QList<QVariant> args = verified.first();
    QCOMPARE(args.at(0).toInt(), 1);
    QCOMPARE(args.at(1).toInt(), 1);
    QCOMPARE(args.at(2).toLongLong(), qint64(body.size()));
    QCOMPARE(args.at(3).toLongLong(), qint64(body.size() * 2));
    QVERIFY(!QFile::exists(dir.filePath(missingFile.path)));

    TaskPlan completePlan;
    completePlan.files.append(cachedFile);
    completePlan.totalBytes = body.size();
    DownloadTask completeTask(completePlan, dir.path(), 2);
    completeTask.setVerifyOnly(true);
    QSignalSpy completeVerified(&completeTask, &DownloadTask::verified);
    completeTask.start();
    QTRY_COMPARE_WITH_TIMEOUT(completeVerified.count(), 1, 8000);
    QCOMPARE(completeVerified.first().at(0).toInt(), 1);
    QCOMPARE(completeVerified.first().at(1).toInt(), 0);
}

void DownloadTest::mapsBmclApiSources()
{
    ScopedEnv mirror("MC_MIRROR_BASE", "https://bmclapi2.bangbang93.com");
    ScopedEnv fallback("MC_MIRROR_FALLBACK", "1");
    ScopedEnv cdn("MC_CDN_BASE", "");

    const QString resource = QStringLiteral("https://resources.download.minecraft.net/ab/abcdef");
    const QStringList resources = DownloadSources::candidates(resource, QStringLiteral("asset"));
    QCOMPARE(resources.size(), 2);
    QCOMPARE(resources.at(0), QStringLiteral("https://bmclapi2.bangbang93.com/assets/ab/abcdef"));
    QCOMPARE(resources.at(1), resource);

    const QString library = QStringLiteral("https://libraries.minecraft.net/org/example/lib.jar");
    QCOMPARE(DownloadSources::candidates(library, QStringLiteral("library")).at(0),
             QStringLiteral("https://bmclapi2.bangbang93.com/maven/org/example/lib.jar"));

    const QString client = QStringLiteral("https://piston-data.mojang.com/v1/objects/hash/client.jar");
    QCOMPARE(DownloadSources::candidates(client, QStringLiteral("client"),
                                         QStringLiteral("1.21.9")).at(0),
             QStringLiteral("https://bmclapi2.bangbang93.com/version/1.21.9/client"));

    const QString index = QStringLiteral("https://piston-meta.mojang.com/v1/packages/hash/27.json");
    QCOMPARE(DownloadSources::candidates(index, QStringLiteral("asset-index"),
                                         QStringLiteral("1.21.9")).at(0),
             QStringLiteral("https://bmclapi2.bangbang93.com/v1/packages/hash/27.json"));
}

void DownloadTest::mapsCustomModCdn()
{
    ScopedEnv cdn("MC_CDN_BASE", "https://mirror.example/modcdn");
    ScopedEnv mirror("MC_MIRROR_BASE", "");

    const QString file = QStringLiteral("https://cdn.modrinth.com/data/project/versions/v/file.jar");
    const QStringList urls = DownloadSources::candidates(file);
    QCOMPARE(urls.size(), 2);
    QCOMPARE(urls.at(0), QStringLiteral("https://mirror.example/modcdn/data/project/versions/v/file.jar"));
    QCOMPARE(urls.at(1), file);

    // Mojang URLs are not rewritten by the mod CDN setting.
    const QString asset = QStringLiteral("https://resources.download.minecraft.net/aa/bb");
    QCOMPARE(DownloadSources::candidates(asset).at(0), asset);
}

void DownloadTest::honoursFailoverSwitch()
{
    ScopedEnv mirror("MC_MIRROR_BASE", "https://bmclapi2.bangbang93.com");
    ScopedEnv fallback("MC_MIRROR_FALLBACK", "0");
    ScopedEnv cdn("MC_CDN_BASE", "");

    const QString resource = QStringLiteral("https://resources.download.minecraft.net/aa/bb");
    const QStringList urls = DownloadSources::candidates(resource);
    QCOMPARE(urls.size(), 1);
    QCOMPARE(urls.at(0), QStringLiteral("https://bmclapi2.bangbang93.com/assets/aa/bb"));
}

void DownloadTest::rejectsInvalidSourceBase()
{
    ScopedEnv mirror("MC_MIRROR_BASE", "ftp://invalid.example/mirror");
    ScopedEnv cdn("MC_CDN_BASE", "");

    const QString resource = QStringLiteral("https://resources.download.minecraft.net/aa/bb");
    QCOMPARE(DownloadSources::candidates(resource), QStringList{resource});
}

void DownloadTest::resolvesTempDownloadRoot()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    // The setting is normalized: whitespace/trailing path components are
    // removed, and a dedicated BMCD/downloads subtree is used below it.
    const QString selected = QDir::cleanPath(temp.path() + QStringLiteral("/nested/.."));
    ScopedEnv env("MC_DOWNLOAD_TEMP_DIR", selected.toUtf8());
    QCOMPARE(AppSettings::configuredDownloadTempDir(), QDir::cleanPath(temp.path()));
    QCOMPARE(AppSettings::resolvedDownloadRoot(),
             QDir(temp.path()).filePath(QStringLiteral("BMCD/downloads")));
}

void DownloadTest::decodesFolderUrlWithoutEscapingSpaces()
{
    // FolderDialog yields a URL; storing its percent-encoded string would
    // create a literal "%20" directory instead of using the chosen path.
    AppSettings settings;
    const QString path = QDir::tempPath() + QStringLiteral("/BMCD folder/下载");
    QCOMPARE(settings.localPath(QUrl::fromLocalFile(path)), QDir::cleanPath(path));
}

void DownloadTest::rejectsRelativeTempDownloadRoot()
{
    // A relative directory would depend on the process working directory and
    // could point anywhere; reject it and fall back to the platform cache.
    ScopedEnv env("MC_DOWNLOAD_TEMP_DIR", "relative/downloads");
    QCOMPARE(AppSettings::resolvedDownloadRoot(), platform::downloadsDir());
}

void DownloadTest::stopsAfterRetryBudget()
{
    const QByteArray body = QByteArrayLiteral("broken payload");
    TinyHttpServer server(body);
    QVERIFY(server.listen(QHostAddress::LocalHost));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    TaskPlan plan;
    PlanFile file;
    file.path = QStringLiteral("mods/broken.jar");
    file.size = body.size();
    file.url = QStringLiteral("http://127.0.0.1:%1/broken.jar").arg(server.serverPort());
    file.sha1 = QStringLiteral("0000000000000000000000000000000000000000");
    plan.files.append(file);
    plan.totalBytes = body.size();

    DownloadTask task(plan, dir.path(), 2);
    task.setMaxAttemptsForTests(2);
    QSignalSpy failed(&task, &DownloadTask::failed);
    task.start();

    QTRY_VERIFY_WITH_TIMEOUT(failed.count() == 1, 8000);
    QVERIFY(!QFile::exists(dir.filePath(file.path)));
}

void DownloadTest::prefersSha256()
{
    const QByteArray body = QByteArrayLiteral("sha256 payload");
    runCase(body, QString::fromLatin1(digest(body, QCryptographicHash::Sha1)),
            QString::fromLatin1(digest(body, QCryptographicHash::Sha256)),
            QString::fromLatin1(digest(body, QCryptographicHash::Sha512)), true);
}

void DownloadTest::fallsBackToSha512()
{
    const QByteArray body = QByteArrayLiteral("sha512 payload");
    runCase(body, QString::fromLatin1(digest(body, QCryptographicHash::Sha1)), QString(),
            QString::fromLatin1(digest(body, QCryptographicHash::Sha512)), true);
}

void DownloadTest::fallsBackToSha1()
{
    const QByteArray body = QByteArrayLiteral("sha1 payload");
    runCase(body, QString::fromLatin1(digest(body, QCryptographicHash::Sha1)), QString(), QString(),
            true);
}

void DownloadTest::rejectsWrongSha256()
{
    const QByteArray body = QByteArrayLiteral("wrong payload");
    runCase(body, QString::fromLatin1(digest(body, QCryptographicHash::Sha1)),
            QString(64, QLatin1Char('0')),
            QString::fromLatin1(digest(body, QCryptographicHash::Sha512)), false);
}

QTEST_MAIN(DownloadTest)
#include "tst_download.moc"
