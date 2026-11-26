#!/bin/sh
# Builds the simulator and writes a PNG of every screen the panel can show into ./shots, the
# same pictures with the touch targets framed into ./shots/targets, and what the simulator's
# checks found (text cut off, buttons too small) into ./shots/checks.txt.
#
#   tools/shots.sh            320x240, into ./shots
#   tools/shots.sh 480x320    that size, into ./shots/480x320
#   tools/shots.sh all        every screen size a board has, the settings web page in each of
#                             its states (./shots/web), and ./shots/index.html to look at it all
set -e
cd "$(dirname "$0")/.."
pio run -e sim -s

render() {
  out=shots${1:+/$1}
  mkdir -p "$out/targets"
  rm -f "$out"/*.png "$out"/*.bmp "$out"/targets/*.png "$out"/targets/*.bmp "$out"/checks.txt
  .pio/build/sim/program --shots "$out" ${1:+--size "$1"} > "$out/run.log" || { tail -5 "$out/run.log"; exit 1; }
  python3 tools/bmp2png.py "$out"/*.bmp "$out"/targets/*.bmp
  echo "wrote $(ls "$out"/*.png | wc -l | tr -d ' ') screenshots to $out/; $(tail -1 "$out/run.log")"
}

if [ "$1" = all ]; then
  render
  render 480x320
  mkdir -p shots/web
  rm -f shots/web/[0-9]-*.html
  .pio/build/sim/program --web shots/web > /dev/null
  python3 tools/sheet.py shots
else
  render "$1"
fi
