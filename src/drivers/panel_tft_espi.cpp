// SPI panels through TFT_eSPI. The chip and pins are TFT_eSPI's own flags in the board's file.
#include "board.h"
#if PANEL_TFT_ESPI

#include <TFT_eSPI.h>

#include "drivers.h"

#ifndef PANEL_ROTATION
#error "PANEL_ROTATION: TFT_eSPI's rotation (0-3) that shows the UI the right way up"
#endif

// TFT_eSPI's size is the panel's own, before rotation; the UI's is after.
#if PANEL_ROTATION % 2
static_assert(TFT_HEIGHT == SCREEN_W && TFT_WIDTH == SCREEN_H, "TFT_WIDTH/HEIGHT don't match SCREEN_W/H");
#else
static_assert(TFT_WIDTH == SCREEN_W && TFT_HEIGHT == SCREEN_H, "TFT_WIDTH/HEIGHT don't match SCREEN_W/H");
#endif

static TFT_eSPI tft;

void panel_begin()
{
  tft.init();
  tft.setRotation(PANEL_ROTATION);
  tft.fillScreen(TFT_BLACK);
}

void panel_push(int x, int y, int w, int h, uint16_t *pixels)
{
  tft.startWrite();
  tft.setAddrWindow(x, y, w, h);
  tft.pushColors(pixels, (uint32_t)w * h, true);
  tft.endWrite();
}

#endif
