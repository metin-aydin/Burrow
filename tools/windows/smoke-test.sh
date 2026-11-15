#!/bin/sh
# Checks a packaged Windows build the way a user gets it: unpacks the portable zip into an empty folder, makes sure
# every DLL it needs is inside it or part of Windows, then starts Burrow (offscreen, throwaway vault) and expects it
# to still be running after a while (a missing QML module or plugin makes it quit at once).
# Usage (MSYS2 UCRT64): tools/windows/smoke-test.sh dist/Burrow-<version>-windows-x64-portable.zip
set -eu
zip=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work"
powershell.exe -NoProfile -Command "Expand-Archive -LiteralPath '$(cygpath -w "$zip")' -DestinationPath '$(cygpath -w "$work")'"
app="$work/Burrow"

# Only the package itself and Windows on the search path, as on a user's PC.
missing=0
for f in $(find "$app" -iname '*.exe' -o -iname '*.dll'); do
    out=$(PATH="$app:/c/Windows/System32" ntldd "$f" 2>&1 || true)
    bad=$(printf '%s\n' "$out" | grep -i 'not found' || true)
    if [ -n "$bad" ]; then
        echo "::error::$(basename "$f") needs a DLL that isn't packaged: $bad"
        missing=1
    fi
done
[ "$missing" = 0 ] || exit 1
echo "All DLLs are packaged."

export BURROW_VAULT="$(cygpath -w "$work/vault/vault.json")" QT_QPA_PLATFORM=offscreen
PATH="$app:/c/Windows/System32" "$app/Burrow.exe" > "$work/out.log" 2>&1 &
pid=$!
sleep 15
if kill -0 "$pid" 2>/dev/null; then
    kill "$pid" 2>/dev/null || true
    echo "Burrow started and kept running."
    exit 0
fi
wait "$pid" && code=0 || code=$?
echo "::error::Burrow quit at start (exit code $code)"
cat "$work/out.log"
exit 1
