// The board, as its file in boards/ describes it with -D flags. This header checks that the
// description is complete and works out what follows from it; it holds no pins itself.
// Firmware only: the UI, the simulator and the tests take the screen size at run time.
#pragma once

#if !defined(SCREEN_W) || !defined(SCREEN_H)
#error "No board: SCREEN_W and SCREEN_H come from the board's file in boards/ (see boards/README.md)"
#endif

// ---- panel driver: exactly one
#ifndef PANEL_TFT_ESPI
#define PANEL_TFT_ESPI 0
#endif
#if PANEL_TFT_ESPI != 1
#error "The board's file must pull in one panel driver, e.g. ${panel_tft_espi.build_flags}"
#endif

// ---- touch driver: exactly one
#ifndef TOUCH_XPT2046
#define TOUCH_XPT2046 0
#endif
#if TOUCH_XPT2046 != 1
#error "The board's file must pull in one touch driver, e.g. ${touch_xpt2046.build_flags}"
#endif

// Resistive touch reports raw controller readings, which calibration maps to the screen.
// A capacitive controller reports screen pixels and needs none.
#define TOUCH_NEEDS_CALIBRATION TOUCH_XPT2046

#if TOUCH_NEEDS_CALIBRATION && \
    !(defined(TOUCH_CAL_X0) && defined(TOUCH_CAL_X1) && defined(TOUCH_CAL_Y0) && defined(TOUCH_CAL_Y1))
#error "Resistive touch needs TOUCH_CAL_X0/X1/Y0/Y1: typical raw readings at the left, right, top and bottom edge"
#endif

// ---- optional parts

// Rows LVGL draws at a time; the draw buffer is this many rows of the screen, in static RAM.
#ifndef DRAW_BUF_ROWS
#define DRAW_BUF_ROWS (SCREEN_H / 10)
#endif

// Backlight on a PWM pin. Leave BACKLIGHT_PIN out when the panel driver switches the light.
#ifdef BACKLIGHT_PIN
#ifndef BACKLIGHT_CHANNEL
#define BACKLIGHT_CHANNEL 0
#endif
#endif

// RGB status LED. Leave LED_R/G/B out on a board without one.
#if defined(LED_R) != defined(LED_G) || defined(LED_R) != defined(LED_B)
#error "Give all three of LED_R, LED_G and LED_B, or none"
#endif
#ifndef LED_ACTIVE_LOW
#define LED_ACTIVE_LOW 0
#endif
