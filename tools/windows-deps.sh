#!/bin/sh
# Prepares a Windows machine (CI runner or a developer's PC) to build Burrow, inside an MSYS2 UCRT64 shell
# (ADR 0010): MSYS2 packages for the toolchain, Qt 6, FreeRDP 3, OpenSSL and zlib, and libopenconnect built
# from source (no Windows package repository has it) with Burrow's named-pipe patch.
# Usage: tools/windows-deps.sh [prefix]      (default prefix: C:/burrow-deps)
# Afterwards: PKG_CONFIG_PATH="<prefix>/lib/pkgconfig", and <prefix>/bin on PATH to run Burrow.
set -eu
[ "${MSYSTEM:-}" = UCRT64 ] || { echo "Run this in an MSYS2 UCRT64 shell." >&2; exit 1; }
PREFIX=$(cygpath -m "${1:-C:/burrow-deps}")
OPENCONNECT_VERSION=9.21
OPENCONNECT_SHA256=5b32369467db6e5f317aa1ed12cfcbb81ed00bdbc765450b6bfcbdc300944a58
HERE=$(cd "$(dirname "$0")" && pwd)

p=mingw-w64-ucrt-x86_64
pacman -S --needed --noconfirm \
    $p-gcc $p-cmake $p-ninja $p-pkgconf $p-ntldd $p-nsis \
    $p-qt6-base $p-qt6-declarative $p-qt6-svg $p-qt6-multimedia $p-qt6-tools $p-qt6-translations \
    $p-freerdp $p-openssl $p-zlib $p-libxml2 $p-lz4 \
    autoconf automake libtool make patch zip

# The stamp names the version and the patch, so changing either rebuilds.
stamp="$PREFIX/.openconnect-$OPENCONNECT_VERSION-$(sha256sum "$HERE/windows/openconnect-pipe-tun.patch" | cut -c1-12)"
if [ -f "$stamp" ]; then
    echo "libopenconnect $OPENCONNECT_VERSION is already in $PREFIX"
    exit 0
fi

# Private scratch space: never reuse a source folder someone else could have planted.
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
curl -fsSLo oc.tar.gz "https://www.infradead.org/openconnect/download/openconnect-$OPENCONNECT_VERSION.tar.gz"
echo "$OPENCONNECT_SHA256  oc.tar.gz" | sha256sum -c -
tar xzf oc.tar.gz
cd "openconnect-$OPENCONNECT_VERSION"
patch -p1 < "$HERE/windows/openconnect-pipe-tun.patch"
# Only the library is built: Burrow drives it in-process and never runs a vpnc-script, so the script
# path is a placeholder (configure insists on one). OpenSSL rather than GnuTLS: Burrow already ships it.
./configure --prefix="$PREFIX" --with-openssl --without-gnutls --with-vpnc-script=burrow-none.js \
    --disable-nls --disable-docs --without-libproxy --without-stoken --without-libpskc --without-libpcsclite \
    --without-gssapi --without-java --disable-dsa-tests
make -j"$(nproc)" libopenconnect.la
make install-libLTLIBRARIES install-includeHEADERS install-pkgconfigDATA
rm -f "$PREFIX"/.openconnect-*
touch "$stamp"
echo "libopenconnect $OPENCONNECT_VERSION installed in $PREFIX"
