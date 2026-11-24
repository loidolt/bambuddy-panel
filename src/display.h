// Screen, touch, backlight and status LED.
#pragma once
#include <Arduino.h>
#include "touch_cal.h"

void display_begin();              // starts the panel, touch and LVGL
void display_loop();               // call often from loop(); runs LVGL
bool display_touched();            // true while a finger is on the screen
bool display_raw_touch(int &x, int &y);        // uncalibrated reading, for the calibration screen
void display_set_touch_cal(const TouchCal &c); // takes effect at once
void display_brightness(uint8_t percent);
void display_led(bool r, bool g, bool b);
