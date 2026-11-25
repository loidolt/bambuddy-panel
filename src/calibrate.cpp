#include "calibrate.h"

#include <Arduino.h>

#include "board.h"
#include "display.h"
#include "settings.h"
#include "touch_cal.h"
#include "ui.h"

static const int INSET = SCREEN_H / 12; // crosses sit this far in from each corner
static const uint32_t GIVE_UP_MS = 60000; // per cross, when nobody touches the screen

static void wait_release()
{
  uint32_t quiet = millis();
  int x, y;
  while (millis() - quiet < 250) {
    if (display_raw_touch(x, y)) quiet = millis();
    delay(10);
  }
}

// Waits for a steady press and averages it. False when nobody touches in time.
static bool sample(long &outX, long &outY)
{
  uint32_t start = millis();
  for (;;) {
    int x, y;
    if (!display_raw_touch(x, y)) {
      if (millis() - start > GIVE_UP_MS) return false;
      delay(10);
      continue;
    }
    // Pressed: skip the first readings (the film is still settling), then average.
    long sx = 0, sy = 0;
    int n = 0;
    bool held = true;
    for (int i = 0; i < 16 && held; i++) {
      delay(15);
      held = display_raw_touch(x, y);
      if (held && i >= 4) {
        sx += x;
        sy += y;
        n++;
      }
    }
    if (held && n > 0) {
      outX = sx / n;
      outY = sy / n;
      wait_release();
      return true;
    }
    // Let go too soon; wait for the next press.
  }
}

bool calibrate_run(bool haveSaved)
{
  const char *keeping = haveSaved ? "Keeping the saved calibration."
                                  : "Using standard values.\nHold the screen at start-up to calibrate.";
  char buf[120];
  static const int target[4][2] = {
      {INSET, INSET},
      {SCREEN_W - 1 - INSET, INSET},
      {SCREEN_W - 1 - INSET, SCREEN_H - 1 - INSET},
      {INSET, SCREEN_H - 1 - INSET},
  };

  wait_release(); // the finger that asked for calibration may still be down

  for (int attempt = 0; attempt < 3; attempt++) {
    long rx[4], ry[4];
    for (int i = 0; i < 4; i++) {
      ui_calibrate(i + 1, 4, target[i][0], target[i][1]);
      if (!sample(rx[i], ry[i])) {
        snprintf(buf, sizeof(buf), "No touch. %s", keeping);
        ui_message("Calibration", buf);
        delay(2500);
        return false;
      }
      Serial.printf("[calibrate] cross %d raw %ld,%ld\n", i + 1, rx[i], ry[i]);
    }

    TouchCal cal;
    if (touch_cal_solve(rx, ry, INSET, SCREEN_W, SCREEN_H, cal)) {
      display_set_touch_cal(cal);
      settings_save_touch(cal);
      Serial.printf("[calibrate] saved x %d..%d y %d..%d swap %d\n", cal.x0, cal.x1, cal.y0, cal.y1, cal.swap);
      ui_message("Calibration", "Saved.");
      delay(800);
      return true;
    }
    ui_message("Calibration", "Those taps didn't line up.\nTry again.");
    delay(1800);
  }
  snprintf(buf, sizeof(buf), "Giving up. %s", keeping);
  ui_message("Calibration", buf);
  delay(2500);
  return false;
}
