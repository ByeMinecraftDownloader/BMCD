#pragma once

#include <QColor>

namespace theme {

struct SystemTheme
{
    bool hasAccent = false;
    QColor accent;
    bool dark = true;
    bool reduceMotion = false;
};

// Query the desktop for an accent colour and light/dark preference.
//   * Windows: HKCU DWM ColorizationColor + Personalize SystemUsesLightTheme
//   * KDE/Linux: [General] AccentColor in kdeglobals + QStyleHints::colorScheme()
//   * Env overrides used by tests and the golden screenshot suite:
//     MC_THEME_ACCENT=#rrggbb, MC_THEME_DARK=0|1
SystemTheme query();

} // namespace theme
