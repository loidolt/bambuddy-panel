// The status screen, the message screen, the calibration screen and the toast. What to show
// is decided in ui_logic.cpp; the maintenance screen lives in ui_tasks.cpp.
#include "ui.h"
#include "bambuddy_api.h"
#include "display.h"
#include "icons.h"
#include "ui_internal.h"
#include "ui_logic.h"

#include <initializer_list>

static const uint32_t TOAST_MS = 2500;
static const uint32_t PRESS_FREEZE_MS = 5000; // a finger this long is not mid-hold any more
static const uint32_t VERDICT_DEBOUNCE_MS = 3000;

static lv_obj_t *mainBox, *msgBox;
static lv_obj_t *lblName, *lblState, *lblJob, *bar, *lblProgress, *lblLayers, *lblHeadline, *lblTemps, *lblDetail, *btnRow;
static lv_obj_t *btn[MAX_ACTIONS], *btnLabel[MAX_ACTIONS];
static lv_obj_t *verdictRow;
static lv_obj_t *calBox, *lblCal, *crossH, *crossV;
static lv_obj_t *lblMsgTitle, *lblMsgBody, *lblToast;

static Action actions[MAX_ACTIONS];
static int actionCount = 0;
static Hold hold;

static uint32_t shownSeq = UINT32_MAX, shownToast = 0, toastUntil = 0, pressedSince = 0;
static View view = View::Offline;
static ViewOptions options;
static int shownArchive = 0, shownRun = 0; // the print the outcome question is about
static uint32_t verdictSentAt = 0;

// ---------------------------------------------------------------- shared helpers

lv_obj_t *ui_box(lv_obj_t *parent)
{
  lv_obj_t *b = lv_obj_create(parent);
  lv_obj_remove_style_all(b);
  lv_obj_set_size(b, ui.w, ui.h);
  lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
  return b;
}

lv_obj_t *ui_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, int x, int y, int w)
{
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  lv_obj_set_pos(l, px(x), px(y));
  lv_obj_set_width(l, px(w));
  lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
  lv_label_set_text(l, "");
  return l;
}

static void plain_button_style(lv_obj_t *b)
{
  lv_obj_set_style_shadow_width(b, 0, 0);
  // The theme grows a pressed button; its parent then clips it into square corners.
  // Keep the size fixed; the theme's darkening is feedback enough.
  lv_obj_set_style_transform_width(b, 0, LV_STATE_PRESSED);
  lv_obj_set_style_transform_height(b, 0, LV_STATE_PRESSED);
}

lv_obj_t *ui_button(lv_obj_t *parent, uint32_t color, int x, int y, int w, int h, const char *text, lv_obj_t **textOut)
{
  lv_obj_t *b = lv_button_create(parent);
  lv_obj_set_pos(b, px(x), px(y));
  lv_obj_set_size(b, px(w), px(h));
  lv_obj_set_style_bg_color(b, lv_color_hex(color), 0);
  lv_obj_set_style_radius(b, px(6), 0);
  lv_obj_set_style_pad_all(b, 0, 0);
  plain_button_style(b);
  if (text) {
    lv_obj_t *l = lv_label_create(b);
    lv_obj_set_style_text_font(l, ui.small, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(C_TEXT), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(l, text);
    lv_obj_center(l);
    if (textOut) *textOut = l;
  }
  return b;
}

lv_obj_t *ui_icon(lv_obj_t *parent, const lv_image_dsc_t *icon, uint32_t color)
{
  lv_obj_t *img = lv_image_create(parent);
  lv_image_set_src(img, icon);
  lv_obj_set_style_image_recolor(img, lv_color_hex(color), 0);
  lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
  if (ui.scale != 256) { // stretched to its size on this screen; untouched at 1 px per unit
    lv_obj_set_size(img, px(icon->header.w), px(icon->header.h));
    lv_image_set_inner_align(img, LV_IMAGE_ALIGN_STRETCH);
  }
  return img;
}

static void show_toast(const char *text)
{
  lv_label_set_text(lblToast, text);
  lv_obj_remove_flag(lblToast, LV_OBJ_FLAG_HIDDEN);
  toastUntil = millis() + TOAST_MS;
}

void ui_send(Command c, int arg, int arg2)
{
  if (!bambuddy_send(c, arg, arg2)) show_toast("Busy, try again");
}

void ui_redraw_soon() { shownSeq = UINT32_MAX; }

// ---------------------------------------------------------------- status screen buttons

static uint32_t look_color(Look l)
{
  switch (l) {
  case Look::Amber:    return C_AMBER;
  case Look::Red:      return C_RED;
  case Look::Green:    return C_GREEN;
  case Look::Blue:     return C_BLUE;
  case Look::LightOn:  return C_LIT;
  case Look::LightOff: return C_GREY;
  }
  return C_GREY;
}

static void draw_buttons()
{
  for (int i = 0; i < MAX_ACTIONS; i++) {
    if (i < actionCount) {
      const Action &a = actions[i];
      lv_label_set_text(btnLabel[i], a.text);
      lv_obj_set_style_bg_color(btn[i], lv_color_hex(look_color(a.look)), 0);
      lv_obj_set_style_text_color(btnLabel[i], lv_color_hex(a.look == Look::LightOn ? C_DARK : C_TEXT), 0);
      // The first button is the main action; the light button stays narrow.
      lv_obj_set_flex_grow(btn[i], a.cmd == Command::SetLight && actionCount > 1 ? 2 : 3);
      lv_obj_remove_flag(btn[i], LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(btn[i], LV_OBJ_FLAG_HIDDEN);
    }
  }
}

static void button_cb(lv_event_t *e)
{
  int i = (int)(intptr_t)lv_event_get_user_data(e);
  if (i >= actionCount) return;
  const Action &a = actions[i];

  switch (lv_event_get_code(e)) {
  case LV_EVENT_PRESSED:
    hold.start = millis();
    hold.fired = false;
    if (a.hold) lv_label_set_text(btnLabel[i], "Keep holding...");
    break;
  case LV_EVENT_PRESSING:
    if (a.hold && !hold.fired && millis() - hold.start >= HOLD_MS) {
      hold.fired = true;
      lv_label_set_text(btnLabel[i], "OK");
      ui_send(a.cmd, a.arg, a.arg2);
    }
    break;
  case LV_EVENT_RELEASED:
  case LV_EVENT_PRESS_LOST:
    if (a.hold && !hold.fired) lv_label_set_text(btnLabel[i], a.text);
    break;
  case LV_EVENT_CLICKED:
    if (!a.hold) ui_send(a.cmd, a.arg, a.arg2);
    break;
  default:
    break;
  }
}

// ---------------------------------------------------------------- outcome question

enum Answer : intptr_t { GOOD = 1, BAD = 2, LATER = 3 };

static void verdict_cb(lv_event_t *e)
{
  intptr_t answer = (intptr_t)lv_event_get_user_data(e);
  if (answer == LATER) {
    // Put off for this print; the plate-clear screen (or whatever is next) comes up.
    options.laterArchive = shownArchive;
    options.laterRun = shownRun;
    ui_redraw_soon();
    return;
  }
  // One answer per tap: the screen only changes after Bambuddy has been read again.
  if (verdictSentAt && millis() - verdictSentAt < VERDICT_DEBOUNCE_MS) return;
  verdictSentAt = millis();
  ui_send(answer == GOOD ? Command::VerdictGood : Command::VerdictReject, shownArchive, shownRun);
}

static void verdict_button(const lv_image_dsc_t *icon, const char *text, uint32_t color, int grow, Answer answer)
{
  lv_obj_t *b = lv_button_create(verdictRow);
  lv_obj_set_height(b, lv_pct(100));
  lv_obj_set_flex_grow(b, grow);
  lv_obj_set_style_bg_color(b, lv_color_hex(color), 0);
  plain_button_style(b);
  lv_obj_add_event_cb(b, verdict_cb, LV_EVENT_CLICKED, (void *)(intptr_t)answer);

  if (icon) {
    lv_obj_center(ui_icon(b, icon, C_TEXT));
  } else {
    lv_obj_t *l = lv_label_create(b);
    lv_obj_set_style_text_font(l, ui.small, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(C_TEXT), 0);
    lv_label_set_text(l, text);
    lv_obj_center(l);
  }
}

// ---------------------------------------------------------------- building the screens

// The calibration cross: odd sizes in pixels, so it has a centre pixel.
#define CROSS_LEN (px(33) | 1)
#define CROSS_THICK (px(3) | 1)

// A row of buttons side by side, sharing the width.
static lv_obj_t *button_row(lv_obj_t *parent, int x, int y, int w, int h)
{
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_remove_style_all(row);
  lv_obj_set_pos(row, px(x), px(y));
  lv_obj_set_size(row, px(w), px(h));
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(row, px(8), 0);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  return row;
}

void ui_begin()
{
  lv_obj_t *scr = lv_screen_active();
  ui_layout_begin(lv_display_get_horizontal_resolution(nullptr), lv_display_get_vertical_resolution(nullptr));
  lv_obj_set_style_bg_color(scr, lv_color_hex(C_BG), 0);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  // Everything below is in units (see ui_internal.h): 320 x 240 on the smallest screen.
  const int W = ui.uw, H = ui.uh;
  const int wide = W - 20;       // the full width, inside a margin of 10
  const int mid = (H - 240) / 2; // moves a screen's centred text down on a taller screen

  // --- status screen: name, wrench (ui_tasks.cpp) and state chip across the top
  mainBox = ui_box(scr);
  lblName = ui_label(mainBox, ui.medium, C_TEXT, 10, 8, W - 192);
  lblState = ui_label(mainBox, ui.small, C_TEXT, W - 130, 8, 120);
  lv_obj_set_style_text_align(lblState, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_bg_opa(lblState, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(lblState, px(6), 0);
  lv_obj_set_style_pad_ver(lblState, px(4), 0);

  lblJob = ui_label(mainBox, ui.small, C_TEXT, 10, 42, wide);

  bar = lv_bar_create(mainBox);
  lv_obj_set_pos(bar, px(10), px(66));
  lv_obj_set_size(bar, px(wide), px(16));
  lv_bar_set_range(bar, 0, 100);

  lblProgress = ui_label(mainBox, ui.medium, C_TEXT, 10, 88, W - 130);
  lblLayers = ui_label(mainBox, ui.small, C_DIM, W - 120, 93, 110);
  lv_obj_set_style_text_align(lblLayers, LV_TEXT_ALIGN_RIGHT, 0);
  lblHeadline = ui_label(mainBox, ui.large, C_TEXT, 10, 66, wide);
  lblTemps = ui_label(mainBox, ui.small, C_DIM, 10, 116, wide);
  lblDetail = ui_label(mainBox, ui.small, C_TEXT, 10, 138, wide);
  lv_obj_set_height(lblDetail, px(34)); // two lines, then "..."

  // --- the buttons, along the bottom edge
  btnRow = button_row(mainBox, 8, H - 62, W - 16, 56);
  for (int i = 0; i < MAX_ACTIONS; i++) {
    btn[i] = lv_button_create(btnRow);
    lv_obj_set_height(btn[i], px(56));
    lv_obj_set_flex_grow(btn[i], 1);
    lv_obj_set_style_pad_hor(btn[i], px(4), 0);
    plain_button_style(btn[i]);
    lv_obj_add_event_cb(btn[i], button_cb, LV_EVENT_ALL, (void *)(intptr_t)i);
    btnLabel[i] = lv_label_create(btn[i]);
    lv_obj_set_style_text_font(btnLabel[i], ui.small, 0);
    lv_obj_set_style_text_align(btnLabel[i], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(btnLabel[i]);
    lv_obj_add_flag(btn[i], LV_OBJ_FLAG_HIDDEN);
  }

  // --- outcome question: two big buttons and a small "Later" in place of the lower half
  verdictRow = button_row(mainBox, 8, 106, W - 16, H - 112);
  verdict_button(&icon_thumb_up, nullptr, C_GREEN, 5, GOOD);
  verdict_button(&icon_thumb_down, nullptr, C_RED, 5, BAD);
  verdict_button(nullptr, "Later", C_GREY, 3, LATER);
  lv_obj_add_flag(verdictRow, LV_OBJ_FLAG_HIDDEN);

  // --- maintenance screen, and its wrench in the status screen's header
  tasks_create(scr, mainBox);

  // --- message screen
  msgBox = ui_box(scr);
  lblMsgTitle = ui_label(msgBox, ui.large, C_TEXT, 10, mid + 40, wide);
  lv_obj_set_style_text_align(lblMsgTitle, LV_TEXT_ALIGN_CENTER, 0);
  lblMsgBody = ui_label(msgBox, ui.small, C_DIM, 10, mid + 90, wide);
  lv_label_set_long_mode(lblMsgBody, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(lblMsgBody, LV_TEXT_ALIGN_CENTER, 0);

  // --- touch calibration: a cross to tap, text in the middle
  calBox = ui_box(scr);
  lv_obj_t *calTitle = ui_label(calBox, ui.medium, C_TEXT, 10, mid + 84, wide);
  lv_obj_set_style_text_align(calTitle, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(calTitle, "Touch calibration");
  lblCal = ui_label(calBox, ui.small, C_DIM, 10, mid + 114, wide);
  lv_label_set_long_mode(lblCal, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(lblCal, LV_TEXT_ALIGN_CENTER, 0);
  for (lv_obj_t **line : {&crossH, &crossV}) {
    *line = lv_obj_create(calBox);
    lv_obj_remove_style_all(*line);
    lv_obj_set_style_bg_color(*line, lv_color_hex(C_LIT), 0);
    lv_obj_set_style_bg_opa(*line, LV_OPA_COVER, 0);
  }
  lv_obj_set_size(crossH, CROSS_LEN, CROSS_THICK);
  lv_obj_set_size(crossV, CROSS_THICK, CROSS_LEN);
  lv_obj_add_flag(calBox, LV_OBJ_FLAG_HIDDEN);

  // --- toast, above everything, over the header: the least needed part of the screen
  lblToast = lv_label_create(lv_layer_top());
  lv_obj_set_style_text_font(lblToast, ui.small, 0);
  lv_obj_set_style_text_color(lblToast, lv_color_hex(C_TEXT), 0);
  lv_obj_set_style_bg_color(lblToast, lv_color_hex(C_CHIP), 0);
  lv_obj_set_style_bg_opa(lblToast, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(lblToast, lv_color_hex(C_DIM), 0);
  lv_obj_set_style_border_width(lblToast, 1, 0);
  lv_obj_set_style_radius(lblToast, px(8), 0);
  lv_obj_set_style_pad_all(lblToast, px(8), 0);
  lv_obj_align(lblToast, LV_ALIGN_TOP_MID, 0, px(4));
  lv_obj_add_flag(lblToast, LV_OBJ_FLAG_HIDDEN);

  lv_obj_add_flag(mainBox, LV_OBJ_FLAG_HIDDEN);
}

void ui_message(const char *title, const char *body)
{
  lv_label_set_text(lblMsgTitle, title);
  lv_label_set_text(lblMsgBody, body);
  lv_obj_add_flag(mainBox, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(calBox, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(tasks_box(), LV_OBJ_FLAG_HIDDEN);
  tasks_close();
  lv_obj_remove_flag(msgBox, LV_OBJ_FLAG_HIDDEN);
  shownSeq = UINT32_MAX; // the status screen redraws when it comes back
  lv_refr_now(nullptr);
}

// x and y are in pixels: the calibration works on the panel's own grid.
void ui_calibrate(int step, int total, int x, int y)
{
  char buf[80];
  snprintf(buf, sizeof(buf), "Tap the centre of the cross (%d of %d).\nA fingernail or stylus works best.", step, total);
  lv_label_set_text(lblCal, buf);
  lv_obj_set_pos(crossH, x - CROSS_LEN / 2, y - CROSS_THICK / 2);
  lv_obj_set_pos(crossV, x - CROSS_THICK / 2, y - CROSS_LEN / 2);
  lv_obj_add_flag(mainBox, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(msgBox, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(tasks_box(), LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(calBox, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(lblToast, LV_OBJ_FLAG_HIDDEN); // nothing may cover the cross or the text
  toastUntil = 0;
  shownSeq = UINT32_MAX;
  lv_refr_now(nullptr);
}

// ---------------------------------------------------------------- drawing the status screen

static void set_headline(const char *text, uint32_t color)
{
  lv_label_set_text(lblHeadline, text);
  lv_obj_set_style_text_color(lblHeadline, lv_color_hex(color), 0);
}

static void set_state_chip(const char *text, uint32_t color)
{
  lv_label_set_text(lblState, text);
  lv_obj_set_style_bg_color(lblState, lv_color_hex(color), 0);
}

static void show(lv_obj_t *o, bool on)
{
  if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static void draw_detail(const Snapshot &s, bool showProgress)
{
  char buf[128], dur[16];
  // One line of "what now": the error, else what is next in the queue.
  if (s.errorCount > 0) {
    snprintf(buf, sizeof(buf), "%s", s.error);
  } else if (view == View::Idle && s.held > 0) {
    snprintf(buf, sizeof(buf), "%d job%s held after a failed print.", s.held, s.held == 1 ? "" : "s");
  } else if (s.nextId != 0) {
    format_duration(dur, sizeof(dur), s.nextSeconds / 60);
    if (s.queued > 1) snprintf(buf, sizeof(buf), "Next of %d (%s): %s", s.queued, dur, s.nextName);
    else snprintf(buf, sizeof(buf), "Next (%s): %s", dur, s.nextName);
  } else if (showProgress && s.stage[0]) {
    snprintf(buf, sizeof(buf), "%s", s.stage);
  } else if (view == View::PlateClear) {
    snprintf(buf, sizeof(buf), s.connected ? "Nothing else queued" : "Printer is switched off");
  } else {
    buf[0] = 0;
  }
  lv_label_set_text(lblDetail, buf);
  lv_obj_set_style_text_color(lblDetail, lv_color_hex(s.errorCount > 0 ? C_BAD_TEXT : C_DIM), 0);
}

static void draw(const Snapshot &s)
{
  char buf[160];

  if (view == View::Offline) {
    describe_offline(s.httpCode, s.printerId, buf, sizeof(buf));
    ui_message("Offline", buf);
    return;
  }
  lv_obj_add_flag(msgBox, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(calBox, LV_OBJ_FLAG_HIDDEN);
  if (tasks_showing()) {
    lv_obj_add_flag(mainBox, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(tasks_box(), LV_OBJ_FLAG_HIDDEN);
    tasks_draw(s);
    return;
  }
  lv_obj_add_flag(tasks_box(), LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(mainBox, LV_OBJ_FLAG_HIDDEN);
  tasks_draw_badge(s);

  lv_label_set_text(lblName, s.name);
  lv_label_set_text(lblJob, s.job);

  bool showProgress = view == View::Printing || view == View::Paused;
  show(bar, showProgress);
  if (showProgress) {
    lv_bar_set_value(bar, progress_percent(s), LV_ANIM_OFF);
    format_progress(buf, sizeof(buf), s);
    lv_label_set_text(lblProgress, buf);
    if (s.totalLayers > 0) snprintf(buf, sizeof(buf), "Layer %d/%d", s.layer, s.totalLayers);
    else buf[0] = 0;
    lv_label_set_text(lblLayers, buf);
  } else {
    lv_label_set_text(lblProgress, "");
    lv_label_set_text(lblLayers, "");
  }

  format_temps(buf, sizeof(buf), s);
  lv_label_set_text(lblTemps, buf);
  draw_detail(s, showProgress);

  // The chip says what the panel wants from you; the headline says what happened.
  bool failed = strcmp(s.state, "FAILED") == 0;
  switch (view) {
  case View::PrinterOffline:
    set_state_chip("Offline", C_GREY);
    set_headline("Printer offline", C_DIM);
    break;
  case View::Printing:
    set_state_chip(strcmp(s.state, "RUNNING") == 0 ? "Printing" : "Preparing", C_BLUE);
    set_headline("", C_TEXT);
    break;
  case View::Paused:
    set_state_chip("Paused", C_AMBER);
    set_headline("", C_TEXT);
    break;
  case View::Verdict:
    set_state_chip("Your call", C_BLUE);
    set_headline("How did it print?", C_TEXT);
    break;
  case View::PlateClear:
    set_state_chip("Clear plate", failed ? C_RED : C_GREEN);
    if (failed) snprintf(buf, sizeof(buf), "Failed at %d%%", progress_percent(s));
    else snprintf(buf, sizeof(buf), "Print finished");
    set_headline(buf, failed ? C_BAD_TEXT : C_OK_TEXT);
    break;
  case View::Error:
    set_state_chip("Error", C_RED);
    set_headline("Needs attention", C_BAD_TEXT);
    break;
  case View::Ready:
    set_state_chip("Ready", C_GREEN);
    set_headline("Ready to start", C_TEXT);
    break;
  default:
    set_state_chip("Idle", C_GREY);
    set_headline(s.held > 0 ? "Queue on hold" : s.nextId != 0 ? "Waiting to start" : "Nothing queued",
                 s.held > 0 ? C_WARN_TEXT : C_DIM);
    break;
  }

  // The outcome question takes over the lower half of the screen.
  bool asking = view == View::Verdict;
  for (lv_obj_t *o : {lblTemps, lblDetail, btnRow}) show(o, !asking);
  show(verdictRow, asking);
  if (asking) {
    shownArchive = s.archiveId;
    shownRun = s.runId;
  }

  actionCount = pick_actions(view, s, actions);
  draw_buttons();
}

// Solid colour while all is well; blinking when someone needs to come over.
static void update_led()
{
  bool on = (millis() / 500) % 2 == 0;
  switch (view) {
  case View::Printing:   display_led(false, false, true); break;
  case View::Paused:     display_led(on, on, false); break;
  case View::Verdict:    display_led(false, on, on); break;
  case View::PlateClear: display_led(false, on, false); break;
  case View::Error:      display_led(on, false, false); break;
  case View::Offline:    display_led(on, false, on); break;
  default:               display_led(false, false, false); break;
  }
}

void ui_update(const Snapshot &s)
{
  // The maintenance screen gives way to the status screen when left alone.
  if (tasks_idle_timeout()) shownSeq = UINT32_MAX;

  // Don't swap the buttons under a finger that is in the middle of a hold. A "finger" that
  // stays far longer than any hold is a stuck or noisy touch, and must not freeze the screen.
  bool pressed = lv_indev_get_state(lv_indev_get_next(nullptr)) == LV_INDEV_STATE_PRESSED;
  if (!pressed) pressedSince = 0;
  else if (!pressedSince) pressedSince = millis() | 1;
  bool holding = pressed && millis() - pressedSince < PRESS_FREEZE_MS;

  if (s.seq != shownSeq && s.seq != 0 && !holding) {
    view = pick_view(s, options);
    draw(s);
    shownSeq = s.seq; // after draw: the offline message resets it
  }

  if (s.toastSeq != shownToast) {
    shownToast = s.toastSeq;
    if (s.toast[0]) show_toast(s.toast);
  } else if (toastUntil && (int32_t)(millis() - toastUntil) > 0) {
    lv_obj_add_flag(lblToast, LV_OBJ_FLAG_HIDDEN);
    toastUntil = 0;
  }

  if (s.seq != 0) update_led();
}
