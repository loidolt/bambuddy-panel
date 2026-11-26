#!/bin/sh
# Plays the simulator's scripted tour of the panel (src/sim/sim_main.cpp, run_demo) and makes
# the films of it: docs/demo.gif for the README, docs/demo.mp4 and docs/demo.webm for a web page.
# Needs ffmpeg. The tour runs on a clock of its own, so every run gives the same frames.
#
#   tools/demo.sh             writes docs/demo.gif, docs/demo.mp4 and docs/demo.webm
#   tools/demo.sh --check     only plays the tour, to see that every button it presses is there
set -e
cd "$(dirname "$0")/.."
pio run -e sim -s

work=shots/demo
mkdir -p "$work"
.pio/build/sim/program --demo "$work" > "$work/run.log" || { tail -5 "$work/run.log"; exit 1; }
tail -1 "$work/run.log"
if [ "$1" = --check ]; then
  rm -f "$work/frames.rgb"
  exit 0
fi

# "1140 frames 344x286 at 20 fps in ..."
set -- $(tail -1 "$work/run.log")
size=$3
fps=$5
in="-loglevel error -y -f rawvideo -pixel_format rgb24 -video_size $size -framerate $fps -i $work/frames.rgb"

# Twice the size, with the pixels kept square-edged: it is a picture of a small screen.
big="scale=iw*2:ih*2:flags=neighbor"

# The GIF with one palette for the whole film and only the changed rectangle stored per frame;
# the screen mostly stands still, so that keeps it small.
ffmpeg $in -vf "$big,split[a][b];[a]palettegen=max_colors=128:stats_mode=diff[p];[b][p]paletteuse=dither=none:diff_mode=rectangle" docs/demo.gif
ffmpeg $in -vf "$big" -c:v libx264 -preset slow -crf 20 -pix_fmt yuv420p -movflags +faststart docs/demo.mp4
ffmpeg $in -vf "$big" -c:v libvpx-vp9 -crf 34 -b:v 0 -pix_fmt yuv420p docs/demo.webm
rm -f "$work/frames.rgb"
ls -l docs/demo.gif docs/demo.mp4 docs/demo.webm | awk '{printf "%s  %d KB\n", $NF, $5 / 1024}'
