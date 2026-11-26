# Third-party work

Bambuddy Panel's own code is under the MIT license (see `LICENSE`). It builds on:

| What | Used for | License |
|---|---|---|
| [Material Icons](https://github.com/google/material-design-icons) (`thumb_up`, `thumb_down`, `build`) | The SVGs in `tools/icons/`, unchanged; rasterised into alpha masks in `src/icons.c`, which says so | Apache 2.0 (`tools/icons/LICENSE.txt`) |
| [LVGL](https://github.com/lvgl/lvgl) | Drawing the screens | MIT |
| [Montserrat](https://github.com/JulietaUla/Montserrat) | The text on screen: LVGL's built-in `lv_font_montserrat_*` fonts are made from it | SIL Open Font License 1.1 ([text](https://github.com/JulietaUla/Montserrat/blob/711e8ae6b4fa9d33d8482c9ec4871d906a345344/OFL.txt)) |
| [Font Awesome Free](https://github.com/FortAwesome/Font-Awesome) | The symbols in those same LVGL fonts (the arrow on "Back") | Font: SIL Open Font License 1.1; icons: CC BY 4.0 ([text](https://github.com/FortAwesome/Font-Awesome/blob/ba907eaec40fab01d410c3023a5572b2cb46cea6/LICENSE.txt)) |
| [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) | Display driver | FreeBSD, MIT and BSD parts; see its `license.txt` |
| [XPT2046_Touchscreen](https://github.com/PaulStoffregen/XPT2046_Touchscreen) | Touch controller | MIT-style, in its source files |
| [ArduinoJson](https://github.com/bblanchon/ArduinoJson) | Reading Bambuddy's JSON | MIT |
| [WiFiManager](https://github.com/tzapu/WiFiManager) | WiFi setup portal | MIT |
| [Arduino core for ESP32](https://github.com/espressif/arduino-esp32) | Platform | LGPL 2.1 |
| [ESP-IDF](https://github.com/espressif/esp-idf), which the Arduino core is built on and brings along prebuilt | WiFi, TCP/IP, flash storage, the operating system | Apache 2.0, with parts under their own licenses, among them FreeRTOS (MIT), lwIP (BSD) and newlib (BSD); Espressif's [list](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32/COPYRIGHT.html) names them all |
| [QR Code generator](https://github.com/nayuki/QR-Code-generator) and [TLSF](https://github.com/mattconte/tlsf) | QR codes and LVGL allocation | MIT and BSD-style notices in the LVGL source headers |
| GCC and newlib | ESP32 C and C++ runtime | GCC GPL with runtime exception, newlib component licenses |
| [Bambuddy wiki](https://github.com/maziggy/bambuddy-wiki) | Documentation stylesheet | MIT, copyright 2025 MartinNYHC. The notice is included in `docs/stylesheets/extra.css` |
| SDL2 and libcurl | Desktop simulator | zlib and curl licenses |
| [Unity](https://github.com/ThrowTheSwitch/Unity) | Unit tests only | MIT |

The spool symbol is drawn for this project. The three Material Icons listed above were rasterised for the panel. The bundled Montserrat 7.200 font credits the Montserrat Project Authors (2011). Its symbol glyphs come from Font Awesome 5 Free Solid 5.9.0, copyright Font Awesome.

## Firmware distribution

Keep the notices ZIP and source archive alongside distributed app and full images. Both CI and release builds run `tools/package_firmware.py`. The notices come from the installed libraries and toolchain and from the exact upstream revisions in [the notice manifest](tools/notice-sources.json). ESP-IDF includes components with separate terms, including the ESP32 WiFi, PHY and Bluetooth binary libraries.

The source archive includes the panel, board profiles, Arduino libraries, editable Arduino core, and ESP32 SDK used for the build. Extract it and follow `REBUILD.txt` to build the application with a modified core using PlatformIO. The release checksums cover both archives as well as each binary.

Bambuddy Panel is an independent project.
