#!/usr/bin/env bash
# Copies OpenSSL's "legacy" provider into Burrow.app (Contents/PlugIns/ossl-modules) after macdeployqt.
# FreeRDP needs it for MD4/RC4, which NTLM (NLA sign-in) depends on. libcrypto otherwise looks for it at the
# build machine's Homebrew path, which other Macs don't have and which the hardened runtime refuses to load
# (different signature). main.cpp points OPENSSL_MODULES at the bundled copy.
# Usage: tools/mac-bundle-ossl-modules.sh build/app/Burrow.app
set -euo pipefail

app=${1:?usage: $0 path/to/Burrow.app}
frameworks="$app/Contents/Frameworks"

# The libcrypto that FreeRDP's winpr was linked against; the module must come from the same OpenSSL.
crypto=$(otool -L "$frameworks/libwinpr3.3.dylib" | awk '{print $1}' | grep -o 'libcrypto\.[0-9]*\.dylib' | head -1)
[ -n "$crypto" ] || { echo "error: libwinpr3 links no libcrypto" >&2; exit 1; }
major=${crypto#libcrypto.}; major=${major%.dylib}
prefix=$(brew --prefix "openssl@$major")
src="$prefix/lib/ossl-modules/legacy.dylib"
[ -f "$src" ] || { echo "error: $src not found" >&2; exit 1; }

dest="$app/Contents/PlugIns/ossl-modules"
mkdir -p "$dest"
cp -f "$src" "$dest/legacy.dylib"
chmod u+w "$dest/legacy.dylib"

# Use the bundled libcrypto, not Homebrew's (the same one FreeRDP loads, so there is a single OpenSSL).
for dep in $(otool -L "$dest/legacy.dylib" | tail -n +2 | awk '{print $1}' | grep 'libcrypto\.'); do
    install_name_tool -change "$dep" "@loader_path/../../Frameworks/$(basename "$dep")" "$dest/legacy.dylib"
done
[ -f "$frameworks/$crypto" ] || { echo "error: $crypto is not bundled" >&2; exit 1; }
if otool -L "$dest/legacy.dylib" | tail -n +2 | awk '{print $1}' | grep -qE '^/(opt|usr/local)/'; then
    echo "error: legacy.dylib still points outside the bundle" >&2; exit 1
fi
echo "Bundled $src -> $dest/legacy.dylib ($crypto)"
