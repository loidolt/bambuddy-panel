# Development

## Build and flash

Install [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/index.html). CI
uses PlatformIO 6.2.0. Clone the panel repository and run the commands from its root.

```bash
git clone https://github.com/loidolt/bambuddy-panel.git
cd bambuddy-panel
pio run -e cyd-st7789
```

Replace `cyd-st7789` with the [profile for your board](boards.md#available-profiles). Connect the
board with a USB data cable and upload the build. The serial monitor runs at 115200 baud.

```bash
pio run -e cyd-st7789 -t upload
pio device monitor -b 115200
```

If more than one board is attached, use `pio device list` to identify the port and add
`--upload-port PORT` to the upload command. Once the board boots,
[complete setup](get-started.md#set-up-a-panel). For an existing panel, you can also install the
built application from `.pio/build/<profile>/firmware.bin` through its [settings page](updating.md).

## Simulator

The desktop simulator runs the panel UI with SDL2 and uses libcurl for live reads. On macOS, install
SDL2 with `brew install sdl2`. On Debian or Ubuntu, install `libsdl2-dev` and
`libcurl4-openssl-dev`.

```bash
pio run -e sim -t exec
```

The default window shows a 320×240 panel at 2× scale. Use the left and right arrows to change sample
printer states, click or hold with the mouse, and press **Q** to quit. Button presses update the
simulated state and print the request to the terminal.

To view a real Bambuddy printer, set `BAMBUDDY_URL` to the HTTP server address, `BAMBUDDY_KEY` to a
read-only API key, and `BAMBUDDY_PRINTER` to the numeric printer ID in your environment. Start the
built simulator in live mode.

```bash
.pio/build/sim/program --live
```

Live mode reads status and suppresses writes. Both modes accept a screen size, for example
`--size 480x320`. Use hardware to check actual display colours, touch accuracy, WiFi and update
behaviour.

### Screenshots and demo

| Command | Output |
|---|---|
| `./tools/shots.sh` | Screens at 320×240 in `shots/`, touch target overlays in `shots/targets/` and layout findings in `shots/checks.txt` |
| `./tools/shots.sh 480x320` | Screenshots at that size in `shots/480x320/` |
| `./tools/shots.sh all` | Both board resolutions, settings-page examples and a gallery at `shots/index.html` |
| `./tools/demo.sh --check` | Exercises the scripted interactions used in the walkthrough |
| `./tools/demo.sh` | Regenerates `docs/demo.gif`, `docs/demo.mp4` and `docs/demo.webm` with FFmpeg |

## Tests

```bash
pio test -e test
```

The native suite tests API parsing, command construction, polling, screen and button selection,
spool handling, touch calibration and firmware image checks. It runs with AddressSanitizer and
UndefinedBehaviorSanitizer.

CI builds and packages every physical profile, rebuilds the source bundle with a custom ArduinoOTA
profile, checks generated icons, runs the simulator tour and compares screenshots with the reference
images. The comparisons include the larger [documented screen sizes](boards.md#screen-sizes). A
version tag such as `v0.1.0` creates a draft GitHub release with application and full images, a
checksum list, third-party notices and a rebuildable source archive.

## This site

Edit the Markdown pages in `docs/` and the navigation in `mkdocs.yml`. From the repository root,
install the pinned Material for MkDocs dependency in a Python virtual environment. Preview locally
or run the strict build used by CI.

```bash
python -m pip install -r requirements-docs.txt
mkdocs build --strict
mkdocs serve
```

Open the local address printed by `mkdocs serve`. The
[documentation workflow](https://github.com/loidolt/bambuddy-panel/blob/main/.github/workflows/docs.yml)
publishes changes from public `main` to GitHub Pages.

## Pushed updates over WiFi

Released firmware supports uploads through the settings page. To push development builds directly
from PlatformIO, create a custom environment in `platformio_override.ini` (ignored by Git). Extend
the profile for your physical board. Set the same private password, at least eight characters, in
both places below.

```ini
[env:my-panel]
extends = env:cyd-st7789
build_flags = ${env:cyd-st7789.build_flags} -DOTA_PASSWORD=\"replace-with-your-password\"
upload_protocol = espota
upload_port = bambuddy-panel-1.local
upload_flags = --auth=replace-with-your-password
```

1. Run `pio run -e my-panel`. Install `.pio/build/my-panel/firmware.bin` once through the panel’s
   settings page. The custom build inherits the physical board identifier.
2. Set `upload_port` to the panel’s IP address or `bambuddy-panel-<printer_id>.local`. The example
   uses printer 1.
3. Run `pio run -e my-panel -t upload` for subsequent uploads.

ArduinoOTA embeds the password in the firmware. The transfer is unencrypted and images are unsigned.
Keep this build on your trusted network.

## Source layout

| Area | Files |
|---|---|
| Boot, configuration and browser setup | `src/main.cpp`, `src/settings.*`, `src/setup_web.cpp`, `src/setup_page.*`, `src/settings_http.*` |
| HTTP requests, parsing and polling | `src/bambuddy.*`, `src/bambuddy_api.*`, `src/bambuddy_parse.*`, `src/poller.*`, `src/spools.*` |
| Screens, controls and layout | `src/ui*.cpp`, `src/ui_logic.*` |
| Hardware and touch calibration | `boards/`, `src/board.h`, `src/drivers/`, `src/display.*`, `src/calibrate.*`, `src/touch_cal.*` |
| Firmware identity and upload checks | `src/version.h`, `src/fw_check.*` |
| Simulator, tests and build profiles | `src/sim/`, `test/`, `platformio.ini` |

For patches and hardware reports, see the
[contribution guide](https://github.com/loidolt/bambuddy-panel/blob/main/CONTRIBUTING.md). The
[board contributor guide](https://github.com/loidolt/bambuddy-panel/blob/main/boards/README.md)
covers new profiles and drivers.
