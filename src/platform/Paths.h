#pragma once

#include <QString>

namespace platform {

// Directory the game expects by default on this OS. Windows uses %APPDATA%,
// macOS uses ~/Library/Application Support, everything else ~/.minecraft.
QString defaultGameDir();

// Per-user cache directory (version manifest, pack index, ...).
QString cacheDir();

// Per-user config directory.
QString configDir();

} // namespace platform
