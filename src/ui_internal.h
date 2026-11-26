// Shared between the screens in ui.cpp and ui_tasks.cpp. Not for use outside the UI.
#pragma once
#include <lvgl.h>
#include "bambuddy.h"
#include "ui_logic.h"

// Colours, by role (docs/ux-design.md). Every fill keeps C_TEXT at a contrast of 4.5:1 or more.
static const uint32_t C_BG = 0x101418, C_TEXT = 0xECEFF1, C_DIM = 0x90A4AE;
static const uint32_t C_BLUE = 0x1A62C2, C_AMBER = 0x9C5700, C_GREEN = 0x2A722E, C_RED = 0xC62828, C_GREY = 0x455A64;
static const uint32_t C_OK_TEXT = 0x81C784, C_BAD_TEXT = 0xFF8A80, C_WARN_TEXT = 0xFFB74D;
static const uint32_t C_CHIP = 0x263238, C_ROW = 0x1B2228, C_LIT = 0xF2C037, C_BAR = 0x2196F3;
// What a held button fills with, per kind: a lighter shade of its own colour.
static const uint32_t C_FILL_GO = 0x43A047, C_FILL_NEUTRAL = 0x3B82E0, C_FILL_QUIET = 0x607D8B;

static const uint32_t HOLD_MS = 1500; // for anything that starts a print or can't be undone
static const int TARGET = 46;         // the least height of anything to press: 7 mm on a 2.4" panel
// The icons in the status screen's header (spool, wrench): small to look at, and taking
// touches from REACH around them, which makes them TARGET tall. HEADER_GAP keeps the reach
// of two neighbours from overlapping.
static const int HEADER_ICON_W = 40, HEADER_ICON_H = 34, HEADER_ICON_REACH = 6, HEADER_GAP = 13;

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
lv_obj_t *ui_plain(lv_obj_t *parent); // an object with no style of its own, not scrollable
lv_obj_t *ui_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, int x, int y, int w);
// Sets a label's text and makes it as tall as the text needs, up to `maxLines`; what doesn't
// fit ends in dots. The label must have its width already.
void ui_set_text(lv_obj_t *label, const char *text, int maxLines = 1);
// The biggest of the three text sizes, starting at `from`, in which `text` fits `maxLines`
// lines of `widthPx`.
const lv_font_t *ui_fit_font(const char *text, int widthPx, int maxLines, const lv_font_t *from);
// An icon, in the given colour and at the size for this screen.
lv_obj_t *ui_icon(lv_obj_t *parent, const lv_image_dsc_t *icon, uint32_t color);

// A button. One kind for the whole UI, so a tap, a hold and "nothing can be pressed right now"
// look and behave the same everywhere.
//
// A hold button fills from the left while the finger stays, and acts when it is full; letting
// go early empties it. A tap button acts on release. Either calls `fire` once.
struct UiButton {
  lv_obj_t *btn = nullptr, *fill = nullptr, *label = nullptr;
  void (*fire)(UiButton &) = nullptr;
  int tag = 0;          // for the owner: which of its buttons this is
  bool hold = false;
  bool enabled = true;
  Kind kind = Kind::Quiet;
  // What the button stood for when the finger came down, and what it stands for now. They
  // differ when the screen changed under a finger that stayed: then nothing is done.
  uint64_t id = 0, idAtPress = UINT64_MAX;
  uint32_t downAt = 0;
  bool fired = false, swallowed = false;
};
void ui_button_create(UiButton &b, lv_obj_t *parent, void (*fire)(UiButton &), int tag = 0);
// What it says and does. `id` tells one action from another (see UiButton::id).
void ui_button_set(UiButton &b, const char *text, Kind kind, bool hold, Icon icon = Icon::None, uint64_t id = 0);
// A button that can't be pressed right now is grey and takes no touch.
void ui_button_enable(UiButton &b, bool on);

// Sends a command and remembers it as under way until Bambuddy has answered and been read
// again; meanwhile ui_busy() is true and nothing else is sent.
void ui_send(Command c, int arg = 0, int arg2 = 0);
void ui_notice(const char *text, bool bad);
bool ui_can_write();
bool ui_busy();
bool ui_pending(Command c, int arg); // this very command is the one under way
bool ui_answered();                  // Bambuddy has said yes to it; its effect hasn't been read yet
// Redraw on the next ui_update, without waiting for new data.
void ui_redraw_soon();
// A touch on a dimmed screen only brightens it. True when this press is that touch.
bool ui_touch_wakes();
void ui_touched(); // someone is using the panel: stay bright
// Restarts the panel into its settings screen, saying so first.
void ui_open_settings();

// ---- filament screens (ui_filament.cpp)
void filament_create(lv_obj_t *header); // the spool in `header`; the screens are made when opened
bool filament_showing();
void filament_close(); // and takes the screen away
bool filament_tick();  // call often; true when the screen should be drawn again
void filament_draw(const Snapshot &s);
void filament_draw_badge(const Snapshot &s);

// ---- maintenance screen (ui_tasks.cpp)
void tasks_create(lv_obj_t *screen, lv_obj_t *header); // the wrench in `header`; the screen is made when opened
bool tasks_showing();
void tasks_close(); // and takes the screen away
bool tasks_idle_timeout(); // closes the screen after a while untouched; true when it did
void tasks_draw(const Snapshot &s);
void tasks_draw_badge(const Snapshot &s);
