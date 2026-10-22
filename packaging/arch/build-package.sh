#!/usr/bin/env bash
# Build an Arch Linux package (.pkg.tar.zst) from this checkout.
#
#   packaging/arch/build-package.sh --version 1.0.0
#
# The release workflow runs this inside an Arch container.  makepkg refuses to
# run as root, so the script creates a throwaway `builder` user when invoked by
# root and re-runs itself as that user.  Everything outside `build/` is only
# read, which keeps the checkout usable after packaging.
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
VERSION=
OUT_DIR="$ROOT/build/arch"
WORK="$ROOT/build/archpkg"

usage() {
    echo "usage: $0 --version X.Y.Z [--out-dir DIR]" >&2
    exit 2
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --version) VERSION=${2:-}; shift 2 ;;
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
command -v makepkg >/dev/null || {
    echo "makepkg not found; run this on Arch Linux or in an archlinux container" >&2
    exit 1
}

# Re-exec as an unprivileged user when the caller is root.  Do it before
# touching the workspace so the generated tree has usable ownership.
if [[ $EUID -eq 0 && ${BMCD_ARCH_AS_BUILDER:-} != 1 ]]; then
    id builder >/dev/null 2>&1 || useradd -m builder
    mkdir -p "$ROOT/build" "$WORK" "$OUT_DIR"
    # makepkg must be able to remove and recreate archpkg, so the parent build
    # directory (only in the throwaway container) belongs to builder too.
    chown builder:builder "$ROOT/build"
    chown -R builder:builder "$WORK" "$OUT_DIR"
    export BMCD_ARCH_AS_BUILDER=1
    exec runuser -u builder -- env \
        HOME="$(getent passwd builder | cut -d: -f6)" \
        BMCD_ARCH_AS_BUILDER=1 \
        "$0" --version "$VERSION" --out-dir "$OUT_DIR"
fi

rm -rf "$WORK"
mkdir -p "$WORK/srcroot/bmcd-$VERSION"
# Build the source archive from the working tree, not from git, so a local
# package build includes the exact files that are being tested.  Release CI
# checks out the tag first, making the tree equivalent to the tag.
tar -C "$ROOT" \
    --exclude=.git --exclude=build --exclude=dist --exclude=.tools \
    --exclude='*/__pycache__' --exclude='*.pyc' \
    -cf - . | tar -C "$WORK/srcroot/bmcd-$VERSION" -xf -

tar -C "$WORK/srcroot" -czf "$WORK/bmcd-$VERSION.tar.gz" "bmcd-$VERSION"
SHA=$(sha256sum "$WORK/bmcd-$VERSION.tar.gz" | awk '{print $1}')

cp "$ROOT/packaging/arch/PKGBUILD" "$WORK/PKGBUILD"
sed -i \
    -e "s/^pkgver=.*/pkgver=$VERSION/" \
    -e "s/^sha256sums=.*/sha256sums=('$SHA')/" \
    "$WORK/PKGBUILD"

(
    cd "$WORK"
    makepkg --noconfirm --force --cleanbuild
)

mkdir -p "$OUT_DIR"
rm -f "$OUT_DIR"/*.pkg.tar.* "$OUT_DIR"/SHA256SUMS
cp "$WORK"/*.pkg.tar.zst "$OUT_DIR"/
(
    cd "$OUT_DIR"
    sha256sum ./*.pkg.tar.zst > SHA256SUMS
)

echo "built Arch package:"
ls -lh "$OUT_DIR"
cat "$OUT_DIR/SHA256SUMS"
