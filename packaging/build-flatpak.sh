#!/usr/bin/env bash
# Build and install BMCD as a Flatpak from packaging/flatpak/org.bmcd.BMCD.yml.
#
#   packaging/build-flatpak.sh            # build + install for the current user
#   packaging/build-flatpak.sh --no-install
#
# Requires flatpak-builder plus the KDE runtime matching the manifest
# (org.kde.Sdk//6.9 and org.kde.Platform//6.9).
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
MANIFEST="$ROOT/packaging/flatpak/org.bmcd.BMCD.yml"
BUILD_DIR="$ROOT/build/flatpak"
REPO_DIR="$ROOT/build/flatpak-repo"
APP_ID=org.bmcd.BMCD
INSTALL=1

[[ ${1:-} == --no-install ]] && INSTALL=0

command -v flatpak-builder >/dev/null || {
    echo "flatpak-builder not found: install it first (Arch: pacman -S flatpak-builder)." >&2
    exit 1
}
flatpak info org.kde.Sdk//6.9 >/dev/null 2>&1 || {
    echo "missing runtime: flatpak install flathub org.kde.Sdk//6.9 org.kde.Platform//6.9" >&2
    exit 1
}

args=(--force-clean --repo="$REPO_DIR" --state-dir="$BUILD_DIR/state")
[[ $INSTALL == 1 ]] && args+=(--user --install)

flatpak-builder "${args[@]}" "$BUILD_DIR/build" "$MANIFEST"

echo
if [[ $INSTALL == 1 ]]; then
    echo "installed: flatpak run $APP_ID --plan 26.3"
else
    echo "built (not installed): $REPO_DIR"
fi
