#include "JavaLocator.h"

#include "AppSettings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>

namespace {

QString javaExecutableName()
{
#if defined(Q_OS_WIN)
    return QStringLiteral("java.exe");
#else
    return QStringLiteral("java");
#endif
}

// A user may point at either the executable or the JDK home. Accept both so a
// path copied from JAVA_HOME still works.
QString executableFromPath(const QString &path)
{
    const QFileInfo info(path);
    if (info.isDir())
        return QDir(path).filePath(QStringLiteral("bin/") + javaExecutableName());
    return path;
}

QString dedupeKey(const QString &path)
{
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
}

} // namespace

JavaLocator::JavaLocator(QObject *parent)
    : QObject(parent)
{
    m_process.setProcessChannelMode(platform::Process::MergedChannels);
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, &JavaLocator::processTimedOut);
    connect(&m_process, &platform::Process::finished, this, &JavaLocator::processFinished);
    connect(&m_process, &platform::Process::errorOccurred, this, &JavaLocator::processFailed);
}

JavaLocator::~JavaLocator()
{
    if (m_process.state() != platform::Process::NotRunning) {
        m_process.kill();
        m_process.waitForFinished(200);
    }
}

void JavaLocator::setScanSystemDirsForTests(bool enabled)
{
    m_scanSystemDirs = enabled;
}

void JavaLocator::setSystemRootsForTests(const QStringList &roots)
{
    m_systemRootsForTests = roots;
}

QString JavaLocator::parseVersion(const QString &output)
{
    static const QRegularExpression quoted(
            QStringLiteral(R"re((?:java|openjdk)\s+version\s+"([^"]+)")re"),
            QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch match = quoted.match(output);
    if (!match.hasMatch()) {
        static const QRegularExpression loose(QStringLiteral(R"re(version\s+"([^"]+)")re"),
                                              QRegularExpression::CaseInsensitiveOption);
        match = loose.match(output);
    }
    return match.hasMatch() ? match.captured(1).trimmed() : QString();
}

int JavaLocator::parseMajor(const QString &output)
{
    const QString version = parseVersion(output);
    if (version.isEmpty())
        return 0;

    const QStringList parts = version.split(QLatin1Char('.'));
    if (parts.size() >= 2 && parts.at(0) == QLatin1String("1"))
        return parts.at(1).toInt();
    return parts.value(0).toInt();
}

void JavaLocator::detect()
{
    detectInternal(false);
}

void JavaLocator::detectAgain()
{
    detectInternal(true);
}

void JavaLocator::detectInternal(bool force)
{
    if (m_checking)
        return;
    if (!force && m_checked)
        return;

    if (m_process.state() != platform::Process::NotRunning) {
        m_process.kill();
        m_process.waitForFinished(200);
    }

    m_checking = true;
    m_checked = false;
    m_runningCandidate = false;
    m_index = 0;
    m_candidates.clear();
    m_best = {};
    m_bestMajor = 0;
    m_version.clear();
    m_error.clear();
    emit changed();

    collectCandidates();
    if (m_candidates.isEmpty()) {
        finishWithError(tr("未找到 Java；可以安装 JRE，或手动指定 java 可执行文件。"));
        return;
    }
    runNext();
}

void JavaLocator::collectCandidates()
{
    QSet<QString> seen;
    const QString env = qEnvironmentVariable("MC_JAVA").trimmed();
    if (!env.isEmpty()) {
        addCandidate(executableFromPath(env), tr("环境变量 MC_JAVA"), true, true, seen);
        return;
    }

    const QString saved = AppSettings::configuredJavaPath().trimmed();
    if (!saved.isEmpty()) {
        addCandidate(executableFromPath(saved), tr("设置指定"), true, true, seen);
        return;
    }

    const QString javaHome = qEnvironmentVariable("JAVA_HOME").trimmed();
    if (!javaHome.isEmpty())
        addCandidate(QDir(javaHome).filePath(QStringLiteral("bin/") + javaExecutableName()),
                     QStringLiteral("JAVA_HOME"), false, true, seen);

    const QString fromPath = QStandardPaths::findExecutable(QStringLiteral("java"));
    if (!fromPath.isEmpty())
        addCandidate(fromPath, QStringLiteral("PATH"), false, true, seen);

    if (!m_scanSystemDirs)
        return;

    QStringList roots = m_systemRootsForTests;
    if (roots.isEmpty()) {
#if defined(Q_OS_WIN)
        for (const char *name : {"ProgramFiles", "ProgramFiles(x86)", "LOCALAPPDATA"}) {
            const QString base = qEnvironmentVariable(name);
            if (!base.isEmpty()) {
                roots << QDir(base).filePath(QStringLiteral("Java"))
                      << QDir(base).filePath(QStringLiteral("Eclipse Adoptium"))
                      << QDir(base).filePath(QStringLiteral("Microsoft"));
            }
        }
#elif defined(Q_OS_MACOS)
        roots << QStringLiteral("/Library/Java/JavaVirtualMachines")
              << QStringLiteral("/System/Library/Java/JavaVirtualMachines");
        roots << QDir::homePath() + QStringLiteral("/Library/Java/JavaVirtualMachines");
#else
        roots << QStringLiteral("/usr/lib/jvm")
              << QStringLiteral("/usr/java")
              << QStringLiteral("/opt/java")
              << QStringLiteral("/opt/jdk");
        roots << QDir::homePath() + QStringLiteral("/.sdkman/candidates/java")
              << QDir::homePath() + QStringLiteral("/.jdks")
              << QDir::homePath() + QStringLiteral("/.local/share/mise/installs/java");
#endif
    }

    for (const QString &root : roots) {
        QDir dir(root);
        if (!dir.exists())
            continue;
        const QStringList entries = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot,
                                                  QDir::Name);
        for (const QString &entry : entries) {
            const QString base = dir.filePath(entry);
            addCandidate(QDir(base).filePath(QStringLiteral("bin/") + javaExecutableName()),
                         tr("系统目录"), false, false, seen);
#if defined(Q_OS_MACOS)
            addCandidate(QDir(base).filePath(QStringLiteral("Contents/Home/bin/") + javaExecutableName()),
                         tr("系统目录"), false, false, seen);
#endif
        }
    }
}

void JavaLocator::addCandidate(const QString &path, const QString &source, bool explicitPath,
                               bool preferFirst, QSet<QString> &seen)
{
    if (path.trimmed().isEmpty())
        return;
    const QString clean = QDir::cleanPath(path);
    const QString key = dedupeKey(clean);
    if (seen.contains(key))
        return;
    seen.insert(key);

    const QFileInfo info(clean);
    // Keep a missing explicit path so detect() can explain that the user's
    // choice is wrong instead of silently falling back to another JDK.
    if (!explicitPath && (!info.exists() || !info.isFile()))
        return;

    Candidate candidate;
    candidate.path = clean;
    candidate.source = source;
    candidate.explicitlyConfigured = explicitPath;
    candidate.preferFirst = preferFirst;
    m_candidates.append(candidate);
}

void JavaLocator::runNext()
{
    if (m_index >= m_candidates.size()) {
        if (m_bestMajor > 0) {
            select(m_best);
            finish();
        } else {
            finishWithError(tr("未找到可用的 Java；请安装 JRE 17/21，或手动指定 java 路径。"));
        }
        return;
    }

    const Candidate candidate = m_candidates.at(m_index++);
    const QFileInfo info(candidate.path);
    if (!info.exists() || !info.isFile() || !info.isExecutable()) {
        if (candidate.explicitlyConfigured) {
            finishWithError(tr("指定的 Java 不可用：%1").arg(candidate.path));
            return;
        }
        QTimer::singleShot(0, this, &JavaLocator::runNext);
        return;
    }

    m_runningCandidate = true;
    m_process.setProgram(candidate.path);
    m_process.setArguments({QStringLiteral("-version")});
    m_process.setWorkingDirectory(info.absolutePath());
    m_process.start();
    m_timeout.start(4000);
}

void JavaLocator::processFinished(int exitCode, platform::Process::ExitStatus status)
{
    if (!m_runningCandidate)
        return;
    m_timeout.stop();
    m_runningCandidate = false;

    const QString output = QString::fromLocal8Bit(m_process.readAll());
    const QString version = parseVersion(output);
    const int major = parseMajor(output);

    if (status == platform::Process::NormalExit && exitCode == 0 && major > 0) {
        const Candidate candidate = m_candidates.value(m_index - 1);
        if (candidate.explicitlyConfigured || candidate.preferFirst) {
            m_best = candidate;
            m_bestMajor = major;
            m_version = version;
            m_index = m_candidates.size();
            select(m_best);
            finish();
            return;
        }
        if (major > m_bestMajor) {
            m_best = candidate;
            m_bestMajor = major;
            m_version = version;
        }
        QTimer::singleShot(0, this, &JavaLocator::runNext);
        return;
    }

    if (m_candidates.value(m_index - 1).explicitlyConfigured) {
        finishWithError(tr("无法读取 Java 版本：%1").arg(m_candidates.value(m_index - 1).path));
        return;
    }
    QTimer::singleShot(0, this, &JavaLocator::runNext);
}

void JavaLocator::processFailed(platform::Process::ProcessError error)
{
    if (!m_runningCandidate)
        return;
    Q_UNUSED(error)
    m_timeout.stop();
    m_runningCandidate = false;

    if (m_candidates.value(m_index - 1).explicitlyConfigured) {
        finishWithError(tr("无法启动 Java：%1").arg(m_candidates.value(m_index - 1).path));
        return;
    }
    QTimer::singleShot(0, this, &JavaLocator::runNext);
}

void JavaLocator::processTimedOut()
{
    if (!m_runningCandidate)
        return;
    m_runningCandidate = false;
    m_process.kill();
    m_process.waitForFinished(200);

    if (m_candidates.value(m_index - 1).explicitlyConfigured) {
        finishWithError(tr("Java 版本检测超时：%1").arg(m_candidates.value(m_index - 1).path));
        return;
    }
    QTimer::singleShot(0, this, &JavaLocator::runNext);
}

void JavaLocator::select(const Candidate &candidate)
{
    m_path = candidate.path;
    m_source = candidate.source;
    m_major = m_bestMajor;
    if (m_version.isEmpty()) {
        // A valid candidate always has a parsed version; keep the property
        // coherent even if a future probe path skips the parser.
        m_version = QString::number(m_major);
    }
    m_error.clear();
}

void JavaLocator::finishWithError(const QString &error)
{
    m_path.clear();
    m_version.clear();
    m_major = 0;
    m_source.clear();
    m_error = error;
    m_checking = false;
    m_checked = true;
    m_runningCandidate = false;
    emit changed();
}

void JavaLocator::finish()
{
    m_checking = false;
    m_checked = true;
    m_runningCandidate = false;
    emit changed();
}
