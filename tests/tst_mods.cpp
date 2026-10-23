#include "mods/ModAliases.h"
#include "mods/ModResolver.h"

#include <QTest>

using namespace mods;

namespace {

Version makeVersion(const QString &id, const QString &project, const QString &date,
                    QVector<Dependency> dependencies = {}, QString fileName = {})
{
    Version version;
    version.id = id;
    version.projectId = project;
    version.name = id;
    version.datePublished = date;
    version.loaders = {QStringLiteral("fabric")};
    version.gameVersions = {QStringLiteral("1.21.1")};
    version.dependencies = std::move(dependencies);
    if (fileName.isEmpty())
        fileName = project + QStringLiteral("-") + id + QStringLiteral(".jar");
    File file;
    file.filename = fileName;
    file.url = QStringLiteral("https://cdn.example/") + fileName;
    file.sha1 = QStringLiteral("0123456789abcdef0123456789abcdef01234567");
    file.size = 1024;
    file.primary = true;
    version.files.append(file);
    return version;
}

Dependency dep(const QString &project, const QString &type = QStringLiteral("required"),
               const QString &version = {})
{
    Dependency dependency;
    dependency.projectId = project;
    dependency.type = type;
    dependency.versionId = version;
    return dependency;
}

Project projectFor(const QString &id)
{
    Project project;
    project.id = id;
    project.slug = id.toLower();
    project.title = id;
    project.licenseId = QStringLiteral("MIT");
    project.licenseName = QStringLiteral("MIT License");
    return project;
}

ResolveResult run(const QVector<Root> &roots, const QHash<QString, QVector<Version>> &catalog,
                  const QString &loader = QStringLiteral("fabric"),
                  const QString &game = QStringLiteral("1.21.1"))
{
    ResolveRequest request;
    request.loader = loader;
    request.gameVersion = game;
    request.roots = roots;
    return resolve(request,
                   [&catalog](const QString &key) { return catalog.value(key); },
                   [](const QString &key) { return projectFor(key); });
}

} // namespace

class TestMods : public QObject
{
    Q_OBJECT
private slots:
    void requiredClosure();
    void pinnedDependency();
    void missingDependency();
    void incompatibleDependency();
    void pathConflict();
    void loaderFiltering();
    void pinnedRootConflict();
    void optionalMissing();
    void dependencyCycle();
    void selfDependency();
    void aliasTable();
    void conflictsAreStructured();
};

void TestMods::requiredClosure()
{
    QHash<QString, QVector<Version>> catalog;
    catalog.insert(QStringLiteral("a"), {makeVersion(QStringLiteral("a1"), QStringLiteral("A"),
                                                    QStringLiteral("2026-01-01"),
                                                    {dep(QStringLiteral("b"))})});
    catalog.insert(QStringLiteral("b"), {makeVersion(QStringLiteral("b1"), QStringLiteral("B"),
                                                    QStringLiteral("2026-01-02"),
                                                    {dep(QStringLiteral("c"))})});
    catalog.insert(QStringLiteral("c"), {makeVersion(QStringLiteral("c1"), QStringLiteral("C"),
                                                    QStringLiteral("2026-01-03"))});

    Root root;
    root.project = QStringLiteral("a");
    const ResolveResult result = run({root}, catalog);
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.versions.size(), 3);
    QCOMPARE(result.projects.size(), 3);
    QCOMPARE(result.addedDependencies,
             QStringList({QStringLiteral("B"), QStringLiteral("C")}));
}

void TestMods::pinnedDependency()
{
    QHash<QString, QVector<Version>> catalog;
    catalog.insert(QStringLiteral("a"), {makeVersion(QStringLiteral("a1"), QStringLiteral("A"),
                                                    QStringLiteral("2026-01-01"),
                                                    {dep(QStringLiteral("b"), QStringLiteral("required"),
                                                         QStringLiteral("b-old"))})});
    catalog.insert(QStringLiteral("b"), {makeVersion(QStringLiteral("b-old"), QStringLiteral("B"),
                                                    QStringLiteral("2025-01-01")),
                                         makeVersion(QStringLiteral("b-new"), QStringLiteral("B"),
                                                    QStringLiteral("2026-06-01"))});

    Root root;
    root.project = QStringLiteral("a");
    const ResolveResult result = run({root}, catalog);
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.versions.size(), 2);
    QCOMPARE(result.versions.at(1).id, QStringLiteral("b-old"));
}

void TestMods::missingDependency()
{
    QHash<QString, QVector<Version>> catalog;
    catalog.insert(QStringLiteral("a"), {makeVersion(QStringLiteral("a1"), QStringLiteral("A"),
                                                    QStringLiteral("2026-01-01"),
                                                    {dep(QStringLiteral("ghost"))})});
    Root root;
    root.project = QStringLiteral("a");
    const ResolveResult result = run({root}, catalog);
    QVERIFY(!result.ok);
    QVERIFY(result.error.contains(QStringLiteral("缺少依赖")));
    QCOMPARE(result.conflicts.size(), 1);
    QCOMPARE(result.conflicts.first().kind, QStringLiteral("missing"));
}

void TestMods::incompatibleDependency()
{
    QHash<QString, QVector<Version>> catalog;
    catalog.insert(QStringLiteral("a"), {makeVersion(QStringLiteral("a1"), QStringLiteral("A"),
                                                    QStringLiteral("2026-01-01"),
                                                    {dep(QStringLiteral("X"), QStringLiteral("incompatible"))})});
    catalog.insert(QStringLiteral("x"), {makeVersion(QStringLiteral("x1"), QStringLiteral("X"),
                                                    QStringLiteral("2026-01-02"))});
    Root rootA;
    rootA.project = QStringLiteral("a");
    Root rootX;
    rootX.project = QStringLiteral("x");
    const ResolveResult result = run({rootA, rootX}, catalog);
    QVERIFY(!result.ok);
    QVERIFY(result.error.contains(QStringLiteral("不兼容")));
}

void TestMods::pathConflict()
{
    QHash<QString, QVector<Version>> catalog;
    catalog.insert(QStringLiteral("a"), {makeVersion(QStringLiteral("a1"), QStringLiteral("A"),
                                                    QStringLiteral("2026-01-01"), {},
                                                    QStringLiteral("same.jar"))});
    catalog.insert(QStringLiteral("b"), {makeVersion(QStringLiteral("b1"), QStringLiteral("B"),
                                                    QStringLiteral("2026-01-02"), {},
                                                    QStringLiteral("same.jar"))});
    Root rootA;
    rootA.project = QStringLiteral("a");
    Root rootB;
    rootB.project = QStringLiteral("b");
    const ResolveResult result = run({rootA, rootB}, catalog);
    QVERIFY(!result.ok);
    QVERIFY(result.error.contains(QStringLiteral("文件冲突")));
}

void TestMods::loaderFiltering()
{
    QHash<QString, QVector<Version>> catalog;
    Version forge = makeVersion(QStringLiteral("a-forge"), QStringLiteral("A"),
                                QStringLiteral("2026-06-01"));
    forge.loaders = {QStringLiteral("forge")};
    Version fabricOld = makeVersion(QStringLiteral("a-old"), QStringLiteral("A"),
                                   QStringLiteral("2025-06-01"));
    catalog.insert(QStringLiteral("a"), {fabricOld, forge});
    Root root;
    root.project = QStringLiteral("a");
    const ResolveResult result = run({root}, catalog);
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.versions.size(), 1);
    QCOMPARE(result.versions.first().id, QStringLiteral("a-old"));
}

void TestMods::pinnedRootConflict()
{
    QHash<QString, QVector<Version>> catalog;
    catalog.insert(QStringLiteral("a"), {makeVersion(QStringLiteral("a1"), QStringLiteral("A"),
                                                    QStringLiteral("2026-01-01")),
                                         makeVersion(QStringLiteral("a2"), QStringLiteral("A"),
                                                    QStringLiteral("2026-02-01"))});
    Root first;
    first.project = QStringLiteral("a");
    first.versionId = QStringLiteral("a1");
    Root second;
    second.project = QStringLiteral("a");
    second.versionId = QStringLiteral("a2");
    const ResolveResult result = run({first, second}, catalog);
    QVERIFY(!result.ok);
    QVERIFY(result.error.contains(QStringLiteral("另一个版本")));
}

void TestMods::optionalMissing()
{
    QHash<QString, QVector<Version>> catalog;
    catalog.insert(QStringLiteral("a"), {makeVersion(QStringLiteral("a1"), QStringLiteral("A"),
                                                    QStringLiteral("2026-01-01"),
                                                    {dep(QStringLiteral("b"), QStringLiteral("optional"))})});
    Root root;
    root.project = QStringLiteral("a");
    const ResolveResult result = run({root}, catalog);
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.versions.size(), 1);
}

void TestMods::dependencyCycle()
{
    QHash<QString, QVector<Version>> catalog;
    catalog.insert(QStringLiteral("a"), {makeVersion(QStringLiteral("a1"), QStringLiteral("A"),
                                                    QStringLiteral("2026-01-01"),
                                                    {dep(QStringLiteral("b"))})});
    catalog.insert(QStringLiteral("b"), {makeVersion(QStringLiteral("b1"), QStringLiteral("B"),
                                                    QStringLiteral("2026-01-01"),
                                                    {dep(QStringLiteral("a"))})});
    Root root;
    root.project = QStringLiteral("a");
    const ResolveResult result = run({root}, catalog);
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.versions.size(), 2);
}

void TestMods::selfDependency()
{
    QHash<QString, QVector<Version>> catalog;
    catalog.insert(QStringLiteral("a"), {makeVersion(QStringLiteral("a1"), QStringLiteral("A"),
                                                    QStringLiteral("2026-01-01"),
                                                    {dep(QStringLiteral("a"))})});
    Root root;
    root.project = QStringLiteral("a");
    const ResolveResult result = run({root}, catalog);
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.versions.size(), 1);
}

QTEST_GUILESS_MAIN(TestMods)
#include "tst_mods.moc"

void TestMods::conflictsAreStructured()
{
    QHash<QString, QVector<Version>> catalog;
    catalog.insert(QStringLiteral("a"), {makeVersion(QStringLiteral("a1"), QStringLiteral("A"),
                                                    QStringLiteral("2026-01-01"),
                                                    {dep(QStringLiteral("X"), QStringLiteral("incompatible"))})});
    catalog.insert(QStringLiteral("x"), {makeVersion(QStringLiteral("x1"), QStringLiteral("X"),
                                                    QStringLiteral("2026-01-02"))});
    Root rootA;
    rootA.project = QStringLiteral("a");
    Root rootX;
    rootX.project = QStringLiteral("x");
    const ResolveResult result = run({rootA, rootX}, catalog);
    QVERIFY(!result.ok);
    QCOMPARE(result.conflicts.size(), 1);
    QCOMPARE(result.conflicts.first().kind, QStringLiteral("incompatible"));
    QCOMPARE(result.conflicts.first().projectId, QStringLiteral("X"));
}

void TestMods::aliasTable()
{
    const QJsonObject table{
        {QStringLiteral("小地图"), QStringLiteral("minimap")},
        {QStringLiteral("optimization"), QStringLiteral("opt")},
    };
    QCOMPARE(expandAlias(QStringLiteral(" 小地图 "), table), QStringLiteral("minimap"));
    QCOMPARE(expandAlias(QStringLiteral("OPTIMIZATION"), table), QStringLiteral("opt"));
    QCOMPARE(expandAlias(QStringLiteral("Xaero"), table), QStringLiteral("Xaero"));
    QCOMPARE(expandAlias(QString(), table), QString());
}
