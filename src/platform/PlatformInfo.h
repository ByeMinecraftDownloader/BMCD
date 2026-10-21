#pragma once

#include <QString>

namespace platform {

// "windows", "macos" or "linux" (the only three we build for).
QString osName();

// True inside Flatpak / Snap / AppImage extract-and-run style sandboxes where
// the process may not see the host filesystem layout.
bool isSandboxed();

// True when the desktop asks for reduced motion (KDE, GTK, Windows).
bool reduceMotion();

// Win32: attach to the parent console and point stdio at it, so a windowed
// (WIN32_EXECUTABLE) build can still print in `bmcd --plan ...` mode. No-op
// elsewhere, but kept in the shared layer so main() has no #ifdef soup.
void attachConsole();

// Set the terminal to UTF-8 where the platform has such a knob; no-op if not.
void setUtf8Console();

} // namespace platform
