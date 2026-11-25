# Boards

One `.ini` file per board. `platformio.ini` reads every file in this folder, so a board is
added by adding a file; nothing else lists the boards.

| File | Board | Screen | Touch | Builds |
|---|---|---|---|---|
| `cyd-2432s028r.ini` | ESP32-2432S028R ("Cheap Yellow Display") | 2.8" 320x240 SPI | resistive, XPT2046 | `cyd-st7789`, `cyd-ili9341` |

## How the pieces fit

| Where | What it knows |
|---|---|
| `boards/<board>.ini` | Everything about one board: chip, pins, screen size, which drivers |
| `platformio.ini` | What all boards share, and one section per driver (library + flag) |
| `src/board.h` | Checks the board's flags are complete; fails the build with a hint when not |
| `src/drivers/` | One file per panel or touch driver, compiled only when its flag is set |
| `src/display.cpp` | Drivers to LVGL: draw buffer, touch with calibration, backlight, LED |
| `src/ui*.cpp` | The screens. They ask LVGL for the screen's size and scale themselves |

Nothing outside `boards/` and `src/drivers/` names a board, a chip or a pin.

## Adding a board that uses the drivers there are

For an SPI panel that TFT_eSPI supports, with XPT2046 resistive touch on its own SPI bus:

1. Copy `cyd-2432s028r.ini` to `<your-board>.ini` and rename its sections. Section names must
   be unique across the folder; `[env:...]` names are what `pio run -e` takes.
2. Set `board` and the partition table. The firmware needs two app slots of about 1.9 MB.
3. Set `SCREEN_W` and `SCREEN_H` to the screen as the UI should see it, in landscape, and
   `PANEL_ROTATION` to the TFT_eSPI rotation that puts it the right way up.
4. Set the panel's chip and pins with [TFT_eSPI's flags](https://github.com/Bodmer/TFT_eSPI/blob/master/User_Setup.h).
   `TFT_WIDTH` and `TFT_HEIGHT` are the panel's own, before rotation; the build checks them
   against `SCREEN_W` and `SCREEN_H`.
5. Set the touch pins and the `TOUCH_CAL_*` starting values. They only need to be roughly
   right: the panel calibrates on first boot.
6. `BACKLIGHT_PIN` and `LED_R`/`LED_G`/`LED_B` are optional. Leave them out on a board
   without a dimmable backlight or a status LED.
7. For a screen bigger than 320x240, turn on the text sizes it needs (below).
8. Look at the UI at that size before flashing: `tools/shots.sh 480x320`.
9. Add the new `[env:...]` to the firmware step in `.github/workflows/build.yml`, and the
   board to the table above and to the README.

### Flags a board's file sets

| Flag | Needed | Meaning |
|---|---|---|
| `SCREEN_W`, `SCREEN_H` | yes | The screen in pixels, as the UI sees it |
| `PANEL_ROTATION` | TFT_eSPI | Rotation 0-3 |
| `TFT_*`, `*_DRIVER`, `SPI_FREQUENCY` | TFT_eSPI | The library's own configuration |
| `TOUCH_CLK`, `TOUCH_MISO`, `TOUCH_MOSI`, `TOUCH_CS`, `TOUCH_IRQ` | XPT2046 | Pins |
| `TOUCH_ROTATION` | no (1) | The touch library's rotation |
| `TOUCH_CAL_X0`, `_X1`, `_Y0`, `_Y1` | resistive | Raw reading at the left, right, top, bottom edge |
| `BACKLIGHT_PIN` | no | PWM backlight |
| `LED_R`, `LED_G`, `LED_B`, `LED_ACTIVE_LOW` | no | Status LED; all three pins or none |
| `DRAW_BUF_ROWS` | no (`SCREEN_H / 10`) | Rows drawn at a time. The buffer is static RAM: `SCREEN_W` x rows x 2 bytes |
| `LV_FONT_MONTSERRAT_nn=1` | bigger screens | Text sizes, see below |

## Screen sizes

The screens are laid out in units, on a grid of 320 x 240 (`src/ui_internal.h`). A 320x240
panel has one pixel per unit. Any other screen gets as many pixels per unit as fit both ways,
and whatever is left over becomes extra width or height that the screens stretch into. So a new
size needs no layout work, as long as it is landscape.

| Screen | Pixels per unit | Text sizes to build in |
|---|---|---|
| 320x240 | 1 | 14, 20, 28 (always there) |
| 480x272 | 1.13 | 16, 22, 32 |
| 480x320 | 1.33 | 18, 26, 36 |
| 800x480 | 2 | 28, 40, 48 |

The UI wants text of 14, 20 and 28 units and takes the biggest built-in Montserrat size that
is not larger. Sizes that are not built in are not an error: the text is just smaller than it
could be. Add them in the board's `build_flags`, e.g. `-DLV_FONT_MONTSERRAT_18=1`. Each size
costs flash, the big ones most.

What does not come for free:

- **Portrait and square screens** run, but the layout is drawn for landscape: text gets small
  and the middle of the screen stays empty. They want a layout of their own in `ui.cpp`.
- **Icons** are drawn for one pixel per unit and stretched on other screens, which costs some
  sharpness. `tools/make_icons.py` can draw them at any size if that starts to matter.
- **Memory.** LVGL has 64 KB (`LV_MEM_SIZE` in `platformio.ini`), enough for the screens at any
  size tried in the simulator, which prints the peak with every screenshot. The draw buffer
  grows with the screen: 800x480 needs 77 KB of static RAM at the default, more than an ESP32
  without PSRAM has spare next to WiFi. Lower `DRAW_BUF_ROWS` there.

## Adding a driver

A board with other hardware (capacitive touch, a parallel or RGB panel, an ESP32-S3 with a
panel TFT_eSPI doesn't drive) needs a driver first:

1. Add `src/drivers/panel_<name>.cpp` or `touch_<name>.cpp`, wrapped in `#if PANEL_<NAME>` or
   `#if TOUCH_<NAME>`, with the functions in `src/drivers/drivers.h`: `panel_begin` and
   `panel_push`, or `touch_begin` and `touch_read`.
2. Add a section for it in `platformio.ini` next to `[panel_tft_espi]`, with its library and
   its `-DPANEL_<NAME>=1` flag.
3. Add the flag to the "exactly one" check in `src/board.h`. For capacitive touch, leave it
   out of `TOUCH_NEEDS_CALIBRATION` there: `touch_read` then returns screen pixels, the
   calibration screen never shows, and a touch at start-up goes straight to setup.

Not there yet, and each needs a small change outside the driver when its first board arrives:
touch on the same SPI bus as the panel, a backlight the panel driver controls (AMOLED), and a
draw buffer in PSRAM.
