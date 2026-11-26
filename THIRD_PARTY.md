# Third-party work

Bambuddy Panel's own code is under the MIT license (see `LICENSE`). It builds on:

| What | Used for | License |
|---|---|---|
| [Material Icons](https://github.com/google/material-design-icons) (`thumb_up`, `thumb_down`, `build`) | The SVGs in `tools/icons/`, unchanged; rasterised into alpha masks in `src/icons.c`, which says so | Apache 2.0 (`tools/icons/LICENSE.txt`) |
| [LVGL](https://github.com/lvgl/lvgl) | Drawing the screens | MIT |
| [Montserrat](https://github.com/JulietaUla/Montserrat) | The text on screen: LVGL's built-in `lv_font_montserrat_*` fonts are made from it | SIL Open Font License 1.1 ([text](https://github.com/JulietaUla/Montserrat/blob/master/OFL.txt)) |
| [Font Awesome Free](https://github.com/FortAwesome/Font-Awesome) | The symbols in those same LVGL fonts (the arrow on "Back") | Font: SIL Open Font License 1.1; icons: CC BY 4.0 ([text](https://github.com/FortAwesome/Font-Awesome/blob/6.x/LICENSE.txt)) |
| [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) | Display driver | FreeBSD, MIT and BSD parts; see its `license.txt` |
| [XPT2046_Touchscreen](https://github.com/PaulStoffregen/XPT2046_Touchscreen) | Touch controller | MIT-style, in its source files |
| [ArduinoJson](https://github.com/bblanchon/ArduinoJson) | Reading Bambuddy's JSON | MIT |
| [WiFiManager](https://github.com/tzapu/WiFiManager) | WiFi setup portal | MIT |
| [Arduino core for ESP32](https://github.com/espressif/arduino-esp32) | Platform | LGPL 2.1 |
| [ESP-IDF](https://github.com/espressif/esp-idf), which the Arduino core is built on and brings along prebuilt | WiFi, TCP/IP, flash storage, the operating system | Apache 2.0, with parts under their own licenses, among them FreeRTOS (MIT), lwIP (BSD) and newlib (BSD); Espressif's [list](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32/COPYRIGHT.html) names them all |
| [Unity](https://github.com/ThrowTheSwitch/Unity) | Unit tests only | MIT |

The libraries are fetched by PlatformIO at build time; none of their code is in this repository.

**Firmware files.** A built `firmware.bin` contains all of the above except Unity, statically
linked. Whoever passes one on must keep these notices with it, and, because the Arduino core is
LGPL 2.1, must let the recipient relink it with a changed core. The source in this repository builds
the same firmware.

"These notices" means the license texts too, not only this table. After a build,
`python3 tools/collect_notices.py notices` gathers what can be gathered into one folder: this
file, the panel's own license, the icons' license and the license file of every library
PlatformIO fetched. CI does this and puts the folder into the firmware download. Three texts
are **not** in that folder, because nothing a build fetches contains them: Montserrat's and
Font Awesome's (LVGL ships the fonts without them) and ESP-IDF's (the Arduino core ships it
prebuilt). The table links to them. A release adds them itself, from those projects at fixed
commits (`.github/workflows/release.yml`), and attaches the whole folder as `notices.zip`; add
them by hand before publishing firmware files any other way. XPT2046_Touchscreen has no license file; its notice is at the top of its source
files, which the folder gets instead.

Bambuddy Panel is an independent project. It is not part of, or endorsed by, Bambuddy or Bambu Lab.
