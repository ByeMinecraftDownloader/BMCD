#include "core/ProfileMerger.h"

#include "platform/FileOps.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

class ProfileMergerTest : public QObject
{
    Q_OBJECT

private slots:
    void createsProfile();
    void preservesUnknownFieldsAndBacksUp();
    void repeatedMergeOnlyMovesLastVersionId();
    void invalidJsonFailsWithoutWriting();
    void profileIdIsStableAndSafe();
};

void ProfileMergerTest::createsProfile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    ProfileSpec spec;
    spec.gameDir = dir.path();
    spec.minecraftVersion = QStringLiteral("1.20.1");
    spec.loaderType = QStringLiteral("fabric");
    spec.loaderVersion = QStringLiteral("0.15.11");
    spec.lastVersionId = QStringLiteral("fabric-loader-0.15.11-1.20.1");
    spec.name = QStringLiteral("My Pack");

    const ProfileMergeResult result = ProfileMerger::merge(spec);
    QVERIFY2(result.ok, qPrintable(result.error));
    QVERIFY(result.created);
    QVERIFY(result.changed);
    QCOMPARE(result.id, QStringLiteral("bmcd-1.20.1-fabric-0.15.11"));
    QVERIFY(result.backupPath.isEmpty());

    QFile file(result.path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    QCOMPARE(root.value(QStringLiteral("version")).toInt(), 3);
    const QJsonObject profile = root.value(QStringLiteral("profiles")).toObject()
                                    .value(result.id).toObject();
    QCOMPARE(profile.value(QStringLiteral("name")).toString(), QStringLiteral("My Pack"));
    QCOMPARE(profile.value(QStringLiteral("type")).toString(), QStringLiteral("custom"));
    QCOMPARE(profile.value(QStringLiteral("lastVersionId")).toString(), spec.lastVersionId);
    QCOMPARE(profile.value(QStringLiteral("gameDir")).toString(), dir.path());
    QVERIFY(!profile.value(QStringLiteral("created")).toString().isEmpty());
}

void ProfileMergerTest::preservesUnknownFieldsAndBacksUp()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QByteArray original = R"({
  "profiles": {
    "vanilla": {"name": "Vanilla", "lastVersionId": "1.20.1", "icon": "grass"}
  },
  "settings": {"enableSnapshots": true, "keep": [1, 2]},
  "selectedProfile": "vanilla",
  "unknownTop": {"nested": true},
  "version": 3
})";
    const QString path = dir.filePath(QStringLiteral("launcher_profiles.json"));
    QVERIFY(platform::writeFileAtomic(path, original));

    ProfileSpec spec;
    spec.gameDir = dir.path();
    spec.minecraftVersion = QStringLiteral("1.20.1");
    spec.loaderType = QStringLiteral("forge");
    spec.loaderVersion = QStringLiteral("47.2.0");
    spec.lastVersionId = QStringLiteral("1.20.1-forge-47.2.0");
    const ProfileMergeResult result = ProfileMerger::merge(spec);
    QVERIFY2(result.ok, qPrintable(result.error));
    QVERIFY(result.created);
    QCOMPARE(QFileInfo(result.backupPath).size(), qint64(original.size()));

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    QCOMPARE(root.value(QStringLiteral("settings")).toObject()
                 .value(QStringLiteral("keep")).toArray().size(), 2);
    QCOMPARE(root.value(QStringLiteral("unknownTop")).toObject()
                 .value(QStringLiteral("nested")).toBool(), true);
    QCOMPARE(root.value(QStringLiteral("selectedProfile")).toString(), QStringLiteral("vanilla"));
    const QJsonObject profiles = root.value(QStringLiteral("profiles")).toObject();
    QCOMPARE(profiles.value(QStringLiteral("vanilla")).toObject()
                 .value(QStringLiteral("icon")).toString(), QStringLiteral("grass"));
    QVERIFY(profiles.contains(result.id));
}

void ProfileMergerTest::repeatedMergeOnlyMovesLastVersionId()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("launcher_profiles.json"));
    const QByteArray original = R"({
  "profiles": {
    "bmcd-1.20.1-fabric-0.15.11": {
      "name": "User renamed me",
      "type": "custom",
      "gameDir": "/somewhere/else",
      "icon": "custom.png",
      "javaArgs": "-Xmx8G",
      "lastVersionId": "fabric-loader-0.15.11-1.20.1"
    }
  },
  "version": 3
})";
    QVERIFY(platform::writeFileAtomic(path, original));

    ProfileSpec spec;
    spec.gameDir = dir.path();
    spec.minecraftVersion = QStringLiteral("1.20.1");
    spec.loaderType = QStringLiteral("fabric");
    spec.loaderVersion = QStringLiteral("0.15.11");
    spec.name = QStringLiteral("ignored on update");
    spec.lastVersionId = QStringLiteral("fabric-loader-0.15.12-1.20.1");
    const ProfileMergeResult result = ProfileMerger::merge(spec);
    QVERIFY2(result.ok, qPrintable(result.error));
    QVERIFY(!result.created);
    QVERIFY(result.changed);
    QCOMPARE(result.name, QStringLiteral("User renamed me"));

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QJsonObject profile = QJsonDocument::fromJson(file.readAll()).object()
                                    .value(QStringLiteral("profiles")).toObject()
                                    .value(result.id).toObject();
    QCOMPARE(profile.value(QStringLiteral("lastVersionId")).toString(),
             QStringLiteral("fabric-loader-0.15.12-1.20.1"));
    QCOMPARE(profile.value(QStringLiteral("name")).toString(), QStringLiteral("User renamed me"));
    QCOMPARE(profile.value(QStringLiteral("gameDir")).toString(), QStringLiteral("/somewhere/else"));
    QCOMPARE(profile.value(QStringLiteral("icon")).toString(), QStringLiteral("custom.png"));
    QCOMPARE(profile.value(QStringLiteral("javaArgs")).toString(), QStringLiteral("-Xmx8G"));

    // A second merge to the same version is a no-op, not another rewrite.
    const ProfileMergeResult again = ProfileMerger::merge(spec);
    QVERIFY(again.ok);
    QVERIFY(!again.changed);
}

void ProfileMergerTest::invalidJsonFailsWithoutWriting()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("launcher_profiles.json"));
    QVERIFY(platform::writeFileAtomic(path, QByteArrayLiteral("{ definitely not json")));

    ProfileSpec spec;
    spec.gameDir = dir.path();
    spec.minecraftVersion = QStringLiteral("1.20.1");
    spec.loaderType = QStringLiteral("fabric");
    spec.loaderVersion = QStringLiteral("0.15.11");
    spec.lastVersionId = QStringLiteral("fabric-loader-0.15.11-1.20.1");
    const ProfileMergeResult result = ProfileMerger::merge(spec);
    QVERIFY(!result.ok);
    QVERIFY(!result.error.isEmpty());

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArrayLiteral("{ definitely not json"));
    QVERIFY(!QFileInfo::exists(path + QStringLiteral(".bmcd-backup")));
}

void ProfileMergerTest::profileIdIsStableAndSafe()
{
    QCOMPARE(ProfileMerger::profileId(QStringLiteral("1.20.1"), QStringLiteral("NeoForge"),
                                      QStringLiteral("21.1.80")),
             QStringLiteral("bmcd-1.20.1-neoforge-21.1.80"));
    QCOMPARE(ProfileMerger::profileId(QStringLiteral("1.20.1"), QStringLiteral("fabric"),
                                      QStringLiteral("../../bad")),
             QStringLiteral("bmcd-1.20.1-fabric-bad"));
    QCOMPARE(ProfileMerger::defaultName(QStringLiteral("1.20.1"), QStringLiteral("quilt")),
             QStringLiteral("BMCD Quilt 1.20.1"));
}

QTEST_GUILESS_MAIN(ProfileMergerTest)
#include "tst_profilemerger.moc"
