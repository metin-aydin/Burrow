#!/bin/sh
# Renders app/resources/icon.svg to PNG sizes, the macOS .icns and the Windows .ico using headless Chrome
# (Qt's SVG renderer does not support the drop-shadow filters).
# Usage: tools/render-icons.sh   (needs Google Chrome; ImageMagick for the .ico)
set -eu
cd "$(dirname "$0")/.."
CHROME="${CHROME:-/Applications/Google Chrome.app/Contents/MacOS/Google Chrome}"
SVG=app/resources/icon.svg
OUT=app/resources/icons
TMP=$(mktemp -d)
mkdir -p "$OUT"
for s in 16 24 32 48 64 96 128 256 512 1024; do
  sed -E "s/<svg width=\"1024\" height=\"1024\"/<svg width=\"$s\" height=\"$s\"/" "$SVG" > "$TMP/i.svg"
  printf '<html><body style="margin:0;background:transparent">%s</body></html>' "$(cat "$TMP/i.svg")" > "$TMP/i.html"
  "$CHROME" --headless=new --disable-gpu --hide-scrollbars --default-background-color=00000000 \
    --window-size="$s,$s" --screenshot="$OUT/${s}x${s}.png" "file://$TMP/i.html" >/dev/null 2>&1
done
# macOS iconset
ICONSET="$TMP/Burrow.iconset"; mkdir -p "$ICONSET"
for s in 16 32 128 256 512; do
  cp "$OUT/${s}x${s}.png" "$ICONSET/icon_${s}x${s}.png"
  d=$((s * 2)); cp "$OUT/${d}x${d}.png" "$ICONSET/icon_${s}x${s}@2x.png"
done
iconutil -c icns "$ICONSET" -o app/resources/Burrow.icns
if command -v magick >/dev/null; then
  magick "$OUT/16x16.png" "$OUT/24x24.png" "$OUT/32x32.png" "$OUT/48x48.png" "$OUT/64x64.png" \
    "$OUT/128x128.png" "$OUT/256x256.png" app/resources/Burrow.ico
fi
rm -rf "$TMP"
echo "icons written to $OUT, app/resources/Burrow.icns, app/resources/Burrow.ico"
