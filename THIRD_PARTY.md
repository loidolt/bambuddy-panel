# Third-party work

Bambuddy Panel's own code is under the MIT license (see `LICENSE`). It builds on:

| What | Used for | License |
|---|---|---|
| [Material Icons](https://github.com/google/material-design-icons) (`thumb_up`, `thumb_down`, `build`) | The SVGs in `tools/icons/`, unchanged; rasterised into alpha masks in `src/icons.c`, which says so | Apache 2.0 (`tools/icons/LICENSE.txt`) |
| [LVGL](https://github.com/lvgl/lvgl) | Drawing the screens | MIT |
| [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) | Display driver | FreeBSD, MIT and BSD parts; see its `license.txt` |
| [XPT2046_Touchscreen](https://github.com/PaulStoffregen/XPT2046_Touchscreen) | Touch controller | MIT-style, in its source files |
| [ArduinoJson](https://github.com/bblanchon/ArduinoJson) | Reading Bambuddy's JSON | MIT |
| [WiFiManager](https://github.com/tzapu/WiFiManager) | WiFi setup portal | MIT |
| [Arduino core for ESP32](https://github.com/espressif/arduino-esp32) | Platform | LGPL 2.1 |
| [Unity](https://github.com/ThrowTheSwitch/Unity) | Unit tests only | MIT |

The libraries are fetched by PlatformIO at build time; none of their code is in this repository.

**Firmware files.** A built `firmware.bin` contains all of the above except Unity, statically
linked. Whoever passes one on must keep these notices with it, and, because the Arduino core is
LGPL 2.1, must let the recipient relink it with a changed core. The source in this repository builds
the same firmware.

Bambuddy Panel is an independent project. It is not part of, or endorsed by, Bambuddy or Bambu Lab.
