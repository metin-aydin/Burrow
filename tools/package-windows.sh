#!/bin/sh
# Packs a Windows build (MSYS2 UCRT64, ADR 0010) into the per-user installer and the portable zip, with Qt,
# QML, FreeRDP, openconnect and the MinGW runtime inside.
# Usage: tools/package-windows.sh <build dir> <version> [deps prefix]
#   e.g. tools/package-windows.sh build 0.4.0 C:/burrow-deps
# Output: dist/Burrow-<version>-windows-x64-setup.exe and dist/Burrow-<version>-windows-x64-portable.zip
set -eu
BUILD=$(cygpath -u "$1")
VERSION=$2
PREFIX=$(cygpath -u "${3:-C:/burrow-deps}")
cd "$(dirname "$0")/.."
ROOT=$PWD
platform=windows-x64

stage=$(mktemp -d)/Burrow
mkdir -p "$stage" dist
cp "$BUILD/app/Burrow.exe" "$stage/"

echo "Deploying Qt..."
# Qt libraries, plugins and the QML modules the app imports.
windeployqt=$(command -v windeployqt6 || command -v windeployqt || echo /ucrt64/share/qt6/bin/windeployqt.exe)
"$windeployqt" --release --qmldir "$ROOT/app/qml" --no-translations --no-system-d3d-compiler \
    --no-opengl-sw "$stage/Burrow.exe"

# Every DLL that Burrow, Qt's plugins and those DLLs load from MSYS2 or the deps prefix (FreeRDP, OpenSSL,
# openconnect, the MinGW runtime...), until nothing new turns up. Windows' own DLLs stay out.
export PATH="$PREFIX/bin:/ucrt64/bin:$PATH"
# Each file is scanned once: the queue holds only what was just added.
queue=$(find "$stage" -iname '*.exe' -o -iname '*.dll')
while [ -n "$queue" ]; do
    next=""
    for f in $queue; do
        for dep in $(ntldd "$f" 2>/dev/null | sed -n 's/.*=> \(.*\.dll\) .*/\1/Ip' | tr '\\' '/'); do
            dep=$(cygpath -u "$dep")
            case "$dep" in
                /ucrt64/bin/*|"$PREFIX"/bin/*) ;;
                *) continue ;;
            esac
            name=$(basename "$dep")
            [ -e "$stage/$name" ] && continue
            cp "$dep" "$stage/"
            next="$next $stage/$name"
        done
    done
    queue=$next
    echo "Collected DLLs: $(ls "$stage" | grep -ci '\.dll$')"
done
# OpenSSL's "legacy" provider (MD4/RC4): FreeRDP's NTLM sign-in needs it. libcrypto would look for it under
# the build machine's MSYS2 path; main.cpp points OPENSSL_MODULES at this copy. Its libcrypto DLL is already staged.
mkdir -p "$stage/ossl-modules"
cp /ucrt64/lib/ossl-modules/legacy.dll "$stage/ossl-modules/"
cp LICENSE THIRD_PARTY_NOTICES.md "$stage/"

echo "Packing..."
# Portable: the folder as it is, zipped (no uninstaller, so it never updates itself).
portable="dist/Burrow-$VERSION-$platform-portable.zip"
rm -f "$portable"
(cd "$(dirname "$stage")" && zip -qr9 "$ROOT/$portable" Burrow)

setup="dist/Burrow-$VERSION-$platform-setup.exe"
makensis -V2 -DVERSION="$VERSION" -DSTAGE="$(cygpath -w "$stage")" -DICON="$(cygpath -w "$ROOT/app/resources/Burrow.ico")" \
    -DOUTFILE="$(cygpath -w "$ROOT/$setup")" "$(cygpath -w "$ROOT/tools/windows/burrow.nsi")"
rm -rf "$(dirname "$stage")"
ls -l "$setup" "$portable"
