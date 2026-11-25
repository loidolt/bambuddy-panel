// Shared between the screens in ui.cpp and ui_tasks.cpp. Not for use outside the UI.
#pragma once
#include <lvgl.h>
#include "bambuddy.h"

// Colours. Every button colour keeps C_TEXT at a contrast of 4.5:1 or more.
static const uint32_t C_BG = 0x101418, C_TEXT = 0xECEFF1, C_DIM = 0x90A4AE;
static const uint32_t C_BLUE = 0x1A62C2, C_AMBER = 0x9C5700, C_GREEN = 0x2A722E, C_RED = 0xC62828, C_GREY = 0x455A64;
static const uint32_t C_LIT = 0xF2C037, C_DARK = 0x1B1B1B, C_OK_TEXT = 0x81C784, C_BAD_TEXT = 0xFF8A80;
static const uint32_t C_CHIP = 0x263238, C_ROW = 0x1B2228, C_WARN_TEXT = 0xFFB74D;

static const uint32_t HOLD_MS = 1500; // for anything that is costly to press by accident

// A press that must be held for HOLD_MS before it counts.
struct Hold {
  uint32_t start = 0;
  bool fired = false;
};

// ---- sizes (ui_layout.cpp)
//
// The screens are laid out in units, not pixels. A 320x240 panel has one pixel per unit; any
// other screen gets as many pixels per unit as fit, and is then at least 320 units wide and
// 240 tall. So a position is written as on the 320x240 panel, measured from whichever edge the
// thing belongs to (ui.uw - 10 for "10 from the right"), and px() turns it into pixels.
// Landscape screens are what this is drawn for; a portrait one works but wastes its height.
struct UiMetrics {
  int w, h;   // the screen in pixels
  int uw, uh; // the screen in units: 320 x 240 or more
  int scale;  // pixels per unit, times 256
  // Text of 14, 20 and 28 units: the nearest size that is built in (see [lvgl] in platformio.ini).
  const lv_font_t *small, *medium, *large;
};
extern UiMetrics ui;

void ui_layout_begin(int w, int h); // once, before anything is drawn
int px(int units);

// ---- building blocks (ui.cpp). Positions and sizes are in units.
lv_obj_t *ui_box(lv_obj_t *parent); // fills the screen
lv_obj_t *ui_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, int x, int y, int w);
// A button without the theme's shadow and press-grow (which the parent would clip).
lv_obj_t *ui_button(lv_obj_t *parent, uint32_t color, int x, int y, int w, int h, const char *text,
                    lv_obj_t **textOut = nullptr);
// An icon, in the given colour and scaled with the screen. Icons are drawn for 1 px per unit.
lv_obj_t *ui_icon(lv_obj_t *parent, const lv_image_dsc_t *icon, uint32_t color);

// Sends a command; when the queue is full, says so instead of dropping it silently.
void ui_send(Command c, int arg = 0, int arg2 = 0);
// Redraw on the next ui_update, without waiting for new data.
void ui_redraw_soon();

// ---- maintenance screen (ui_tasks.cpp)
void tasks_create(lv_obj_t *screen, lv_obj_t *header); // the screen, and the wrench in `header`
bool tasks_showing();
void tasks_close();
bool tasks_idle_timeout(); // closes the screen after a while untouched; true when it did
void tasks_draw(const Snapshot &s);
void tasks_draw_badge(const Snapshot &s);
lv_obj_t *tasks_box();
