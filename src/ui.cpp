#include "ui.h"
#include "board.h"
#include "display.h"
#include "icons.h"

#include <lvgl.h>

#include <initializer_list>

// What the screen is about right now; decides the buttons and the LED.
enum class View { Offline, PrinterOffline, Printing, Paused, Verdict, PlateClear, Error, Ready, Idle };

struct Slot {
  const char *text;
  Command cmd;
  bool hold;      // must be held for HOLD_MS; for anything that is costly to press by accident
  uint32_t color;
  uint32_t textColor;
};

static const uint32_t HOLD_MS = 1500;
static const uint32_t TOAST_MS = 2500;

static const uint32_t C_BG = 0x101418, C_TEXT = 0xECEFF1, C_DIM = 0x90A4AE;
static const uint32_t C_BLUE = 0x1E6FD9, C_AMBER = 0xC77700, C_GREEN = 0x2E7D32, C_RED = 0xC62828, C_GREY = 0x455A64;
static const uint32_t C_LIT = 0xF2C037, C_DARK = 0x1B1B1B, C_OK_TEXT = 0x81C784, C_BAD_TEXT = 0xFF8A80;

static lv_obj_t *mainBox, *msgBox;
static lv_obj_t *lblName, *lblState, *lblJob, *bar, *lblProgress, *lblLayers, *lblHeadline, *lblTemps, *lblDetail, *btnRow;
static lv_obj_t *btn[3], *btnLabel[3];
static lv_obj_t *verdictRow;
static lv_obj_t *calBox, *lblCal, *crossH, *crossV;
static lv_obj_t *lblMsgTitle, *lblMsgBody, *lblToast;

static Slot slots[3];
static int slotCount = 0;
static uint32_t pressStart = 0;
static bool pressFired = false;

static uint32_t shownSeq = UINT32_MAX, shownToast = 0, toastUntil = 0;
static View view = View::Offline;

static bool is(const Snapshot &s, const char *state) { return strcmp(s.state, state) == 0; }

static View pick_view(const Snapshot &s)
{
  if (!s.reachable) return View::Offline;
  if (!s.connected) return View::PrinterOffline;
  if (is(s, "RUNNING") || is(s, "PREPARE") || is(s, "SLICING")) return View::Printing;
  if (is(s, "PAUSE")) return View::Paused;
  if (s.verdictPending) return View::Verdict; // before the plate: clearing it can answer for you
  if (s.awaitingPlateClear) return View::PlateClear;
  if (s.errorCount > 0) return View::Error;
  if (s.nextId != 0 && s.nextManual) return View::Ready;
  return View::Idle;
}

static void set_slots(View v, const Snapshot &s)
{
  // Shows the light's state, not the action: lit and yellow when on, grey when off.
  const Slot light = {s.light ? "Light\nON" : "Light\nOFF", Command::ToggleLight, false,
                      s.light ? C_LIT : C_GREY, s.light ? C_DARK : C_TEXT};
  slotCount = 0;
  auto add = [](Slot x) { slots[slotCount++] = x; };
  switch (v) {
  case View::Printing:
    add({"Pause", Command::Pause, false, C_AMBER, C_TEXT});
    add({"Hold to stop", Command::Stop, true, C_RED, C_TEXT});
    add(light);
    break;
  case View::Paused:
    add({"Resume", Command::Resume, false, C_GREEN, C_TEXT});
    add({"Hold to stop", Command::Stop, true, C_RED, C_TEXT});
    add(light);
    break;
  case View::PlateClear:
    add({"Hold when plate is clear", Command::ClearPlate, true, C_GREEN, C_TEXT});
    add(light);
    break;
  case View::Error:
    add({"Clear error", Command::ClearErrors, false, C_AMBER, C_TEXT});
    add({"Resume queue", Command::ResumeQueue, false, C_BLUE, C_TEXT});
    add(light);
    break;
  case View::Ready:
    add({"Hold to start", Command::StartNext, true, C_GREEN, C_TEXT});
    add(light);
    break;
  case View::Idle:
    add(light);
    break;
  default:
    break;
  }
}

static void draw_buttons()
{
  for (int i = 0; i < 3; i++) {
    if (i < slotCount) {
      lv_label_set_text(btnLabel[i], slots[i].text);
      lv_obj_set_style_bg_color(btn[i], lv_color_hex(slots[i].color), 0);
      lv_obj_set_style_text_color(btnLabel[i], lv_color_hex(slots[i].textColor), 0);
      // The first button is the main action; the light button stays narrow.
      lv_obj_set_flex_grow(btn[i], slots[i].cmd == Command::ToggleLight && slotCount > 1 ? 2 : 3);
      lv_obj_remove_flag(btn[i], LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(btn[i], LV_OBJ_FLAG_HIDDEN);
    }
  }
}

static void button_cb(lv_event_t *e)
{
  int i = (int)(intptr_t)lv_event_get_user_data(e);
  if (i >= slotCount) return;
  const Slot &slot = slots[i];

  switch (lv_event_get_code(e)) {
  case LV_EVENT_PRESSED:
    pressStart = millis();
    pressFired = false;
    if (slot.hold) lv_label_set_text(btnLabel[i], "Keep holding...");
    break;
  case LV_EVENT_PRESSING:
    if (slot.hold && !pressFired && millis() - pressStart >= HOLD_MS) {
      pressFired = true;
      lv_label_set_text(btnLabel[i], "OK");
      bambuddy_send(slot.cmd);
    }
    break;
  case LV_EVENT_RELEASED:
  case LV_EVENT_PRESS_LOST:
    if (slot.hold && !pressFired) lv_label_set_text(btnLabel[i], slot.text);
    break;
  case LV_EVENT_CLICKED:
    if (!slot.hold) bambuddy_send(slot.cmd);
    break;
  default:
    break;
  }
}

// ---------------------------------------------------------------- maintenance screen

static const int TASK_ROWS = 4;               // per page
static const uint32_t TASKS_IDLE_MS = 30000;  // then back to the status screen by itself
static const uint32_t C_ROW = 0x1B2228, C_WARN_TEXT = 0xFFB74D;

static lv_obj_t *maintBox, *btnTasks, *lblTasksCount, *btnMore, *lblMore, *lblNoTasks;
static lv_obj_t *taskRow[TASK_ROWS], *lblTaskName[TASK_ROWS], *lblTaskDue[TASK_ROWS], *lblTaskDone[TASK_ROWS], *btnTaskDone[TASK_ROWS];
static bool showTasks = false;
static int taskPage = 0;
static uint32_t tasksTouchedAt = 0;
static Task shownTasks[MAX_TASKS];
static int shownTaskCount = 0;

// Redraw on the next ui_update, without waiting for new data.
static void redraw_soon()
{
  shownSeq = UINT32_MAX;
  tasksTouchedAt = millis();
}

static void tasks_open_cb(lv_event_t *)
{
  showTasks = true;
  taskPage = 0;
  redraw_soon();
}

static void tasks_back_cb(lv_event_t *)
{
  showTasks = false;
  redraw_soon();
}

static void tasks_more_cb(lv_event_t *)
{
  taskPage++; // wraps to the first page when drawn
  redraw_soon();
}

// "Done" resets the task's counter in Bambuddy, so it takes a hold, like Stop.
static void task_done_cb(lv_event_t *e)
{
  int row = (int)(intptr_t)lv_event_get_user_data(e);
  int i = taskPage * TASK_ROWS + row;
  if (i >= shownTaskCount) return;

  switch (lv_event_get_code(e)) {
  case LV_EVENT_PRESSED:
    pressStart = tasksTouchedAt = millis();
    pressFired = false;
    lv_label_set_text(lblTaskDone[row], "Hold...");
    break;
  case LV_EVENT_PRESSING:
    if (!pressFired && millis() - pressStart >= HOLD_MS) {
      pressFired = true;
      lv_label_set_text(lblTaskDone[row], "OK");
      bambuddy_send(Command::PerformTask, shownTasks[i].id);
    }
    break;
  case LV_EVENT_RELEASED:
  case LV_EVENT_PRESS_LOST:
    if (!pressFired) lv_label_set_text(lblTaskDone[row], "Done");
    break;
  default:
    break;
  }
}

static void format_due(char *out, size_t size, const Task &t)
{
  const char *unit = t.days ? (t.left >= 1.5f || t.left <= -1.5f ? "days" : "day") : "h";
  int n = (int)(t.left < 0 ? -t.left + 0.5f : t.left + 0.5f);
  if (t.level == 2 && (t.left > -1 || n == 0)) snprintf(out, size, "Due now");
  else if (t.left < 0) snprintf(out, size, "Overdue by %d %s", n, unit);
  else snprintf(out, size, "Due in %d %s", n, unit);
}

static void draw_tasks(const Snapshot &s)
{
  shownTaskCount = s.taskCount;
  for (int i = 0; i < s.taskCount; i++) shownTasks[i] = s.tasks[i];

  int pages = (s.taskCount + TASK_ROWS - 1) / TASK_ROWS;
  if (pages < 1) pages = 1;
  if (taskPage >= pages) taskPage = 0;

  char buf[40];
  if (pages > 1) {
    snprintf(buf, sizeof(buf), "More %d/%d", taskPage + 1, pages);
    lv_label_set_text(lblMore, buf);
    lv_obj_remove_flag(btnMore, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(btnMore, LV_OBJ_FLAG_HIDDEN);
  }

  if (s.taskCount == 0) {
    lv_label_set_text(lblNoTasks, s.tasksKnown ? "No maintenance tasks for this printer." : "Bambuddy hasn't sent the tasks yet.");
    lv_obj_remove_flag(lblNoTasks, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(lblNoTasks, LV_OBJ_FLAG_HIDDEN);
  }

  for (int row = 0; row < TASK_ROWS; row++) {
    int i = taskPage * TASK_ROWS + row;
    if (i >= s.taskCount) {
      lv_obj_add_flag(taskRow[row], LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    const Task &t = s.tasks[i];
    uint32_t edge = t.level == 2 ? C_RED : t.level == 1 ? C_AMBER : C_GREY;
    uint32_t text = t.level == 2 ? C_BAD_TEXT : t.level == 1 ? C_WARN_TEXT : C_DIM;
    lv_label_set_text(lblTaskName[row], t.name);
    format_due(buf, sizeof(buf), t);
    lv_label_set_text(lblTaskDue[row], buf);
    lv_obj_set_style_text_color(lblTaskDue[row], lv_color_hex(text), 0);
    lv_obj_set_style_border_color(taskRow[row], lv_color_hex(edge), 0);
    lv_label_set_text(lblTaskDone[row], "Done");
    // Green only where something is actually due, so the eye lands there first.
    lv_obj_set_style_bg_color(btnTaskDone[row], lv_color_hex(t.level > 0 ? C_GREEN : C_GREY), 0);
    lv_obj_remove_flag(taskRow[row], LV_OBJ_FLAG_HIDDEN);
  }
}

// The wrench in the status screen's header: quiet when all is fine, coloured with a count when not.
static void draw_badge(const Snapshot &s)
{
  if (!s.tasksKnown) {
    lv_obj_add_flag(btnTasks, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_obj_remove_flag(btnTasks, LV_OBJ_FLAG_HIDDEN);
  int n = s.tasksDue + s.tasksWarn;
  char buf[8] = "";
  if (n > 0) snprintf(buf, sizeof(buf), "%d", n);
  lv_label_set_text(lblTasksCount, buf);
  lv_obj_set_style_bg_color(btnTasks, lv_color_hex(s.tasksDue ? C_RED : s.tasksWarn ? C_AMBER : 0x263238), 0);
}

// A button without the theme's shadow and press-grow (which the parent would clip).
static lv_obj_t *button(lv_obj_t *parent, uint32_t color, int x, int y, int w, int h, const char *text, lv_obj_t **textOut = nullptr)
{
  lv_obj_t *b = lv_button_create(parent);
  lv_obj_set_pos(b, x, y);
  lv_obj_set_size(b, w, h);
  lv_obj_set_style_bg_color(b, lv_color_hex(color), 0);
  lv_obj_set_style_radius(b, 6, 0);
  lv_obj_set_style_pad_all(b, 0, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_style_transform_width(b, 0, LV_STATE_PRESSED);
  lv_obj_set_style_transform_height(b, 0, LV_STATE_PRESSED);
  if (text) {
    lv_obj_t *l = lv_label_create(b);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(C_TEXT), 0);
    lv_label_set_text(l, text);
    lv_obj_center(l);
    if (textOut) *textOut = l;
  }
  return b;
}

// ---------------------------------------------------------------- outcome question

static void verdict_cb(lv_event_t *e)
{
  bool good = lv_event_get_user_data(e) != nullptr;
  bambuddy_send(good ? Command::VerdictGood : Command::VerdictReject);
}

// One of the two big answer buttons on the outcome screen.
static void verdict_button(const lv_image_dsc_t *icon, uint32_t color, bool good)
{
  lv_obj_t *b = lv_button_create(verdictRow);
  lv_obj_set_height(b, lv_pct(100));
  lv_obj_set_flex_grow(b, 1);
  lv_obj_set_style_bg_color(b, lv_color_hex(color), 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_style_transform_width(b, 0, LV_STATE_PRESSED);
  lv_obj_set_style_transform_height(b, 0, LV_STATE_PRESSED);
  lv_obj_add_event_cb(b, verdict_cb, LV_EVENT_CLICKED, good ? (void *)1 : nullptr);

  lv_obj_t *img = lv_image_create(b);
  lv_image_set_src(img, icon);
  lv_obj_set_style_image_recolor(img, lv_color_hex(C_TEXT), 0);
  lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
  lv_obj_center(img);
}

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, int x, int y, int w)
{
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  lv_obj_set_pos(l, x, y);
  lv_obj_set_width(l, w);
  lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
  lv_label_set_text(l, "");
  return l;
}

static lv_obj_t *box(lv_obj_t *parent)
{
  lv_obj_t *b = lv_obj_create(parent);
  lv_obj_remove_style_all(b);
  lv_obj_set_size(b, SCREEN_W, SCREEN_H);
  lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
  return b;
}

void ui_begin()
{
  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(C_BG), 0);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  // --- status screen
  mainBox = box(scr);
  lblName = label(mainBox, &lv_font_montserrat_20, C_TEXT, 10, 8, 128);
  lblState = label(mainBox, &lv_font_montserrat_14, C_TEXT, 190, 8, 120);
  lv_obj_set_style_text_align(lblState, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_bg_opa(lblState, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(lblState, 6, 0);
  lv_obj_set_style_pad_ver(lblState, 4, 0);

  // Wrench: opens the maintenance screen. Small, so it takes taps from a little around it too.
  btnTasks = button(mainBox, 0x263238, 144, 4, 42, 30, nullptr);
  lv_obj_set_ext_click_area(btnTasks, 8);
  lv_obj_set_flex_flow(btnTasks, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(btnTasks, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(btnTasks, 3, 0);
  lv_obj_add_event_cb(btnTasks, tasks_open_cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *wrench = lv_image_create(btnTasks);
  lv_image_set_src(wrench, &icon_wrench);
  lv_obj_set_style_image_recolor(wrench, lv_color_hex(C_TEXT), 0);
  lv_obj_set_style_image_recolor_opa(wrench, LV_OPA_COVER, 0);
  lblTasksCount = lv_label_create(btnTasks);
  lv_obj_set_style_text_font(lblTasksCount, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(lblTasksCount, lv_color_hex(C_TEXT), 0);
  lv_label_set_text(lblTasksCount, "");
  lv_obj_add_flag(btnTasks, LV_OBJ_FLAG_HIDDEN);

  lblJob = label(mainBox, &lv_font_montserrat_14, C_TEXT, 10, 42, 300);

  bar = lv_bar_create(mainBox);
  lv_obj_set_pos(bar, 10, 66);
  lv_obj_set_size(bar, 300, 16);
  lv_bar_set_range(bar, 0, 100);

  lblProgress = label(mainBox, &lv_font_montserrat_20, C_TEXT, 10, 88, 190);
  lblLayers = label(mainBox, &lv_font_montserrat_14, C_DIM, 200, 93, 110);
  lv_obj_set_style_text_align(lblLayers, LV_TEXT_ALIGN_RIGHT, 0);
  lblHeadline = label(mainBox, &lv_font_montserrat_28, C_TEXT, 10, 66, 300);
  lblTemps = label(mainBox, &lv_font_montserrat_14, C_DIM, 10, 116, 300);
  lblDetail = label(mainBox, &lv_font_montserrat_14, C_TEXT, 10, 138, 300);
  lv_obj_set_height(lblDetail, 34); // two lines, then "..."

  btnRow = lv_obj_create(mainBox);
  lv_obj_remove_style_all(btnRow);
  lv_obj_set_pos(btnRow, 8, 178);
  lv_obj_set_size(btnRow, 304, 56);
  lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(btnRow, 8, 0);
  lv_obj_remove_flag(btnRow, LV_OBJ_FLAG_SCROLLABLE);

  for (int i = 0; i < 3; i++) {
    btn[i] = lv_button_create(btnRow);
    lv_obj_set_height(btn[i], 56);
    lv_obj_set_flex_grow(btn[i], 1);
    lv_obj_set_style_shadow_width(btn[i], 0, 0);
    // The theme grows a pressed button; the row then clips it into square corners.
    // Keep the size fixed; the theme's darkening is feedback enough.
    lv_obj_set_style_transform_width(btn[i], 0, LV_STATE_PRESSED);
    lv_obj_set_style_transform_height(btn[i], 0, LV_STATE_PRESSED);
    lv_obj_add_event_cb(btn[i], button_cb, LV_EVENT_ALL, (void *)(intptr_t)i);
    btnLabel[i] = lv_label_create(btn[i]);
    lv_obj_set_style_text_font(btnLabel[i], &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(btnLabel[i], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(btnLabel[i]);
    lv_obj_add_flag(btn[i], LV_OBJ_FLAG_HIDDEN);
  }

  // --- outcome question: two big buttons in place of the lower half
  verdictRow = lv_obj_create(mainBox);
  lv_obj_remove_style_all(verdictRow);
  lv_obj_set_pos(verdictRow, 8, 106);
  lv_obj_set_size(verdictRow, 304, 128);
  lv_obj_set_flex_flow(verdictRow, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(verdictRow, 8, 0);
  lv_obj_remove_flag(verdictRow, LV_OBJ_FLAG_SCROLLABLE);
  verdict_button(&icon_thumb_up, C_GREEN, true);
  verdict_button(&icon_thumb_down, C_RED, false);
  lv_obj_add_flag(verdictRow, LV_OBJ_FLAG_HIDDEN);

  // --- maintenance screen: a header and four task rows per page
  maintBox = box(scr);
  lv_obj_t *back = button(maintBox, C_GREY, 8, 4, 72, 32, LV_SYMBOL_LEFT " Back");
  lv_obj_add_event_cb(back, tasks_back_cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *maintTitle = label(maintBox, &lv_font_montserrat_14, C_TEXT, 84, 12, 140);
  lv_obj_set_style_text_align(maintTitle, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(maintTitle, "Maintenance");
  btnMore = button(maintBox, C_GREY, 228, 4, 84, 32, "More", &lblMore);
  lv_obj_add_event_cb(btnMore, tasks_more_cb, LV_EVENT_CLICKED, nullptr);
  for (int row = 0; row < TASK_ROWS; row++) {
    lv_obj_t *r = taskRow[row] = lv_obj_create(maintBox);
    lv_obj_remove_style_all(r);
    lv_obj_set_pos(r, 8, 42 + row * 49);
    lv_obj_set_size(r, 304, 45);
    lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(r, lv_color_hex(C_ROW), 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(r, 6, 0);
    lv_obj_set_style_border_side(r, LV_BORDER_SIDE_LEFT, 0); // coloured edge = how urgent
    lv_obj_set_style_border_width(r, 4, 0);
    lblTaskName[row] = label(r, &lv_font_montserrat_14, C_TEXT, 8, 5, 198);
    lblTaskDue[row] = label(r, &lv_font_montserrat_14, C_DIM, 8, 24, 198);
    lv_obj_t *done = btnTaskDone[row] = button(r, C_GREY, 216, 4, 78, 37, "Done", &lblTaskDone[row]);
    lv_obj_add_event_cb(done, task_done_cb, LV_EVENT_ALL, (void *)(intptr_t)row);
  }
  lblNoTasks = label(maintBox, &lv_font_montserrat_14, C_DIM, 10, 110, 300);
  lv_obj_set_style_text_align(lblNoTasks, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_add_flag(maintBox, LV_OBJ_FLAG_HIDDEN);

  // --- message screen
  msgBox = box(scr);
  lblMsgTitle = label(msgBox, &lv_font_montserrat_28, C_TEXT, 10, 50, 300);
  lv_obj_set_style_text_align(lblMsgTitle, LV_TEXT_ALIGN_CENTER, 0);
  lblMsgBody = label(msgBox, &lv_font_montserrat_14, C_DIM, 10, 100, 300);
  lv_label_set_long_mode(lblMsgBody, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(lblMsgBody, LV_TEXT_ALIGN_CENTER, 0);

  // --- touch calibration: a cross to tap, text in the middle
  calBox = box(scr);
  lv_obj_t *calTitle = label(calBox, &lv_font_montserrat_20, C_TEXT, 10, 84, 300);
  lv_obj_set_style_text_align(calTitle, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(calTitle, "Touch calibration");
  lblCal = label(calBox, &lv_font_montserrat_14, C_DIM, 10, 114, 300);
  lv_label_set_long_mode(lblCal, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(lblCal, LV_TEXT_ALIGN_CENTER, 0);
  for (lv_obj_t **bar : {&crossH, &crossV}) {
    *bar = lv_obj_create(calBox);
    lv_obj_remove_style_all(*bar);
    lv_obj_set_style_bg_color(*bar, lv_color_hex(C_LIT), 0);
    lv_obj_set_style_bg_opa(*bar, LV_OPA_COVER, 0);
  }
  lv_obj_set_size(crossH, 33, 3);
  lv_obj_set_size(crossV, 3, 33);
  lv_obj_add_flag(calBox, LV_OBJ_FLAG_HIDDEN);

  // --- toast, above everything
  lblToast = lv_label_create(lv_layer_top());
  lv_obj_set_style_text_font(lblToast, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(lblToast, lv_color_hex(C_TEXT), 0);
  lv_obj_set_style_bg_color(lblToast, lv_color_hex(0x263238), 0);
  lv_obj_set_style_bg_opa(lblToast, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(lblToast, 8, 0);
  lv_obj_set_style_pad_all(lblToast, 8, 0);
  lv_obj_align(lblToast, LV_ALIGN_BOTTOM_MID, 0, -70); // just above the buttons
  lv_obj_add_flag(lblToast, LV_OBJ_FLAG_HIDDEN);

  lv_obj_add_flag(mainBox, LV_OBJ_FLAG_HIDDEN);
}

void ui_message(const char *title, const char *body)
{
  lv_label_set_text(lblMsgTitle, title);
  lv_label_set_text(lblMsgBody, body);
  lv_obj_add_flag(mainBox, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(calBox, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(maintBox, LV_OBJ_FLAG_HIDDEN);
  showTasks = false;
  lv_obj_remove_flag(msgBox, LV_OBJ_FLAG_HIDDEN);
  shownSeq = UINT32_MAX; // the status screen redraws when it comes back
  lv_refr_now(nullptr);
}

void ui_calibrate(int step, int total, int x, int y)
{
  char buf[80];
  snprintf(buf, sizeof(buf), "Tap the centre of the cross (%d of %d).\nA fingernail or stylus works best.", step, total);
  lv_label_set_text(lblCal, buf);
  lv_obj_set_pos(crossH, x - 16, y - 1);
  lv_obj_set_pos(crossV, x - 1, y - 16);
  lv_obj_add_flag(mainBox, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(msgBox, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(maintBox, LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(calBox, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(lblToast, LV_OBJ_FLAG_HIDDEN); // nothing may cover the cross or the text
  toastUntil = 0;
  shownSeq = UINT32_MAX;
  lv_refr_now(nullptr);
}

static void format_duration(char *out, size_t size, int minutes)
{
  if (minutes >= 60) snprintf(out, size, "%dh %02dm", minutes / 60, minutes % 60);
  else snprintf(out, size, "%dm", minutes);
}

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

static void draw(const Snapshot &s)
{
  char buf[128], dur[16];

  if (view == View::Offline) {
    snprintf(buf, sizeof(buf), s.httpCode == 401 || s.httpCode == 403
                                   ? "Bambuddy refused the API key (%d)."
                                   : "Can't reach Bambuddy (%d). Retrying...",
             s.httpCode);
    ui_message("Offline", buf);
    return;
  }
  lv_obj_add_flag(msgBox, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(calBox, LV_OBJ_FLAG_HIDDEN);
  if (showTasks) {
    lv_obj_add_flag(mainBox, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(maintBox, LV_OBJ_FLAG_HIDDEN);
    draw_tasks(s);
    return;
  }
  lv_obj_add_flag(maintBox, LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(mainBox, LV_OBJ_FLAG_HIDDEN);
  draw_badge(s);

  lv_label_set_text(lblName, s.name);
  lv_label_set_text(lblJob, s.job);

  bool showProgress = view == View::Printing || view == View::Paused;
  if (showProgress) {
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_HIDDEN);
    lv_bar_set_value(bar, (int)(s.progress + 0.5f), LV_ANIM_OFF);
    format_duration(dur, sizeof(dur), s.remainingMin);
    // "left" is dropped for very long prints so the time itself always fits.
    snprintf(buf, sizeof(buf), s.remainingMin >= 600 ? "%d%%   %s" : "%d%%   %s left", (int)(s.progress + 0.5f), dur);
    lv_label_set_text(lblProgress, buf);
    if (s.totalLayers > 0) snprintf(buf, sizeof(buf), "Layer %d/%d", s.layer, s.totalLayers);
    else buf[0] = 0;
    lv_label_set_text(lblLayers, buf);
  } else {
    lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(lblProgress, "");
    lv_label_set_text(lblLayers, "");
  }

  snprintf(buf, sizeof(buf), "Nozzle %d\xC2\xB0   Bed %d\xC2\xB0   Chamber %d\xC2\xB0",
           (int)(s.nozzle + 0.5f), (int)(s.bed + 0.5f), (int)(s.chamber + 0.5f));
  lv_label_set_text(lblTemps, buf);

  // One line of "what now": the error, else what is next in the queue.
  if (s.errorCount > 0) {
    snprintf(buf, sizeof(buf), "%s", s.error);
  } else if (s.nextId != 0) {
    format_duration(dur, sizeof(dur), s.nextSeconds / 60);
    if (s.queued > 1) snprintf(buf, sizeof(buf), "Next of %d (%s): %s", s.queued, dur, s.nextName);
    else snprintf(buf, sizeof(buf), "Next (%s): %s", dur, s.nextName);
  } else if (showProgress && s.stage[0]) {
    snprintf(buf, sizeof(buf), "%s", s.stage);
  } else {
    snprintf(buf, sizeof(buf), view == View::PlateClear ? "Nothing else queued" : "");
  }
  lv_label_set_text(lblDetail, buf);
  lv_obj_set_style_text_color(lblDetail, lv_color_hex(s.errorCount > 0 ? C_BAD_TEXT : C_DIM), 0);

  // The chip says what the panel wants from you; the headline says what happened.
  bool failed = is(s, "FAILED");
  switch (view) {
  case View::PrinterOffline:
    set_state_chip("Offline", C_GREY);
    set_headline("Printer offline", C_DIM);
    break;
  case View::Printing:
    set_state_chip(is(s, "RUNNING") ? "Printing" : "Preparing", C_BLUE);
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
    if (failed) snprintf(buf, sizeof(buf), "Failed at %d%%", (int)(s.progress + 0.5f));
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
    set_headline(s.nextId != 0 ? "Waiting to start" : "Nothing queued", C_DIM);
    break;
  }

  // The outcome question takes over the lower half of the screen.
  bool asking = view == View::Verdict;
  for (lv_obj_t *o : {lblTemps, lblDetail, btnRow}) {
    if (asking) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
  }
  if (asking) lv_obj_remove_flag(verdictRow, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(verdictRow, LV_OBJ_FLAG_HIDDEN);

  set_slots(view, s);
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
  if (showTasks && millis() - tasksTouchedAt > TASKS_IDLE_MS) {
    showTasks = false;
    shownSeq = UINT32_MAX;
  }

  if (s.seq != shownSeq && s.seq != 0) {
    // Don't swap the buttons under a finger that is in the middle of a hold.
    bool pressing = lv_indev_get_state(lv_indev_get_next(nullptr)) == LV_INDEV_STATE_PRESSED;
    if (!pressing) {
      view = pick_view(s);
      draw(s);
      shownSeq = s.seq; // after draw: the offline message resets it
    }
  }

  if (s.toastSeq != shownToast && !s.toast[0]) shownToast = s.toastSeq; // nothing to say
  if (s.toastSeq != shownToast) {
    shownToast = s.toastSeq;
    lv_label_set_text(lblToast, s.toast);
    lv_obj_remove_flag(lblToast, LV_OBJ_FLAG_HIDDEN);
    toastUntil = millis() + TOAST_MS;
  } else if (toastUntil && millis() > toastUntil) {
    lv_obj_add_flag(lblToast, LV_OBJ_FLAG_HIDDEN);
    toastUntil = 0;
  }

  if (s.seq != 0) update_led();
}
