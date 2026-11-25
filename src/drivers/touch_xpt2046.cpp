// Resistive touch: XPT2046 with a chip select (TOUCH_CS) and an interrupt line (TOUCH_IRQ).
// Either on an SPI bus of its own (TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI) or, with
// TOUCH_SHARES_PANEL_SPI, on the panel's. Gives raw readings, so the panel needs calibrating.
#include "board.h"
#if TOUCH_XPT2046

#include <SPI.h>
#include <XPT2046_Touchscreen.h>

#include "drivers.h"

#ifndef TOUCH_ROTATION
#define TOUCH_ROTATION 1
#endif
#ifndef TOUCH_SHARES_PANEL_SPI
#define TOUCH_SHARES_PANEL_SPI 0
#endif

#if TOUCH_SHARES_PANEL_SPI
#if !PANEL_TFT_ESPI
#error "TOUCH_SHARES_PANEL_SPI is only there for TFT_eSPI panels"
#endif
#include <TFT_eSPI.h>
#else
static SPIClass touchSpi(VSPI);
#endif

static XPT2046_Touchscreen touch(TOUCH_CS, TOUCH_IRQ);

void touch_begin()
{
#if TOUCH_SHARES_PANEL_SPI
  // The same SPI object as the panel: both take the bus per transfer, at their own speed.
  touch.begin(TFT_eSPI::getSPIinstance());
#else
  touchSpi.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  touch.begin(touchSpi);
#endif
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
