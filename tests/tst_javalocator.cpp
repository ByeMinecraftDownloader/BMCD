#include "core/AppSettings.h"
#include "core/JavaLocator.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

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

#if !defined(Q_OS_WIN)
QString writeFakeJava(const QString &root, const QString &version)
{
    QDir bin(QDir(root).filePath(QStringLiteral("bin")));
    if (!bin.exists() && !QDir().mkpath(bin.path()))
        return {};
    const QString path = bin.filePath(QStringLiteral("java"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return {};
    const QByteArray script = "#!/bin/sh\n"
                              "printf 'openjdk version \"" + version.toUtf8() + "\" 2024-01-01\\n' >&2\n";
    if (file.write(script) != script.size())
        return {};
    file.close();
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner
                        | QFileDevice::ReadGroup | QFileDevice::ExeGroup
                        | QFileDevice::ReadOther | QFileDevice::ExeOther);
    return path;
}
#endif

} // namespace

class JavaLocatorTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void parsesModernVersion();
    void parsesLegacyVersion();
    void detectsExplicitJava();
    void rejectsMissingExplicitJava();
    void prefersJavaHomeBeforePath();
    void choosesHighestSystemJdk();
    void storesAbsoluteJavaPath();

private:
    QTemporaryDir m_settingsDir;
};

void JavaLocatorTest::initTestCase()
{
    QVERIFY(m_settingsDir.isValid());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDir.path());
}

void JavaLocatorTest::init()
{
    QSettings store(QStringLiteral("BMCD"), QStringLiteral("bmcd"));
    store.remove(QStringLiteral("javaPath"));
}

void JavaLocatorTest::parsesModernVersion()
{
    QCOMPARE(JavaLocator::parseVersion(
                     QStringLiteral("openjdk version \"21.0.1\" 2023-10-17")),
             QStringLiteral("21.0.1"));
    QCOMPARE(JavaLocator::parseMajor(
                     QStringLiteral("openjdk version \"21.0.1\" 2023-10-17")),
             21);
}

void JavaLocatorTest::parsesLegacyVersion()
{
    QCOMPARE(JavaLocator::parseMajor(
                     QStringLiteral("java version \"1.8.0_392\"")),
             8);
    QCOMPARE(JavaLocator::parseVersion(QStringLiteral("not java")), QString());
    QCOMPARE(JavaLocator::parseMajor(QStringLiteral("not java")), 0);
}

void JavaLocatorTest::detectsExplicitJava()
{
#if defined(Q_OS_WIN)
    QSKIP("Fake java is a POSIX shell script");
#else
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString java = writeFakeJava(dir.filePath(QStringLiteral("jdk21")),
                                       QStringLiteral("21.0.1"));
    QVERIFY(!java.isEmpty());

    ScopedEnv env("MC_JAVA", java.toUtf8());
    JavaLocator locator;
    locator.setScanSystemDirsForTests(false);
    locator.detect();
    QTRY_VERIFY_WITH_TIMEOUT(!locator.checking(), 5000);

    QVERIFY(locator.available());
    QCOMPARE(locator.major(), 21);
    QCOMPARE(locator.version(), QStringLiteral("21.0.1"));
    QCOMPARE(locator.path(), java);
    QVERIFY(locator.error().isEmpty());
#endif
}

void JavaLocatorTest::rejectsMissingExplicitJava()
{
#if defined(Q_OS_WIN)
    QSKIP("Fake java is a POSIX shell script");
#else
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString missing = dir.filePath(QStringLiteral("missing/java"));

    ScopedEnv env("MC_JAVA", missing.toUtf8());
    JavaLocator locator;
    locator.setScanSystemDirsForTests(false);
    locator.detect();
    QTRY_VERIFY_WITH_TIMEOUT(!locator.checking(), 5000);

    QVERIFY(!locator.available());
    QCOMPARE(locator.major(), 0);
    QVERIFY(!locator.error().isEmpty());
    QVERIFY(locator.path().isEmpty());
#endif
}

void JavaLocatorTest::prefersJavaHomeBeforePath()
{
#if defined(Q_OS_WIN)
    QSKIP("Fake java is a POSIX shell script");
#else
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString java8 = writeFakeJava(dir.filePath(QStringLiteral("jdk8")),
                                        QStringLiteral("1.8.0_392"));
    const QString java21 = writeFakeJava(dir.filePath(QStringLiteral("jdk21")),
                                         QStringLiteral("21.0.1"));
    QVERIFY(!java8.isEmpty());
    QVERIFY(!java21.isEmpty());

    ScopedEnv noExplicit("MC_JAVA", QByteArray());
    ScopedEnv javaHome("JAVA_HOME",
                       QFileInfo(java8).absolutePath().remove(QStringLiteral("/bin")).toUtf8());
    ScopedEnv path("PATH", QFileInfo(java21).absolutePath().toUtf8()
                           + ":" + qgetenv("PATH"));

    JavaLocator locator;
    locator.setScanSystemDirsForTests(false);
    locator.detect();
    QTRY_VERIFY_WITH_TIMEOUT(!locator.checking(), 8000);

    QVERIFY(locator.available());
    QCOMPARE(locator.major(), 8);
    QCOMPARE(locator.path(), java8);
    QCOMPARE(locator.source(), QStringLiteral("JAVA_HOME"));
#endif
}

void JavaLocatorTest::choosesHighestSystemJdk()
{
#if defined(Q_OS_WIN)
    QSKIP("Fake java is a POSIX shell script");
#else
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString java8 = writeFakeJava(dir.filePath(QStringLiteral("jdk8")),
                                        QStringLiteral("1.8.0_392"));
    const QString java21 = writeFakeJava(dir.filePath(QStringLiteral("jdk21")),
                                         QStringLiteral("21.0.1"));
    QVERIFY(!java8.isEmpty());
    QVERIFY(!java21.isEmpty());

    ScopedEnv noExplicit("MC_JAVA", QByteArray());
    ScopedEnv noHome("JAVA_HOME", QByteArray());
    ScopedEnv noPath("PATH", QByteArray());

    JavaLocator locator;
    locator.setScanSystemDirsForTests(true);
    locator.setSystemRootsForTests({dir.path()});
    locator.detect();
    QTRY_VERIFY_WITH_TIMEOUT(!locator.checking(), 8000);

    QVERIFY(locator.available());
    QCOMPARE(locator.major(), 21);
    QCOMPARE(locator.path(), java21);
#endif
}

void JavaLocatorTest::storesAbsoluteJavaPath()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString absolute = dir.filePath(QStringLiteral("jdk/bin/java"));
    AppSettings settings;
    settings.setJavaPath(absolute);
    QCOMPARE(AppSettings::configuredJavaPath(), QDir::cleanPath(absolute));

    settings.setJavaPath(QStringLiteral("relative/java"));
    QCOMPARE(AppSettings::configuredJavaPath(), QString());
}

QTEST_GUILESS_MAIN(JavaLocatorTest)
#include "tst_javalocator.moc"
