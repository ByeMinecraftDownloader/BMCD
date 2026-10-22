#include "AppSettings.h"

#include "platform/Paths.h"
#include "platform/PlatformInfo.h"

#include <QDir>
#include <QLocale>
#include <QSettings>
#include <QUrl>

namespace {
QSettings settings()
{
    return QSettings(QStringLiteral("BMCD"), QStringLiteral("bmcd"));
}

QString normaliseLanguage(const QString &language)
{
    return language.startsWith(QLatin1String("en"), Qt::CaseInsensitive)
            ? QStringLiteral("en")
            : QStringLiteral("zh_CN");
}

QString normaliseBase(const QString &base)
{
    QString text = base.trimmed();
    if (text.isEmpty())
        return {};

    if (!text.contains(QLatin1String("://")))
        text.prepend(QLatin1String("https://"));

    QUrl url(text);
    if (!url.isValid() || url.host().isEmpty()
        || (url.scheme() != QLatin1String("http") && url.scheme() != QLatin1String("https")))
        return {};

    QString path = url.path();
    while (path.endsWith(QLatin1Char('/')))
        path.chop(1);
    url.setPath(path);
    url.setQuery({});
    url.setFragment({});
    return url.toString();
}

QString envOrStored(const char *envName, const char *key)
{
    if (qEnvironmentVariableIsSet(envName))
        return normaliseBase(qEnvironmentVariable(envName));
    return normaliseBase(settings().value(QLatin1String(key)).toString());
}

QString normaliseDir(const QString &dir)
{
    const QString text = dir.trimmed();
    if (text.isEmpty() || QDir::isRelativePath(text))
        return {};
    return QDir::cleanPath(text);
}

QString normaliseJavaPath(const QString &path)
{
    const QString text = path.trimmed();
    if (text.isEmpty() || QDir::isRelativePath(text))
        return {};
    return QDir::cleanPath(text);
}

QString envOrStoredDir(const char *envName, const char *key)
{
    if (qEnvironmentVariableIsSet(envName))
        return normaliseDir(qEnvironmentVariable(envName));
    return normaliseDir(settings().value(QLatin1String(key)).toString());
}

QString downloadRootFor(const QString &base)
{
    return base.isEmpty() ? platform::downloadsDir()
                          : QDir(base).filePath(QStringLiteral("BMCD/downloads"));
}

bool envBool(const char *envName, bool fallback)
{
    if (!qEnvironmentVariableIsSet(envName))
        return fallback;
    const QString value = qEnvironmentVariable(envName).trimmed().toLower();
    return value != QLatin1String("0") && value != QLatin1String("false")
            && value != QLatin1String("off") && value != QLatin1String("no");
}
} // namespace

AppSettings::AppSettings(QObject *parent)
    : QObject(parent)
{
}

QString AppSettings::localPath(const QUrl &url) const
{
    return url.toLocalFile();
}

QString AppSettings::storedTargetDir()
{
    return settings().value(QStringLiteral("targetDir")).toString();
}

QString AppSettings::resolvedTargetDir()
{
    const QString env = qEnvironmentVariable("MC_TARGET");
    if (!env.isEmpty())
        return env;
    const QString stored = storedTargetDir();
    return stored.isEmpty() ? platform::defaultGameDir() : stored;
}

QString AppSettings::targetDir() const
{
    return resolvedTargetDir();
}

void AppSettings::setTargetDir(const QString &dir)
{
    const QString clean = QDir::cleanPath(dir);
    if (clean.isEmpty() || clean == storedTargetDir())
        return;
    QSettings store = settings();
    store.setValue(QStringLiteral("targetDir"), clean);
    emit targetDirChanged();
}

QString AppSettings::configuredDownloadTempDir()
{
    return envOrStoredDir("MC_DOWNLOAD_TEMP_DIR", "downloadTempDir");
}

QString AppSettings::downloadTempDir() const
{
    return configuredDownloadTempDir();
}

QString AppSettings::downloadRoot() const
{
    return resolvedDownloadRoot();
}

QString AppSettings::resolvedDownloadRoot()
{
    return downloadRootFor(configuredDownloadTempDir());
}

void AppSettings::setDownloadTempDir(const QString &dir)
{
    const QString clean = normaliseDir(dir);
    if (clean == settings().value(QStringLiteral("downloadTempDir")).toString())
        return;
    QSettings store = settings();
    store.setValue(QStringLiteral("downloadTempDir"), clean);
    emit downloadTempDirChanged();
}

QString AppSettings::language() const
{
    const QString stored = settings().value(QStringLiteral("language")).toString();
    if (!stored.isEmpty())
        return normaliseLanguage(stored);
    return QLocale::system().name().startsWith(QLatin1String("en"), Qt::CaseInsensitive)
            ? QStringLiteral("en")
            : QStringLiteral("zh_CN");
}

void AppSettings::setLanguage(const QString &language)
{
    const QString clean = normaliseLanguage(language);
    if (clean == this->language())
        return;
    QSettings store = settings();
    store.setValue(QStringLiteral("language"), clean);
    emit languageChanged();
}

int AppSettings::concurrency()
{
    return qBound(1, settings().value(QStringLiteral("downloadConcurrency"), 6).toInt(), 16);
}

int AppSettings::downloadConcurrency() const
{
    return concurrency();
}

void AppSettings::setDownloadConcurrency(int concurrency)
{
    const int clean = qBound(1, concurrency, 16);
    if (clean == downloadConcurrency())
        return;
    QSettings store = settings();
    store.setValue(QStringLiteral("downloadConcurrency"), clean);
    emit downloadConcurrencyChanged();
}

bool AppSettings::reducedMotion()
{
    return settings().value(QStringLiteral("reduceMotion"), platform::reduceMotion()).toBool();
}

bool AppSettings::reduceMotion() const
{
    return reducedMotion();
}

void AppSettings::setReduceMotion(bool reduced)
{
    if (reduced == reduceMotion())
        return;
    QSettings store = settings();
    store.setValue(QStringLiteral("reduceMotion"), reduced);
    emit reduceMotionChanged();
}

QString AppSettings::configuredCdnBase()
{
    return envOrStored("MC_CDN_BASE", "cdnBase");
}

QString AppSettings::cdnBase() const
{
    return configuredCdnBase();
}

void AppSettings::setCdnBase(const QString &base)
{
    const QString clean = normaliseBase(base);
    if (clean == settings().value(QStringLiteral("cdnBase")).toString())
        return;
    QSettings store = settings();
    store.setValue(QStringLiteral("cdnBase"), clean);
    emit cdnBaseChanged();
}

QString AppSettings::configuredMirrorBase()
{
    return envOrStored("MC_MIRROR_BASE", "mirrorBase");
}

QString AppSettings::mirrorBase() const
{
    return configuredMirrorBase();
}

void AppSettings::setMirrorBase(const QString &base)
{
    const QString clean = normaliseBase(base);
    if (clean == settings().value(QStringLiteral("mirrorBase")).toString())
        return;
    QSettings store = settings();
    store.setValue(QStringLiteral("mirrorBase"), clean);
    emit mirrorBaseChanged();
}

bool AppSettings::failoverEnabled()
{
    if (qEnvironmentVariableIsSet("MC_MIRROR_FALLBACK"))
        return envBool("MC_MIRROR_FALLBACK", true);
    return settings().value(QStringLiteral("mirrorFailover"), true).toBool();
}

bool AppSettings::mirrorFailover() const
{
    return failoverEnabled();
}

void AppSettings::setMirrorFailover(bool enabled)
{
    if (enabled == failoverEnabled())
        return;
    QSettings store = settings();
    store.setValue(QStringLiteral("mirrorFailover"), enabled);
    emit mirrorFailoverChanged();
}

QString AppSettings::configuredJavaPath()
{
    return normaliseJavaPath(settings().value(QStringLiteral("javaPath")).toString());
}

QString AppSettings::javaPath() const
{
    return configuredJavaPath();
}

void AppSettings::setJavaPath(const QString &path)
{
    const QString clean = normaliseJavaPath(path);
    if (clean == settings().value(QStringLiteral("javaPath")).toString())
        return;
    QSettings store = settings();
    store.setValue(QStringLiteral("javaPath"), clean);
    emit javaPathChanged();
}
