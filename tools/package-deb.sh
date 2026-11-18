#!/bin/sh
# Packs the AppDir that tools/package-appimage.sh prepared (Qt, QML, FreeRDP and openconnect inside) into an Ubuntu
# .deb, laid out like JumpDesk's: the app in /opt/burrow, `burrow` on PATH, a menu entry and an icon. Ubuntu 24.04
# ships Qt and FreeRDP versions older than Burrow needs, so they stay bundled; everything else comes from the system
# and is listed as a dependency, found by asking dpkg which package owns each system library Burrow loads.
# Usage: tools/package-deb.sh <AppDir> <version>
# Output: dist/burrow_<version>_<arch>.deb
set -eu
APPDIR=$1
VERSION=$2
cd "$(dirname "$0")/.."
ROOT=$PWD
arch=$(dpkg --print-architecture)

pkg=$(mktemp -d)/burrow
mkdir -p "$pkg/DEBIAN" "$pkg/opt/burrow" "$pkg/usr/bin" "$pkg/usr/share/applications" \
    "$pkg/usr/share/icons/hicolor/256x256/apps"
cp -a "$APPDIR/usr/." "$pkg/opt/burrow/"
rm -rf "$pkg/opt/burrow/share/applications" "$pkg/opt/burrow/share/icons"
ln -s /opt/burrow/bin/burrow "$pkg/usr/bin/burrow"
cp app/resources/linux/burrow.desktop "$pkg/usr/share/applications/burrow.desktop"
cp app/resources/icons/256x256.png "$pkg/usr/share/icons/hicolor/256x256/apps/burrow.png"

# System libraries the bundled files load (anything ldd resolves outside /opt/burrow), mapped to their packages.
deps=$(
    find "$pkg/opt/burrow" -type f \( -name '*.so*' -o -perm -u+x \) -exec sh -c 'file -b "$1" | grep -q ELF' _ {} \; -print |
    # `|| true`: ldd fails on a static file (burrow-agent), which has no libraries to list; under set -e that
    # would end the loop early and leave the list empty.
    while read -r f; do LD_LIBRARY_PATH="$pkg/opt/burrow/lib" ldd "$f" 2>/dev/null || true; done |
    awk '/=> \// {print $3}' | grep -v "^$pkg/" | sort -u |
    while read -r lib; do dpkg -S "$(readlink -f "$lib")" 2>/dev/null || dpkg -S "$lib" 2>/dev/null || true; done |
    cut -d: -f1 | sort -u | tr '\n' ',' | sed 's/,$//; s/,/, /g'
)
[ -n "$deps" ] || { echo "No dependencies found; is this an Ubuntu machine with the libraries installed?" >&2; exit 1; }

size=$(du -sk "$pkg" | cut -f1)
cat > "$pkg/DEBIAN/control" <<EOF
Package: burrow
Version: $VERSION
Architecture: $arch
Maintainer: metin-aydin <https://github.com/metin-aydin/Burrow>
Installed-Size: $size
Depends: $deps
Section: net
Priority: optional
Homepage: https://github.com/metin-aydin/Burrow
Description: Remote desktop with private tunnels built in
 RDP to Windows and Ubuntu and Screen Sharing to Macs, with the GlobalProtect,
 Tailscale or Cloudflare tunnel those computers sit behind opened inside the
 app only.
EOF

mkdir -p dist
out="dist/burrow_${VERSION}_${arch}.deb"
dpkg-deb --root-owner-group --build "$pkg" "$ROOT/$out" >/dev/null
rm -rf "$(dirname "$pkg")"
echo "$out"
echo "Depends: $deps"
