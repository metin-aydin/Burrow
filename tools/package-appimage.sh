#!/bin/sh
# Packs a Linux build into an AppImage with Qt, QML, FreeRDP and openconnect inside.
# Usage: tools/package-appimage.sh <build dir> <version> [deps prefix]
#   e.g. tools/package-appimage.sh build 0.4.0 /opt/burrow-deps
# Output: dist/Burrow-<version>-linux-<arch>.AppImage
set -eu
BUILD=$1
VERSION=$2
PREFIX=${3:-/opt/burrow-deps}
# Pinned linuxdeploy tools (continuous builds move; bump deliberately).
LINUXDEPLOY_TAG=1-alpha-20251107-1
QTPLUGIN_TAG=1-alpha-20250213-1
cd "$(dirname "$0")/.."

case "$(uname -m)" in
    x86_64) arch=x86_64; platform=linux-x64 ;;
    aarch64) arch=aarch64; platform=linux-arm64 ;;
    *) echo "Unsupported CPU $(uname -m)" >&2; exit 1 ;;
esac

tools=$(mktemp -d)
curl -fsSL -o "$tools/linuxdeploy" \
    "https://github.com/linuxdeploy/linuxdeploy/releases/download/$LINUXDEPLOY_TAG/linuxdeploy-$arch.AppImage"
curl -fsSL -o "$tools/linuxdeploy-plugin-qt" \
    "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/$QTPLUGIN_TAG/linuxdeploy-plugin-qt-$arch.AppImage"
chmod +x "$tools/linuxdeploy" "$tools/linuxdeploy-plugin-qt"

appdir=$(mktemp -d)/AppDir
DESTDIR="$appdir" cmake --install "$BUILD" --prefix /usr
mkdir -p "$appdir/usr/share/icons/hicolor/256x256/apps"
cp app/resources/icons/256x256.png "$appdir/usr/share/icons/hicolor/256x256/apps/burrow.png"

mkdir -p dist
# Qt Multimedia uses its FFmpeg backend (shipped with Qt); the GStreamer backend uses the system's GStreamer.
# Containers and CI runners have no FUSE: run the tool AppImages extracted.
export APPIMAGE_EXTRACT_AND_RUN=1
export QMAKE="$PREFIX/qt/bin/qmake"
export QML_SOURCES_PATHS="$PWD/app/qml"
export EXTRA_PLATFORM_PLUGINS="libqwayland.so"
export EXTRA_QT_MODULES="svg;waylandclient;multimedia"
export LD_LIBRARY_PATH="$PREFIX/freerdp/lib:$PREFIX/qt/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export LDAI_OUTPUT="dist/Burrow-$VERSION-$platform.AppImage"
export PATH="$tools:$PATH"
linuxdeploy --appdir "$appdir" \
    --desktop-file app/resources/linux/burrow.desktop \
    --icon-file "$appdir/usr/share/icons/hicolor/256x256/apps/burrow.png" \
    --exclude-library 'libgst*' \
    --plugin qt --output appimage
echo "$LDAI_OUTPUT"
