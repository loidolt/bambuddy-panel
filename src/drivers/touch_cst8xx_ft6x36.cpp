// Capacitive touch over I2C: Hynitron CST816S / CST820 and FocalTech FT6236 / FT6336. The two
// families keep the finger in the same registers; only the address differs. They report
// pixels on the panel's own grid, normally in portrait.
#include "board.h"
#if TOUCH_CST8XX || TOUCH_FT6X36

#include "drivers.h"
#include "touch_i2c.h"

#ifndef TOUCH_I2C_ADDR
#define TOUCH_I2C_ADDR (TOUCH_CST8XX ? 0x15 : 0x38)
#endif

static const uint8_t REG_FINGERS = 0x02; // then X high, X low, Y high, Y low

void touch_begin() { touch_i2c_begin(50); }

bool touch_read(int &x, int &y)
{
  // A CST8xx sleeps until it is touched and doesn't answer while asleep: no answer, no finger.
  uint8_t b[5];
  if (!touch_i2c_read(TOUCH_I2C_ADDR, REG_FINGERS, 1, b, 5)) return false;
  int fingers = b[0] & 0x0F;
  if (fingers < 1 || fingers > 2) return false;
  // The high bytes carry flags in their upper bits.
  x = (b[1] & 0x0F) << 8 | b[2];
  y = (b[3] & 0x0F) << 8 | b[4];
  return true;
}

#endif
