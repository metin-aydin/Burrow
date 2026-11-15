#!/bin/sh
# Prepares an Ubuntu 24.04 machine (CI runner or container) to build Burrow:
# distro packages, Qt from the Qt installer mirrors (Ubuntu ships 6.4, Burrow needs 6.10+) and
# FreeRDP built from source (Ubuntu ships 3.5, Burrow needs 3.33+).
# Usage: tools/linux-deps.sh [prefix]     (default prefix: /opt/burrow-deps)
# Afterwards: CMAKE_PREFIX_PATH="<prefix>/qt;<prefix>/freerdp"
set -eu
PREFIX=${1:-/opt/burrow-deps}
QT_VERSION=6.10.1
FREERDP_VERSION=3.33.1
# The commit the tag points to; a moved tag stops the build instead of being built.
FREERDP_COMMIT=65bcca1913a28cbdd7eb913599f692bd2b707991

# Private scratch space: never reuse a source or build folder someone else could have planted in /tmp.
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

SUDO=""
[ "$(id -u)" -ne 0 ] && SUDO=sudo
# The prefix may live in the home folder (CI cache) or under /opt (needs sudo).
mkdir -p "$PREFIX" 2>/dev/null || $SUDO mkdir -p "$PREFIX"
PSUDO=""
[ -w "$PREFIX" ] || PSUDO=$SUDO
export DEBIAN_FRONTEND=noninteractive
$SUDO apt-get update -q
$SUDO apt-get install -y -q --no-install-recommends \
    build-essential cmake ninja-build pkg-config git curl ca-certificates file python3-pip python3-venv \
    libopenconnect-dev libssl-dev zlib1g-dev libcjson-dev libusb-1.0-0-dev libkrb5-dev \
    libgl1-mesa-dev libegl1-mesa-dev libxkbcommon-dev libxkbcommon-x11-0 libfontconfig1-dev libfreetype-dev \
    libx11-xcb1 libxcb-cursor0 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 libxcb-randr0 libxcb-render-util0 \
    libxcb-shape0 libxcb-xinerama0 libxcb-xkb1 libdbus-1-3 libpulse0 libgstreamer-plugins-base1.0-0 \
    libsm6 libice6 libxcb-util1 libxcb-sync1 libxcb-xfixes0 libxrender1 libxext6 libxi6 libxrandr2 libgstreamer-gl1.0-0 libgstreamer-plugins-bad1.0-0 libgstreamer-plugins-good1.0-0 libwayland-dev

# Qt (aqtinstall reads the official Qt mirrors and checks every archive against the SHA-256 that
# Qt publishes in its update metadata).
if [ "$("$PREFIX/qt/bin/qmake" -query QT_VERSION 2>/dev/null)" != "$QT_VERSION" ]; then
    [ -d "$PREFIX/qt" ] && $PSUDO mv "$PREFIX/qt" "$PREFIX/qt-old-$(date +%s)"
    case "$(uname -m)" in
        x86_64) host=linux; arch=linux_gcc_64 ;;
        aarch64) host=linux_arm64; arch=linux_gcc_arm64 ;;
        *) echo "Unsupported CPU $(uname -m)" >&2; exit 1 ;;
    esac
    python3 -m venv "$WORK/aqt-venv"
    "$WORK/aqt-venv/bin/pip" install -q aqtinstall==3.3.0
    "$WORK/aqt-venv/bin/aqt" install-qt "$host" desktop "$QT_VERSION" "$arch" \
        -m qtmultimedia -O "$WORK/qt-install"
    $PSUDO mv "$WORK/qt-install/$QT_VERSION/$(ls "$WORK/qt-install/$QT_VERSION" | head -n1)" "$PREFIX/qt"
fi

# FreeRDP: only the client library pieces Burrow uses (no X11/SDL/Wayland clients, no server).
if [ ! -f "$PREFIX/freerdp/lib/cmake/FreeRDP3/FreeRDPConfig.cmake" ] \
   && [ ! -f "$PREFIX/freerdp/lib64/cmake/FreeRDP3/FreeRDPConfig.cmake" ]; then
    src="$WORK/freerdp-src"
    git clone -q --depth 1 --branch "$FREERDP_VERSION" https://github.com/FreeRDP/FreeRDP.git "$src"
    if [ "$(git -C "$src" rev-parse HEAD)" != "$FREERDP_COMMIT" ]; then
        echo "FreeRDP $FREERDP_VERSION is not commit $FREERDP_COMMIT; refusing to build it." >&2
        exit 1
    fi
    cmake -S "$src" -B "$WORK/freerdp-build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$PREFIX/freerdp" -DCMAKE_INSTALL_LIBDIR=lib \
        -DWITH_CLIENT_SDL=OFF -DWITH_CLIENT_SDL2=OFF -DWITH_CLIENT_SDL3=OFF -DWITH_X11=OFF -DWITH_WAYLAND=OFF \
        -DWITH_SERVER=OFF -DWITH_SHADOW=OFF -DWITH_PROXY=OFF -DWITH_PLATFORM_SERVER=OFF -DWITH_SAMPLE=OFF \
        -DWITH_FFMPEG=OFF -DWITH_SWSCALE=OFF -DWITH_DSP_FFMPEG=OFF -DWITH_CUPS=OFF -DWITH_PCSC=OFF \
        -DWITH_PULSE=OFF -DWITH_ALSA=OFF -DWITH_OSS=OFF -DWITH_FUSE=OFF -DWITH_MANPAGES=OFF \
        -DWITH_WEBVIEW=OFF -DCHANNEL_URBDRC=OFF -DBUILD_TESTING=OFF -DWITH_CLIENT_CHANNELS=ON
    cmake --build "$WORK/freerdp-build"
    $PSUDO cmake --install "$WORK/freerdp-build"
fi
echo "CMAKE_PREFIX_PATH=$PREFIX/qt;$PREFIX/freerdp"
