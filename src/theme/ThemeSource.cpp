#include "theme/ThemeSource.h"

#include <QGuiApplication>
#include <QSettings>
#include <QStyleHints>
#include <QStringList>

#include "platform/PlatformInfo.h"

namespace theme {
namespace {

QColor parseKdeTriplet(const QString &raw)
{
    const QStringList parts = raw.split(QLatin1Char(','), Qt::SkipEmptyParts);
    if (parts.size() < 3)
        return QColor();
    bool ok = true;
    const int r = parts.at(0).trimmed().toInt(&ok);
    if (!ok)
        return QColor();
    const int g = parts.at(1).trimmed().toInt(&ok);
    if (!ok)
        return QColor();
    const int b = parts.at(2).trimmed().toInt(&ok);
    if (!ok)
        return QColor();
    return QColor(r, g, b);
}

QColor windowsAccent()
{
    QSettings dwm(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\DWM"),
                  QSettings::NativeFormat);
    const uint colorization = dwm.value(QStringLiteral("ColorizationColor"), 0u).toUInt();
    if (colorization == 0)
        return QColor();
    // 0xAARRGGBB, alpha is opaque+flags in the high byte; keep RGB only.
    return QColor(int((colorization >> 16) & 0xFF), int((colorization >> 8) & 0xFF), int(colorization & 0xFF));
}

QColor kdeAccent()
{
    const QSettings kde(QStringLiteral("kdeglobals"), QSettings::IniFormat);
    const QColor direct = parseKdeTriplet(kde.value(QStringLiteral("General/AccentColor")).toString());
    if (direct.isValid())
        return direct;
    // Plasma 5 stored the accent in General/BackgroundNormal of the colour
    // scheme, which is not useful; fall through to no accent.
    return QColor();
}

bool systemIsDark()
{
    if (QGuiApplication *app = qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        const Qt::ColorScheme scheme = app->styleHints()->colorScheme();
        if (scheme == Qt::ColorScheme::Dark)
            return true;
        if (scheme == Qt::ColorScheme::Light)
            return false;
    }
    return true; // BMCD shipped dark-first before tokens existed.
}

} // namespace

SystemTheme query()
{
    SystemTheme result;
    result.dark = systemIsDark();

#if defined(Q_OS_WIN)
    result.accent = windowsAccent();
    QSettings personalize(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"),
                          QSettings::NativeFormat);
    const int appsLight = personalize.value(QStringLiteral("AppsUseLightTheme"), 0).toInt();
    result.dark = appsLight == 0;
#else
    result.accent = kdeAccent();
#endif

    if (result.accent.isValid())
        result.hasAccent = true;

    const QString accentEnv = qEnvironmentVariable("MC_THEME_ACCENT").trimmed();
    if (!accentEnv.isEmpty()) {
        const QColor fromEnv(accentEnv);
        if (fromEnv.isValid()) {
            result.accent = fromEnv;
            result.hasAccent = true;
        }
    }
    const QString darkEnv = qEnvironmentVariable("MC_THEME_DARK").trimmed();
    if (!darkEnv.isEmpty())
        result.dark = darkEnv != QLatin1String("0");

    result.reduceMotion = platform::reduceMotion();
    return result;
}

} // namespace theme
