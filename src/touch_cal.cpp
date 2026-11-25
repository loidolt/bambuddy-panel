#include "touch_cal.h"

#include <cstdlib>

static const int MIN_SPAN = 800; // raw units between opposite crosses; a real panel gives ~3000
static const int MAX_SKEW = 500; // raw units two crosses on the same edge may disagree

bool touch_cal_solve(long rx[4], long ry[4], int inset, int w, int h, TouchCal &cal)
{
  // If moving across the screen changes raw Y more than raw X, the axes are swapped.
  cal.swap = labs(rx[1] - rx[0]) < labs(ry[1] - ry[0]);
  if (cal.swap)
    for (int i = 0; i < 4; i++) {
      long t = rx[i];
      rx[i] = ry[i];
      ry[i] = t;
    }

  if (labs(rx[0] - rx[3]) > MAX_SKEW || labs(rx[1] - rx[2]) > MAX_SKEW) return false;
  if (labs(ry[0] - ry[1]) > MAX_SKEW || labs(ry[3] - ry[2]) > MAX_SKEW) return false;

  long left = (rx[0] + rx[3]) / 2, right = (rx[1] + rx[2]) / 2;
  long top = (ry[0] + ry[1]) / 2, bottom = (ry[3] + ry[2]) / 2;
  if (labs(right - left) < MIN_SPAN || labs(bottom - top) < MIN_SPAN) return false;

  // The crosses are inset; stretch the readings out to the screen edges.
  long spanX = w - 1 - 2 * inset, spanY = h - 1 - 2 * inset;
  cal.x0 = left - (right - left) * inset / spanX;
  cal.x1 = right + (right - left) * inset / spanX;
  cal.y0 = top - (bottom - top) * inset / spanY;
  cal.y1 = bottom + (bottom - top) * inset / spanY;
  return true;
}

static int scale(long v, long from0, long from1, long to1)
{
  long out = (v - from0) * to1 / (from1 - from0);
  return out < 0 ? 0 : out > to1 ? to1 : (int)out;
}

void touch_cal_apply(const TouchCal &cal, int w, int h, int rawX, int rawY, int &x, int &y)
{
  if (cal.swap) {
    int t = rawX;
    rawX = rawY;
    rawY = t;
  }
  x = scale(rawX, cal.x0, cal.x1, w - 1);
  y = scale(rawY, cal.y0, cal.y1, h - 1);
}

void touch_orient(int w, int h, bool swap, bool mirrorX, bool mirrorY, int &x, int &y)
{
  if (swap) {
    int t = x;
    x = y;
    y = t;
  }
  if (mirrorX) x = w - 1 - x;
  if (mirrorY) y = h - 1 - y;
  x = x < 0 ? 0 : x > w - 1 ? w - 1 : x;
  y = y < 0 ? 0 : y > h - 1 ? h - 1 : y;
}
