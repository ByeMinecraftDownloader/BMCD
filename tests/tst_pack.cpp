#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

#include "core/Deployer.h"
#include "core/Instances.h"
#include "core/LicenseGate.h"
#include "core/Pack.h"
#include "core/PackImporter.h"
#include "core/Renderers.h"
#include "core/ZipReader.h"
#include "core/ZipWriter.h"

namespace {

QString sha1Of(const QByteArray &data)
{
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha1).toHex());
}

QByteArray readAll(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

// Central-directory scanner: enough to prove the archive we wrote is a valid
// zip with the expected entry names (the full parser lives in unzip).
QStringList zipEntryNames(const QByteArray &bytes)
{
    QStringList names;
    int offset = 0;
    while ((offset = bytes.indexOf("PK\x01\x02", offset)) >= 0) {
        if (offset + 46 > bytes.size())
            break;
        const auto *raw = reinterpret_cast<const uchar *>(bytes.constData() + offset);
        const int nameLength = raw[28] | (raw[29] << 8);
        const int extraLength = raw[30] | (raw[31] << 8);
        const int commentLength = raw[32] | (raw[33] << 8);
        names.append(QString::fromUtf8(bytes.mid(offset + 46, nameLength)));
        offset += 46 + nameLength + extraLength + commentLength;
    }
    return names;
}

void makePack(const QTemporaryDir &source, const QString &target, Pack *packPtr)
{
    Pack &pack = *packPtr;
    pack.name = QStringLiteral("Test Pack");
    pack.versionId = QStringLiteral("1.0.0");
    pack.minecraftVersion = QStringLiteral("26.3");
    pack.loaderType = QStringLiteral("fabric");
    pack.loaderVersion = QStringLiteral("0.19.5");
    pack.target = target;
    pack.generator = QStringLiteral("test");

    const QList<QPair<QString, QByteArray>> contents{
        {QStringLiteral("mods/alpha.jar"), QByteArray("alpha-bytes")},
        {QStringLiteral("config/beta.toml"), QByteArray("beta = 1\n")},
    };
    for (const auto &entry : contents) {
        QDir().mkpath(QFileInfo(source.filePath(entry.first)).absolutePath());
        QFile file(source.filePath(entry.first));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(entry.second);
        file.close();

        PackFile packFile;
        packFile.path = entry.first;
        packFile.url = QStringLiteral("https://example.invalid/") + entry.first;
        packFile.sha1 = sha1Of(entry.second);
        packFile.size = entry.second.size();
        packFile.kind = entry.first.startsWith(QLatin1String("mods/")) ? QStringLiteral("mod")
                                                                      : QStringLiteral("config");
        packFile.licenseId = QStringLiteral("MIT");
        packFile.author = QStringLiteral("tester");
        pack.files.append(packFile);
    }
}

} // namespace

class TestPack : public QObject
{
    Q_OBJECT

private slots:
    void safePaths();
    void packJsonRoundTrip();
    void packValidation();
    void zipArchiveHasEntries();
    void mrpackHasIndex();
    void ftbHasManifest();
    void selfContainedRequiresLicences();
    void deployEmptyDirectory();
    void deployRejectsMissingCache();
    void deployDryRunWritesNothing();
    void deployWithoutOverwriteReportsConflict();
    void verifyDeploymentDetectsCorruption();
    void licenceGateVerdicts();
    void licenceGroupsByParent();
    void zipReaderRoundTrip();
    void importModrinthArchive();
    void importCurseForgeManifest();
    void ftbEncodesUrlSpaces();
    void instancesAreDiscovered();
};

void TestPack::safePaths()
{
    QVERIFY(isSafeRelativePath(QStringLiteral("mods/a.jar")));
    QVERIFY(isSafeRelativePath(QStringLiteral("config/x/y.toml")));
    for (const char *bad : {"", "/abs", "../x", "a/../../b", "C:/x", "a//b", "a/./b", "~/.ssh/x",
                            "a/\x01/b"})
        QVERIFY2(!isSafeRelativePath(QString::fromLatin1(bad)), bad);
}

void TestPack::packJsonRoundTrip()
{
    QTemporaryDir source;
    Pack original;
    makePack(source, source.path(), &original);
    const Pack parsed = Pack::fromJson(original.toJson());
    QCOMPARE(parsed.name, original.name);
    QCOMPARE(parsed.files.size(), original.files.size());
    QCOMPARE(parsed.files.first().sha1, original.files.first().sha1);
    QCOMPARE(parsed.loaderVersion, original.loaderVersion);
}

void TestPack::packValidation()
{
    QTemporaryDir source;
    Pack pack;
    makePack(source, source.path(), &pack);
    QVERIFY(pack.isValid());
    pack.files.append(pack.files.first());
    QVERIFY(!pack.isValid());

    Pack noHash;
    makePack(source, source.path(), &noHash);
    noHash.files[0].sha1.clear();
    noHash.files[0].sha512.clear();
    QVERIFY(!noHash.isValid());
}

void TestPack::zipArchiveHasEntries()
{
    QTemporaryDir tmp;
    const QString path = tmp.filePath(QStringLiteral("test.zip"));
    ZipWriter writer(path);
    QVERIFY(writer.addData(QStringLiteral("a.txt"), QByteArray("hello"), true));
    QVERIFY(writer.addData(QStringLiteral("dir/b.txt"), QByteArray(500, 'x'), false));
    QVERIFY(writer.close());
    const QStringList names = zipEntryNames(readAll(path));
    QCOMPARE(names.size(), 2);
    QVERIFY(names.contains(QStringLiteral("a.txt")));
    QVERIFY(names.contains(QStringLiteral("dir/b.txt")));
}

void TestPack::mrpackHasIndex()
{
    QTemporaryDir source;
    Pack pack;
    makePack(source, source.path(), &pack);
    RenderOptions options;
    options.outputPath = source.filePath(QStringLiteral("pack.mrpack"));
    const RenderResult result = renderMrpack(pack, options);
    QVERIFY2(result.ok, qPrintable(result.error));
    const QStringList names = zipEntryNames(readAll(options.outputPath));
    QVERIFY(names.contains(QStringLiteral("modrinth.index.json")));
    QCOMPARE(result.fileCount, 1); // index only: files are referenced by URL
}

void TestPack::ftbHasManifest()
{
    QTemporaryDir source;
    Pack pack;
    makePack(source, source.path(), &pack);
    RenderOptions options;
    options.outputPath = source.filePath(QStringLiteral("pack.zip"));
    const RenderResult result = renderFtb(pack, options);
    QVERIFY2(result.ok, qPrintable(result.error));
    QVERIFY(zipEntryNames(readAll(options.outputPath)).contains(QStringLiteral("manifest.json")));
}

void TestPack::selfContainedRequiresLicences()
{
    QTemporaryDir source;
    Pack pack;
    makePack(source, source.path(), &pack);
    license::GateResult gate = license::evaluate(pack);
    QVERIFY(gate.exportable);

    RenderOptions options;
    options.outputPath = source.filePath(QStringLiteral("self.zip"));
    RenderResult result = renderSelfContained(pack, options, gate);
    QVERIFY2(result.ok, qPrintable(result.error));
    const QStringList names = zipEntryNames(readAll(options.outputPath));
    QVERIFY(names.contains(QStringLiteral("modrinth.index.json")));
    QVERIFY(names.contains(QStringLiteral("NOTICE.md")));
    QVERIFY(names.contains(QStringLiteral("redistribution.json")));
    QVERIFY(names.contains(QStringLiteral("overrides/mods/alpha.jar")));
    ZipReader zip(options.outputPath);
    QVERIFY2(zip.open(), "self export is not a readable zip");
    QJsonObject index;
    QVERIFY(zip.readJson(QStringLiteral("modrinth.index.json"), &index));
    QCOMPARE(index.value(QStringLiteral("files")).toArray().size(), 0);
    QCOMPARE(index.value(QStringLiteral("dependencies")).toObject()
                 .value(QStringLiteral("minecraft")).toString(),
             pack.minecraftVersion);

    Pack denied;
    makePack(source, source.path(), &denied);
    denied.files[0].licenseId = QStringLiteral("LicenseRef-All-Rights-Reserved");
    const license::GateResult blocked = license::evaluate(denied);
    QVERIFY(!blocked.exportable);
    QCOMPARE(blocked.denied.size(), 1);
    const RenderResult refused = renderSelfContained(denied, options, blocked);
    QVERIFY(!refused.ok);
}

void TestPack::deployEmptyDirectory()
{
    QTemporaryDir source;
    Pack pack;
    makePack(source, source.path(), &pack);
    QTemporaryDir target;
    DeployOptions options;
    options.targetDir = target.path();
    const DeployReport report = deploy(pack, options);
    QVERIFY2(report.ok, qPrintable(report.error));
    QCOMPARE(report.deployed, 2);
    QVERIFY(QFileInfo::exists(target.filePath(QStringLiteral("mods/alpha.jar"))));
    QVERIFY(QFileInfo::exists(target.filePath(QStringLiteral(".bmcd-deployment.json"))));
    QVERIFY(QDir(target.path()).entryList(QStringList{QStringLiteral(".bmcd-staging-*")}, QDir::Dirs).isEmpty());
    QVERIFY(verifyDeployment(pack, target.path()).ok);
}

void TestPack::deployRejectsMissingCache()
{
    QTemporaryDir source;
    Pack pack;
    makePack(source, source.path(), &pack);
    QFile::remove(source.filePath(QStringLiteral("mods/alpha.jar")));
    QTemporaryDir target;
    DeployOptions options;
    options.targetDir = target.path();
    const DeployReport report = deploy(pack, options);
    QVERIFY(!report.ok);
    QCOMPARE(report.failed, 1);
    QVERIFY(!QFileInfo::exists(target.filePath(QStringLiteral("config/beta.toml"))));
    QVERIFY(QDir(target.path()).entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot).isEmpty());
}

void TestPack::deployDryRunWritesNothing()
{
    QTemporaryDir source;
    Pack pack;
    makePack(source, source.path(), &pack);
    QTemporaryDir target;
    DeployOptions options;
    options.targetDir = target.path();
    options.dryRun = true;
    const DeployReport report = deploy(pack, options);
    QVERIFY2(report.ok, qPrintable(report.error));
    QCOMPARE(report.deployed, 0);
    QVERIFY(QDir(target.path()).entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot).isEmpty());
}

void TestPack::deployWithoutOverwriteReportsConflict()
{
    QTemporaryDir source;
    Pack pack;
    makePack(source, source.path(), &pack);
    QTemporaryDir target;
    QFile existing(target.filePath(QStringLiteral("mods/alpha.jar")));
    QDir().mkpath(target.filePath(QStringLiteral("mods")));
    QVERIFY(existing.open(QIODevice::WriteOnly));
    existing.write("different");
    existing.close();

    DeployOptions options;
    options.targetDir = target.path();
    options.overwrite = false;
    const DeployReport report = deploy(pack, options);
    QVERIFY(!report.ok);
    QCOMPARE(report.conflicts.size(), 1);
    QVERIFY(readAll(target.filePath(QStringLiteral("mods/alpha.jar"))) == QByteArray("different"));
}

void TestPack::verifyDeploymentDetectsCorruption()
{
    QTemporaryDir source;
    Pack pack;
    makePack(source, source.path(), &pack);
    QTemporaryDir target;
    DeployOptions options;
    options.targetDir = target.path();
    QVERIFY(deploy(pack, options).ok);

    QFile corrupted(target.filePath(QStringLiteral("mods/alpha.jar")));
    QVERIFY(corrupted.open(QIODevice::WriteOnly | QIODevice::Truncate));
    corrupted.write("tampered");
    corrupted.close();

    const VerifyReport report = verifyDeployment(pack, target.path());
    QVERIFY(!report.ok);
    QCOMPARE(report.corrupt, 1);
}

void TestPack::licenceGateVerdicts()
{
    Pack pack;
    pack.name = QStringLiteral("L");
    pack.minecraftVersion = QStringLiteral("26.3");
    pack.versionId = QStringLiteral("1");
    const QList<QPair<QString, QString>> cases{
        {QStringLiteral("mods/mit.jar"), QStringLiteral("MIT")},
        {QStringLiteral("mods/gpl.jar"), QStringLiteral("GPL-3.0-only")},
        {QStringLiteral("mods/unknown.jar"), QStringLiteral("LicenseRef-Custom")},
        {QStringLiteral("mods/none.jar"), QString()},
    };
    for (const auto &entry : cases) {
        PackFile file;
        file.path = entry.first;
        file.url = QStringLiteral("https://example.invalid/x");
        file.sha1 = QString(40, QLatin1Char('a'));
        file.size = 1;
        file.kind = QStringLiteral("mod");
        file.licenseId = entry.second;
        pack.files.append(file);
    }

    const license::GateResult gate = license::evaluate(pack);
    QCOMPARE(gate.verdicts.value(QStringLiteral("mods/mit.jar")), license::Verdict::Allowed);
    QCOMPARE(gate.verdicts.value(QStringLiteral("mods/gpl.jar")), license::Verdict::NeedsConfirm);
    QCOMPARE(gate.verdicts.value(QStringLiteral("mods/unknown.jar")), license::Verdict::Denied);
    QCOMPARE(gate.verdicts.value(QStringLiteral("mods/none.jar")), license::Verdict::Denied);
    QVERIFY(!gate.exportable);
    QVERIFY(gate.noticeMarkdown.contains(QStringLiteral("mods/gpl.jar")));

    const license::GateResult confirmed = license::evaluate(pack, {QStringLiteral("mods/gpl.jar"),
                                                                  QStringLiteral("mods/none.jar")});
    QCOMPARE(confirmed.verdicts.value(QStringLiteral("mods/gpl.jar")), license::Verdict::Allowed);
    QCOMPARE(confirmed.verdicts.value(QStringLiteral("mods/none.jar")), license::Verdict::Allowed);
    QVERIFY(confirmed.redistributionJson.contains("\"confirmed\": true"));
}

QTEST_GUILESS_MAIN(TestPack)
#include "tst_pack.moc"


void TestPack::licenceGroupsByParent()
{
    Pack pack;
    pack.name = QStringLiteral("G");
    pack.minecraftVersion = QStringLiteral("26.3");
    pack.versionId = QStringLiteral("1");

    const auto add = [&pack](const QString &path, const QString &license,
                             const QString &projectId, const QString &kind) {
        PackFile file;
        file.path = path;
        file.url = QStringLiteral("https://example.invalid/") + path;
        file.sha1 = QString(40, QLatin1Char('b'));
        file.size = 1;
        file.licenseId = license;
        file.projectId = projectId;
        file.projectName = projectId == QLatin1String("alpha") ? QStringLiteral("Alpha Mod")
                                                               : QString();
        file.kind = kind;
        pack.files.append(file);
    };
    // Two artefacts of the same parent mod: one click must cover both.
    add(QStringLiteral("mods/alpha.jar"), QString(), QStringLiteral("alpha"), QStringLiteral("mod"));
    add(QStringLiteral("config/alpha.toml"), QStringLiteral("MIT"), QStringLiteral("alpha"),
        QStringLiteral("config"));
    // A second parent mod with a copyleft licence.
    add(QStringLiteral("mods/beta.jar"), QStringLiteral("GPL-3.0-only"), QStringLiteral("beta"),
        QStringLiteral("mod"));
    // Launcher-installed files are not redistributed by a self-contained pack.
    add(QStringLiteral("assets/objects/ab/cdef"), QString(), QString(), QStringLiteral("asset"));

    const license::GateResult gate = license::evaluate(pack);
    QVERIFY(!gate.verdicts.contains(QStringLiteral("assets/objects/ab/cdef")));
    QCOMPARE(gate.groups.size(), 2);
    QCOMPARE(gate.denied.size(), 1);
    QCOMPARE(gate.deniedGroups.size(), 1);
    QCOMPARE(gate.needsConfirmGroups.size(), 1);

    int alphaIndex = -1;
    for (int i = 0; i < gate.groups.size(); ++i) {
        if (gate.groups[i].id == QLatin1String("@project:alpha"))
            alphaIndex = i;
    }
    QVERIFY(alphaIndex >= 0);
    QCOMPARE(gate.groups[alphaIndex].name, QStringLiteral("Alpha Mod"));
    QCOMPARE(gate.groups[alphaIndex].fileCount, 2);
    QCOMPARE(gate.groups[alphaIndex].pendingCount, 1);
    QCOMPARE(gate.groups[alphaIndex].verdict, license::Verdict::Denied);

    const license::GateResult confirmed = license::evaluate(pack, {QStringLiteral("@project:alpha")});
    QCOMPARE(confirmed.verdicts.value(QStringLiteral("mods/alpha.jar")), license::Verdict::Allowed);
    QCOMPARE(confirmed.verdicts.value(QStringLiteral("config/alpha.toml")), license::Verdict::Allowed);
    QVERIFY(confirmed.deniedGroups.isEmpty());
    QVERIFY(confirmed.exportable);
}

void TestPack::zipReaderRoundTrip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("roundtrip.zip"));

    ZipWriter writer(path);
    QVERIFY(writer.addData(QStringLiteral("modrinth.index.json"),
                           QByteArray("{\"formatVersion\":1}"), true));
    QVERIFY(writer.addData(QStringLiteral("overrides/config/app.cfg"),
                           QByteArray("key=value\n"), false));
    QVERIFY(writer.addData(QStringLiteral("empty.txt"), QByteArray(), true));
    QVERIFY(writer.close());

    ZipReader reader(path);
    QString error;
    QVERIFY2(reader.open(&error), qPrintable(error));
    QCOMPARE(reader.names().size(), 3);
    QVERIFY(reader.contains(QStringLiteral("overrides/config/app.cfg")));

    QByteArray data;
    QVERIFY2(reader.read(QStringLiteral("overrides/config/app.cfg"), &data, &error),
             qPrintable(error));
    QCOMPARE(data, QByteArray("key=value\n"));

    QJsonObject index;
    QVERIFY2(reader.readJson(QStringLiteral("modrinth.index.json"), &index, &error),
             qPrintable(error));
    QCOMPARE(index.value(QStringLiteral("formatVersion")).toInt(), 1);

    QVERIFY(!reader.read(QStringLiteral("nope.txt"), &data, &error));
}

void TestPack::importModrinthArchive()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QByteArray payload("mod-jar-bytes");
    const QString archive = dir.filePath(QStringLiteral("pack.mrpack"));

    const QJsonObject index{
        {QStringLiteral("formatVersion"), 1},
        {QStringLiteral("game"), QStringLiteral("minecraft")},
        {QStringLiteral("name"), QStringLiteral("Imported")},
        {QStringLiteral("versionId"), QStringLiteral("2.0.0")},
        {QStringLiteral("summary"), QStringLiteral("from test")},
        {QStringLiteral("dependencies"),
         QJsonObject{{QStringLiteral("minecraft"), QStringLiteral("1.20.1")},
                     {QStringLiteral("fabric-loader"), QStringLiteral("0.16.9")}}},
        {QStringLiteral("files"),
         QJsonArray{QJsonObject{
             {QStringLiteral("path"), QStringLiteral("mods/alpha.jar")},
             {QStringLiteral("hashes"), QJsonObject{{QStringLiteral("sha1"), sha1Of(payload)}}},
             {QStringLiteral("downloads"),
              QJsonArray{QStringLiteral("https://cdn.example.invalid/alpha.jar")}},
             {QStringLiteral("fileSize"), payload.size()},
             {QStringLiteral("env"), QJsonObject{{QStringLiteral("client"), QStringLiteral("required")},
                                                 {QStringLiteral("server"), QStringLiteral("unsupported")}}},
         }}},
    };

    ZipWriter writer(archive);
    QVERIFY(writer.addData(QStringLiteral("modrinth.index.json"),
                           QJsonDocument(index).toJson(), true));
    QVERIFY(writer.addData(QStringLiteral("overrides/config/client.json"),
                           QByteArray("{\"sodium\":true}"), true));
    QVERIFY(writer.addData(QStringLiteral("client-overrides/config/extra.json"),
                           QByteArray("{}"), true));
    QVERIFY(writer.close());

    const ImportResult result = importPackArchive(archive);
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.format, QStringLiteral("modrinth"));
    QCOMPARE(result.pack.name, QStringLiteral("Imported"));
    QCOMPARE(result.pack.minecraftVersion, QStringLiteral("1.20.1"));
    QCOMPARE(result.pack.loaderType, QStringLiteral("fabric"));
    QCOMPARE(result.pack.loaderVersion, QStringLiteral("0.16.9"));
    QCOMPARE(result.pack.files.size(), 1);
    QCOMPARE(result.overrideCount, 2);
    QCOMPARE(result.pack.overrides.size(), 2);
    QVERIFY(QFileInfo::exists(QDir(result.stagingDir).filePath(QStringLiteral("config/client.json"))));

    // Simulate the download having completed, then deploy both trees.
    QDir().mkpath(QDir(result.pack.target).filePath(QStringLiteral("mods")));
    QFile cached(QDir(result.pack.target).filePath(QStringLiteral("mods/alpha.jar")));
    QVERIFY(cached.open(QIODevice::WriteOnly));
    cached.write(payload);
    cached.close();

    QTemporaryDir target;
    DeployOptions options;
    options.targetDir = target.path();
    const DeployReport report = deploy(result.pack, options);
    QVERIFY2(report.ok, qPrintable(report.error));
    QCOMPARE(report.deployed, 3);
    QCOMPARE(readAll(target.filePath(QStringLiteral("mods/alpha.jar"))), payload);
    QVERIFY(QFileInfo::exists(target.filePath(QStringLiteral("config/client.json"))));
    QVERIFY(QFileInfo::exists(target.filePath(QStringLiteral("config/extra.json"))));

    const VerifyReport verify = verifyDeployment(result.pack, target.path());
    QVERIFY2(verify.ok, qPrintable(verify.problems.join(QStringLiteral(", "))));

    // The regenerated mrpack carries the overrides tree too.
    const QString exported = dir.filePath(QStringLiteral("out.mrpack"));
    const RenderResult rendered = renderMrpack(result.pack, RenderOptions{exported, {}, {}, {}});
    QVERIFY2(rendered.ok, qPrintable(rendered.error));
    ZipReader exportedReader(exported);
    QVERIFY(exportedReader.open());
    QVERIFY(exportedReader.contains(QStringLiteral("overrides/config/client.json")));
}

void TestPack::importCurseForgeManifest()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString archive = dir.filePath(QStringLiteral("cf.zip"));
    QJsonObject resolvedFile{
        {QStringLiteral("projectID"), 123},
        {QStringLiteral("fileID"), 456},
        {QStringLiteral("path"), QStringLiteral("mods/cf.jar")},
        {QStringLiteral("url"), QStringLiteral("https://edge.example.invalid/cf.jar")},
        {QStringLiteral("required"), true},
        {QStringLiteral("hashes"),
         QJsonArray{QJsonObject{{QStringLiteral("algo"), 1},
                                {QStringLiteral("value"), sha1Of(QByteArray("cf"))}}}},
    };
    QJsonObject unresolvedFile{
        {QStringLiteral("projectID"), 9},
        {QStringLiteral("fileID"), 10},
        {QStringLiteral("path"), QStringLiteral("mods/unresolved.jar")},
        {QStringLiteral("required"), false},
    };
    const QJsonObject manifest{
        {QStringLiteral("manifestType"), QStringLiteral("minecraftModpack")},
        {QStringLiteral("manifestVersion"), 1},
        {QStringLiteral("name"), QStringLiteral("CF Pack")},
        {QStringLiteral("version"), QStringLiteral("3.1")},
        {QStringLiteral("minecraft"),
         QJsonObject{{QStringLiteral("version"), QStringLiteral("1.19.2")},
                     {QStringLiteral("modLoaders"),
                      QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("forge-43.2.0")},
                                             {QStringLiteral("primary"), true}}}}}},
        {QStringLiteral("files"), QJsonArray{resolvedFile, unresolvedFile}},
    };

    ZipWriter writer(archive);
    QVERIFY(writer.addData(QStringLiteral("manifest.json"), QJsonDocument(manifest).toJson(), true));
    QVERIFY(writer.addData(QStringLiteral("overrides/config/cf.toml"), QByteArray("x=1"), true));
    QVERIFY(writer.close());

    const ImportResult result = importPackArchive(archive);
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.format, QStringLiteral("curseforge"));
    QCOMPARE(result.pack.loaderType, QStringLiteral("forge"));
    QCOMPARE(result.pack.loaderVersion, QStringLiteral("43.2.0"));
    QCOMPARE(result.pack.files.size(), 1);
    QCOMPARE(result.pack.files.first().projectId, QStringLiteral("123"));
    QCOMPARE(result.pack.files.first().fileId, QStringLiteral("456"));
    QCOMPARE(result.unresolved.size(), 1);
    QVERIFY(result.unresolved.first().contains(QStringLiteral("unresolved.jar")));
    QCOMPARE(result.overrideCount, 1);
}

void TestPack::ftbEncodesUrlSpaces()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString archive = dir.filePath(QStringLiteral("ftb.zip"));
    const QJsonObject manifest{
        {QStringLiteral("name"), QStringLiteral("FTB Space")},
        {QStringLiteral("version"), QStringLiteral("1.0")},
        {QStringLiteral("targets"),
         QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("modloader")},
                                {QStringLiteral("name"), QStringLiteral("forge")},
                                {QStringLiteral("version"), QStringLiteral("14.23.5.2846")}},
                    QJsonObject{{QStringLiteral("type"), QStringLiteral("game")},
                                {QStringLiteral("name"), QStringLiteral("minecraft")},
                                {QStringLiteral("version"), QStringLiteral("1.12.2")}}}},
        {QStringLiteral("files"),
         QJsonArray{QJsonObject{
             {QStringLiteral("path"), QStringLiteral("mods/a b.jar")},
             {QStringLiteral("downloads"),
              QJsonArray{QStringLiteral("https://dist.example.invalid/FTB Academy-1.1.0/mods/a b.jar")}},
             {QStringLiteral("hashes"),
              QJsonArray{QJsonObject{{QStringLiteral("algo"), 1},
                                     {QStringLiteral("value"), sha1Of(QByteArray("x"))}}}},
             {QStringLiteral("fileSize"), 1},
         }}},
    };

    ZipWriter writer(archive);
    QVERIFY(writer.addData(QStringLiteral("manifest.json"), QJsonDocument(manifest).toJson(), true));
    QVERIFY(writer.addData(QStringLiteral("overrides/.keep"), QByteArray(), true));
    QVERIFY(writer.close());

    const ImportResult result = importPackArchive(archive);
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.format, QStringLiteral("ftb"));
    QCOMPARE(result.pack.files.size(), 1);
    QCOMPARE(result.pack.files.first().url,
             QStringLiteral("https://dist.example.invalid/FTB%20Academy-1.1.0/mods/a%20b.jar"));
}

void TestPack::instancesAreDiscovered()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QDir().mkpath(root.filePath(QStringLiteral("MyPack/mods")));
    QDir().mkpath(root.filePath(QStringLiteral("Vanilla")));

    QFile packFile(root.filePath(QStringLiteral("MyPack/bmcd-pack.json")));
    QVERIFY(packFile.open(QIODevice::WriteOnly));
    packFile.write(QJsonDocument(QJsonObject{
        {QStringLiteral("name"), QStringLiteral("My Pack")},
        {QStringLiteral("minecraftVersion"), QStringLiteral("1.20.1")},
        {QStringLiteral("loaderType"), QStringLiteral("fabric")},
        {QStringLiteral("loaderVersion"), QStringLiteral("0.16.9")},
        {QStringLiteral("files"), QJsonArray{}},
        {QStringLiteral("overrides"), QJsonArray{QStringLiteral("config/a.toml")}},
    }).toJson());
    packFile.close();
    QFile jar(root.filePath(QStringLiteral("MyPack/mods/a.jar")));
    QVERIFY(jar.open(QIODevice::WriteOnly));
    jar.write("x");
    jar.close();

    QDir().mkpath(root.filePath(QStringLiteral("MmcInstance/mods")));
    QFile mmc(root.filePath(QStringLiteral("MmcInstance/mmc-pack.json")));
    QVERIFY(mmc.open(QIODevice::WriteOnly));
    mmc.write(QJsonDocument(QJsonObject{{QStringLiteral("components"),
        QJsonArray{QJsonObject{{QStringLiteral("uid"), QStringLiteral("net.minecraft")},
                               {QStringLiteral("version"), QStringLiteral("1.19.2")}},
                   QJsonObject{{QStringLiteral("uid"), QStringLiteral("net.minecraftforge")},
                               {QStringLiteral("version"), QStringLiteral("43.2.0")}}}}}).toJson());
    mmc.close();

    QDir().mkpath(root.filePath(QStringLiteral("Deployed/mods")));
    QFile receipt(root.filePath(QStringLiteral("Deployed/.bmcd-deployment.json")));
    QVERIFY(receipt.open(QIODevice::WriteOnly));
    receipt.write(QJsonDocument(QJsonObject{
        {QStringLiteral("format"), 1},
        {QStringLiteral("pack"), QJsonObject{
             {QStringLiteral("name"), QStringLiteral("Deployed Pack")},
             {QStringLiteral("minecraftVersion"), QStringLiteral("1.21.1")},
             {QStringLiteral("loaderType"), QStringLiteral("neoforge")},
             {QStringLiteral("loaderVersion"), QStringLiteral("21.1.80")},
             {QStringLiteral("files"), QJsonArray{}}}},
    }).toJson());
    receipt.close();

    const QVector<LocalInstance> instances = scanInstances(root.path());
    QCOMPARE(instances.size(), 3);
    bool foundPack = false;
    bool foundMmc = false;
    bool foundDeployment = false;
    for (const LocalInstance &instance : instances) {
        if (instance.name == QStringLiteral("My Pack")) {
            foundPack = true;
            QCOMPARE(instance.modCount, 1);
            QCOMPARE(instance.source, QStringLiteral("bmcd"));
        }
        if (instance.path.endsWith(QStringLiteral("Deployed"))) {
            foundDeployment = true;
            QCOMPARE(instance.name, QStringLiteral("Deployed Pack"));
            QCOMPARE(instance.minecraftVersion, QStringLiteral("1.21.1"));
            QCOMPARE(instance.loaderType, QStringLiteral("neoforge"));
        }
        if (instance.path.endsWith(QStringLiteral("MmcInstance"))) {
            foundMmc = true;
            QCOMPARE(instance.minecraftVersion, QStringLiteral("1.19.2"));
            QCOMPARE(instance.loaderType, QStringLiteral("forge"));
        }
    }
    QVERIFY(foundPack);
    QVERIFY(foundMmc);
    QVERIFY(foundDeployment);
}
