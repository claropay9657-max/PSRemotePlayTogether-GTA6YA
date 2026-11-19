#!/bin/bash
# Regenerates every icon file of the app from the one drawing in
# assets/icon/psremoteplaytogether.svg. Run on a Mac (uses sips and iconutil)
# with Python 3 and Pillow for the Windows icon:
#
#   scripts/make-icons.sh
#
# The drawing has the rounded square at 64..960 of a 1024 canvas. Three crops
# of it are used: as drawn, without the margin ("full", for Windows, Linux and
# inside the app), and with the wider margin macOS icons have ("mac").

set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

SRC=assets/icon/psremoteplaytogether.svg
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

HEAD='width="1024" height="1024" viewBox="0 0 1024 1024"'
grep -q "$HEAD" "$SRC" || { echo "$SRC: unexpected <svg> header" >&2; exit 1; }
sed "s|$HEAD|width=\"1024\" height=\"1024\" viewBox=\"64 64 896 896\"|" "$SRC" > "$WORK/full.svg"
# Apple's grid: the shape takes 824 of 1024
sed "s|$HEAD|width=\"1024\" height=\"1024\" viewBox=\"-44.75 -44.75 1113.5 1113.5\"|" "$SRC" > "$WORK/mac.svg"

sips -s format png "$WORK/full.svg" --out "$WORK/full.png" >/dev/null
sips -s format png "$WORK/mac.svg" --out "$WORK/mac.png" >/dev/null

# Inside the app (window icon, About) and for whoever wants the artwork
cp "$SRC" assets/chiaking.svg
cp "$WORK/full.svg" gui/res/chiaking.svg
cp "$WORK/full.svg" gui/res/chiaking-logo.svg
cp "$WORK/full.svg" gui/res/chiaking-logo-white.svg
cp "$WORK/mac.svg" gui/res/chiaking_macos.svg
cp "$WORK/mac.svg" gui/res/chiaki_macos.svg

# Linux
sips -z 512 512 "$WORK/full.png" --out gui/chiaking.png >/dev/null

# macOS
SET="$WORK/app.iconset"
mkdir "$SET"
for size in 16 32 128 256 512; do
	sips -z $size $size "$WORK/mac.png" --out "$SET/icon_${size}x${size}.png" >/dev/null
	sips -z $((size * 2)) $((size * 2)) "$WORK/mac.png" --out "$SET/icon_${size}x${size}@2x.png" >/dev/null
done
iconutil -c icns "$SET" -o gui/chiaking.icns
cp gui/chiaking.icns assets/chiaking.icns

# Windows, and the pictures a Steam shortcut gets
python3 - "$WORK/full.png" <<'PY'
import sys
from PIL import Image, ImageDraw, ImageFont

full = Image.open(sys.argv[1]).convert("RGBA")
full.save("gui/chiaking.ico", sizes=[(s, s) for s in (16, 24, 32, 48, 64, 128, 256)])
full.resize((184, 184), Image.LANCZOS).save("gui/res/steam_icon.png")

# Steam's logo picture: the icon next to the name, on nothing
logo = Image.new("RGBA", (1280, 720), (0, 0, 0, 0))
icon = full.resize((300, 300), Image.LANCZOS)
font = None
for path, index in (("/System/Library/Fonts/HelveticaNeue.ttc", 1), ("/System/Library/Fonts/Helvetica.ttc", 1),
		("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 0)):
	try:
		font = ImageFont.truetype(path, 78, index=index)
		break
	except OSError:
		pass
draw = ImageDraw.Draw(logo)
text = "PSRemotePlayTogether"
if font:
	width = draw.textlength(text, font=font)
	x = (1280 - (300 + 40 + width)) / 2
	logo.alpha_composite(icon, (int(x), 210))
	draw.text((x + 340, 360), text, font=font, fill=(255, 255, 255, 255), anchor="lm")
else:
	logo.alpha_composite(icon, (490, 210))
logo.save("gui/res/steam_logo.png")
PY

echo "Icons written."
