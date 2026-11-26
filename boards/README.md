# Boards

One `.ini` file per board. `platformio.ini` reads every file in this folder, so a board is
added by adding a file; nothing else lists the boards.

| File | Board | Screen | Touch | Builds |
|---|---|---|---|---|
| `cyd-2432s028r.ini` | ESP32-2432S028R ("Cheap Yellow Display") | 2.8" 320x240, ST7789 or ILI9341 | resistive, XPT2046 | `cyd-st7789`, `cyd-ili9341` |
| `esp32-2432s024.ini` | ESP32-2432S024R / C | 2.4" 320x240, ILI9341 | resistive XPT2046 / capacitive CST820 | `esp32-2432s024r`, `esp32-2432s024c` |
| `esp32-2432s032.ini` | ESP32-2432S032R / C | 3.2" 320x240, ST7789 IPS | resistive XPT2046 / capacitive GT911 | `esp32-2432s032r`, `esp32-2432s032c` |
| `esp32-3248s035.ini` | ESP32-3248S035R / C | 3.5" 480x320, ST7796 | resistive XPT2046 / capacitive GT911 | `esp32-3248s035r`, `esp32-3248s035c` |
| `jc2432w328.ini` | Guition JC2432W328C | 2.8" 320x240, ST7789 IPS | capacitive CST820 | `jc2432w328c` |
| `wt32-sc01.ini` | Wireless-Tag WT32-SC01 | 3.5" 480x320, ST7796 | capacitive FT6336 | `wt32-sc01` |

All are an ESP32 with an SPI panel, so they share one panel driver.

### Only `cyd-st7789` has run on the real board yet

That one works: picture, touch calibration, backlight, setup. Every other profile builds, and
the simulator draws every screen at both sizes without failing (at 320x240 the pictures are
compared with the ones in `docs/screenshots/`; at 480x320 nobody but you looks at them), but nobody has flashed
one. The pins are taken from board definitions other projects use on these boards
([rzeldent/platformio-espressif32-sunton](https://github.com/rzeldent/platformio-espressif32-sunton)
and [openHASP](https://github.com/HASwitchPlate/openHASP)); what those can't settle is how this
firmware's drivers behave on them. If you have one, flash it and report what you see. What to
expect to need, most likely first:

| Symptom | Change in the board's file |
|---|---|
| Red and blue swapped | `TFT_RGB_ORDER=TFT_BGR` to `TFT_RGB`, or add it |
| Colours look like a negative | `TFT_INVERSION_ON` to `TFT_INVERSION_OFF`, or the other way |
| Picture upside down | `PANEL_ROTATION=1` to `3` (capacitive: also swap the touch mirror, as the file says; resistive: the panel asks for a new touch calibration at the next start, since the old one belongs to the other rotation) |
| Capacitive touch mirrored left-right or top-bottom | flip `TOUCH_MIRROR_X` or `TOUCH_MIRROR_Y` |
| Capacitive touch dead | the chip's address: `-DTOUCH_I2C_ADDR=0x..` (CST820, FT6336) |
| Resistive touch lands in the wrong place | recalibrate: hold the screen at start-up |
| White or garbled picture on an ILI9341 board | `ILI9341_2_DRIVER` to `ILI9341_DRIVER` |

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

For an SPI panel that TFT_eSPI supports, with one of the touch chips there is a driver for:

1. Copy the file of the board most like yours to `<your-board>.ini` and rename its sections. Section names must
   be unique across the folder; `[env:...]` names are what `pio run -e` takes.
2. Set `board` and the partition table. The firmware needs two app slots of about 1.9 MB.
3. Set `SCREEN_W` and `SCREEN_H` to the screen as the UI should see it, in landscape, and
   `PANEL_ROTATION` to the TFT_eSPI rotation that puts it the right way up.
4. Set the panel's chip and pins with [TFT_eSPI's flags](https://github.com/Bodmer/TFT_eSPI/blob/master/User_Setup.h).
   `TFT_WIDTH` and `TFT_HEIGHT` are the panel's own, before rotation; the build checks them
   against `SCREEN_W` and `SCREEN_H`.
5. Pull in the touch driver and set its pins. Resistive: the `TOUCH_CAL_*` starting values
   only need to be roughly right, since the panel calibrates on first boot. Capacitive: say
   how the touch grid lies on the screen with `TOUCH_SWAP_XY`, `TOUCH_MIRROR_X` and
   `TOUCH_MIRROR_Y`.
6. `BACKLIGHT_PIN` and `LED_R`/`LED_G`/`LED_B` are optional. Leave them out on a board
   without a dimmable backlight or a status LED.
7. For a screen bigger than 320x240, turn on the text sizes it needs (below).
8. Look at the UI at that size before flashing: `tools/shots.sh 480x320`.
9. Add the board to the table above and to `docs/boards.md`. CI builds every `[env:...]` in this
   folder by itself.

### Flags a board's file sets

| Flag | Needed | Meaning |
|---|---|---|
| `SCREEN_W`, `SCREEN_H` | yes | The screen in pixels, as the UI sees it |
| `PANEL_ROTATION` | TFT_eSPI | Rotation 0-3 |
| `TFT_*`, `*_DRIVER`, `SPI_FREQUENCY` | TFT_eSPI | The library's own configuration |
| `TOUCH_CS`, `TOUCH_IRQ` | XPT2046 | Pins |
| `TOUCH_CLK`, `TOUCH_MISO`, `TOUCH_MOSI` | XPT2046 on its own bus | Pins |
| `TOUCH_SHARES_PANEL_SPI` | XPT2046 on the panel's bus | Instead of the three above |
| `TOUCH_ROTATION` | no (1) | The XPT2046 library's rotation |
| `TOUCH_CAL_X0`, `_X1`, `_Y0`, `_Y1` | resistive | Raw reading at the left, right, top, bottom edge |
| `TOUCH_SDA`, `TOUCH_SCL` | capacitive | I2C pins |
| `TOUCH_RST` | no | Reset line of a capacitive chip |
| `TOUCH_I2C_ADDR` | no | CST8xx (0x15) or FT6x36 (0x38) at another address |
| `TOUCH_SWAP_XY`, `TOUCH_MIRROR_X`, `TOUCH_MIRROR_Y` | capacitive (0) | How the touch grid lies on the screen |
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

There are drivers for SPI panels through TFT_eSPI, and for XPT2046, GT911, CST8xx and FT6x36
touch. A board with other hardware (a parallel or RGB panel, an ESP32-S3 with a panel TFT_eSPI
doesn't drive, another touch chip) needs a driver first:

1. Add `src/drivers/panel_<name>.cpp` or `touch_<name>.cpp`, wrapped in `#if PANEL_<NAME>` or
   `#if TOUCH_<NAME>`, with the functions in `src/drivers/drivers.h`: `panel_begin` and
   `panel_push`, or `touch_begin` and `touch_read`.
2. Add a section for it in `platformio.ini` next to `[panel_tft_espi]`, with its library and
   its `-DPANEL_<NAME>=1` flag.
3. Add the flag to the "exactly one" check in `src/board.h`. For capacitive touch, leave it
   out of `TOUCH_NEEDS_CALIBRATION` there: `touch_read` then returns pixels on the chip's own
   grid, the calibration screen never shows, and a touch at start-up goes straight to setup.
   `src/drivers/touch_i2c.h` has the I2C bus and reset for such a chip.

Not there yet, and each needs a small change outside the driver when its first board arrives:
a backlight the panel driver controls (AMOLED), and a draw buffer in PSRAM. Both come with the
ESP32-S3 boards with 4.3" to 7" RGB panels (ESP32-8048S043, -S050, -S070 and their like), which
are the next family worth adding.
