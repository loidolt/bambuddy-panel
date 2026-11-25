#!/bin/sh
# Builds the simulator and writes one PNG per printer state into ./shots.
# With a screen size, e.g. `tools/shots.sh 480x320`, into ./shots/480x320 at that size.
set -e
cd "$(dirname "$0")/.."
pio run -e sim -s
out=shots${1:+/$1}
mkdir -p "$out"
.pio/build/sim/program --shots "$out" ${1:+--size "$1"}
python3 tools/bmp2png.py "$out"/*.bmp
echo "wrote $(ls "$out"/*.png | wc -l | tr -d ' ') screenshots to $out/"
