// Capacitive touch: Goodix GT911 over I2C. Reports pixels on the grid it was configured for
// at the factory, normally the panel's own in portrait.
#include "board.h"
#if TOUCH_GT911

#include "drivers.h"
#include "touch_i2c.h"

static const uint16_t REG_STATUS = 0x814E; // bit 7: a new report is ready; low 4 bits: fingers
static const uint16_t REG_POINT1 = 0x8150; // X low, X high, Y low, Y high
static const uint32_t STALE_MS = 200;      // reports come every ~10 ms while a finger is down

static uint8_t addr = 0x5D;
static bool down = false;
static int lastX = 0, lastY = 0;
static uint32_t lastReport = 0;

static bool answers(uint8_t a)
{
  Wire.beginTransmission(a);
  return Wire.endTransmission() == 0;
}

void touch_begin()
{
  touch_i2c_begin(120);
  // The chip takes 0x5D or 0x14, depending on its interrupt line while it comes out of reset.
  // That line isn't driven here, so see where it answers.
  if (!answers(0x5D) && answers(0x14)) addr = 0x14;
}

bool touch_read(int &x, int &y)
{
  uint8_t status;
  if (!touch_i2c_read(addr, REG_STATUS, 2, &status, 1)) {
    down = false;
  } else if (status & 0x80) {
    uint8_t p[4];
    down = (status & 0x0F) > 0 && touch_i2c_read(addr, REG_POINT1, 2, p, 4);
    if (down) {
      lastX = p[0] | p[1] << 8;
      lastY = p[2] | p[3] << 8;
    }
    lastReport = millis();
    // The chip keeps the report until it is told the report was read.
    Wire.beginTransmission(addr);
    Wire.write((uint8_t)(REG_STATUS >> 8));
    Wire.write((uint8_t)REG_STATUS);
    Wire.write((uint8_t)0);
    Wire.endTransmission();
  } else if (down && millis() - lastReport > STALE_MS) {
    down = false; // no report for a while: the finger is gone, whatever the last one said
  }
  // Between two reports the finger is where the last report put it.
  if (!down) return false;
  x = lastX;
  y = lastY;
  return true;
}

#endif
