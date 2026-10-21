#include "theme/Palette.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>

#include "platform/PlatformInfo.h"
#include "theme/ColorMath.h"
#include "theme/ThemeSource.h"
#include "theme/TokenBuilder.h"

namespace {

// matugen palettes have no elevation steps, so derive them by blending
// towards the foreground colour.
QColor mixColors(const QColor &base, const QColor &towards, qreal ratio)
{
    return QColor::fromRgbF(base.redF() * (1.0 - ratio) + towards.redF() * ratio,
                            base.greenF() * (1.0 - ratio) + towards.greenF() * ratio,
                            base.blueF() * (1.0 - ratio) + towards.blueF() * ratio);
}

// KDE colour schemes store colours either as "R,G,B[,A]" tuples or as "#rrggbb".
QColor parseKdeColor(const QString &raw, const QColor &fallback)
{
    const QString value = raw.trimmed();
    if (value.isEmpty())
        return fallback;

    if (value.startsWith(QLatin1Char('#'))) {
        const QColor color(value);
        return color.isValid() ? color : fallback;
    }

    const QStringList parts = value.split(QLatin1Char(','), Qt::SkipEmptyParts);
    if (parts.size() != 3 && parts.size() != 4)
        return fallback;

    bool ok = false;
    const int r = parts.at(0).trimmed().toInt(&ok);
    if (!ok)
        return fallback;
    const int g = parts.at(1).trimmed().toInt(&ok);
    if (!ok)
        return fallback;
    const int b = parts.at(2).trimmed().toInt(&ok);
    if (!ok)
        return fallback;

    int a = 255;
    if (parts.size() == 4) {
        a = parts.at(3).trimmed().toInt(&ok);
        if (!ok)
            a = 255;
    }

    return QColor(r, g, b, a);
}

QString colorSchemesDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
           + QStringLiteral("/color-schemes");
}

// Resolution order:
//   1. MC_DOWNLOADER_COLORS (explicit override, handy for testing)
//   2. ~/.local/share/color-schemes/DankMatugen.colors  (DMS default name)
//   3. any *matugen*.colors in that directory, preferring the current variant
QString resolveSchemePath()
{
    const QString override = qEnvironmentVariable("MC_DOWNLOADER_COLORS");
    if (!override.isEmpty() && QFileInfo::exists(override))
        return override;

    const QDir dir(colorSchemesDir());
    const QString canonical = dir.filePath(QStringLiteral("DankMatugen.colors"));
    if (QFileInfo::exists(canonical))
        return canonical;

    const QStringList candidates =
            dir.entryList({QStringLiteral("*matugen*.colors"), QStringLiteral("*Matugen*.colors")},
                          QDir::Files, QDir::Time);
    for (const QString &name : candidates) {
        if (name.contains(QLatin1String("Light"), Qt::CaseInsensitive))
            continue;
        return dir.filePath(name);
    }
    return candidates.isEmpty() ? QString() : dir.filePath(candidates.first());
}

} // namespace

Palette::Palette(QObject *parent)
    : QObject(parent)
{
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, &Palette::reload);
    // matugen commonly replaces the file by rename, which shows up as a
    // directory change rather than a file change.
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &Palette::reload);
    reload();
}

void Palette::reload()
{
    applyFallback();
    m_tokenMode = QStringLiteral("fallback");
    m_live = false;
    m_sourcePath.clear();

    // Resolution order: explicit matugen/KDE scheme file (Palette A+), then the
    // accent-driven token ladder (scheme B), then the built-in fallback.
    const QString path = resolveSchemePath();
    if (!path.isEmpty() && loadFromFile(path)) {
        m_live = true;
        m_sourcePath = path;
        m_tokenMode = QStringLiteral("scheme-file");
    } else {
        const theme::SystemTheme systemTheme = theme::query();
        m_reduceMotion = systemTheme.reduceMotion;
        if (systemTheme.hasAccent) {
            theme::Tokens tokens = theme::buildTokens(systemTheme.accent, systemTheme.dark);
            if (theme::enforceContrast(tokens)) {
                applyTokens(tokens);
                m_tokenMode = QStringLiteral("accent:%1:%2")
                                      .arg(tokens.sourceName, tokens.dark ? QStringLiteral("dark")
                                                                          : QStringLiteral("light"));
            } else {
                tokens = theme::fallbackTokens(systemTheme.dark);
                theme::enforceContrast(tokens);
                applyTokens(tokens);
                m_tokenMode = QStringLiteral("fallback-contrast");
            }
        } else {
            m_reduceMotion = systemTheme.reduceMotion;
        }
    }

    rewatch();
    emit changed();
}

QString Palette::tokensJson()
{
    return theme::toJson(m_tokens);
}

void Palette::applyTokens(const theme::Tokens &tokens)
{
    m_tokens = tokens;
    m_schemeName = tokens.sourceName;
    m_background = tokens.surface;
    m_onBackground = tokens.onSurface;
    m_surface = tokens.surfaceContainer;
    m_surfaceAlt = tokens.surfaceContainerHigh;
    m_onSurface = tokens.onSurface;
    m_onSurfaceMuted = tokens.onSurfaceVariant;
    m_primary = tokens.primary;
    m_onPrimary = tokens.onPrimary;
    m_outline = tokens.outline;
    m_error = tokens.error;
    m_warning = tokens.warning;
    m_success = tokens.success;
    m_link = tokens.link;
    m_surfaceContainer = tokens.surfaceContainer;
    m_surfaceContainerHigh = tokens.surfaceContainerHigh;
    m_primaryContainer = tokens.primaryContainer;
    m_onPrimaryContainer = tokens.onPrimaryContainer;
    m_onSurfaceVariant = tokens.onSurfaceVariant;
}

void Palette::rewatch()
{
    if (!m_watcher.files().isEmpty())
        m_watcher.removePaths(m_watcher.files());
    if (!m_watcher.directories().isEmpty())
        m_watcher.removePaths(m_watcher.directories());

    const QString dir = colorSchemesDir();
    if (QFileInfo::exists(dir))
        m_watcher.addPath(dir);
    if (!m_sourcePath.isEmpty() && QFileInfo::exists(m_sourcePath))
        m_watcher.addPath(m_sourcePath);
}

bool Palette::loadFromFile(const QString &path)
{
    QSettings settings(path, QSettings::IniFormat);
    settings.sync(); // the file is rewritten by matugen, never trust the cache
    if (settings.status() != QSettings::NoError) {
        qWarning() << "Palette: cannot read" << path;
        return false;
    }
    if (!settings.childGroups().contains(QStringLiteral("Colors:Window")))
        return false;

    auto pick = [&settings](const char *group, const char *key, const QColor &fallback) {
        const QVariant raw = settings.value(QString::fromLatin1(group) + QLatin1Char('/')
                                            + QString::fromLatin1(key));
        // QSettings reads "190,200,200" as a QStringList (comma = list separator in
        // INI), so toString() would hand back an empty string. Join first.
        return parseKdeColor(raw.toStringList().join(QLatin1Char(',')), fallback);
    };

    // Start from the fallback values so a partial scheme still yields a usable UI.
    const QColor fbBackground = m_background;
    const QColor fbOnBackground = m_onBackground;
    const QColor fbSurface = m_surface;
    const QColor fbSurfaceAlt = m_surfaceAlt;
    const QColor fbOnSurface = m_onSurface;
    const QColor fbMuted = m_onSurfaceMuted;
    const QColor fbPrimary = m_primary;
    const QColor fbOnPrimary = m_onPrimary;
    const QColor fbOutline = m_outline;
    const QColor fbError = m_error;
    const QColor fbWarning = m_warning;
    const QColor fbSuccess = m_success;
    const QColor fbLink = m_link;

    m_background = pick("Colors:Window", "BackgroundNormal", fbBackground);
    m_onBackground = pick("Colors:Window", "ForegroundNormal", fbOnBackground);
    m_onSurfaceMuted = pick("Colors:Window", "ForegroundInactive", fbMuted);

    // matugen schemes give Window and View the same BackgroundNormal, which would
    // make cards invisible on the window. KDE's BackgroundAlternate is the
    // intended "elevated" step, so prefer it and only fall back to View when the
    // scheme actually differentiates the two.
    m_surface = pick("Colors:View", "BackgroundAlternate", QColor());
    if (!m_surface.isValid())
        m_surface = pick("Colors:Window", "BackgroundAlternate", fbSurface);
    if (!m_surface.isValid() || m_surface == m_background)
        m_surface = pick("Colors:View", "BackgroundNormal", fbSurface);

    const QColor viewForeground = pick("Colors:View", "ForegroundNormal", fbOnSurface);
    m_onSurface = viewForeground;
    m_surfaceAlt = mixColors(m_surface, m_onSurface, 0.08);

    m_primary = pick("Colors:Selection", "BackgroundNormal",
                     pick("Colors:Window", "DecorationFocus", fbPrimary));
    m_onPrimary = pick("Colors:Selection", "ForegroundNormal", fbOnPrimary);
    m_outline = mixColors(m_background, m_onSurface, 0.25);

    m_error = pick("Colors:Button", "ForegroundNegative",
                   pick("Colors:Window", "ForegroundNegative", fbError));
    m_warning = pick("Colors:Button", "ForegroundNeutral",
                     pick("Colors:Window", "ForegroundNeutral", fbWarning));
    m_success = pick("Colors:Button", "ForegroundPositive",
                     pick("Colors:Window", "ForegroundPositive", fbSuccess));
    m_link = pick("Colors:Button", "ForegroundLink",
                  pick("Colors:Window", "ForegroundLink", fbLink));

    const QString name = settings.value(QStringLiteral("General/ColorScheme")).toString();
    m_schemeName = name.isEmpty() ? QStringLiteral("matugen") : name;
    return true;
}

void Palette::applyFallback()
{
    // Seed values are lifted from the DMS/niri palette observed on this machine,
    // so even the "no scheme found" case does not look foreign.
    m_schemeName = QStringLiteral("builtin-dark");
    m_background = QColor(QStringLiteral("#101418"));
    m_onBackground = QColor(QStringLiteral("#e1e3e3"));
    m_surface = QColor(QStringLiteral("#181c20"));
    m_surfaceAlt = QColor(QStringLiteral("#1f2428"));
    m_onSurface = QColor(QStringLiteral("#e1e3e3"));
    m_onSurfaceMuted = QColor(QStringLiteral("#899393"));
    m_primary = QColor(QStringLiteral("#80d4d7"));
    m_onPrimary = QColor(QStringLiteral("#003739"));
    m_outline = QColor(QStringLiteral("#3d4a4a"));
    m_error = QColor(QStringLiteral("#ffb4ab"));
    m_warning = QColor(QStringLiteral("#f0c27a"));
    m_success = QColor(QStringLiteral("#9bd794"));
    m_link = QColor(QStringLiteral("#80d4d7"));

    // The legacy literal palette doubles as scheme A (the two-level fallback).
    m_surfaceContainer = m_surface;
    m_surfaceContainerHigh = m_surfaceAlt;
    m_primaryContainer = QColor(QStringLiteral("#1c3d40"));
    m_onPrimaryContainer = QColor(QStringLiteral("#c9f2f4"));
    m_onSurfaceVariant = m_onSurfaceMuted;

    m_tokens = theme::fallbackTokens(true);
    m_tokens.surface = m_background;
    m_tokens.surfaceContainer = m_surface;
    m_tokens.surfaceContainerHigh = m_surfaceAlt;
    m_tokens.onSurface = m_onSurface;
    m_tokens.onSurfaceVariant = m_onSurfaceMuted;
    m_tokens.primary = m_primary;
    m_tokens.onPrimary = m_onPrimary;
    m_tokens.outline = m_outline;
    m_tokens.error = m_error;
    m_tokens.warning = m_warning;
    m_tokens.success = m_success;
    m_tokens.link = m_link;
    m_tokens.sourceName = m_schemeName;
}
