#include "core/LoaderInstaller.h"
#include "core/ProfileMerger.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class TestLoaderInstaller : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void installsFabricAndMergesProfile();
    void installsQuiltWithOfficialCli();
    void installsForgeWithInstallClient();
    void rejectsBadChecksumBeforeRunningInstaller();
    void onlyOfficialHttpsHostsAreAllowed();
    void parsesFabricCatalog();
    void parsesForgePromotions();
    void parsesNeoForgeCatalogForMinecraft();
    void rejectsInvalidCatalog();

private:
    QString writeJson(const QString &path, const QJsonObject &object);
    QString versionJson(const QString &id, const QString &mc, const QString &libraryPath);
    LoaderInstaller::Request fabricRequest() const;

    QTemporaryDir *m_dir = nullptr;
    QString m_gameDir;
    QString m_cacheDir;
};

void TestLoaderInstaller::initTestCase()
{
    QSettings::setDefaultFormat(QSettings::IniFormat);
    m_dir = new QTemporaryDir;
    QVERIFY(m_dir->isValid());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_dir->path() + "/settings");
}

void TestLoaderInstaller::init()
{
    QSettings settings;
    settings.clear();
    m_gameDir = m_dir->path() + "/game-" + QTest::currentTestFunction();
    m_cacheDir = m_dir->path() + "/cache-" + QTest::currentTestFunction();
    QDir(m_gameDir).removeRecursively();
    QDir(m_cacheDir).removeRecursively();
    QVERIFY(QDir().mkpath(m_gameDir));
}

QString TestLoaderInstaller::writeJson(const QString &path, const QJsonObject &object)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return {};
    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    return path;
}

QString TestLoaderInstaller::versionJson(const QString &id, const QString &mc, const QString &libraryPath)
{
    QJsonObject artifact{{QStringLiteral("path"), libraryPath},
                         {QStringLiteral("url"), QStringLiteral("https://example.invalid/library.jar")},
                         {QStringLiteral("size"), 1}};
    QJsonObject downloads{{QStringLiteral("artifact"), artifact}};
    QJsonObject library{{QStringLiteral("name"), QStringLiteral("net.fabricmc:fabric-loader:0.15.11")},
                        {QStringLiteral("downloads"), downloads}};
    QJsonObject object{{QStringLiteral("id"), id},
                       {QStringLiteral("inheritsFrom"), mc},
                       {QStringLiteral("mainClass"), QStringLiteral("net.fabricmc.loader.impl.launch.knot.KnotClient")},
                       {QStringLiteral("libraries"), QJsonArray{library}}};
    return writeJson(m_gameDir + "/versions/" + id + "/" + id + ".json", object);
}

LoaderInstaller::Request TestLoaderInstaller::fabricRequest() const
{
    LoaderInstaller::Request request;
    request.gameDir = m_gameDir;
    request.minecraftVersion = QStringLiteral("1.20.1");
    request.loaderType = QStringLiteral("fabric");
    request.loaderVersion = QStringLiteral("0.15.11");
    request.javaPath = QStringLiteral("/usr/bin/java");
    return request;
}

void TestLoaderInstaller::installsFabricAndMergesProfile()
{
    const QByteArray jar = QByteArrayLiteral("fake-fabric-installer");
    const QString installerUrl = QStringLiteral("https://maven.fabricmc.net/net/fabricmc/fabric-installer/1.0.1/fabric-installer-1.0.1.jar");
    const QString versionId = QStringLiteral("fabric-loader-0.15.11-1.20.1");
    const QString libraryPath = QStringLiteral("net/fabricmc/fabric-loader/0.15.11/fabric-loader-0.15.11.jar");
    QDir().mkpath(QFileInfo(m_gameDir + "/libraries/" + libraryPath).absolutePath());
    QFile library(m_gameDir + "/libraries/" + libraryPath);
    QVERIFY(library.open(QIODevice::WriteOnly));
    library.write("library");

    QHash<QString, QByteArray> responses;
    responses.insert(QStringLiteral("https://meta.fabricmc.net/v2/versions/installer"),
                     QByteArrayLiteral("[{\"url\":\"") + installerUrl.toUtf8()
                         + QByteArrayLiteral("\",\"version\":\"1.0.1\",\"stable\":true}]"));
    responses.insert(installerUrl + QStringLiteral(".sha1"),
                     QCryptographicHash::hash(jar, QCryptographicHash::Sha1).toHex());
    responses.insert(installerUrl, jar);

    QStringList capturedArgs;
    QString capturedProgram;
    LoaderInstaller::Hooks hooks;
    hooks.fetch = [responses](const QUrl &url, QString *error) {
        const auto it = responses.constFind(url.toString());
        if (it == responses.constEnd()) {
            if (error)
                *error = QStringLiteral("unexpected fetch: %1").arg(url.toString());
            return QByteArray();
        }
        return it.value();
    };
    hooks.run = [&](const QString &program, const QStringList &args, const QString &workingDir,
                    QByteArray *output, QString *error) {
        capturedProgram = program;
        capturedArgs = args;
        Q_UNUSED(workingDir);
        Q_UNUSED(error);
        *output = "fabric installer output\n";
        versionJson(versionId, QStringLiteral("1.20.1"), libraryPath);
        return 0;
    };

    LoaderInstaller installer;
    installer.setCacheDirForTests(m_cacheDir);
    installer.setHooksForTests(hooks);
    QSignalSpy finished(&installer, &LoaderInstaller::finished);
    installer.install(fabricRequest());
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);

    QCOMPARE(installer.error(), QString());
    QCOMPARE(installer.state(), QStringLiteral("done"));
    QCOMPARE(installer.versionId(), versionId);
    QCOMPARE(capturedProgram, QStringLiteral("/usr/bin/java"));
    QCOMPARE(capturedArgs,
             QStringList({QStringLiteral("-jar"),
                          m_cacheDir + QStringLiteral("/loader/fabric-installer-1.0.1.jar"),
                          QStringLiteral("client"), QStringLiteral("-dir"), m_gameDir,
                          QStringLiteral("-mcversion"), QStringLiteral("1.20.1"),
                          QStringLiteral("-loader"), QStringLiteral("0.15.11"),
                          QStringLiteral("-noprofile")}));
    QVERIFY(QFileInfo::exists(m_cacheDir + QStringLiteral("/loader/fabric-installer-1.0.1.jar")));
    QVERIFY(QFileInfo::exists(m_gameDir + QStringLiteral("/launcher_profiles.json")));
    QVERIFY(QFileInfo::exists(installer.logPath()));
    QFile profileFile(m_gameDir + QStringLiteral("/launcher_profiles.json"));
    QVERIFY(profileFile.open(QIODevice::ReadOnly));
    const QJsonObject profile = QJsonDocument::fromJson(profileFile.readAll()).object();
    const QString profileId = ProfileMerger::profileId(QStringLiteral("1.20.1"), QStringLiteral("fabric"),
                                                      QStringLiteral("0.15.11"));
    QCOMPARE(profile.value(QStringLiteral("profiles")).toObject().value(profileId).toObject()
                 .value(QStringLiteral("lastVersionId")).toString(),
             versionId);
}

void TestLoaderInstaller::installsQuiltWithOfficialCli()
{
    const QByteArray jar = QByteArrayLiteral("fake-quilt-installer");
    const QString installerUrl = QStringLiteral("https://maven.quiltmc.org/repository/release/org/quiltmc/quilt-installer/0.15.1/quilt-installer-0.15.1.jar");
    const QString versionId = QStringLiteral("quilt-loader-0.20.0-1.20.1");
    const QString libraryPath = QStringLiteral("org/quiltmc/quilt-loader/0.20.0/quilt-loader-0.20.0.jar");
    QDir().mkpath(QFileInfo(m_gameDir + "/libraries/" + libraryPath).absolutePath());
    QFile library(m_gameDir + "/libraries/" + libraryPath);
    QVERIFY(library.open(QIODevice::WriteOnly));
    library.write("library");

    QHash<QString, QByteArray> responses;
    responses.insert(QStringLiteral("https://meta.quiltmc.org/v3/versions/installer"),
                     QByteArrayLiteral("[{\"url\":\"") + installerUrl.toUtf8()
                         + QByteArrayLiteral("\",\"version\":\"0.15.1\",\"hashes\":{\"sha1\":\"")
                         + QCryptographicHash::hash(jar, QCryptographicHash::Sha1).toHex()
                         + QByteArrayLiteral("\"}}]"));
    responses.insert(installerUrl, jar);

    QStringList capturedArgs;
    LoaderInstaller::Hooks hooks;
    hooks.fetch = [responses](const QUrl &url, QString *error) {
        const auto it = responses.constFind(url.toString());
        if (it == responses.constEnd()) {
            if (error)
                *error = QStringLiteral("unexpected fetch: %1").arg(url.toString());
            return QByteArray();
        }
        return it.value();
    };
    hooks.run = [&](const QString &, const QStringList &args, const QString &, QByteArray *output,
                    QString *) {
        capturedArgs = args;
        *output = "quilt installer output\n";
        versionJson(versionId, QStringLiteral("1.20.1"), libraryPath);
        return 0;
    };

    LoaderInstaller installer;
    installer.setCacheDirForTests(m_cacheDir);
    installer.setHooksForTests(hooks);
    LoaderInstaller::Request request = fabricRequest();
    request.loaderType = QStringLiteral("quilt");
    request.loaderVersion = QStringLiteral("0.20.0");
    QSignalSpy finished(&installer, &LoaderInstaller::finished);
    installer.install(request);
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
    QCOMPARE(installer.error(), QString());
    QCOMPARE(installer.state(), QStringLiteral("done"));
    QCOMPARE(installer.versionId(), versionId);
    QCOMPARE(capturedArgs,
             QStringList({QStringLiteral("-jar"),
                          m_cacheDir + QStringLiteral("/loader/quilt-installer-0.15.1.jar"),
                          QStringLiteral("install"), QStringLiteral("client"),
                          QStringLiteral("1.20.1"), QStringLiteral("0.20.0"),
                          QStringLiteral("--install-dir=\"%1\"").arg(m_gameDir),
                          QStringLiteral("--no-profile")}));
}

void TestLoaderInstaller::installsForgeWithInstallClient()
{
    const QByteArray jar = QByteArrayLiteral("fake-forge-installer");
    const QString installerUrl = QStringLiteral("https://maven.minecraftforge.net/net/minecraftforge/forge/1.20.1-47.2.0/forge-1.20.1-47.2.0-installer.jar");
    const QString versionId = QStringLiteral("1.20.1-forge-47.2.0");
    const QString libraryPath = QStringLiteral("net/minecraftforge/forge/1.20.1-47.2.0/forge-1.20.1-47.2.0.jar");
    QDir().mkpath(QFileInfo(m_gameDir + "/libraries/" + libraryPath).absolutePath());
    QFile library(m_gameDir + "/libraries/" + libraryPath);
    QVERIFY(library.open(QIODevice::WriteOnly));
    library.write("library");

    QHash<QString, QByteArray> responses;
    responses.insert(installerUrl + QStringLiteral(".sha1"),
                     QCryptographicHash::hash(jar, QCryptographicHash::Sha1).toHex());
    responses.insert(installerUrl, jar);

    QStringList capturedArgs;
    LoaderInstaller::Hooks hooks;
    hooks.fetch = [responses](const QUrl &url, QString *error) {
        const auto it = responses.constFind(url.toString());
        if (it == responses.constEnd()) {
            if (error)
                *error = QStringLiteral("unexpected fetch: %1").arg(url.toString());
            return QByteArray();
        }
        return it.value();
    };
    hooks.run = [&](const QString &, const QStringList &args, const QString &, QByteArray *output,
                    QString *) {
        capturedArgs = args;
        *output = "forge installer output\n";
        versionJson(versionId, QStringLiteral("1.20.1"), libraryPath);
        return 0;
    };

    LoaderInstaller installer;
    installer.setCacheDirForTests(m_cacheDir);
    installer.setHooksForTests(hooks);
    LoaderInstaller::Request request = fabricRequest();
    request.loaderType = QStringLiteral("forge");
    request.loaderVersion = QStringLiteral("47.2.0");
    QSignalSpy finished(&installer, &LoaderInstaller::finished);
    installer.install(request);
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
    QCOMPARE(installer.error(), QString());
    QCOMPARE(installer.state(), QStringLiteral("done"));
    QCOMPARE(installer.versionId(), versionId);
    QVERIFY(capturedArgs.contains(QStringLiteral("--installClient")));
    QCOMPARE(capturedArgs.at(capturedArgs.indexOf(QStringLiteral("--installClient")) + 1), m_gameDir);
}

void TestLoaderInstaller::rejectsBadChecksumBeforeRunningInstaller()
{
    const QByteArray jar = QByteArrayLiteral("fake-installer");
    const QString installerUrl = QStringLiteral("https://maven.minecraftforge.net/net/minecraftforge/forge/1.20.1-47.2.0/forge-1.20.1-47.2.0-installer.jar");
    QHash<QString, QByteArray> responses;
    responses.insert(installerUrl + QStringLiteral(".sha1"),
                     QByteArrayLiteral("0000000000000000000000000000000000000000"));
    responses.insert(installerUrl, jar);

    bool ran = false;
    LoaderInstaller::Hooks hooks;
    hooks.fetch = [responses](const QUrl &url, QString *error) {
        const auto it = responses.constFind(url.toString());
        if (it == responses.constEnd()) {
            if (error)
                *error = QStringLiteral("unexpected fetch: %1").arg(url.toString());
            return QByteArray();
        }
        return it.value();
    };
    hooks.run = [&](const QString &, const QStringList &, const QString &, QByteArray *, QString *) {
        ran = true;
        return 0;
    };

    LoaderInstaller installer;
    installer.setCacheDirForTests(m_cacheDir);
    installer.setHooksForTests(hooks);
    LoaderInstaller::Request request = fabricRequest();
    request.loaderType = QStringLiteral("forge");
    request.loaderVersion = QStringLiteral("47.2.0");
    QSignalSpy finished(&installer, &LoaderInstaller::finished);
    installer.install(request);
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
    QCOMPARE(installer.state(), QStringLiteral("error"));
    QVERIFY(installer.error().contains(QStringLiteral("SHA-1")));
    QVERIFY(!ran);
    QVERIFY(!QFileInfo::exists(m_gameDir + QStringLiteral("/launcher_profiles.json")));
    QVERIFY(QFileInfo::exists(installer.logPath()));
    QFile log(installer.logPath());
    QVERIFY(log.open(QIODevice::ReadOnly));
    QVERIFY(log.readAll().contains("[error]"));
}

void TestLoaderInstaller::onlyOfficialHttpsHostsAreAllowed()
{
    QVERIFY(LoaderInstaller::isAllowedUrl(QUrl(QStringLiteral("https://meta.fabricmc.net/v2/versions/installer"))));
    QVERIFY(LoaderInstaller::isAllowedUrl(QUrl(QStringLiteral("https://maven.minecraftforge.net/x.jar"))));
    QVERIFY(!LoaderInstaller::isAllowedUrl(QUrl(QStringLiteral("http://meta.fabricmc.net/x"))));
    QVERIFY(!LoaderInstaller::isAllowedUrl(QUrl(QStringLiteral("https://evil.example/x.jar"))));
}

void TestLoaderInstaller::parsesFabricCatalog()
{
    const QByteArray body = R"([
        {"loader":{"version":"0.16.9","stable":true}},
        {"loader":{"version":"0.15.11","stable":false}},
        {"loader":{"version":"0.15.11","stable":false}}
    ])";
    QString error;
    const QVariantList list = LoaderInstaller::parseLoaderVersions(
        QStringLiteral("fabric"), QStringLiteral("1.20.1"), body, &error);
    QCOMPARE(error, QString());
    QCOMPARE(list.size(), 2);
    QCOMPARE(list.at(0).toMap().value(QStringLiteral("version")).toString(), QStringLiteral("0.16.9"));
    QCOMPARE(list.at(0).toMap().value(QStringLiteral("label")).toString(), QStringLiteral("0.16.9 · stable"));
    QVERIFY(list.at(0).toMap().value(QStringLiteral("stable")).toBool());
    QCOMPARE(list.at(1).toMap().value(QStringLiteral("version")).toString(), QStringLiteral("0.15.11"));
    QVERIFY(!list.at(1).toMap().value(QStringLiteral("stable")).toBool());
}

void TestLoaderInstaller::parsesForgePromotions()
{
    const QByteArray body = R"({"promos":{
        "1.20.1-recommended":"47.2.0",
        "1.20.1-latest":"47.3.0",
        "1.19.2-recommended":"43.2.0"
    }})";
    QString error;
    const QVariantList list = LoaderInstaller::parseLoaderVersions(
        QStringLiteral("forge"), QStringLiteral("1.20.1"), body, &error);
    QCOMPARE(error, QString());
    QCOMPARE(list.size(), 2);
    QCOMPARE(list.at(0).toMap().value(QStringLiteral("version")).toString(), QStringLiteral("47.2.0"));
    QCOMPARE(list.at(1).toMap().value(QStringLiteral("version")).toString(), QStringLiteral("47.3.0"));
}

void TestLoaderInstaller::parsesNeoForgeCatalogForMinecraft()
{
    const QByteArray body = R"(<metadata><versioning><versions>
        <version>47.1.0</version><version>47.2.0</version><version>21.1.0</version>
    </versions></versioning></metadata>)";
    QString error;
    const QVariantList list = LoaderInstaller::parseLoaderVersions(
        QStringLiteral("neoforge"), QStringLiteral("1.20.1"), body, &error);
    QCOMPARE(error, QString());
    QCOMPARE(list.size(), 2);
    QCOMPARE(list.at(0).toMap().value(QStringLiteral("version")).toString(), QStringLiteral("47.2.0"));
    QCOMPARE(list.at(1).toMap().value(QStringLiteral("version")).toString(), QStringLiteral("47.1.0"));
}

void TestLoaderInstaller::rejectsInvalidCatalog()
{
    QString error;
    QVERIFY(LoaderInstaller::parseLoaderVersions(
                QStringLiteral("fabric"), QStringLiteral("1.20.1"), QByteArrayLiteral("not json"), &error).isEmpty());
    QVERIFY(!error.isEmpty());
    error.clear();
    QVERIFY(LoaderInstaller::parseLoaderVersions(
                QStringLiteral("neoforge"), QStringLiteral("1.5.2"), QByteArrayLiteral("<metadata/>"), &error).isEmpty());
    QVERIFY(error.contains(QStringLiteral("1.5.2")));
}

QTEST_MAIN(TestLoaderInstaller)
#include "tst_loaderinstaller.moc"
