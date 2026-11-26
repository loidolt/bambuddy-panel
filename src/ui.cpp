// The status screen, the message screens, the calibration screen and the notice band. What to
// show is decided in ui_logic.cpp; the maintenance screen lives in ui_tasks.cpp. The design is
// described in docs/ux-design.md.
#include "ui.h"
#include "bambuddy_api.h"
#include "display.h"
#include "icons.h"
#include "ui_internal.h"
#include "ui_logic.h"

#include <initializer_list>

static const uint32_t NOTICE_MS = 2500, NOTICE_BAD_MS = 6000; // a failure stays to be read
static const uint32_t PRESS_FREEZE_MS = 5000; // a finger this long is not mid-hold any more
static const uint32_t PENDING_GIVE_UP_MS = 12000; // longer than any request can take
static const uint32_t DIM_AFTER_MS = 60000;
static const uint8_t BRIGHT = 100, DIM = 35; // backlight, percent
static const int HEADER_H = 40, BUTTON_H = 56, QUEUE_ROWS = 1 + MAX_MORE;

static lv_obj_t *mainBox, *msgBox, *calBox;
// status screen
static lv_obj_t *lblName, *lblChip, *body, *jobRow, *lblJob, *lblBy, *bar, *progRow, *lblPct, *lblLeft, *lblLayers;
static lv_obj_t *lblHeadline, *lblSub, *lblWhy, *errBox, *lblErr, *lblErrCount, *queueBox, *lblTemps, *lblNext;
static lv_obj_t *queueRow[QUEUE_ROWS], *lblQueueName[QUEUE_ROWS], *lblQueueTime[QUEUE_ROWS];
static lv_obj_t *btnRow, *verdictRow;
static UiButton btn[MAX_ACTIONS], btnGood, btnBad, btnSkip;
// message and code screens
static lv_obj_t *lblMsgTitle, *msgMid, *spinner, *lblMsgBody, *codeBox, *qrFrame, *qr, *lblCode[5], *msgBtnRow;
static UiButton btnMsg[2];
static int msgPressed = 0;
static bool msgOffersSettings = false; // the first button is "Hold to open settings"
// calibration
static lv_obj_t *lblCal, *crossH, *crossV;
// notice band
static lv_obj_t *noticeBox, *lblNotice;
static uint32_t noticeUntil = 0; // 0: none; a command's result otherwise
static bool lostShown = false;   // the band is saying Bambuddy doesn't answer

static Action actions[MAX_ACTIONS];
static int actionCount = 0;
static uint32_t shownSeq = UINT32_MAX, shownToast = 0, pressedSince = 0;
static View view = View::Offline;
static ViewOptions options;
static int shownArchive = 0, shownRun = 0; // the print the outcome question is about
static int errorShown = 0;                 // which of the errors is on screen

// A command Bambuddy has and hasn't answered, or has answered and hasn't been read again.
static struct {
  bool active = false;
  Command cmd = Command::Pause;
  int arg = 0;
  uint32_t since = 0;
  bool answered = false;
  uint32_t seqAtAnswer = 0;
} pend;

static bool dimmed = false;
static uint32_t activeAt = 0;

// ---------------------------------------------------------------- shared helpers

lv_obj_t *ui_plain(lv_obj_t *parent)
{
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
  return o;
}

lv_obj_t *ui_box(lv_obj_t *parent)
{
  lv_obj_t *b = ui_plain(parent);
  lv_obj_set_size(b, ui.w, ui.h);
  // Solid, so a screen covers the one under it: the maintenance screen is still there for a
  // moment after it is closed (see tasks_close), and must not show through.
  lv_obj_set_style_bg_color(b, lv_color_hex(C_BG), 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  return b;
}

static int line_height(const lv_font_t *f) { return lv_font_get_line_height(f); }

// A label on one line; what doesn't fit ends in dots. Give w = 0 for a width set elsewhere.
lv_obj_t *ui_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, int x, int y, int w)
{
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  lv_obj_set_pos(l, px(x), px(y));
  if (w) lv_obj_set_width(l, px(w));
  lv_obj_set_height(l, line_height(font));
  lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
  lv_label_set_text(l, "");
  return l;
}

// How many lines `text` takes in `font` at `widthPx`, and how wide its widest line is.
static int text_lines(const char *text, const lv_font_t *font, int widthPx, int *widest = nullptr)
{
  lv_point_t size;
  lv_text_get_size(&size, text, font, 0, 0, widthPx, LV_TEXT_FLAG_NONE);
  if (widest) *widest = size.x;
  int h = line_height(font);
  return size.y <= h ? 1 : (size.y + h - 1) / h;
}

void ui_set_text(lv_obj_t *label, const char *text, int maxLines)
{
  lv_obj_update_layout(label); // its width may come from the row it is in
  const lv_font_t *font = lv_obj_get_style_text_font(label, 0);
  int lines = text_lines(text, font, lv_obj_get_content_width(label));
  if (lines > maxLines) lines = maxLines;
  lv_obj_set_style_text_line_space(label, 0, 0);
  lv_obj_set_height(label, lines * line_height(font));
  lv_label_set_text(label, text);
}

const lv_font_t *ui_fit_font(const char *text, int widthPx, int maxLines, const lv_font_t *from)
{
  const lv_font_t *sizes[] = {ui.large, ui.medium, ui.small};
  bool reached = false;
  for (const lv_font_t *f : sizes) {
    reached = reached || f == from;
    if (!reached) continue;
    int widest;
    if (text_lines(text, f, widthPx, &widest) <= maxLines && widest <= widthPx) return f;
  }
  return ui.small;
}

lv_obj_t *ui_icon(lv_obj_t *parent, const lv_image_dsc_t *icon, uint32_t color)
{
  lv_obj_t *img = lv_image_create(parent);
  lv_image_set_src(img, icon);
  lv_obj_set_style_image_recolor(img, lv_color_hex(color), 0);
  lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
  return img;
}

static void show(lv_obj_t *o, bool on)
{
  if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static lv_obj_t *row_of(lv_obj_t *parent, int gap)
{
  lv_obj_t *r = ui_plain(parent);
  lv_obj_set_width(r, lv_pct(100));
  lv_obj_set_height(r, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(r, px(gap), 0);
  return r;
}

static uint32_t text_of(Tone t)
{
  switch (t) {
  case Tone::Dim:     return C_DIM;
  case Tone::Go:      return C_OK_TEXT;
  case Tone::Caution: return C_WARN_TEXT;
  case Tone::Danger:  return C_BAD_TEXT;
  default:            return C_TEXT;
  }
}

static uint32_t fill_of(Tone t)
{
  switch (t) {
  case Tone::Go:      return C_GREEN;
  case Tone::Neutral: return C_BLUE;
  case Tone::Caution: return C_AMBER;
  case Tone::Danger:  return C_RED;
  default:            return C_GREY;
  }
}

// ---------------------------------------------------------------- backlight

static void set_dimmed(bool on)
{
  if (on == dimmed) return;
  dimmed = on;
  display_brightness(on ? DIM : BRIGHT);
}

void ui_touched() { activeAt = millis() | 1; }

bool ui_touch_wakes()
{
  ui_touched();
  if (!dimmed) return false;
  set_dimmed(false);
  return true;
}

// ---------------------------------------------------------------- the button

static void style_button(UiButton &b)
{
  uint32_t bg = C_CHIP, text = C_TEXT, fill = C_FILL_QUIET, border = 0;
  switch (b.kind) {
  case Kind::Go:      bg = C_GREEN; fill = C_FILL_GO; break;
  case Kind::Neutral: bg = C_BLUE;  fill = C_FILL_NEUTRAL; break;
  case Kind::Stop:    bg = C_BG; text = C_BAD_TEXT; fill = C_RED; border = C_RED; break;
  case Kind::Quiet:   break;
  }
  if (!b.enabled) {
    bg = b.kind == Kind::Stop ? C_BG : C_ROW;
    text = C_DIM;
    if (border) border = C_GREY;
  }
  lv_obj_set_style_bg_color(b.btn, lv_color_hex(bg), 0);
  lv_obj_set_style_border_width(b.btn, border ? px(2) : 0, 0);
  lv_obj_set_style_border_color(b.btn, lv_color_hex(border), 0);
  lv_obj_set_style_text_color(b.label, lv_color_hex(text), 0);
  if (b.fill) lv_obj_set_style_bg_color(b.fill, lv_color_hex(fill), 0);
}

static void set_fill(UiButton &b, uint32_t elapsed)
{
  if (!b.fill) return;
  if (elapsed > HOLD_MS) elapsed = HOLD_MS;
  int w = (int)((int64_t)lv_obj_get_width(b.btn) * elapsed / HOLD_MS);
  lv_obj_set_width(b.fill, w);
  show(b.fill, w > 0);
  // Stop's light red words would vanish against its red fill.
  if (b.kind == Kind::Stop && b.enabled) lv_obj_set_style_text_color(b.label, lv_color_hex(w > 0 ? C_TEXT : C_BAD_TEXT), 0);
}

static void button_event(lv_event_t *e)
{
  UiButton &b = *(UiButton *)lv_event_get_user_data(e);
  switch (lv_event_get_code(e)) {
  case LV_EVENT_PRESSED:
    b.swallowed = ui_touch_wakes(); // a touch on a dimmed screen only brightens it
    b.idAtPress = b.id;
    b.downAt = millis();
    b.fired = false;
    break;
  case LV_EVENT_PRESSING:
    // The screen can change under a finger that stays: what was pressed is then not what is
    // there, and must not be acted on.
    if (b.swallowed || !b.enabled || !b.hold || b.fired || b.idAtPress != b.id) break;
    set_fill(b, millis() - b.downAt);
    if (millis() - b.downAt >= HOLD_MS) {
      b.fired = true;
      if (b.fire) b.fire(b);
    }
    break;
  case LV_EVENT_RELEASED:
  case LV_EVENT_PRESS_LOST:
    ui_touched();
    if (b.hold && !b.fired) set_fill(b, 0); // let go early: nothing happened
    break;
  case LV_EVENT_CLICKED:
    if (!b.swallowed && b.enabled && !b.hold && b.idAtPress == b.id && b.fire) b.fire(b);
    b.swallowed = false;
    break;
  default:
    break;
  }
}

void ui_button_create(UiButton &b, lv_obj_t *parent, void (*fire)(UiButton &), int tag)
{
  b.fire = fire;
  b.tag = tag;
  b.btn = lv_button_create(parent);
  lv_obj_set_style_shadow_width(b.btn, 0, 0);
  lv_obj_set_style_radius(b.btn, px(6), 0);
  lv_obj_set_style_pad_all(b.btn, 0, 0);
  // The theme grows a pressed button; its parent then clips it into square corners. Keep the
  // size fixed; the theme's darkening is feedback enough.
  lv_obj_set_style_transform_width(b.btn, 0, LV_STATE_PRESSED);
  lv_obj_set_style_transform_height(b.btn, 0, LV_STATE_PRESSED);
  lv_obj_remove_flag(b.btn, LV_OBJ_FLAG_SCROLLABLE);

  b.fill = nullptr; // made when the button first becomes a hold (ui_button_set)
  b.label = lv_label_create(b.btn);
  lv_obj_set_style_text_font(b.label, ui.small, 0);
  lv_obj_set_style_text_align(b.label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_line_space(b.label, 0, 0);
  lv_label_set_text(b.label, "");
  lv_obj_center(b.label);
  lv_obj_add_event_cb(b.btn, button_event, LV_EVENT_ALL, &b);
  style_button(b);
}

void ui_button_set(UiButton &b, const char *text, Kind kind, bool hold, Icon icon, int id)
{
  b.kind = kind;
  b.hold = hold;
  b.id = id;
  b.fired = false;
  if (hold && !b.fill) {
    // What a hold fills with. Rounded like the button, not clipped to it: clipping to rounded
    // corners makes LVGL draw the button into a buffer of its own, which the panel has no
    // memory for. Only hold buttons have one: every object costs memory.
    b.fill = ui_plain(b.btn);
    lv_obj_set_style_bg_opa(b.fill, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(b.fill, px(6), 0);
    lv_obj_set_height(b.fill, lv_pct(100));
    lv_obj_set_pos(b.fill, 0, 0);
    lv_obj_add_flag(b.fill, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_move_to_index(b.fill, 0); // under the words
    lv_obj_add_flag(b.fill, LV_OBJ_FLAG_HIDDEN);
  }
  char buf[80];
  const char *symbol = icon == Icon::Pause  ? LV_SYMBOL_PAUSE
                       : icon == Icon::Play ? LV_SYMBOL_PLAY
                       : icon == Icon::Stop ? LV_SYMBOL_STOP
                       : icon == Icon::Check ? LV_SYMBOL_OK
                                             : "";
  snprintf(buf, sizeof(buf), "%s%s%s", symbol, *symbol ? "  " : "", text);
  // The larger size when the words fit the button, on as many lines as they were written in.
  lv_obj_update_layout(b.btn);
  int lines = 1;
  for (const char *p = text; *p; p++) lines += *p == '\n';
  const lv_font_t *font = ui_fit_font(buf, lv_obj_get_width(b.btn) - px(12), lines, ui.medium);
  if (lines * line_height(font) > lv_obj_get_height(b.btn) - px(4)) font = ui.small;
  lv_obj_set_style_text_font(b.label, font, 0);
  lv_label_set_text(b.label, buf);
  set_fill(b, 0);
  style_button(b);
}

void ui_button_enable(UiButton &b, bool on)
{
  b.enabled = on;
  if (on) lv_obj_add_flag(b.btn, LV_OBJ_FLAG_CLICKABLE);
  else lv_obj_remove_flag(b.btn, LV_OBJ_FLAG_CLICKABLE);
  style_button(b);
}

// ---------------------------------------------------------------- notice band

static void draw_notice(const char *text, bool bad)
{
  char buf[96];
  snprintf(buf, sizeof(buf), "%s%s", bad ? LV_SYMBOL_WARNING "  " : "", text);
  lv_label_set_text(lblNotice, buf);
  lv_obj_set_style_bg_color(noticeBox, lv_color_hex(bad ? C_RED : C_CHIP), 0);
  lv_obj_set_style_border_width(noticeBox, bad ? 0 : 1, 0);
  show(noticeBox, true);
}

static void show_notice(const char *text, bool bad)
{
  draw_notice(text, bad);
  noticeUntil = (millis() + (bad ? NOTICE_BAD_MS : NOTICE_MS)) | 1;
  lostShown = false;
}

static void hide_notice()
{
  show(noticeBox, false);
  noticeUntil = 0;
  lostShown = false;
}

// ---------------------------------------------------------------- commands under way

void ui_send(Command c, int arg, int arg2)
{
  if (pend.active) return;
  if (!bambuddy_send(c, arg, arg2)) {
    show_notice("Busy, try again", true);
    return;
  }
  pend.active = true;
  pend.cmd = c;
  pend.arg = arg;
  pend.since = millis();
  pend.answered = false;
}

bool ui_busy() { return pend.active; }
bool ui_pending(Command c, int arg) { return pend.active && pend.cmd == c && pend.arg == arg; }
bool ui_answered() { return pend.active && pend.answered; }

void ui_redraw_soon() { shownSeq = UINT32_MAX; }

void ui_open_settings()
{
  ui_message("Opening settings", "Restarting...");
  panel_restart(true);
}

// ---------------------------------------------------------------- status screen buttons

static int action_id(const Action &a) { return ((int)a.cmd + 1) * 1000003 + a.arg * 31 + a.arg2; }

static void draw_buttons()
{
  for (int i = 0; i < MAX_ACTIONS; i++) {
    show(btn[i].btn, i < actionCount);
    if (i >= actionCount) continue;
    const Action &a = actions[i];
    // Beside a main button a quiet one is narrower; two of equal weight share the row.
    lv_obj_set_flex_grow(btn[i].btn, a.kind == Kind::Quiet && actionCount > 1 ? 2 : 3);
  }
  lv_obj_update_layout(btnRow);
  for (int i = 0; i < actionCount; i++) {
    const Action &a = actions[i];
    bool under_way = ui_pending(a.cmd, a.arg);
    ui_button_set(btn[i], under_way ? a.pending : a.text, a.kind, a.hold, under_way ? Icon::None : a.icon, action_id(a));
    // Nothing can be pressed while a command is under way, or while Bambuddy isn't answering.
    ui_button_enable(btn[i], !ui_busy() && !lostShown);
  }
}

static void action_fire(UiButton &b)
{
  if (b.tag >= actionCount) return;
  const Action a = actions[b.tag];
  ui_send(a.cmd, a.arg, a.arg2);
  draw_buttons(); // at once: the finger may still be down, which holds back the full redraw
}

// ---------------------------------------------------------------- outcome question

enum Answer { GOOD = 1, BAD = 2, SKIP = 3 };

static void draw_verdict_buttons()
{
  for (UiButton *b : {&btnGood, &btnBad, &btnSkip}) ui_button_enable(*b, !ui_busy() && !lostShown);
}

static void verdict_fire(UiButton &b)
{
  if (b.tag == SKIP) {
    // Skipped for this print; the plate-clear screen (or whatever is next) comes up.
    options.skippedArchive = shownArchive;
    options.skippedRun = shownRun;
    ui_redraw_soon();
    return;
  }
  ui_send(b.tag == GOOD ? Command::VerdictGood : Command::VerdictReject, shownArchive, shownRun);
  draw_verdict_buttons(); // one answer per tap: nothing more is taken until Bambuddy has it
}

static void verdict_button(UiButton &b, const lv_image_dsc_t *icon, const char *text, Kind kind, int grow, Answer answer)
{
  ui_button_create(b, verdictRow, verdict_fire, answer);
  lv_obj_set_height(b.btn, lv_pct(100));
  lv_obj_set_flex_grow(b.btn, grow);
  ui_button_set(b, text, kind, false);
  if (icon) lv_obj_center(ui_icon(b.btn, icon, C_TEXT));
}

// ---------------------------------------------------------------- building the screens

// The calibration cross: odd sizes in pixels, so it has a centre pixel.
#define CROSS_LEN (px(33) | 1)
#define CROSS_THICK (px(3) | 1)

// A row of buttons side by side, sharing the width.
static lv_obj_t *button_row(lv_obj_t *parent, int x, int y, int w, int h)
{
  lv_obj_t *row = ui_plain(parent);
  lv_obj_set_pos(row, px(x), px(y));
  lv_obj_set_size(row, px(w), px(h));
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(row, px(8), 0);
  return row;
}

static void error_next(lv_event_t *)
{
  if (ui_touch_wakes()) return;
  errorShown++; // wraps when drawn
  ui_redraw_soon();
}

static void notice_tapped(lv_event_t *)
{
  ui_touched();
  if (noticeUntil) hide_notice(); // a command's result can be put away; "no answer" stays
}

static void message_fire(UiButton &b)
{
  if (b.tag == 1 && msgOffersSettings) ui_open_settings();
  else msgPressed = b.tag;
}

static void build_status(lv_obj_t *scr)
{
  const int W = ui.uw, H = ui.uh;
  mainBox = ui_box(scr);

  // Header: the printer's name takes what the wrench (ui_tasks.cpp) and the chip leave.
  lv_obj_t *header = row_of(mainBox, HEADER_GAP);
  lv_obj_set_size(header, ui.w, px(HEADER_H));
  lv_obj_set_style_pad_hor(header, px(10), 0);
  lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lblName = ui_label(header, ui.medium, C_TEXT, 0, 0, 0);
  lv_obj_set_flex_grow(lblName, 1);
  filament_create(header);
  tasks_create(scr, header);
  lblChip = lv_label_create(header);
  lv_obj_set_style_text_font(lblChip, ui.small, 0);
  lv_obj_set_style_text_color(lblChip, lv_color_hex(C_TEXT), 0);
  lv_obj_set_style_bg_opa(lblChip, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(lblChip, px(6), 0);
  lv_obj_set_style_pad_hor(lblChip, px(10), 0);
  lv_obj_set_style_pad_ver(lblChip, px(4), 0);
  lv_label_set_text(lblChip, "");

  // Body: a column; each view shows the parts it needs and the rest take no room.
  body = ui_plain(mainBox);
  lv_obj_set_pos(body, px(10), px(HEADER_H));
  lv_obj_set_width(body, px(W - 20));
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(body, px(6), 0);

  jobRow = row_of(body, 8);
  lblJob = ui_label(jobRow, ui.small, C_TEXT, 0, 0, 0);
  lv_obj_set_flex_grow(lblJob, 1);
  lblBy = ui_label(jobRow, ui.small, C_DIM, 0, 0, 0);
  lv_obj_set_width(lblBy, LV_SIZE_CONTENT);
  lv_label_set_long_mode(lblBy, LV_LABEL_LONG_CLIP);

  bar = lv_bar_create(body);
  lv_obj_set_size(bar, lv_pct(100), px(12));
  lv_bar_set_range(bar, 0, 100);

  progRow = row_of(body, 10);
  lv_obj_set_flex_align(progRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
  lblPct = ui_label(progRow, ui.large, C_TEXT, 0, 0, 0);
  lblLeft = ui_label(progRow, ui.medium, C_TEXT, 0, 0, 0);
  for (lv_obj_t *l : {lblPct, lblLeft}) {
    lv_obj_set_width(l, LV_SIZE_CONTENT);
    lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
  }
  lblLayers = ui_label(progRow, ui.small, C_DIM, 0, 0, 0);
  lv_obj_set_flex_grow(lblLayers, 1);
  lv_obj_set_style_text_align(lblLayers, LV_TEXT_ALIGN_RIGHT, 0);

  lblHeadline = ui_label(body, ui.large, C_TEXT, 0, 0, 0);
  lblSub = ui_label(body, ui.small, C_DIM, 0, 0, 0);
  lblWhy = ui_label(body, ui.small, C_WARN_TEXT, 0, 0, 0);
  for (lv_obj_t *l : {lblHeadline, lblSub, lblWhy}) lv_obj_set_width(l, lv_pct(100));

  // The error: a box, not just red words, with a count when there are more. A tap shows the next.
  errBox = row_of(body, 8);
  lv_obj_set_style_bg_color(errBox, lv_color_hex(C_ROW), 0);
  lv_obj_set_style_bg_opa(errBox, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(errBox, px(6), 0);
  lv_obj_set_style_border_side(errBox, LV_BORDER_SIDE_LEFT, 0);
  lv_obj_set_style_border_width(errBox, px(4), 0);
  lv_obj_set_style_border_color(errBox, lv_color_hex(C_RED), 0);
  lv_obj_set_style_pad_hor(errBox, px(8), 0);
  lv_obj_set_style_pad_ver(errBox, px(6), 0);
  lv_obj_set_style_min_height(errBox, px(TARGET), 0);
  lv_obj_set_flex_align(errBox, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_t *warn = ui_label(errBox, ui.small, C_BAD_TEXT, 0, 0, 0);
  lv_obj_set_width(warn, LV_SIZE_CONTENT);
  lv_label_set_text(warn, LV_SYMBOL_WARNING);
  lblErr = ui_label(errBox, ui.small, C_BAD_TEXT, 0, 0, 0);
  lv_obj_set_flex_grow(lblErr, 1);
  lblErrCount = ui_label(errBox, ui.small, C_DIM, 0, 0, 0);
  lv_obj_set_width(lblErrCount, LV_SIZE_CONTENT);
  lv_label_set_long_mode(lblErrCount, LV_LABEL_LONG_CLIP);
  lv_obj_add_event_cb(errBox, error_next, LV_EVENT_CLICKED, nullptr);

  // What is queued: a name and how long it takes, one job to a line.
  queueBox = ui_plain(body);
  lv_obj_set_width(queueBox, lv_pct(100));
  lv_obj_set_height(queueBox, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(queueBox, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(queueBox, px(4), 0);
  for (int i = 0; i < QUEUE_ROWS; i++) {
    queueRow[i] = row_of(queueBox, 8);
    lblQueueName[i] = ui_label(queueRow[i], ui.small, i == 0 ? C_TEXT : C_DIM, 0, 0, 0);
    lv_obj_set_flex_grow(lblQueueName[i], 1);
    lblQueueTime[i] = ui_label(queueRow[i], ui.small, C_DIM, 0, 0, 0);
    lv_obj_set_width(lblQueueTime[i], LV_SIZE_CONTENT);
    lv_label_set_long_mode(lblQueueTime[i], LV_LABEL_LONG_CLIP);
  }

  lblTemps = ui_label(body, ui.small, C_DIM, 0, 0, 0);
  lblNext = ui_label(body, ui.small, C_DIM, 0, 0, 0);
  for (lv_obj_t *l : {lblTemps, lblNext}) lv_obj_set_width(l, lv_pct(100));

  // The buttons, along the bottom edge, in the same place in every view.
  btnRow = button_row(mainBox, 8, H - BUTTON_H - 6, W - 16, BUTTON_H);
  for (int i = 0; i < MAX_ACTIONS; i++) {
    ui_button_create(btn[i], btnRow, action_fire, i);
    lv_obj_set_height(btn[i].btn, lv_pct(100));
    lv_obj_set_flex_grow(btn[i].btn, 1);
    show(btn[i].btn, false);
  }

  // Outcome question: two big buttons and a small "Skip" in place of the lower half.
  verdictRow = button_row(mainBox, 8, H - 110, W - 16, 104);
  verdict_button(btnGood, ICON(icon_thumb_up), "", Kind::Go, 5, GOOD);
  verdict_button(btnBad, ICON(icon_thumb_down), "", Kind::Stop, 5, BAD);
  verdict_button(btnSkip, nullptr, "Skip", Kind::Quiet, 3, SKIP);
  show(verdictRow, false);
}

// The message and code screens, and the calibration screen below, are made when they are shown
// and taken away when the status screen comes back: LVGL's memory is small, and only one of
// them and the status screen is ever needed at a time.
static void build_message()
{
  if (msgBox) return;
  const int W = ui.uw, H = ui.uh;
  msgBox = ui_box(lv_screen_active());
  for (UiButton &b : btnMsg) b = UiButton();
  lblMsgTitle = ui_label(msgBox, ui.medium, C_TEXT, 10, 9, W - 20);

  // A plain message: its words in the middle, under a spinner when the panel is waiting.
  msgMid = ui_plain(msgBox);
  lv_obj_set_pos(msgMid, px(10), px(HEADER_H));
  lv_obj_set_width(msgMid, px(W - 20));
  lv_obj_set_flex_flow(msgMid, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(msgMid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(msgMid, px(12), 0);
#if LV_USE_SPINNER
  spinner = lv_spinner_create(msgMid);
  lv_spinner_set_anim_params(spinner, 1200, 240);
  lv_obj_set_size(spinner, px(34), px(34));
  lv_obj_set_style_arc_width(spinner, px(4), LV_PART_MAIN);
  lv_obj_set_style_arc_width(spinner, px(4), LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(spinner, lv_color_hex(C_CHIP), LV_PART_MAIN);
  lv_obj_set_style_arc_color(spinner, lv_color_hex(C_BAR), LV_PART_INDICATOR);
  lv_obj_remove_flag(spinner, LV_OBJ_FLAG_CLICKABLE);
#else
  spinner = ui_plain(msgMid);
#endif
  lblMsgBody = lv_label_create(msgMid);
  lv_obj_set_width(lblMsgBody, lv_pct(100));
  lv_obj_set_style_text_color(lblMsgBody, lv_color_hex(C_TEXT), 0);
  lv_obj_set_style_text_align(lblMsgBody, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(lblMsgBody, LV_LABEL_LONG_WRAP);

  // Something to join or open: a QR code, and beside it the same as text, large enough to type from.
  codeBox = ui_plain(msgBox);
  lv_obj_set_pos(codeBox, px(10), px(HEADER_H + 4));
  lv_obj_set_size(codeBox, px(W - 20), px(H - HEADER_H - 4));
  const int QR = 92, COL = QR + 12 + 8;
  qrFrame = ui_plain(codeBox); // the white margin a QR code needs around it
  lv_obj_set_size(qrFrame, px(QR + 12), px(QR + 12));
  lv_obj_set_style_bg_color(qrFrame, lv_color_white(), 0);
  lv_obj_set_style_bg_opa(qrFrame, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(qrFrame, px(4), 0);
#if LV_USE_QRCODE
  qr = lv_qrcode_create(qrFrame);
  lv_qrcode_set_size(qr, px(QR));
  lv_qrcode_set_dark_color(qr, lv_color_black());
  lv_qrcode_set_light_color(qr, lv_color_white());
  lv_obj_center(qr);
#endif
  const int y[5] = {0, 16, 44, 60, 94};
  for (int i = 0; i < 5; i++) lblCode[i] = ui_label(codeBox, ui.small, i == 1 || i == 3 ? C_TEXT : C_DIM, COL, y[i], W - 20 - COL);

  msgBtnRow = button_row(msgBox, 8, H - BUTTON_H - 6, W - 16, BUTTON_H);
  for (int i = 0; i < 2; i++) {
    ui_button_create(btnMsg[i], msgBtnRow, message_fire, i + 1);
    lv_obj_set_height(btnMsg[i].btn, lv_pct(100));
    lv_obj_set_flex_grow(btnMsg[i].btn, 1);
  }
  show(msgBtnRow, false);
}

static void drop_message()
{
  if (!msgBox) return;
  lv_obj_delete(msgBox); // never reached from one of its own buttons, so it can go at once
  msgBox = nullptr;
  msgPressed = 0;
  msgOffersSettings = false;
}

static void build_calibration()
{
  if (calBox) return;
  const int W = ui.uw, H = ui.uh;
  const int wide = W - 20;       // the full width, inside a margin of 10
  const int mid = (H - 240) / 2; // moves the centred text down on a taller screen
  calBox = ui_box(lv_screen_active());
  lv_obj_t *calTitle = ui_label(calBox, ui.medium, C_TEXT, 10, mid + 84, wide);
  lv_obj_set_style_text_align(calTitle, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(calTitle, "Touch calibration");
  lblCal = lv_label_create(calBox);
  lv_obj_set_style_text_font(lblCal, ui.small, 0);
  lv_obj_set_style_text_color(lblCal, lv_color_hex(C_DIM), 0);
  lv_obj_set_pos(lblCal, px(10), px(mid + 114));
  lv_obj_set_width(lblCal, px(wide));
  lv_label_set_long_mode(lblCal, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(lblCal, LV_TEXT_ALIGN_CENTER, 0);
  for (lv_obj_t **line : {&crossH, &crossV}) {
    *line = ui_plain(calBox);
    lv_obj_set_style_bg_color(*line, lv_color_hex(C_LIT), 0);
    lv_obj_set_style_bg_opa(*line, LV_OPA_COVER, 0);
  }
  lv_obj_set_size(crossH, CROSS_LEN, CROSS_THICK);
  lv_obj_set_size(crossV, CROSS_THICK, CROSS_LEN);
}

static void drop_calibration()
{
  if (!calBox) return;
  lv_obj_delete(calBox);
  calBox = nullptr;
}

void ui_begin()
{
  lv_obj_t *scr = lv_screen_active();
  ui_layout_begin(lv_display_get_horizontal_resolution(nullptr), lv_display_get_vertical_resolution(nullptr));
  lv_obj_set_style_bg_color(scr, lv_color_hex(C_BG), 0);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  build_status(scr); // and, through tasks_create, the wrench that opens the maintenance screen

  // The notice: a band over the whole header, above everything. It covers the header on
  // purpose and entirely, the part of the screen that can best be missed for a moment.
  noticeBox = ui_plain(lv_layer_top());
  lv_obj_set_size(noticeBox, ui.w, px(HEADER_H));
  lv_obj_set_style_bg_opa(noticeBox, LV_OPA_COVER, 0);
  lv_obj_set_style_border_side(noticeBox, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_color(noticeBox, lv_color_hex(C_GREY), 0);
  lv_obj_set_style_pad_hor(noticeBox, px(10), 0);
  lv_obj_add_flag(noticeBox, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(noticeBox, px(TARGET - HEADER_H));
  lv_obj_add_event_cb(noticeBox, notice_tapped, LV_EVENT_CLICKED, nullptr);
  lblNotice = ui_label(noticeBox, ui.small, C_TEXT, 0, 0, 0);
  lv_obj_set_width(lblNotice, lv_pct(100));
  lv_obj_align(lblNotice, LV_ALIGN_LEFT_MID, 0, 0);
  show(noticeBox, false);

  show(mainBox, false);
  activeAt = millis() | 1;
}

// ---------------------------------------------------------------- message screens

static void show_message_screen(const char *title, bool code)
{
  build_message();
  drop_calibration();
  lv_label_set_text(lblMsgTitle, title);
  show(msgMid, !code);
  show(codeBox, code);
  show(msgBtnRow, false);
  msgPressed = 0;
  msgOffersSettings = false;
  show(mainBox, false);
  tasks_close();
  filament_close();
  shownSeq = UINT32_MAX; // the status screen redraws when it comes back
}

// The middle of a plain message: as tall as the room above the buttons, when there are any.
static void size_message(bool buttons)
{
  lv_obj_set_height(msgMid, px(ui.uh - HEADER_H - (buttons ? BUTTON_H + 12 : 10)));
}

void ui_message(const char *title, const char *body, bool waiting)
{
  show_message_screen(title, false);
  show(spinner, waiting);
  // A short message is worth reading from a step away; a long one has to fit.
  lv_obj_set_style_text_font(lblMsgBody, ui_fit_font(body, px(ui.uw - 20), 3, ui.medium), 0);
  lv_label_set_text(lblMsgBody, body);
  size_message(false);
  lv_refr_now(nullptr);
}

void ui_code(const CodeScreen &c)
{
  show_message_screen(c.title, true);
#if LV_USE_QRCODE
  lv_qrcode_update(qr, c.qr, strlen(c.qr));
#endif
  const char *text[5] = {c.label1, c.value1, c.label2, c.value2, c.note};
  lv_obj_update_layout(codeBox);
  for (int i = 0; i < 5; i++) {
    // The two values are what gets typed: as large as fits beside the code.
    if (i == 1 || i == 3) {
      const lv_font_t *f = ui_fit_font(text[i], lv_obj_get_width(lblCode[i]), 1, i == 3 ? ui.large : ui.medium);
      lv_obj_set_style_text_font(lblCode[i], f, 0);
      lv_obj_set_height(lblCode[i], line_height(f));
    }
    lv_label_set_text(lblCode[i], text[i]);
  }
  lv_refr_now(nullptr);
}

void ui_message_buttons(const char *a, const char *b)
{
  if (!msgBox) return; // no message screen is showing
  const char *text[2] = {a, b};
  show(msgBtnRow, a || b);
  for (int i = 0; i < 2; i++) show(btnMsg[i].btn, text[i] != nullptr);
  lv_obj_update_layout(msgBtnRow);
  for (int i = 0; i < 2; i++)
    if (text[i]) ui_button_set(btnMsg[i], text[i], Kind::Quiet, false);
  size_message(a || b);
  msgPressed = 0;
  msgOffersSettings = false;
  lv_refr_now(nullptr);
}

int ui_message_pressed()
{
  int p = msgPressed;
  msgPressed = 0;
  return p;
}

// Bambuddy said no, or has been silent too long: say why, and offer the way to the fix.
static void show_problem(const Snapshot &s)
{
  char buf[160];
  describe_offline(s.httpCode, s.printerId, buf, sizeof(buf));
  ui_message(view == View::CheckSettings ? "Check settings" : "Offline", buf);
  ui_message_buttons("Hold to open settings");
  ui_button_set(btnMsg[0], "Hold to open settings", Kind::Neutral, true);
  msgOffersSettings = true;
  lv_refr_now(nullptr);
}

// x and y are in pixels: the calibration works on the panel's own grid.
void ui_calibrate(int step, int total, int x, int y)
{
  char buf[80];
  snprintf(buf, sizeof(buf), "Tap the centre of the cross (%d of %d).\nA fingernail or stylus works best.", step, total);
  build_calibration();
  drop_message();
  tasks_close();
  filament_close();
  lv_label_set_text(lblCal, buf);
  lv_obj_set_pos(crossH, x - CROSS_LEN / 2, y - CROSS_THICK / 2);
  lv_obj_set_pos(crossV, x - CROSS_THICK / 2, y - CROSS_LEN / 2);
  show(mainBox, false);
  hide_notice(); // nothing may cover the cross or the text
  shownSeq = UINT32_MAX;
  lv_refr_now(nullptr);
}

// ---------------------------------------------------------------- drawing the status screen

// The headline: as large as fits on one line, else the middle size on two.
static void draw_headline(const Face &f)
{
  show(lblHeadline, f.headline[0] != 0);
  if (!f.headline[0]) return;
  lv_obj_update_layout(lblHeadline);
  int w = lv_obj_get_width(lblHeadline);
  const lv_font_t *font = f.headlineIsName ? ui.medium : ui_fit_font(f.headline, w, 1, ui.large);
  int lines = 1;
  if (font == ui.small || f.headlineIsName) {
    font = ui.medium;
    lines = 2;
  }
  lv_obj_set_style_text_font(lblHeadline, font, 0);
  lv_obj_set_style_text_color(lblHeadline, lv_color_hex(text_of(f.headlineTone)), 0);
  ui_set_text(lblHeadline, f.headline, lines);
}

static void draw_error(const Snapshot &s, bool large)
{
  int kept = s.errorCount < MAX_ERRORS ? s.errorCount : MAX_ERRORS;
  if (errorShown >= kept) errorShown = 0;
  char buf[32] = "";
  if (s.errorCount > 1) snprintf(buf, sizeof(buf), "%d of %d", errorShown + 1, s.errorCount);
  lv_label_set_text(lblErrCount, buf);
  show(lblErrCount, buf[0] != 0);
  lv_obj_set_style_text_font(lblErr, large ? ui.medium : ui.small, 0);
  // With more than one, a tap shows the next; with one there is nothing to tap for.
  if (s.errorCount > 1) lv_obj_add_flag(errBox, LV_OBJ_FLAG_CLICKABLE);
  else lv_obj_remove_flag(errBox, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_update_layout(errBox);
  ui_set_text(lblErr, s.errors[errorShown], large ? 4 : 2);
}

// Up to `rows` queued jobs, starting with the next one or with the one after it.
static void draw_queue(const Snapshot &s, bool fromNext, int rows)
{
  char dur[16];
  int shown = 0;
  auto put = [&](const char *name, int seconds) {
    if (shown >= rows || shown >= QUEUE_ROWS) return;
    format_duration(dur, sizeof(dur), seconds / 60);
    lv_label_set_text(lblQueueTime[shown], dur);
    lv_obj_set_style_text_color(lblQueueName[shown], lv_color_hex(shown == 0 && fromNext ? C_TEXT : C_DIM), 0);
    show(queueRow[shown], true);
    lv_obj_update_layout(queueRow[shown]);
    ui_set_text(lblQueueName[shown], name);
    shown++;
  };
  for (int i = 0; i < QUEUE_ROWS; i++) show(queueRow[i], false);
  if (s.nextId != 0) {
    show(queueBox, true);
    if (fromNext) put(s.nextName, s.nextSeconds);
    for (int i = 0; i < s.moreCount; i++) put(s.more[i].name, s.more[i].seconds);
  }
  show(queueBox, shown > 0);
}

static void draw(const Snapshot &s)
{
  char buf[160];

  if (view == View::Offline || view == View::CheckSettings) {
    hide_notice();
    show_problem(s);
    return;
  }
  drop_message();
  drop_calibration();
  if (tasks_showing()) {
    filament_close();
    show(mainBox, false);
    tasks_draw(s);
    return;
  }
  tasks_close();
  if (filament_showing()) {
    show(mainBox, false);
    filament_draw(s);
    return;
  }
  filament_close();
  show(mainBox, true);
  tasks_draw_badge(s);
  filament_draw_badge(s);

  Face f;
  pick_face(view, s, f);
  const bool prep = view == View::Printing && preparing(s);
  const bool running = view == View::Printing && !prep;
  const bool paused = view == View::Paused;
  const bool asking = view == View::Verdict;
  const bool error = s.errorCount > 0 && (running || prep || paused || view == View::Error);

  actionCount = pick_actions(view, s, actions);
  // The body ends above whatever is along the bottom, or runs to the edge when nothing is.
  lv_obj_set_height(body, px(ui.uh - HEADER_H - (asking ? 116 : actionCount ? BUTTON_H + 12 : 6)));

  // The chip names the state and nothing else. One missed answer greys it: what is on screen
  // may be a few seconds old.
  bool stale = s.httpCode != 200;
  lv_label_set_text(lblChip, f.chip);
  lv_obj_set_style_bg_color(lblChip, lv_color_hex(stale ? C_GREY : fill_of(f.chipTone)), 0);
  lv_obj_update_layout(lblChip);
  // A long name gets smaller type before it gets cut.
  lv_obj_update_layout(lblName);
  const lv_font_t *nameFont = ui_fit_font(s.name, lv_obj_get_width(lblName), 1, ui.medium);
  lv_obj_set_style_text_font(lblName, nameFont, 0);
  ui_set_text(lblName, s.name);

  bool job = running || prep || paused || asking || view == View::PlateClear || view == View::Error;
  show(jobRow, job && s.job[0]);
  if (job) {
    bool past = view == View::PlateClear || view == View::Error;
    lv_obj_set_style_text_color(lblJob, lv_color_hex(past ? C_DIM : C_TEXT), 0);
    // Who queued it, when that fits beside the job's whole name; the name matters more.
    buf[0] = 0;
    if ((running || paused) && s.jobBy[0]) snprintf(buf, sizeof(buf), "by %s", s.jobBy);
    int nameW, byW;
    text_lines(s.job, ui.small, 10000, &nameW);
    text_lines(buf, ui.small, 10000, &byW);
    bool by = buf[0] && nameW + px(8) + byW <= px(ui.uw - 20);
    lv_label_set_text(lblBy, by ? buf : "");
    show(lblBy, by);
    lv_obj_update_layout(jobRow);
    ui_set_text(lblJob, s.job);
  }

  show(bar, running || paused);
  if (running || paused) lv_bar_set_value(bar, progress_percent(s), LV_ANIM_OFF);
  show(progRow, running);
  if (running) {
    snprintf(buf, sizeof(buf), "%d%%", progress_percent(s));
    lv_label_set_text(lblPct, buf);
    format_left(buf, sizeof(buf), s);
    lv_label_set_text(lblLeft, buf);
    // The layer count takes what the row has left; the word goes before the numbers do.
    lv_label_set_text(lblLayers, "");
    lv_obj_update_layout(progRow);
    buf[0] = 0;
    if (s.totalLayers > 0) {
      snprintf(buf, sizeof(buf), "Layer %d/%d", s.layer, s.totalLayers);
      int widest;
      text_lines(buf, ui.small, 10000, &widest);
      if (widest > lv_obj_get_width(lblLayers)) snprintf(buf, sizeof(buf), "%d/%d", s.layer, s.totalLayers);
    }
    lv_label_set_text(lblLayers, buf);
  }

  draw_headline(f);

  // Paused without an error: where the print stands, since the progress row is not shown.
  if (paused && !error) {
    char left[24];
    format_left(left, sizeof(left), s);
    if (s.totalLayers > 0) snprintf(f.sub, sizeof(f.sub), "%s, layer %d/%d", left, s.layer, s.totalLayers);
    else snprintf(f.sub, sizeof(f.sub), "%s", left);
  }
  show(lblSub, f.sub[0] != 0);
  if (f.sub[0]) {
    lv_obj_set_style_text_color(lblSub, lv_color_hex(text_of(f.subTone)), 0);
    ui_set_text(lblSub, f.sub, 2);
  }
  bool why = view == View::Ready && s.nextWhy[0];
  show(lblWhy, why);
  if (why) ui_set_text(lblWhy, s.nextWhy, 2);

  show(errBox, error);
  if (error) draw_error(s, view == View::Error);

  switch (view) {
  case View::PlateClear: draw_queue(s, true, 2); break;
  case View::Ready:      draw_queue(s, false, why ? 0 : 2); break;
  case View::QueueHeld:  draw_queue(s, true, 2); break;
  case View::Sending:    draw_queue(s, true, 2); break; // what comes after the one starting
  case View::Idle:       draw_queue(s, true, QUEUE_ROWS); break;
  default:               draw_queue(s, true, 0); break;
  }

  // Temperatures matter while something is hot; the printer's own screen has them otherwise.
  bool temps = (running || paused) && !error;
  format_temps(buf, sizeof(buf), s);
  show(lblTemps, temps && buf[0]);
  if (temps) ui_set_text(lblTemps, buf);
  bool next = (running || prep) && !error && s.nextId != 0;
  show(lblNext, next);
  if (next) {
    format_next(buf, sizeof(buf), s);
    ui_set_text(lblNext, buf);
  }

  show(btnRow, !asking && actionCount > 0);
  show(verdictRow, asking);
  if (asking) {
    shownArchive = s.archiveId;
    shownRun = s.runId;
    lv_obj_update_layout(verdictRow);
    ui_button_set(btnSkip, "Skip", Kind::Quiet, false); // sized now that the row has its width
    draw_verdict_buttons();
  }
  draw_buttons();
}

// Solid colour while all is well; blinking when someone needs to come over.
static void update_led(const Snapshot &s)
{
  bool on = (millis() / 500) % 2 == 0;
  if (link_lost(s)) return display_led(on, false, on);
  switch (view) {
  case View::Printing:
  case View::Sending:       display_led(false, false, true); break;
  case View::Paused:        display_led(on, on, false); break;
  case View::QueueHeld:     display_led(on, on, false); break;
  case View::Verdict:       display_led(false, on, on); break;
  case View::PlateClear:    display_led(false, on, false); break;
  case View::Error:         display_led(on, false, false); break;
  case View::Offline:
  case View::CheckSettings: display_led(on, false, on); break;
  default:                  display_led(false, false, false); break;
  }
}

// Views that want someone to come over keep the screen bright by themselves.
static bool wants_someone()
{
  switch (view) {
  case View::Printing:
  case View::Sending:
  case View::Idle:
  case View::PrinterOffline: return false;
  default:                   return true;
  }
}

void ui_update(const Snapshot &s)
{
  // The maintenance screen gives way to the status screen when left alone.
  if (tasks_idle_timeout()) shownSeq = UINT32_MAX;
  if (filament_tick()) shownSeq = UINT32_MAX; // closed itself, or its spool list has come

  // Don't change the screen under a finger that is in the middle of a hold. A "finger" that
  // stays far longer than any hold is a stuck or noisy touch, and must not freeze the screen.
  bool pressed = lv_indev_get_state(lv_indev_get_next(nullptr)) == LV_INDEV_STATE_PRESSED;
  if (!pressed) pressedSince = 0;
  else if (!pressedSince) pressedSince = millis() | 1;
  bool holding = pressed && millis() - pressedSince < PRESS_FREEZE_MS;

  // A command's answer: say it, and let go of "under way" once Bambuddy has been read again
  // (or at once when it failed: nothing is going to change).
  if (s.toastSeq != shownToast) {
    shownToast = s.toastSeq;
    // A task marked done, or a spool put in a slot, says so in its own row (ui_tasks.cpp,
    // ui_filament.cpp): a band there would lie over Back and the page buttons. A failure is said in the band wherever it happens.
    bool inRow = !s.toastBad && pend.active &&
                 ((pend.cmd == Command::PerformTask && tasks_showing()) ||
                  ((pend.cmd == Command::AssignSpool || pend.cmd == Command::UnassignSpool) && filament_showing()));
    if (s.toast[0] && !inRow) show_notice(s.toast, s.toastBad);
    if (pend.active) {
      if (s.toastBad) pend.active = false;
      else {
        pend.answered = true;
        pend.seqAtAnswer = s.seq;
      }
      ui_redraw_soon();
    }
  }
  if (pend.active && ((pend.answered && s.seq != pend.seqAtAnswer) || millis() - pend.since > PENDING_GIVE_UP_MS)) {
    pend.active = false;
    ui_redraw_soon();
  }

  // Bambuddy not answering: a band says so and for how long, over the last status. A
  // command's result has the band while it shows.
  bool lost = s.seq != 0 && link_lost(s);
  if (noticeUntil && (int32_t)(millis() - noticeUntil) > 0) hide_notice();
  if (lost && !noticeUntil) {
    char buf[64];
    format_lost(buf, sizeof(buf), s);
    if (!lostShown) ui_redraw_soon(); // the buttons go quiet
    draw_notice(buf, true);
    lostShown = true;
  } else if (!lost && lostShown) {
    hide_notice();
    ui_redraw_soon();
  }

  if (s.seq != shownSeq && s.seq != 0 && !holding) {
    view = pick_view(s, options);
    draw(s);
    shownSeq = s.seq; // after draw: a message screen resets it
  }

  if (s.seq != 0) update_led(s);

  // Bright while someone is at the panel or should come to it; dimmed otherwise.
  if (pressed) ui_touched();
  bool status = !lv_obj_has_flag(mainBox, LV_OBJ_FLAG_HIDDEN);
  bool rest = s.seq != 0 && status && !wants_someone() && !lost && !pend.active;
  set_dimmed(rest && millis() - activeAt > DIM_AFTER_MS);
}
