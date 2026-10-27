#!/bin/sh
# Regenerates emerick.icns and emerick.png from emerick.svg. Needs rsvg-convert (brew install librsvg) and iconutil.
set -eu
cd "$(dirname "$0")"
set_dir=$(mktemp -d)/emerick.iconset
mkdir "$set_dir"
for size in 16 32 128 256 512; do
    rsvg-convert -w $size -h $size emerick.svg -o "$set_dir/icon_${size}x${size}.png"
    rsvg-convert -w $((size * 2)) -h $((size * 2)) emerick.svg -o "$set_dir/icon_${size}x${size}@2x.png"
done
iconutil -c icns "$set_dir" -o emerick.icns
rsvg-convert -w 256 -h 256 emerick.svg -o emerick.png
rm -r "$(dirname "$set_dir")"
