#include "core/Pack.h"
#include "core/PackController.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>

class PackControllerTest : public QObject
{
    Q_OBJECT

private slots:
    // Cancelling a running download used to emit failed() synchronously from
    // DownloadTask::cancel(); the failure handler nulled m_download, and
    // stopDownload() then dereferenced it. This is the regression guard.
    void cancelWhileDownloadingDoesNotCrash();
    void pauseThenResumeAndCancel();
    void serveAndExportRequireCookedDish();
    void cookThenServeAndExport();
    void adoptDownloadedVersionRoot();
    void preflightCountsOnlyFailedFiles();
    void groupedConfirmationCoversParentMod();
};

void PackControllerTest::cancelWhileDownloadingDoesNotCrash()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    Pack pack;
    pack.format = 3;
    pack.name = QStringLiteral("cancel-test");
    pack.versionId = QStringLiteral("1.0");
    pack.minecraftVersion = QStringLiteral("1.21.1");
    pack.target = dir.path();
    PackFile file;
    file.path = QStringLiteral("mods/a.jar");
    file.url = QStringLiteral("http://127.0.0.1:9/a.jar");
    file.sha1 = QString(40, QLatin1Char('a'));
    file.size = 16;
    file.kind = QStringLiteral("mod");
    pack.files.append(file);

    PackController controller;
    QVERIFY(controller.adoptPack(QString::fromUtf8(QJsonDocument(pack.toJson()).toJson())));
    QVERIFY(!controller.hasGameFiles());
    QCOMPARE(controller.minecraftVersion(), QStringLiteral("1.21.1"));
    controller.startDownload();
    QCOMPARE(controller.state(), QStringLiteral("working"));
    controller.cancelDownload();
    QCOMPARE(controller.state(), QStringLiteral("ready"));
    QCOMPARE(controller.downloadFiles().size(), 1);
}

void PackControllerTest::pauseThenResumeAndCancel()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    connect(&server, &QTcpServer::newConnection, &server, [&server]() {
        QTcpSocket *socket = server.nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, socket, [socket]() {
            const QByteArray request = socket->readAll();
            if (!request.startsWith("GET "))
                return;
            socket->write("HTTP/1.1 200 OK\r\nContent-Length: 1000000\r\n"
                          "Content-Type: application/java-archive\r\n\r\n");
            socket->write(QByteArray(4096, 'x'));
            socket->flush();
            // Keep the socket open: the client should still be downloading.
        });
    });

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Pack pack;
    pack.format = 3;
    pack.name = QStringLiteral("pause-test");
    pack.versionId = QStringLiteral("1.0");
    pack.minecraftVersion = QStringLiteral("1.21.1");
    pack.target = dir.path();
    PackFile file;
    file.path = QStringLiteral("mods/a.jar");
    file.url = QStringLiteral("http://127.0.0.1:%1/a.jar").arg(server.serverPort());
    file.sha1 = QString(40, QLatin1Char('a'));
    file.size = 1000000;
    file.kind = QStringLiteral("mod");
    pack.files.append(file);

    PackController controller;
    QVERIFY(controller.adoptPack(QString::fromUtf8(QJsonDocument(pack.toJson()).toJson())));
    controller.startDownload();
    QCOMPARE(controller.state(), QStringLiteral("working"));
    const QString cookingRoot = controller.sourceRoot();
    QVERIFY(!cookingRoot.isEmpty());

    controller.pauseDownload();
    QCOMPARE(controller.state(), QStringLiteral("paused"));
    QTest::qWait(150);
    QCOMPARE(controller.state(), QStringLiteral("paused"));

    controller.resumeDownload();
    QCOMPARE(controller.state(), QStringLiteral("working"));
    controller.cancelDownload();
    QCOMPARE(controller.state(), QStringLiteral("ready"));
    QVERIFY(!QDir(cookingRoot).exists());
}

void PackControllerTest::preflightCountsOnlyFailedFiles()
{
    Pack pack;
    pack.format = 3;
    pack.name = QStringLiteral("preflight-count");
    pack.versionId = QStringLiteral("1.0");
    pack.minecraftVersion = QStringLiteral("1.21.1");
    PackFile file;
    file.path = QStringLiteral("mods/missing.jar");
    file.url = QStringLiteral("http://127.0.0.1:9/missing.jar");
    file.sha1 = QString(40, QLatin1Char('a'));
    file.size = 16;
    file.kind = QStringLiteral("mod");
    pack.files.append(file);

    PackController controller;
    QVERIFY(controller.adoptPack(QString::fromUtf8(QJsonDocument(pack.toJson()).toJson())));
    QDir(controller.sourceRoot()).removeRecursively();
    controller.preflight();
    // The summary line ("1 个文件未通过预检") used to be appended to the
    // per-file failures, so the UI showed one missing file too many.
    QCOMPARE(controller.missingFiles().size(), 1);
    QVERIFY(controller.missingFiles().first().contains(QStringLiteral("缓存缺失")));
}


void PackControllerTest::groupedConfirmationCoversParentMod()
{
    Pack pack;
    pack.format = 3;
    pack.name = QStringLiteral("group-test");
    pack.versionId = QStringLiteral("1.0");
    pack.minecraftVersion = QStringLiteral("1.21.1");
    const auto add = [&pack](const QString &path, const QString &license) {
        PackFile file;
        file.path = path;
        file.url = QStringLiteral("http://127.0.0.1:9/") + path;
        file.sha1 = QString(40, QLatin1Char('a'));
        file.size = 16;
        file.kind = QStringLiteral("mod");
        file.projectId = QStringLiteral("alpha");
        file.projectName = QStringLiteral("Alpha Mod");
        file.licenseId = license;
        pack.files.append(file);
    };
    add(QStringLiteral("mods/alpha-extra.jar"), QStringLiteral("MIT"));
    add(QStringLiteral("mods/alpha.jar"), QString());

    PackController controller;
    QVERIFY(controller.adoptPack(QString::fromUtf8(QJsonDocument(pack.toJson()).toJson())));
    QCOMPARE(controller.licenceGroups().size(), 1);
    QCOMPARE(controller.licenceDeniedGroups(), 1);
    QCOMPARE(controller.licenceDenied(), 1);
    const QVariantMap group = controller.licenceGroups().first().toMap();
    QCOMPARE(group.value(QStringLiteral("id")).toString(), QStringLiteral("@project:alpha"));
    QCOMPARE(group.value(QStringLiteral("name")).toString(), QStringLiteral("Alpha Mod"));
    QVERIFY(group.value(QStringLiteral("licenseId")).toString().isEmpty());

    controller.confirmGroup(QStringLiteral("@project:alpha"));
    QVERIFY(controller.licenceGroups().isEmpty());
    QCOMPARE(controller.licenceDenied(), 0);
    QCOMPARE(controller.licenceDeniedGroups(), 0);
}

void PackControllerTest::adoptDownloadedVersionRoot()
{
    // This is the vanilla cooking path: VersionDetail writes into
    // downloads/versions/<id>, then 出餐 must rebuild the plan against that
    // exact root instead of PackController's private cooking directory.
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QByteArray jar("jar!");
    const QByteArray index("{}");
    const QString jarPath = root.filePath(QStringLiteral("versions/testdl/testdl.jar"));
    const QString indexPath = root.filePath(QStringLiteral("assets/indexes/test.json"));
    QVERIFY(QDir().mkpath(QFileInfo(jarPath).absolutePath()));
    QVERIFY(QDir().mkpath(QFileInfo(indexPath).absolutePath()));
    {
        QFile f(jarPath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(jar);
    }
    {
        QFile f(indexPath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(index);
    }

    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    const QByteArray jarSha1 =
            QCryptographicHash::hash(jar, QCryptographicHash::Sha1).toHex();
    const QByteArray indexSha1 =
            QCryptographicHash::hash(index, QCryptographicHash::Sha1).toHex();
    QByteArray versionBody = QStringLiteral(R"({
        "id":"testdl",
        "type":"release",
        "assetIndex":{"id":"test","url":"http://127.0.0.1:%1/assets.json","sha1":"%2","size":%3},
        "downloads":{"client":{"url":"http://127.0.0.1:%1/client.jar","sha1":"%4","size":%5}}
    })").arg(server.serverPort())
            .arg(QString::fromLatin1(indexSha1)).arg(index.size())
            .arg(QString::fromLatin1(jarSha1)).arg(jar.size()).toUtf8();
    const QByteArray prettyVersionJson =
            QJsonDocument::fromJson(versionBody).toJson(QJsonDocument::Indented);
    versionBody = QJsonDocument::fromJson(versionBody).toJson(QJsonDocument::Compact);
    {
        const QString versionPath = root.filePath(QStringLiteral("versions/testdl/testdl.json"));
        QVERIFY(QDir().mkpath(QFileInfo(versionPath).absolutePath()));
        QFile f(versionPath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(prettyVersionJson);
    }
    connect(&server, &QTcpServer::newConnection, &server, [&server, jarSha1, indexSha1, &index, &versionBody]() {
        QTcpSocket *socket = server.nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, socket, [socket, jarSha1, indexSha1, &index, &versionBody]() {
            const QByteArray request = socket->readAll();
            if (!request.startsWith("GET "))
                return;
            const QList<QByteArray> parts = request.split(' ');
            const QByteArray path = parts.size() > 1 ? parts.at(1) : QByteArray();
            QByteArray body;
            if (path == "/version.json") {
                body = versionBody;
            } else if (path == "/assets.json") {
                body = index;
            } else {
                body = QByteArray();
            }
            socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                          + QByteArray::number(body.size()) + "\r\n\r\n" + body);
            socket->disconnectFromHost();
        });
    });

    PackController controller;
    const QString versionUrl = QStringLiteral("http://127.0.0.1:%1/version.json")
                                       .arg(server.serverPort());
    controller.prepareDownloaded(QStringLiteral("testdl"), versionUrl, root.path());
    QTRY_COMPARE_WITH_TIMEOUT(controller.state(), QStringLiteral("ready"), 5000);
    QCOMPARE(controller.sourceRoot(), root.path());
    QCOMPARE(controller.fileCount(), 3);
    QVERIFY(controller.hasGameFiles());
    QCOMPARE(controller.minecraftVersion(), QStringLiteral("testdl"));
    QVERIFY(controller.adopted());
    QVERIFY2(controller.missingFiles().isEmpty(), qPrintable(controller.missingFiles().join('\n')));
    QVERIFY(controller.cooked());
}

void PackControllerTest::serveAndExportRequireCookedDish()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    Pack pack;
    pack.format = 3;
    pack.name = QStringLiteral("serve-test");
    pack.versionId = QStringLiteral("1.0");
    pack.minecraftVersion = QStringLiteral("1.21.1");
    pack.target = dir.filePath(QStringLiteral("staging"));
    PackFile file;
    file.path = QStringLiteral("mods/a.jar");
    file.url = QStringLiteral("http://127.0.0.1:9/a.jar");
    file.sha1 = QString(40, QLatin1Char('a'));
    file.size = 16;
    file.kind = QStringLiteral("mod");
    pack.files.append(file);

    PackController controller;
    QVERIFY(controller.adoptPack(QString::fromUtf8(QJsonDocument(pack.toJson()).toJson())));
    QVERIFY(!controller.cooked());

    const QString out = dir.filePath(QStringLiteral("out/plan.mrpack"));
    controller.exportPackTo(QStringLiteral("mrpack"), out);
    QCOMPARE(controller.state(), QStringLiteral("error"));
    QVERIFY(!QFile::exists(out));

    controller.serveTo(dir.filePath(QStringLiteral("game")));
    QCOMPARE(controller.state(), QStringLiteral("error"));
    QVERIFY(!QFile::exists(dir.filePath(QStringLiteral("game/mods/a.jar"))));
}

void PackControllerTest::cookThenServeAndExport()
{
    const QByteArray body = QByteArrayLiteral("0123456789abcdef");
    const QByteArray sha1 = QCryptographicHash::hash(body, QCryptographicHash::Sha1).toHex();

    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    connect(&server, &QTcpServer::newConnection, &server, [&server, body]() {
        QTcpSocket *socket = server.nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, socket, [socket, body]() {
            if (!socket->readAll().startsWith("GET "))
                return;
            socket->write("HTTP/1.1 200 OK\r\nContent-Length: 16\r\n"
                          "Content-Type: application/java-archive\r\n\r\n");
            socket->write(body);
            socket->disconnectFromHost();
        });
    });

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Pack pack;
    pack.format = 3;
    pack.name = QStringLiteral("cook-serve-test");
    pack.versionId = QStringLiteral("1.0");
    pack.minecraftVersion = QStringLiteral("1.21.1");
    PackFile file;
    file.path = QStringLiteral("mods/a.jar");
    file.url = QStringLiteral("http://127.0.0.1:%1/a.jar").arg(server.serverPort());
    file.sha1 = QString::fromLatin1(sha1);
    file.size = body.size();
    file.kind = QStringLiteral("mod");
    pack.files.append(file);

    PackController controller;
    QVERIFY(controller.adoptPack(QString::fromUtf8(QJsonDocument(pack.toJson()).toJson())));
    controller.startDownload();
    QTRY_VERIFY_WITH_TIMEOUT(controller.state() == QLatin1String("done"), 5000);
    QVERIFY(controller.cooked());
    QVERIFY(controller.missingFiles().isEmpty());

    const QString game = dir.filePath(QStringLiteral("game"));
    controller.serveTo(game);
    QTRY_VERIFY_WITH_TIMEOUT(controller.state() == QLatin1String("done"), 5000);
    QVERIFY(QFile::exists(game + QStringLiteral("/mods/a.jar")));

    const QString out = dir.filePath(QStringLiteral("out/plan.mrpack"));
    controller.exportPackTo(QStringLiteral("mrpack"), out);
    QTRY_VERIFY_WITH_TIMEOUT(controller.state() == QLatin1String("done"), 5000);
    QVERIFY(QFile::exists(out));

    controller.cancelDownload();
}

QTEST_MAIN(PackControllerTest)
#include "tst_packcontroller.moc"
