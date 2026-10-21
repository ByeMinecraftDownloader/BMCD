#pragma once

#include <QString>

namespace platform {

// Install the system proxy configuration (Windows registry / KDE / GNOME) into
// Qt's network stack, unless MC_NO_SYSTEM_PROXY is set. Corporate proxies are
// invisible to Qt by default, which reads as "download failed" to users.
void installSystemProxy();

// User agent for every outbound request, e.g. "BMCD/1.0.0".
QString userAgent();

} // namespace platform
