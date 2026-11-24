#include "display.h"
#include "board.h"

#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <lvgl.h>

static TFT_eSPI tft;
static SPIClass touchSpi(VSPI);
static XPT2046_Touchscreen touch(TOUCH_CS, TOUCH_IRQ);

// LVGL renders into this strip (a tenth of the screen) and we push each strip to the panel.
alignas(4) static uint8_t drawBuf[SCREEN_W * SCREEN_H / 10 * 2];

static uint32_t tick_cb() { return millis(); }

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px)
{
  uint32_t w = lv_area_get_width(area);
  uint32_t h = lv_area_get_height(area);
  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.pushColors((uint16_t *)px, w * h, true);
  tft.endWrite();
  lv_display_flush_ready(disp);
}

// Typical values for this board, used until the panel has been calibrated.
static TouchCal cal = {TOUCH_X_MIN, TOUCH_X_MAX, TOUCH_Y_MIN, TOUCH_Y_MAX, false};

bool display_raw_touch(int &x, int &y)
{
  if (!touch.tirqTouched() || !touch.touched()) return false;
  TS_Point p = touch.getPoint();
  x = p.x;
  y = p.y;
  return true;
}

void display_set_touch_cal(const TouchCal &c)
{
  if (c.x0 != c.x1 && c.y0 != c.y1) cal = c;
}

static bool read_touch(int32_t &x, int32_t &y)
{
  int rx, ry;
  if (!display_raw_touch(rx, ry)) return false;
  int sx, sy;
  touch_cal_apply(cal, rx, ry, sx, sy);
  x = sx;
  y = sy;
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
  pinMode(LED_R, OUTPUT);
  pinMode(LED_G, OUTPUT);
  pinMode(LED_B, OUTPUT);
  display_led(false, false, false);

  tft.init();
  tft.setRotation(1); // landscape, USB on the right
  tft.fillScreen(TFT_BLACK);

  ledcSetup(BACKLIGHT_CHANNEL, 5000, 8);
  ledcAttachPin(BACKLIGHT_PIN, BACKLIGHT_CHANNEL);
  display_brightness(100);

  touchSpi.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  touch.begin(touchSpi);
  touch.setRotation(1);

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
  if (percent > 100) percent = 100;
  ledcWrite(BACKLIGHT_CHANNEL, percent * 255 / 100);
}

void display_led(bool r, bool g, bool b)
{
  digitalWrite(LED_R, r ? LOW : HIGH);
  digitalWrite(LED_G, g ? LOW : HIGH);
  digitalWrite(LED_B, b ? LOW : HIGH);
}
