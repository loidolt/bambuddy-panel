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

// Called after panel_begin(), so a touch chip on the panel's bus finds the bus started.
void touch_begin();

// Where the finger is; false when nothing touches the screen. A resistive controller gives its
// raw readings, which display.cpp maps with the calibration. A capacitive one gives pixels on
// its own grid, which display.cpp turns to the screen (TOUCH_SWAP_XY and the mirrors).
// Which it is: TOUCH_NEEDS_CALIBRATION in board.h.
bool touch_read(int &x, int &y);
