#include "platform/PlatformInfo.h"

#include <QDebug>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QSettings>
#include <QStringList>

#ifdef Q_OS_WIN
#  include <qt_windows.h>
#endif

namespace platform {
namespace {

bool envFlag(const char *name, bool fallback = false)
{
    const QString value = qEnvironmentVariable(name).trimmed().toLower();
    if (value.isEmpty())
        return fallback;
    return value != QLatin1String("0") && value != QLatin1String("false") && value != QLatin1String("off");
}

} // namespace

QString osName()
{
#if defined(Q_OS_WIN)
    return QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("macos");
#else
    return QStringLiteral("linux");
#endif
}

bool isSandboxed()
{
    static const bool sandboxed = [] {
        if (QFileInfo::exists(QStringLiteral("/.flatpak-info")))
            return true;
        const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        if (!env.value(QStringLiteral("FLATPAK_ID")).isEmpty())
            return true;
        if (!env.value(QStringLiteral("SNAP")).isEmpty())
            return true;
        if (!env.value(QStringLiteral("APPIMAGE")).isEmpty())
            return true;
        return false;
    }();
    return sandboxed;
}

bool reduceMotion()
{
    if (envFlag("MC_REDUCE_MOTION"))
        return true;
#ifdef Q_OS_WIN
    BOOL animations = TRUE;
    if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0))
        return animations == FALSE;
#endif
    const QString configHome = QProcessEnvironment::systemEnvironment().value(
            QStringLiteral("XDG_CONFIG_HOME"),
            QSettings().fileName().section(QLatin1Char('/'), 0, -3) + QStringLiteral("/.config"));
    const QSettings kde(QStringLiteral("kdeglobals"), QSettings::IniFormat);
    if (kde.contains(QStringLiteral("KDE/AnimationDurationFactor"))) {
        const double factor = kde.value(QStringLiteral("KDE/AnimationDurationFactor")).toDouble();
        if (factor <= 0.0)
            return true;
    }
    const QString gtkPath = QStringLiteral("%1/gtk-3.0/settings.ini").arg(configHome);
    if (QFileInfo::exists(gtkPath)) {
        const QSettings gtk(gtkPath, QSettings::IniFormat);
        if (gtk.value(QStringLiteral("Settings/gtk-enable-animations"), true).toBool() == false)
            return true;
    }
    return false;
}

void attachConsole()
{
#ifdef Q_OS_WIN
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE *stream = nullptr;
        freopen_s(&stream, "CONOUT$", "w", stdout);
        freopen_s(&stream, "CONOUT$", "w", stderr);
        freopen_s(&stream, "CONIN$", "r", stdin);
    }
#endif
}

void setUtf8Console()
{
#ifdef Q_OS_WIN
    SetConsoleOutputCP(CP_UTF8);
#endif
}

} // namespace platform
