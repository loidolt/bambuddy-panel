// Shared by the I2C touch drivers: the bus on TOUCH_SDA / TOUCH_SCL, the reset pulse on
// TOUCH_RST when the board has one, and register reads. Only included by a driver that is
// compiled in.
#pragma once
#include <Arduino.h>
#include <Wire.h>

#include "board.h"

static inline void touch_i2c_begin(int settleMs)
{
#ifdef TOUCH_RST
  pinMode(TOUCH_RST, OUTPUT);
  digitalWrite(TOUCH_RST, LOW);
  delay(10);
  digitalWrite(TOUCH_RST, HIGH);
#endif
  delay(settleMs);
  Wire.begin(TOUCH_SDA, TOUCH_SCL, 400000U);
}

// Reads `count` bytes from a register whose number is `regBytes` (1 or 2) bytes long,
// high byte first. False when the chip doesn't answer.
static inline bool touch_i2c_read(uint8_t addr, uint16_t reg, int regBytes, uint8_t *out, int count)
{
  Wire.beginTransmission(addr);
  if (regBytes == 2) Wire.write((uint8_t)(reg >> 8));
  Wire.write((uint8_t)reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)addr, count) != count) return false;
  for (int i = 0; i < count; i++) out[i] = Wire.read();
  return true;
}
