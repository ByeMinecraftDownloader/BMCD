#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "platform/Console.h"
#include "platform/FileOps.h"
#include "platform/Paths.h"
#include "platform/PlatformInfo.h"

class TestPlatform : public QObject
{
    Q_OBJECT

private slots:
    void osNameIsKnown();
    void defaultGameDirFollowsPlatform();
    void replaceFileOverwrites();
    void copyFileWorks();
    void writeFileAtomicCreatesParents();
    void removeIfExistsIsIdempotent();
    void nativePathKeepsPathsUsable();
    void userAgentCarriesVersion();
};

void TestPlatform::osNameIsKnown()
{
    const QString os = platform::osName();
    QVERIFY(os == QLatin1String("windows") || os == QLatin1String("macos")
            || os == QLatin1String("linux"));
}

void TestPlatform::defaultGameDirFollowsPlatform()
{
    const QString dir = platform::defaultGameDir();
    QVERIFY(dir.endsWith(QLatin1String(".minecraft")) || dir.endsWith(QLatin1String("minecraft")));
    QVERIFY(QDir::isAbsolutePath(dir));
#if defined(Q_OS_WIN)
    QVERIFY(dir.contains(QLatin1String("APPDATA")) || dir.contains(QLatin1String("Roaming")));
#endif
}

void TestPlatform::replaceFileOverwrites()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString target = tmp.filePath(QStringLiteral("a.txt"));
    const QString staged = tmp.filePath(QStringLiteral("a.txt.part"));
    QFile(target).open(QIODevice::WriteOnly);
    QFile(target).close();
    QVERIFY(platform::writeFileAtomic(target, "old"));
    QVERIFY(platform::writeFileAtomic(staged, "new"));
    QVERIFY(platform::replaceFile(staged, target));
    QFile file(target);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArray("new"));
    QVERIFY(!QFileInfo::exists(staged));
}

void TestPlatform::copyFileWorks()
{
    QTemporaryDir tmp;
    const QString source = tmp.filePath(QStringLiteral("src.bin"));
    const QString target = tmp.filePath(QStringLiteral("nested/dst.bin"));
    QVERIFY(platform::writeFileAtomic(source, QByteArray(1024, 'x')));
    QVERIFY(platform::copyFile(source, target));
    QCOMPARE(QFileInfo(target).size(), qint64(1024));
    QVERIFY(QFileInfo::exists(source)); // copy must not consume the source
}

void TestPlatform::writeFileAtomicCreatesParents()
{
    QTemporaryDir tmp;
    const QString path = tmp.filePath(QStringLiteral("a/b/c/d.txt"));
    QVERIFY(platform::writeFileAtomic(path, "payload"));
    QCOMPARE(QFileInfo(path).size(), qint64(7));
}

void TestPlatform::removeIfExistsIsIdempotent()
{
    QTemporaryDir tmp;
    const QString path = tmp.filePath(QStringLiteral("gone.txt"));
    QVERIFY(platform::removeIfExists(path));
    QVERIFY(platform::writeFileAtomic(path, "x"));
    QVERIFY(platform::removeIfExists(path));
    QVERIFY(!QFileInfo::exists(path));
}

void TestPlatform::nativePathKeepsPathsUsable()
{
    const QString path = platform::nativePath(QStringLiteral("/tmp/x/y.txt"));
    QVERIFY(!path.isEmpty());
    QCOMPARE(QFileInfo(path).fileName(), QStringLiteral("y.txt"));
}

void TestPlatform::userAgentCarriesVersion()
{
    QCoreApplication::setApplicationVersion(QStringLiteral("9.9.9"));
    QCOMPARE(platform::userAgent(), QStringLiteral("BMCD/9.9.9"));
}

QTEST_MAIN(TestPlatform)
#include "tst_platform.moc"
