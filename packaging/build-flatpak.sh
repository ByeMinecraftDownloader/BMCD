#!/usr/bin/env bash
# Build BMCD as a Flatpak from packaging/flatpak/org.bmcd.BMCD.yml.
#
#   packaging/build-flatpak.sh                 # build + install + bundle + smoke
#   packaging/build-flatpak.sh --no-install    # build + bundle, do not install/run
#   packaging/build-flatpak.sh --working-tree  # dev only: build uncommitted tree
#
# Release builds use the manifest's `type: git` source at a fixed commit. The
# local checkout's HEAD is substituted into a generated manifest so a clean
# checkout builds exactly its own commit; --working-tree is only for testing
# changes that have not been committed and is rejected by release CI.
#
# Requires flatpak-builder plus the KDE runtime matching the manifest
# (org.kde.Sdk//6.9 and org.kde.Platform//6.9).
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
MANIFEST="$ROOT/packaging/flatpak/org.bmcd.BMCD.yml"
BUILD_DIR="$ROOT/build/flatpak"
REPO_DIR="$ROOT/build/flatpak-repo"
BUNDLE="$ROOT/build/BMCD-x86_64.flatpak"
APP_ID=org.bmcd.BMCD
INSTALL=1
WORKING_TREE=0

for arg in "$@"; do
    case "$arg" in
        --no-install) INSTALL=0 ;;
        --working-tree) WORKING_TREE=1 ;;
        *) echo "unknown argument: $arg" >&2; exit 2 ;;
    esac
done

command -v flatpak-builder >/dev/null || {
    echo "flatpak-builder not found: install it first (Arch: pacman -S flatpak-builder)." >&2
    exit 1
}
flatpak info org.kde.Sdk//6.9 >/dev/null 2>&1 || {
    echo "missing runtime: flatpak install flathub org.kde.Sdk//6.9 org.kde.Platform//6.9" >&2
    exit 1
}

HEAD=$(git -C "$ROOT" rev-parse HEAD)
mkdir -p "$BUILD_DIR"
if [[ $WORKING_TREE == 1 ]]; then
    if [[ ${BMCD_RELEASE:-} == 1 ]]; then
        echo "refusing --working-tree for a release build; use the fixed git commit" >&2
        exit 2
    fi
    if [[ -n $(git -C "$ROOT" status --porcelain) ]]; then
        echo "warning: --working-tree includes uncommitted files; never publish this bundle" >&2
    fi
    GENERATED="$BUILD_DIR/org.bmcd.BMCD.working-tree.yml"
    # Replace the git source with the checkout root, keeping the rest of the
    # manifest byte-for-byte. This branch is for local tests only.
    awk -v root="$ROOT" '
        /- type: git/        { print "      - type: dir"; in_git=1; next }
        in_git && /url:/    { print "        path: " root; next }
        in_git && /commit:/ { in_git=0; next }
        { print }
    ' "$MANIFEST" > "$GENERATED"
else
    if [[ -n $(git -C "$ROOT" status --porcelain) ]]; then
        echo "warning: working tree is dirty; the Flatpak will contain HEAD $HEAD only" >&2
        echo "         use --working-tree to test the working tree locally" >&2
    fi
    GENERATED="$BUILD_DIR/org.bmcd.BMCD.generated.yml"
    sed -e "s|^        commit: .*|        commit: $HEAD|" "$MANIFEST" > "$GENERATED"
fi

rm -rf "$REPO_DIR" "$BUILD_DIR/build"
flatpak-builder --force-clean --repo="$REPO_DIR" --state-dir="$BUILD_DIR/state" \
    "$BUILD_DIR/build" "$GENERATED"

flatpak build-bundle --arch=x86_64 "$REPO_DIR" "$BUNDLE" "$APP_ID"
(cd "$(dirname "$BUNDLE")" && sha256sum "$(basename "$BUNDLE")" > SHA256SUMS)
echo "built: $BUNDLE"
cat "$(dirname "$BUNDLE")/SHA256SUMS"

if [[ $INSTALL == 1 ]]; then
    flatpak install --user --noninteractive --reinstall "$REPO_DIR" "$APP_ID"
    "$ROOT/packaging/smoke-artifact.sh" flatpak
else
    echo "not installed; install $BUNDLE, then run packaging/smoke-artifact.sh flatpak" >&2
fi
