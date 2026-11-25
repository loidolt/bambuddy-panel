// Resistive touch: XPT2046 on an SPI bus of its own (TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI,
// TOUCH_CS) with an interrupt line (TOUCH_IRQ). Gives raw readings, so the panel needs calibrating.
#include "board.h"
#if TOUCH_XPT2046

#include <SPI.h>
#include <XPT2046_Touchscreen.h>

#include "drivers.h"

#ifndef TOUCH_ROTATION
#define TOUCH_ROTATION 1
#endif

static SPIClass touchSpi(VSPI);
static XPT2046_Touchscreen touch(TOUCH_CS, TOUCH_IRQ);

void touch_begin()
{
  touchSpi.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  touch.begin(touchSpi);
  touch.setRotation(TOUCH_ROTATION);
}

bool touch_read(int &x, int &y)
{
  if (!touch.tirqTouched() || !touch.touched()) return false;
  TS_Point p = touch.getPoint();
  x = p.x;
  y = p.y;
  return true;
}

#endif
