# Development

The source is at [github.com/loidolt/bambuddy-panel](https://github.com/loidolt/bambuddy-panel).

## Build and flash

Needs [PlatformIO](https://platformio.org/install/cli).

```bash
pio run -t upload
```

```bash
pio run -e cyd-ili9341 -t upload
```

```bash
pio device monitor
```

The second line is for another board: the build names are in [Boards](boards.md), where there
is also what to change [if the picture or the touch is wrong](boards.md#if-the-picture-or-the-touch-is-wrong).

The serial monitor shows the firmware's version at start-up (`[boot] ...`) and, once a minute,
the version, the free memory and the background task's spare stack (`[health] ...`), which
helps when reporting a problem. The settings page has the version at its foot. A build of your
own is version `dev`.

## Simulator

The panel's screens, its JSON reading, its requests and its polling round run on the desktop in
a window the size of the panel's screen (320x240, shown at 2x), so screens can be worked on
without flashing a board. Needs SDL2
(macOS: `brew install sdl2`; Debian/Ubuntu: `apt install libsdl2-dev`).

```bash
pio run -e sim -t exec
```

Left/Right switch between made-up printer states (`src/sim/scenarios.cpp`), the mouse is the
finger, Q quits. Buttons change the made-up state and print the request the panel would send;
nothing is sent anywhere. The window title shows the state name and the LED colour.

A PNG of every screen the panel can show, into `shots/`: each state, the maintenance lists,
pressed and held buttons, every toast, and the start-up, setup, offline and calibration screens.
`shots/targets/` has the same pictures with each touch target framed and measured, and
`shots/checks.txt` lists text that was cut off and targets under 7 mm on the smallest panel
with that resolution:

```bash
./tools/shots.sh
```

All of that for every screen size a board has, plus the settings web page in each of its
states, on one page to scroll through (`shots/index.html`):

```bash
./tools/shots.sh all
```

The film on the [home page](index.md), again after the screens have changed (needs `ffmpeg`). It
is a scripted tour, `run_demo` in `src/sim/sim_main.cpp`, on a clock of its own, so it comes out
the same every time; it writes `docs/demo.gif`, and `docs/demo.mp4` and `docs/demo.webm` for
web pages:

```bash
./tools/demo.sh
```

Another screen size, for a board that isn't 320x240. `--size` works with the window and with
`--live` too:

```bash
./tools/shots.sh 480x320
```

```bash
.pio/build/sim/program --size 480x320
```

A real printer, read-only (the buttons send nothing):

```bash
BAMBUDDY_URL=http://host:3700 BAMBUDDY_KEY=... BAMBUDDY_PRINTER=1 .pio/build/sim/program --live
```

The simulator shows layout, fonts, text cut-off and 16-bit colour faithfully, and runs LVGL with
the panel's memory size and strip-by-strip drawing. It stands in for the display and touch
drivers, the LED, HTTP (curl instead of the ESP32's client, in the window's own thread) and, in
made-up mode, Bambuddy itself. It does not show the real panel's weaker contrast and viewing
angle, the imprecision of resistive touch, redraw speed, the boot and setup screens' timing, or
anything about WiFi.

## Tests

```bash
pio test -e test
```

Covers the parts with no hardware in them: reading Bambuddy's JSON (status, queue, outcome
question, maintenance, settings), the requests each button sends, the polling round, which
screen and buttons each printer state gets, the text on screen, and the touch calibration maths.

The tests run with AddressSanitizer and UBSan, so a write past the end of a text field fails
them.

CI runs the tests, builds the firmware for every board (also with updates over WiFi turned on),
checks that `src/icons.c` and the screenshots in `docs/screenshots/` are up to date, and draws
the screens at three more sizes (480x320, 480x272, 800x480), which shows that nothing crashes
there and every button the screenshots press still exists. Of those sizes only the two
pictures each that [Boards](boards.md#screen-sizes) shows are compared; the rest nobody but you
looks at. A warning in the
panel's own code (`src/`) fails every build; the test file itself is compiled without that.

A tag like `v0.1.0` builds every board as that version and puts the firmware files and the
license texts into a draft release (`.github/workflows/release.yml`), to be looked over and
published by hand.

## This site

The pages are the Markdown files in `docs/`, built with [MkDocs](https://www.mkdocs.org/) and
[Material for MkDocs](https://squidfunk.github.io/mkdocs-material/). To look at them locally:

```bash
pip install -r requirements-docs.txt
```

```bash
mkdocs serve
```

CI builds the site with `--strict`, so a link to a page or heading that isn't there fails, and
publishes it when `main` changes.

## Pushed updates over WiFi

This is for development: pushing a build from PlatformIO. To update a panel with a release, see
[Updating](updating.md). Off unless built with a password of at least 8 characters (the build fails on a shorter one).
Put it in `platformio_override.ini` (ignored by git, read when it is there), as a build of your
own on top of your board's:

```ini
[env:my-panel]
extends = env:cyd-st7789
build_flags = ${env:cyd-st7789.build_flags} -DOTA_PASSWORD=\"change-me-please\"
upload_protocol = espota
upload_port = bambuddy-panel-1.local
upload_flags = --auth=change-me-please
```

```bash
pio run -e my-panel -t upload
```

The password is checked with a challenge, so it doesn't cross the network, but the firmware
itself is neither encrypted nor signed, and the password is in the firmware file.

## Layout

| Path | Job |
|---|---|
| `src/main.cpp` | Boot, WiFi and setup portal, main loop |
| `src/bambuddy.*` | Background task: HTTP, polls Bambuddy, sends commands |
| `src/poller.*` | One round of reading Bambuddy; shared with the simulator and the tests |
| `src/bambuddy_api.*` | The requests each command sends, and the wording of results |
| `src/bambuddy_parse.*` | JSON to state |
| `src/ui_logic.*` | Which screen and buttons each state gets, and the text on it |
| `src/ui.cpp`, `src/ui_tasks.cpp` | Drawing: the status, message and calibration screens; the maintenance screen |
| `src/ui_layout.cpp` | Fits the screens to the panel's size: the unit grid and the text sizes |
| `src/display.*` | The board's drivers tied to LVGL: drawing, touch, backlight, LED |
| `src/drivers/` | One file per panel or touch chip; the board picks |
| `src/calibrate.*`, `src/touch_cal.*` | Touch calibration: the screen flow, and the maths |
| `src/settings.*` | Settings kept in flash |
| `src/setup_web.cpp`, `src/setup_page.*` | The settings page: the web server with the code check and the firmware upload; the page's text |
| `src/fw_check.*` | Checks an uploaded firmware file: an application, and built for this board |
| `src/icons.*` | Icons, made by `tools/make_icons.py` from the SVGs in `tools/icons/` |
| `src/board.h` | Checks the board's description is complete |
| `src/version.h` | The firmware's version: `dev`, or the tag a release was built from |
| `boards/` | One file per board: chip, pins, screen size, drivers. [How to add one](https://github.com/loidolt/bambuddy-panel/blob/main/boards/README.md) |
| `platformio.ini` | What every build shares; the simulator and test builds |
| `src/sim/` | Desktop simulator and its made-up states |
| `test/` | Unit tests |
