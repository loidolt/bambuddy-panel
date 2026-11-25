// Touch calibration maths. No hardware and no board in here, so it runs in the unit tests too.
#pragma once
#include <cstdint>

// How raw touch readings map to the screen: the raw value at each screen edge.
// x0 > x1 (or y0 > y1) simply means that axis runs the other way.
struct TouchCal {
  int16_t x0, x1; // raw X at the left and right edge
  int16_t y0, y1; // raw Y at the top and bottom edge
  bool swap;      // the panel's X and Y are exchanged
};

// Works the mapping out from four taps on crosses `inset` pixels in from each corner of a
// `w` x `h` screen, given in the order top-left, top-right, bottom-right, bottom-left. Changes
// rx/ry when the axes are swapped. False when the taps can't be right (too close together, or
// a corner far off).
bool touch_cal_solve(long rx[4], long ry[4], int inset, int w, int h, TouchCal &cal);

// Raw reading to a pixel of a `w` x `h` screen, clamped to the screen.
void touch_cal_apply(const TouchCal &cal, int w, int h, int rawX, int rawY, int &x, int &y);
