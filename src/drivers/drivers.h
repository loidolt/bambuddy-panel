// What the panel firmware needs from a board's display and touch hardware. One panel driver
// and one touch driver in this folder are compiled in, chosen by the board's file in boards/
// (each driver file is wrapped in its own #if). display.cpp is the only caller.
#pragma once
#include <cstdint>

// ---- panel

// Switches the panel on, SCREEN_W x SCREEN_H in the UI's orientation, and clears it to black.
void panel_begin();

// Draws a rectangle of RGB565 pixels, in the processor's byte order.
void panel_push(int x, int y, int w, int h, uint16_t *pixels);

// ---- touch

void touch_begin();

// Where the finger is; false when nothing touches the screen. A resistive controller gives its
// raw readings (display.cpp maps them with the calibration); a capacitive one gives screen
// pixels in the UI's orientation. Which it is: TOUCH_NEEDS_CALIBRATION in board.h.
bool touch_read(int &x, int &y);
