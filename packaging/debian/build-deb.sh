#!/usr/bin/env bash
# Build a self-contained Debian package from the AppDir produced by
# packaging/build-appimage.sh.
#
#   packaging/debian/build-deb.sh --version 0.1.0 --appdir build/AppDir
#
# The package installs the payload under /opt/bmcd and exposes /usr/bin/bmcd as
# a symlink to its AppRun launcher.  Bundling Qt avoids depending on a distro
# Qt version that may be older than the project's Qt 6.5 requirement.
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
VERSION=
APPDIR="$ROOT/build/AppDir"
OUT_DIR="$ROOT/build/debian"

usage() {
    echo "usage: $0 --version X.Y.Z [--appdir DIR] [--out-dir DIR]" >&2
    exit 2
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --version) VERSION=${2:-}; shift 2 ;;
        --appdir) APPDIR=${2:-}; shift 2 ;;
        --out-dir) OUT_DIR=${2:-}; shift 2 ;;
        -h|--help) usage ;;
        *) echo "unknown argument: $1" >&2; usage ;;
    esac
done

VERSION=${VERSION#v}
[[ $VERSION =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || {
    echo "invalid version: '$VERSION' (expected X.Y.Z)" >&2
    exit 2
}
APPDIR=$(realpath "$APPDIR")
OUT_DIR=$(realpath -m "$OUT_DIR")
[[ -x "$APPDIR/AppRun" && -x "$APPDIR/usr/bin/bmcd" ]] || {
    echo "not an AppDir (missing AppRun or usr/bin/bmcd): $APPDIR" >&2
    exit 1
}
command -v md5sum >/dev/null || {
    echo "md5sum not found" >&2
    exit 1
}

STAGE="$ROOT/build/deb-stage"
DEB_NAME="bmcd_${VERSION}_amd64.deb"
DEB="$OUT_DIR/$DEB_NAME"
rm -rf "$STAGE" "$OUT_DIR"
mkdir -p "$STAGE/DEBIAN" "$STAGE/opt" "$STAGE/usr/bin" \
         "$STAGE/usr/share/applications" \
         "$STAGE/usr/share/icons/hicolor/512x512/apps" \
         "$STAGE/usr/share/metainfo" \
         "$STAGE/usr/share/doc/bmcd"

cp -a "$APPDIR" "$STAGE/opt/bmcd"
ln -s /opt/bmcd/AppRun "$STAGE/usr/bin/bmcd"
install -m 644 "$APPDIR/usr/share/applications/org.bmcd.BMCD.desktop" \
    "$STAGE/usr/share/applications/org.bmcd.BMCD.desktop"
install -m 644 "$APPDIR/usr/share/icons/hicolor/512x512/apps/org.bmcd.BMCD.png" \
    "$STAGE/usr/share/icons/hicolor/512x512/apps/org.bmcd.BMCD.png"
install -m 644 "$ROOT/packaging/flatpak/org.bmcd.BMCD.metainfo.xml" \
    "$STAGE/usr/share/metainfo/org.bmcd.BMCD.metainfo.xml"
install -m 644 "$ROOT/LICENSE" "$STAGE/usr/share/doc/bmcd/copyright"

# dpkg's md5sums are relative to the package root and omit symlinks.
(
    cd "$STAGE"
    find . -path ./DEBIAN -prune -o -type f -print0 |
        sed -z 's|^\./||' |
        xargs -0 -r md5sum > DEBIAN/md5sums
)

INSTALLED_SIZE=$(du -sk "$STAGE/opt" "$STAGE/usr" | awk '{sum += $1} END {print sum}')
cat > "$STAGE/DEBIAN/control" <<EOF
Package: bmcd
Version: $VERSION
Architecture: amd64
Maintainer: BMCD maintainers <maintainers@bmcd.invalid>
Installed-Size: $INSTALLED_SIZE
Depends: libc6 (>= 2.35), libstdc++6, libgl1, libegl1, libfontconfig1, libfreetype6, libx11-6, libxext6, libxkbcommon0, libxkbcommon-x11-0, libxcb1, libxcb-cursor0, libxcb-icccm4, libxcb-keysyms1, libxcb-shape0, libxcb-xinerama0, libxcb-xkb1, fonts-dejavu-core
Section: utils
Priority: optional
Homepage: https://github.com/ByeMinecraftDownloader/BMCD
Description: Minecraft resource downloader
 BMCD (Bye!MinecraftDownloader) fetches Minecraft game files, resolves
 loader and pack requirements, and verifies downloads with SHA-1.
 .
 The package is self-contained under /opt/bmcd so it can run without
 matching the host's Qt 6 version.
EOF

mkdir -p "$OUT_DIR"
if command -v dpkg-deb >/dev/null; then
    dpkg-deb --build --root-owner-group "$STAGE" "$DEB"
else
    # dpkg-deb is not present on Arch hosts.  Emit the same ar-based layout
    # manually so the package can still be inspected locally.
    WORK="$ROOT/build/deb-ar"
    rm -rf "$WORK"
    mkdir -p "$WORK"
    printf '2.0\n' > "$WORK/debian-binary"
    tar -C "$STAGE/DEBIAN" --owner=0 --group=0 --numeric-owner \
        -czf "$WORK/control.tar.gz" .
    tar -C "$STAGE" --owner=0 --group=0 --numeric-owner \
        --exclude=DEBIAN -czf "$WORK/data.tar.gz" .
    (
        cd "$WORK"
        ar r "$DEB" debian-binary control.tar.gz data.tar.gz >/dev/null
    )
fi

(
    cd "$OUT_DIR"
    sha256sum "$DEB_NAME" > SHA256SUMS
)
echo "built Debian package:"
ls -lh "$DEB"
cat "$OUT_DIR/SHA256SUMS"
