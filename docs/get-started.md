# Get started

A board, a firmware file, an API key from Bambuddy, and a few minutes at the panel.

## What you need

Written for the ESP32-2432S028R: ESP32-WROOM, 2.8" 320x240 display, resistive touch, no PSRAM.
Sold under many names. Two display chips are common and both are supported:

| Variant | Build |
|---|---|
| ST7789 display (often the board with two USB ports) | `cyd-st7789` (default); the one build that has run on its board |
| ILI9341 display (often the board with one USB port) | `cyd-ili9341`; not flashed yet |

The USB ports are only a hint; sellers mix them. If the picture stays white or garbled, try the
other build.

Power it from any 5 V USB supply.

Its relatives with 2.4", 3.2" and 3.5" screens have builds too, none of them tried on its board
yet: see [Boards](boards.md).

You also need a USB cable that carries data, and a running Bambuddy that the panel can reach
over WiFi by `http://`.

## Flash the firmware

The [releases](https://github.com/loidolt/bambuddy-panel/releases) have a firmware file for
every build in the [list of boards](boards.md). For a board that doesn't run Bambuddy Panel yet, take your
build's `-full.bin` and write it at address 0x0, with [esptool](https://docs.espressif.com/projects/esptool/)
(`pip install esptool`):

```bash
esptool.py --chip esp32 write_flash 0x0 bambuddy-panel-0.1.0-cyd-st7789-full.bin
```

Any other ESP32 flashing tool does as well, as long as it writes the file at 0x0. A `-full.bin`
also clears the panel's settings and touch calibration.

To build it yourself instead, see [Development](development.md#build-and-flash). For a later
release, see [Updating](updating.md): that needs no USB.

## Set up a panel

1. In Bambuddy, Settings > API keys, create a key for this panel. Bambuddy ticks most
    permissions for a new key; untick everything the table doesn't list.

    | Permission | Needed for |
    |---|---|
    | Read Status | Everything shown |
    | Control Printer | Pause, resume, stop, plate is clear, clear error |
    | Manage Queue | Start the next job, resume the queue |
    | Manage Archives (optional) | Answering the outcome question |
    | Manage Maintenance (optional) | Marking maintenance tasks done |
    | Manage Inventory (optional) | Assigning a spool to a slot, or taking it out |

    A key never does more than the user who owns it may do. In particular:

    - **Resume queue** needs an owner who may manage *all* queue jobs.
    - **Start** works on the owner's own jobs, or on anyone's if the owner may manage all queue
      jobs. Jobs waiting for review need someone who may start unreviewed jobs.
    - **Answering the outcome question** works on the owner's own prints, or on anyone's if the
      owner may edit all archives.

    Without a permission the panel says which one the key lacks; the rest works.

    Bambuddy's settings page can't limit a key to one printer (1.3.x); its API can, with
    `PATCH /api/v1/api-keys/{key_id}` and the body `{"printer_ids": [<printer id>]}`.
2. You don't need to look up the printer's id: the panel's settings page lists the printers
    the key may see. (By hand: `GET /api/v1/printers/` lists each printer with its `id`.)
3. Power the panel. A new panel first asks you to tap four crosses (touch calibration), then
    opens the setup network `BambuddyPanel-Setup` and shows a QR code for it, with its name and
    password beside it. Scan the code with a phone, or join the network by hand, and choose
    your WiFi in the page that opens (or open `192.168.4.1`). That page asks for the WiFi only.
4. Once it's on your WiFi, the panel shows a second QR code, with its address and a code beside
    it, e.g. `192.168.1.80` and `abcd-efgh`. Scan it, or open that address in any browser on the
    same network and type the code. Enter the Bambuddy URL (`http://host:port`; https isn't
    supported) and the API key. With those in, the page tries the key with Bambuddy, without
    saving, and turns the printer field into a list of the printers the key may see. The page
    also explains which permissions the key needs. It's an ordinary web page, so you can switch
    away to copy the key and come back. **Save** checks the settings with Bambuddy straight away
    and says what's wrong if it doesn't accept them (or, if Bambuddy is only down for now, **Use
    these settings anyway**).

To change settings later, open the maintenance screen (the wrench) and hold the **gear**. The
panel restarts into its settings screen, with a new code. From there:

- the settings page again (leave the key blank to keep the saved one; with a new Bambuddy URL
  the key has to be entered again). It has a **Change WiFi** button, which forgets the WiFi and
  restarts into the setup network. If the saved WiFi can't be joined, the setup network opens
  instead.
- **Calibrate touch**, on panels with resistive touch
- **Cancel**, back to normal running with nothing changed

When Bambuddy refuses the key or can't be reached for five minutes, the screen that says so
has an **Open settings** button that does the same.

If the touch is too far off to press anything, touch the screen while it says "Starting", just
after power-on: let go when asked for the settings screen, or keep holding about four seconds
for touch calibration.

The settings page and the setup network only open on a new panel or when asked for in one of
these ways. If the WiFi is down when a panel that's set up starts, it says so and keeps trying.
