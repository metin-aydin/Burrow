#!/bin/sh
# Re-takes the README screenshots in docs/images/ (window frame and shadow included, 1600 px wide) from
# the built app with demo data, in English: gallery light and dark, the Mac device editor and the
# GlobalProtect tunnel editor. A throwaway vault is used; the app's theme and language settings are
# put back afterwards. Windows open on screen for a few seconds each; keep the pointer off them.
# Usage: tools/readme-shots.sh   (needs a build in build/, Xcode's swiftc, and Screen Recording
#        permission for the terminal)
set -eu
cd "$(dirname "$0")/.."
BIN=build/app/Burrow.app/Contents/MacOS/Burrow
DOMAIN=io.github.metin-aydin.Burrow
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

# Finds the main window id of a process (screencapture -l needs a CGWindowID).
cat > "$TMP/winid.swift" <<'EOF'
import CoreGraphics
let pid = Int32(CommandLine.arguments[1])!
let list = CGWindowListCopyWindowInfo([.optionOnScreenOnly], kCGNullWindowID) as! [[String: Any]]
var best = (0, 0.0)
for w in list where (w[kCGWindowOwnerPID as String] as? Int32) == pid && (w[kCGWindowLayer as String] as? Int) == 0 {
    let b = w[kCGWindowBounds as String] as! [String: Double]
    if b["Width"]! * b["Height"]! > best.1 { best = (w[kCGWindowNumber as String] as! Int, b["Width"]! * b["Height"]!) }
}
print(best.0)
EOF
swiftc -O "$TMP/winid.swift" -o "$TMP/winid"

old_theme=$(defaults read "$DOMAIN" ui.theme 2>/dev/null || echo __unset__)
old_lang=$(defaults read "$DOMAIN" ui.language 2>/dev/null || echo __unset__)
restore() {
    if [ "$2" = __unset__ ]; then defaults delete "$DOMAIN" "$1" 2>/dev/null || true
    else defaults write "$DOMAIN" "$1" -string "$2"; fi
}
trap 'restore ui.theme "$old_theme"; restore ui.language "$old_lang"; rm -rf "$TMP"' EXIT
defaults write "$DOMAIN" ui.language -string en
pw=$(openssl rand -hex 12)

shoot() {   # theme  demo-screen  output-name  [locked]
    defaults write "$DOMAIN" ui.theme -string "$1"
    unlock="$pw"; [ "${4:-}" = locked ] && unlock=""
    BURROW_VAULT="$TMP/vault.json" BURROW_DEMO_PASSWORD="$unlock" BURROW_DEMO_OPEN="$2" "$BIN" --seed-demo >/dev/null 2>&1 &
    pid=$!
    sleep 5
    screencapture -x -l "$("$TMP/winid" "$pid")" "docs/images/$3.png"
    kill "$pid"; wait "$pid" 2>/dev/null || true
    sips -Z 1600 "docs/images/$3.png" >/dev/null
    echo "docs/images/$3.png"
}
shoot light ""          gallery-light
shoot dark  ""          gallery-dark
shoot dark  device-mac  device-editor
shoot dark  tunnel-otp  tunnel-editor
shoot dark  tunnel-ts   tailscale-editor
shoot dark  tunnels     tunnel-profiles
shoot dark  settings    settings
shoot dark  ""          lock-screen locked   # the vault exists by now; no password → locked
