#!/usr/bin/env bash
# Bundle BMCD as a self-contained AppImage.
#
#   packaging/build-appimage.sh                      # build + bundle
#   BMCD_SKIP_BUILD=1 packaging/build-appimage.sh    # bundle the existing build/bmcd
#
# Qt is linked dynamically on purpose: LGPLv3 allows shipping the .so files
# inside the AppImage, whereas a static Qt build would put the whole application
# under the LGPL. Only Core/Gui/Quick/Qml/Network are used — all LGPL.
#
# The tools themselves are AppImages and this machine has no /dev/fuse, so every
# tool invocation runs with APPIMAGE_EXTRACT_AND_RUN=1.
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
TOOLS=${BMCD_TOOLS:-"$ROOT/.tools"}
BUILD=${BMCD_BUILD_DIR:-"$ROOT/build"}
APPDIR="$BUILD/AppDir"
PLUGIN_ROOT="$BUILD/qt-plugins"
ARCH=x86_64
OUTPUT=${BMCD_OUTPUT:-"$BUILD/BMCD-$ARCH.AppImage"}
QT_PLUGINS_SRC=${BMCD_QT_PLUGINS:-}

LINUXDEPLOY="$TOOLS/linuxdeploy-$ARCH.AppImage"
QT_PLUGIN="$TOOLS/linuxdeploy-plugin-qt-$ARCH.AppImage"
APPIMAGETOOL="$TOOLS/appimagetool-$ARCH.AppImage"

export APPIMAGE_EXTRACT_AND_RUN=1
export ARCH

# docs/plan.md 1.5.1: the published AppImage must run on Ubuntu 22.04. Building
# on a newer distro silently binds to a newer glibc, so refuse that by default;
# CI builds on ubuntu-22.04 and packaging/smoke-artifact.sh checks the result.
GLIBC_BASELINE=${BMCD_GLIBC_BASELINE:-2.35}
GLIBC_HOST=$(getconf GNU_LIBC_VERSION 2>/dev/null | awk '{print $2}' || true)
if [[ -n $GLIBC_HOST ]] && \
   [[ $(printf '%s\n%s\n' "$GLIBC_HOST" "$GLIBC_BASELINE" | sort -V | tail -1) != "$GLIBC_BASELINE" ]]; then
    if [[ ${BMCD_ALLOW_NEW_GLIBC:-} != 1 ]]; then
        echo "host glibc $GLIBC_HOST is newer than the $GLIBC_BASELINE baseline" >&2
        echo "build on Ubuntu 22.04, or set BMCD_ALLOW_NEW_GLIBC=1 for a local-only artifact" >&2
        exit 1
    fi
    echo "warning: host glibc $GLIBC_HOST exceeds baseline $GLIBC_BASELINE; local artifact only" >&2
fi

for tool in "$LINUXDEPLOY" "$QT_PLUGIN" "$APPIMAGETOOL"; do
    [[ -x $tool ]] || { echo "missing tool: $tool (see packaging/README.md)" >&2; exit 1; }
done

QT_QMAKE=${QT_QMAKE:-$(command -v qmake6 || command -v qmake)}

# Distro Qt lives in /usr/lib/qt6, but CI installs Qt elsewhere; ask qmake.
if [[ -z $QT_PLUGINS_SRC ]]; then
    QT_PLUGINS_SRC=$("$QT_QMAKE" -query QT_INSTALL_PLUGINS 2>/dev/null || true)
fi
if [[ ! -d $QT_PLUGINS_SRC ]]; then
    QT_PLUGINS_SRC=/usr/lib/qt6/plugins
fi
[[ -d $QT_PLUGINS_SRC ]] || { echo "Qt plugin directory not found: $QT_PLUGINS_SRC" >&2; exit 1; }

if [[ -z ${BMCD_SKIP_BUILD:-} ]]; then
    cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build "$BUILD"
fi

rm -rf "$APPDIR"
install -d "$APPDIR/usr/bin" \
           "$APPDIR/usr/share/applications" \
           "$APPDIR/usr/share/icons/hicolor/512x512/apps"
install -m 755 "$BUILD/bmcd" "$APPDIR/usr/bin/bmcd"
install -m 644 "$ROOT/packaging/bmcd.desktop" "$APPDIR/usr/share/applications/org.bmcd.BMCD.desktop"
rsvg-convert -w 512 -h 512 -o "$APPDIR/usr/share/icons/hicolor/512x512/apps/org.bmcd.BMCD.png" \
             "$ROOT/packaging/bmcd.svg"

# ── staged plugin root ───────────────────────────────────────────────────
# The Qt deployer walks the plugin root and treats a plugin with an unresolved
# dependency as fatal (this Arch ships kimg_jxr.so without libjpegxr.so), and a
# missing directory it expects is fatal too. Mirror the whole tree with hard
# links, then drop only the plugins whose dependencies do not resolve.
rm -rf "$PLUGIN_ROOT"
mkdir -p "$PLUGIN_ROOT"
cp -al "$QT_PLUGINS_SRC"/. "$PLUGIN_ROOT"/ 2>/dev/null || cp -a "$QT_PLUGINS_SRC"/. "$PLUGIN_ROOT"/
find "$PLUGIN_ROOT" -name '*.so' -print0 | while IFS= read -r -d '' so; do
    if ldd "$so" 2>/dev/null | grep -q "not found"; then
        echo "dropping plugin with unresolved dependencies: ${so#"$PLUGIN_ROOT"/}"
        rm -f "$so"
    fi
done

# A qmake that answers QT_INSTALL_PLUGINS with the curated root above.
QMAKE_WRAPPER="$BUILD/qmake-staged"
cat > "$QMAKE_WRAPPER" <<WRAPPER
#!/bin/sh
"$QT_QMAKE" "\$@" | while IFS= read -r line; do
    case "\$line" in
        QT_INSTALL_PLUGINS:*) printf 'QT_INSTALL_PLUGINS:%s\\n' "$PLUGIN_ROOT" ;;
        *) printf '%s\\n' "\$line" ;;
    esac
done
WRAPPER
chmod +x "$QMAKE_WRAPPER"
# Arch ships qmake-qt5 as well; the deployer probes Qt 5 first unless it is told
# which qmake to use.
export QMAKE="$QMAKE_WRAPPER"

# QML_SOURCES_PATHS tells the Qt plugin which imports to deploy; Main.qml is
# compiled into the binary, but qmlimportscanner still has to see qml2.
#
# NO_STRIP: the strip bundled in linuxdeploy is older binutils and chokes on the
# `.relr.dyn` sections Arch's toolchain emits ("unknown type [0x13] section"),
# which aborts the whole deployment. Keep the symbols; appimagetool compresses
# the payload anyway.
QML_SOURCES_PATHS="$ROOT/qml2" NO_STRIP=1 \
    "$LINUXDEPLOY" --appdir "$APPDIR" --plugin qt \
                   --executable "$BUILD/bmcd" \
                   --desktop-file "$APPDIR/usr/share/applications/org.bmcd.BMCD.desktop" \
                   --icon-file "$APPDIR/usr/share/icons/hicolor/512x512/apps/org.bmcd.BMCD.png"

# linuxdeploy only deploys the platform plugins matching the build host, which
# leaves the bundle without a headless platform. Ship offscreen and minimal so
# `QT_QPA_PLATFORM=offscreen` smoke tests and MC_SCREENSHOT work from the
# AppImage itself.
for plugin in libqoffscreen.so libqminimal.so; do
    [[ -f $QT_PLUGINS_SRC/platforms/$plugin ]] &&
        install -m 755 "$QT_PLUGINS_SRC/platforms/$plugin" "$APPDIR/usr/plugins/platforms/"
done

# linuxdeploy leaves AppRun as a symlink to the binary. When the AppImage
# runtime executes that path, Qt derives its application dir from argv[0]
# (the symlink), misses usr/bin/qt.conf and searches for plugins in the
# payload root — the app then starts without a TLS backend. A real launcher
# that resolves the payload root and execs the binary fixes it.
rm -f "$APPDIR/AppRun"
cat > "$APPDIR/AppRun" <<'LAUNCHER'
#!/bin/sh
# AppImage entry point: locate the payload, point Qt at the bundled trees.
set -eu
HERE=$(dirname "$(readlink -f "$0")")
export APPDIR=${APPDIR:-$HERE}
export LD_LIBRARY_PATH="$APPDIR/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$APPDIR/usr/plugins${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}"
export QML2_IMPORT_PATH="$APPDIR/usr/qml${QML2_IMPORT_PATH:+:$QML2_IMPORT_PATH}"
exec "$APPDIR/usr/bin/bmcd" "$@"
LAUNCHER
chmod 755 "$APPDIR/AppRun"

# The host check above catches the usual mistake; this one checks the actual
# payload, including Qt libraries copied by linuxdeploy. A GLIBC_2.36+ symbol
# here means the artifact will not start on the Ubuntu 22.04 baseline.
if command -v readelf >/dev/null; then
    MAX_GLIBC=$(find "$APPDIR/usr/bin" "$APPDIR/usr/lib" -type f \
        \( -perm -u+x -o -name '*.so*' \) -print0 2>/dev/null |
        xargs -0 -r readelf --version-info --wide 2>/dev/null |
        grep -oE 'GLIBC_[0-9]+\.[0-9]+' | sort -V | tail -1 || true)
    if [[ -n $MAX_GLIBC ]] && \
       [[ $(printf '%s\n%s\n' "$MAX_GLIBC" "GLIBC_$GLIBC_BASELINE" | sort -V | tail -1) != "GLIBC_$GLIBC_BASELINE" ]]; then
        if [[ ${BMCD_ALLOW_NEW_GLIBC:-} != 1 ]]; then
            echo "payload requires $MAX_GLIBC, newer than the GLIBC_$GLIBC_BASELINE baseline" >&2
            exit 1
        fi
        echo "warning: payload requires $MAX_GLIBC; local artifact only" >&2
    else
        echo "payload glibc requirement: ${MAX_GLIBC:-unknown} (baseline GLIBC_$GLIBC_BASELINE)"
    fi
fi

if [[ -n ${BMCD_APPIMAGE_RUNTIME:-} ]]; then
    "$APPIMAGETOOL" --runtime-file "$BMCD_APPIMAGE_RUNTIME" "$APPDIR" "$OUTPUT"
else
    "$APPIMAGETOOL" "$APPDIR" "$OUTPUT"
fi
(cd "$(dirname "$OUTPUT")" && sha256sum "$(basename "$OUTPUT")" > SHA256SUMS)
echo
echo "built: $OUTPUT"
cat "$(dirname "$OUTPUT")/SHA256SUMS"

if [[ -z ${BMCD_NO_SMOKE:-} ]]; then
    "$ROOT/packaging/smoke-artifact.sh" appimage "$OUTPUT"
fi
