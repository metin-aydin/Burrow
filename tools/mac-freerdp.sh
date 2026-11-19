#!/bin/sh
# Builds FreeRDP from source on a Mac whose Homebrew lags behind (Intel Macs: Homebrew's Intel bottles stopped at
# 3.30, Burrow needs 3.33+), with Homebrew's own options for the client library (FFmpeg for H.264, JPEG, CUPS
# printers, Core Audio sound) and without its X11/SDL clients and server.
# Same version and commit as tools/linux-deps.sh; change both together.
# Usage: tools/mac-freerdp.sh <prefix>      Afterwards: CMAKE_PREFIX_PATH="$(brew --prefix qt);<prefix>"
# Needs: brew install cmake ninja pkgconf ffmpeg jpeg-turbo openssl@3
set -eu
PREFIX=${1:?usage: $0 <prefix>}
FREERDP_VERSION=3.33.1
# The commit the tag points to; a moved tag stops the build instead of being built.
FREERDP_COMMIT=65bcca1913a28cbdd7eb913599f692bd2b707991

if [ -f "$PREFIX/lib/cmake/FreeRDP3/FreeRDPConfig.cmake" ]; then
    echo "FreeRDP is already in $PREFIX"
    exit 0
fi

# Private scratch space: never reuse a source or build folder someone else could have planted.
WORK=$(mktemp -d)
trap 'rm -rf "${WORK:?}"' EXIT
src="$WORK/freerdp-src"
git clone -q --depth 1 --branch "$FREERDP_VERSION" https://github.com/FreeRDP/FreeRDP.git "$src"
if [ "$(git -C "$src" rev-parse HEAD)" != "$FREERDP_COMMIT" ]; then
    echo "FreeRDP $FREERDP_VERSION is not commit $FREERDP_COMMIT; refusing to build it." >&2
    exit 1
fi
cmake -S "$src" -B "$WORK/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_INSTALL_NAME_DIR="$PREFIX/lib" -DBUILD_SHARED_LIBS=ON \
    -DOPENSSL_ROOT_DIR="$(brew --prefix openssl@3)" \
    -DWITH_X11=OFF -DWITH_CLIENT_SDL=OFF -DWITH_CLIENT_SDL2=OFF -DWITH_CLIENT_SDL3=OFF -DWITH_CLIENT_MAC=OFF \
    -DWITH_SERVER=OFF -DWITH_SHADOW=OFF -DWITH_PROXY=OFF -DWITH_PLATFORM_SERVER=OFF -DWITH_SAMPLE=OFF \
    -DWITH_JPEG=ON -DWITH_FFMPEG=ON -DWITH_CUPS=ON -DWITH_PCSC=OFF -DWITH_FUSE=OFF -DWITH_MANPAGES=OFF \
    -DWITH_WEBVIEW=OFF -DCHANNEL_URBDRC=OFF -DBUILD_TESTING=OFF -DWITH_CLIENT_CHANNELS=ON
cmake --build "$WORK/build"
cmake --install "$WORK/build"
echo "FreeRDP $FREERDP_VERSION installed in $PREFIX"
