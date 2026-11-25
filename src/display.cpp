// Ties the board's drivers to LVGL: draw buffer, touch input with calibration, backlight, LED.
// Nothing in here knows which panel or touch chip the board has; that is in src/drivers/.
#include "display.h"
#include "board.h"
#include "drivers/drivers.h"

#include <lvgl.h>

// LVGL renders into this strip (a tenth of the screen unless the board says otherwise) and
// each strip is pushed to the panel.
alignas(4) static uint8_t drawBuf[SCREEN_W * DRAW_BUF_ROWS * 2];

static uint32_t tick_cb() { return millis(); }

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px)
{
  panel_push(area->x1, area->y1, lv_area_get_width(area), lv_area_get_height(area), (uint16_t *)px);
  lv_display_flush_ready(disp);
}

bool display_needs_calibration() { return TOUCH_NEEDS_CALIBRATION; }

bool display_raw_touch(int &x, int &y) { return touch_read(x, y); }

#if TOUCH_NEEDS_CALIBRATION
// Typical values for this board, used until the panel has been calibrated.
static TouchCal cal = {TOUCH_CAL_X0, TOUCH_CAL_X1, TOUCH_CAL_Y0, TOUCH_CAL_Y1, false};

void display_set_touch_cal(const TouchCal &c)
{
  if (c.x0 != c.x1 && c.y0 != c.y1) cal = c;
}
#else
void display_set_touch_cal(const TouchCal &) {}
#endif

// The finger in screen pixels.
static bool read_touch(int32_t &x, int32_t &y)
{
  int tx, ty;
  if (!touch_read(tx, ty)) return false;
#if TOUCH_NEEDS_CALIBRATION
  int sx, sy;
  touch_cal_apply(cal, SCREEN_W, SCREEN_H, tx, ty, sx, sy);
  x = sx;
  y = sy;
#else
  x = constrain(tx, 0, SCREEN_W - 1);
  y = constrain(ty, 0, SCREEN_H - 1);
#endif
  return true;
}

static void touch_cb(lv_indev_t *, lv_indev_data_t *data)
{
  int32_t x, y;
  if (read_touch(x, y)) {
    data->point.x = x;
    data->point.y = y;
    data->state = LV_INDEV_STATE_PRESSED;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

void display_begin()
{
#ifdef LED_R
  pinMode(LED_R, OUTPUT);
  pinMode(LED_G, OUTPUT);
  pinMode(LED_B, OUTPUT);
#endif
  display_led(false, false, false);

  panel_begin();

#ifdef BACKLIGHT_PIN
  ledcSetup(BACKLIGHT_CHANNEL, 5000, 8);
  ledcAttachPin(BACKLIGHT_PIN, BACKLIGHT_CHANNEL);
#endif
  display_brightness(100);

  touch_begin();

  lv_init();
  lv_tick_set_cb(tick_cb);

  lv_display_t *disp = lv_display_create(SCREEN_W, SCREEN_H);
  lv_display_set_flush_cb(disp, flush_cb);
  lv_display_set_buffers(disp, drawBuf, nullptr, sizeof(drawBuf), LV_DISPLAY_RENDER_MODE_PARTIAL);

  lv_indev_t *indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touch_cb);
}

void display_loop() { lv_timer_handler(); }

bool display_touched()
{
  int32_t x, y;
  return read_touch(x, y);
}

void display_brightness(uint8_t percent)
{
#ifdef BACKLIGHT_PIN
  if (percent > 100) percent = 100;
  ledcWrite(BACKLIGHT_CHANNEL, percent * 255 / 100);
#else
  (void)percent;
#endif
}

// A board without a status LED simply has none; the screen says the same thing.
void display_led(bool r, bool g, bool b)
{
#ifdef LED_R
  const int on = LED_ACTIVE_LOW ? LOW : HIGH, off = LED_ACTIVE_LOW ? HIGH : LOW;
  digitalWrite(LED_R, r ? on : off);
  digitalWrite(LED_G, g ? on : off);
  digitalWrite(LED_B, b ? on : off);
#else
  (void)r, (void)g, (void)b;
#endif
}
