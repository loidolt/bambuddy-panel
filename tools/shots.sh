#!/bin/sh
# Builds the simulator and writes one PNG per printer state into ./shots.
set -e
cd "$(dirname "$0")/.."
pio run -e sim -s
mkdir -p shots
.pio/build/sim/program --shots shots
python3 tools/bmp2png.py shots/*.bmp
echo "wrote $(ls shots/*.png | wc -l | tr -d ' ') screenshots to shots/"
