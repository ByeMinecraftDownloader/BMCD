#!/usr/bin/env bash
# Headless smoke test for a packaged BMCD artifact.
#
#   packaging/smoke-artifact.sh appimage [path/to/BMCD-x86_64.AppImage]
#   packaging/smoke-artifact.sh flatpak
#   packaging/smoke-artifact.sh binary [path/to/bmcd]
#
# Every mode runs the same set of acceptance commands:
# --version, --i18n-check, and one offscreen UI start. No display or network
# is required for these checks.
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
MODE=${1:-}
[[ -n $MODE ]] || { echo "usage: $0 appimage|flatpak|binary [path]" >&2; exit 2; }
shift || true

case "$MODE" in
    appimage)
        ART=${1:-"$ROOT/dist/BMCD-x86_64.AppImage"}
        [[ -x $ART ]] || { echo "not executable: $ART" >&2; exit 1; }
        run() { APPIMAGE_EXTRACT_AND_RUN=1 "$ART" "$@"; }
        run_offscreen() {
            APPIMAGE_EXTRACT_AND_RUN=1 QT_QPA_PLATFORM=offscreen \
                QT_QUICK_BACKEND=software timeout 30 "$ART" "$@"
        }
        ;;
    flatpak)
        run() { flatpak run org.bmcd.BMCD "$@"; }
        run_offscreen() {
            timeout 30 flatpak run --env=QT_QPA_PLATFORM=offscreen \
                --env=QT_QUICK_BACKEND=software org.bmcd.BMCD "$@"
        }
        ;;
    binary)
        BIN=${1:-"$ROOT/build/bmcd"}
        [[ -x $BIN ]] || { echo "not executable: $BIN" >&2; exit 1; }
        run() { "$BIN" "$@"; }
        run_offscreen() {
            QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
                timeout 30 "$BIN" "$@"
        }
        ;;
    *)
        echo "unknown mode: $MODE (expected appimage, flatpak or binary)" >&2
        exit 2
        ;;
esac

version=$(run --version)
[[ -n $version ]] || { echo "empty --version output" >&2; exit 1; }
echo "version: $version"

run --i18n-check
echo "i18n-check: ok"

# --dump-layout exits after rendering once; if it hangs, the timeout is a
# failure rather than a silently passing smoke test.
run_offscreen --view settings --dump-layout >/dev/null
echo "offscreen UI: ok"
