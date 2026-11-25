// Pins and fixed sizes for the ESP32-2432S028R. Display pins live in platformio.ini (TFT_eSPI flags).
#pragma once

#define SCREEN_W 320
#define SCREEN_H 240

// Resistive touch controller (XPT2046) on its own SPI bus.
#define TOUCH_CLK 25
#define TOUCH_MISO 39
#define TOUCH_MOSI 32
#define TOUCH_CS 33
#define TOUCH_IRQ 36

// Raw touch range, mapped to the screen in landscape. Typical values for this board,
// used until the panel is calibrated on the device (see calibrate.cpp).
#define TOUCH_X_MIN 200
#define TOUCH_X_MAX 3700
#define TOUCH_Y_MIN 240
#define TOUCH_Y_MAX 3800

#define BACKLIGHT_PIN 21
#define BACKLIGHT_CHANNEL 0

// RGB status LED on the back, active low.
#define LED_R 4
#define LED_G 16
#define LED_B 17
