# Bambuddy Panel

A small touch panel that sits next to a 3D printer and shows what
[Bambuddy](https://github.com/maziggy/bambuddy) knows about it, with the few actions you want
while standing there. No phone, no browser tab.

One panel per printer. It runs on the inexpensive ESP32-2432S028R ("Cheap Yellow Display") and
talks only to your Bambuddy server over WiFi, never to the printer or to any cloud, so it works
with every printer Bambuddy supports.

![A minute with the panel: starting a queued job, the print, the outcome question, clearing the plate, assigning a spool, maintenance, an error](docs/demo.gif)

*A minute with the panel, from the simulator: the panel's own screens, a made-up printer. The
dot is a finger, the lamp under the screen is the LED on the board's back.*

| Printing | Print finished | Outcome question | Maintenance |
|---|---|---|---|
| ![Printing](docs/screenshots/printing.png) | ![Finished](docs/screenshots/finished.png) | ![Outcome](docs/screenshots/verdict.png) | ![Maintenance](docs/screenshots/maintenance.png) |

*Screenshots are from the simulator, at the panel's real 320x240.*

> **Status: early.** It runs on one board, the ESP32-2432S028R with the ST7789 display
> (`cyd-st7789`): display, touch calibration, WiFi and Bambuddy setup, status, and the filament
> slots of a real printer. The buttons' commands (pause, stop, plate is clear, start and the
> rest) are unit-tested against Bambuddy's API as documented, but **have not all been pressed
> against a real Bambuddy yet**, and no other board has been flashed. Expect rough edges until
> this note goes away.

## What it does

The panel offers what only Bambuddy can do, and the few printer controls that are slow to reach
on a printer's own screen:

- Pause, resume and stop a print.
- **Plate is clear**, so Bambuddy starts the next job in the queue.
- Bambuddy's *Ask for outcome* question, with a thumbs up or down.
- Start a job that waits for a manual start; resume a queue held after a failed print.
- The printer's error in words, and clearing it.
- The filament slots, and assigning a spool of Bambuddy's inventory to one.
- Maintenance tasks, most urgent first, and marking them done.
- An LED on the back that blinks when someone should come over.

Anything that starts a print or can't be undone needs a 1.5 second hold, so a stray touch can't
stop a print or start one on a dirty plate. [Every screen and button](https://loidolt.github.io/bambuddy-panel/using/).

## Documentation

**[loidolt.github.io/bambuddy-panel](https://loidolt.github.io/bambuddy-panel/)**

| | |
|---|---|
| [Get started](https://loidolt.github.io/bambuddy-panel/get-started/) | What to buy, flashing, the API key and its permissions, setup |
| [Using the panel](https://loidolt.github.io/bambuddy-panel/using/) | What each screen shows and what its buttons do |
| [Updating](https://loidolt.github.io/bambuddy-panel/updating/) | A new release onto a panel, without USB |
| [Boards](https://loidolt.github.io/bambuddy-panel/boards/) | Which boards have a build, and which have been tried |
| [Troubleshooting](https://loidolt.github.io/bambuddy-panel/troubleshooting/) | A wrong picture, a touch that is off, no answer from Bambuddy |
| [Security](https://loidolt.github.io/bambuddy-panel/security/) | Where the API key is, and who can change what |
| [How it talks to Bambuddy](https://loidolt.github.io/bambuddy-panel/api/) | Every request it makes |
| [Development](https://loidolt.github.io/bambuddy-panel/development/) | Building, the simulator, the tests, the layout of the source |

The pages are the Markdown files in [docs/](docs/), so they can be read here as well.

## Quick start

1. Get an ESP32-2432S028R ("Cheap Yellow Display": ESP32, 2.8" 320x240 resistive touch) and a
   5 V USB supply. [Other boards](https://loidolt.github.io/bambuddy-panel/boards/) have builds too, none of them tried yet.
2. Write a release's firmware onto it. `cyd-st7789` is for the board with the ST7789 display
   (often the one with two USB ports), `cyd-ili9341` for the other:

   ```bash
   esptool.py --chip esp32 write_flash 0x0 bambuddy-panel-0.1.0-cyd-st7789-full.bin
   ```

3. In Bambuddy, create an API key with **Read Status**, **Control Printer** and **Manage
   Queue**; the [optional ones](https://loidolt.github.io/bambuddy-panel/get-started/#set-up-a-panel) are for the outcome
   question, maintenance and spools.
4. Power the panel. Tap the four crosses, scan the QR code it shows and choose your WiFi.
5. Scan the second QR code, and enter the Bambuddy URL (`http://host:port`) and the key. The
   page lists the printers the key may see: pick this panel's.

Later releases go on [without USB](https://loidolt.github.io/bambuddy-panel/updating/), from the panel's settings page.

## Development

Needs [PlatformIO](https://platformio.org/install/cli); the simulator also needs SDL2.

```bash
pio run -t upload
```

```bash
pio test -e test
```

```bash
pio run -e sim -t exec
```

The simulator runs the panel's own screens in a window on the desktop, with made-up printer
states or a real Bambuddy read-only, and makes the screenshots and the film on this page.
[More](https://loidolt.github.io/bambuddy-panel/development/). To add a board: [boards/README.md](boards/README.md).

## License

MIT, see [LICENSE](LICENSE). Icons and libraries: [THIRD_PARTY.md](THIRD_PARTY.md).

Bambuddy Panel is an independent community project. It is not part of, or endorsed by, Bambuddy
or Bambu Lab.
