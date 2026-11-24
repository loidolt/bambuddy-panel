# Bambuddy Panel

A small touch panel that sits next to a 3D printer and shows what
[Bambuddy](https://github.com/maziggy/bambuddy) knows about it, with the few actions you want
while standing there. No phone, no browser tab.

One panel per printer. It runs on the inexpensive ESP32-2432S028R ("Cheap Yellow Display") and
talks only to your Bambuddy server over WiFi, never to the printer or to any cloud, so it works
with every printer Bambuddy supports.

| Printing | Print finished | Outcome question | Maintenance |
|---|---|---|---|
| ![Printing](docs/screenshots/printing.png) | ![Finished](docs/screenshots/finished.png) | ![Outcome](docs/screenshots/verdict.png) | ![Maintenance](docs/screenshots/maintenance.png) |

*Screenshots are from the simulator, at the panel's real 320x240.*

> **Status: early.** Everything here builds, the logic is unit-tested and the screens run in the
> simulator, but the firmware has **not yet run on a real board**. Expect rough edges until this
> note goes away.

## What it does

The buttons follow the printer's state:

| Printer state | Buttons |
|---|---|
| Printing | Pause, Stop (hold), Light |
| Paused | Resume, Stop (hold), Light |
| Finished, and the print was started with *Ask for outcome* | Thumbs up, Thumbs down |
| Finished or failed, plate not yet cleared | Plate is clear (hold), Light |
| Error | Clear error, Resume queue, Light |
| Idle, next queue item waits for a manual start | Start (hold), Light |
| Idle | Light |

- **Hold** buttons need 1.5 seconds, so a stray touch can't stop a print or start one on a dirty plate.
- The **outcome question** comes before "plate is clear" on purpose: Bambuddy can be set to
  record an unanswered print as good when the plate is cleared.
- The **wrench** in the header opens the printer's maintenance tasks, most urgent first. It turns
  red with a count when tasks are due and amber when some are due soon. Hold **Done** to mark a
  task performed. The list returns to the status screen after 30 seconds without a touch.
- The **LED** on the back blinks when someone should come over: cyan for the outcome question,
  green for "clear the plate", yellow for paused, red for an error, purple when Bambuddy can't be
  reached. Solid blue while printing.

## Hardware

ESP32-2432S028R: ESP32-WROOM, 2.8" 320x240 display, resistive touch, no PSRAM. Sold under many
names. There are two variants and both are supported:

| Variant | Build |
|---|---|
| Two USB ports (ST7789 display) | `cyd-st7789` (default) |
| One USB port (ILI9341 display) | `cyd-ili9341` |

Power it from any 5 V USB supply.

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

If the picture is wrong on first boot, change one flag in `platformio.ini` under your variant:

| Symptom | Change |
|---|---|
| Red and blue swapped | `TFT_RGB_ORDER=TFT_BGR` to `TFT_RGB` |
| Colours look like a negative | `TFT_INVERSION_OFF` to `TFT_INVERSION_ON` |
| Touch lands in the wrong place | Recalibrate (see below) |

## Set up a panel

1. In Bambuddy, Settings > API keys, create a key for this panel, limited to this one printer:

   | Permission | Needed for |
   |---|---|
   | Read status | Everything shown |
   | Control printer | Pause, resume, stop, plate is clear, light, clear error |
   | Queue | Start the next job, resume the queue |
   | Manage archives (optional) | Answering the outcome question |
   | Manage maintenance (optional) | Marking maintenance tasks done |

   Without an optional permission its button answers "Not allowed (API key)"; the rest works.
2. Find the printer's id (`GET /api/v1/printers/`).
3. Power the panel. A new panel first asks you to tap four crosses (touch calibration), then
   opens a WiFi network called `BambuddyPanel-Setup`. Join it, open `192.168.4.1`, and enter your
   WiFi, the Bambuddy URL (`http://host:port`), the API key and the printer id.

To change settings later, touch the screen while it says "Starting":

- let go when asked: WiFi and Bambuddy setup
- keep holding about four seconds: touch calibration

**Security.** The key is stored on the device and sent over plain HTTP on your LAN, and the
setup network is open while it is up. Keep the key limited to one printer and to the permissions
above, so a leaked key can do little.

## Simulator

The same `src/ui.cpp` runs on the desktop in a 320x240 window (shown at 2x), so screens can be
worked on without flashing a board. Needs SDL2 (macOS: `brew install sdl2`; Debian/Ubuntu:
`apt install libsdl2-dev`).

```bash
pio run -e sim -t exec
```

Left/Right switch between made-up printer states (`src/sim/scenarios.cpp`), the mouse is the
finger, Q quits. Buttons change the made-up state; nothing is sent anywhere. The window title
shows the state name and the LED colour.

A PNG of every state, into `shots/`:

```bash
./tools/shots.sh
```

A real printer, read-only (the buttons do nothing):

```bash
BAMBUDDY_URL=http://host:3700 BAMBUDDY_KEY=... BAMBUDDY_PRINTER=1 .pio/build/sim/program --live
```

The simulator shows layout, fonts, text cut-off and 16-bit colour faithfully. It does not show
the real panel's weaker contrast and viewing angle, the imprecision of resistive touch, or
redraw speed.

## Tests

```bash
pio test -e test
```

Covers the parts with no hardware in them: reading Bambuddy's JSON (status, queue, outcome
question, maintenance) and the touch calibration maths.

## Updates over WiFi

Off unless built with a password. Put it in `platformio_override.ini` (ignored by git) and add
`extra_configs = platformio_override.ini` under `[platformio]`:

```ini
[env:cyd-st7789]
build_flags = ${esp32.build_flags} -DST7789_DRIVER=1 -DTFT_RGB_ORDER=TFT_BGR -DTFT_INVERSION_OFF=1 -DOTA_PASSWORD=\"change-me\"
upload_protocol = espota
upload_port = bambuddy-panel-1.local
upload_flags = --auth=change-me
```

## How it talks to Bambuddy

REST only, with the `X-API-Key` header. Written against Bambuddy 1.3.1.

| When | Request |
|---|---|
| Every ~4 s | `GET /api/v1/printers/{id}/status` |
| Every ~4 s | `GET /api/v1/queue/?status=pending&printer_id={id}` |
| Every ~4 s, not while printing | `GET /api/v1/archives/last-per-printer` |
| Every 60 s | `GET /api/v1/maintenance/printers/{id}` |
| Pause, resume, stop | `POST /api/v1/printers/{id}/print/pause` · `resume` · `stop` |
| Plate is clear | `POST /api/v1/printers/{id}/clear-plate` |
| Light | `POST /api/v1/printers/{id}/chamber-light?on=` |
| Clear error | `POST /api/v1/printers/{id}/hms/clear` |
| Start next | `POST /api/v1/queue/{item}/start` |
| Resume queue | `POST /api/v1/queue/printer/{id}/resume` |
| Thumbs | `PATCH /api/v1/archives/{id}` with `user_verdict` |
| Task done | `POST /api/v1/maintenance/items/{id}/perform` |

Responses are read through a field filter straight off the socket, so the panel never holds a
whole status document in memory.

## Layout

| Path | Job |
|---|---|
| `src/main.cpp` | Boot, WiFi and setup portal, main loop |
| `src/bambuddy.*` | Background task: polls Bambuddy, sends commands |
| `src/bambuddy_parse.*` | JSON to state; shared with the simulator and the tests |
| `src/ui.*` | The screens; picks buttons from the printer state |
| `src/display.*` | Screen, touch, backlight, LED |
| `src/calibrate.*`, `src/touch_cal.*` | Touch calibration: the screen flow, and the maths |
| `src/settings.*` | Settings kept in flash |
| `src/icons.*` | Icons, made by `tools/make_icons.py` from the SVGs in `tools/icons/` |
| `src/board.h` | Pins |
| `src/sim/` | Desktop simulator and its made-up states |
| `test/` | Unit tests |

## Not done yet

- First run on real hardware, and fixing what that turns up.
- Lock or PIN on the controls.
- Queue list screen; backlight dimming from the light sensor.
- Prebuilt firmware and a browser flasher, so nobody needs PlatformIO to try it.
- A printable case.

## License

MIT, see [LICENSE](LICENSE). Icons and libraries: [THIRD_PARTY.md](THIRD_PARTY.md).

Bambuddy Panel is an independent community project. It is not part of, or endorsed by, Bambuddy
or Bambu Lab.
