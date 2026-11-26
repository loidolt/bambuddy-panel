// The maintenance screen: the printer's maintenance tasks, most urgent first, four to a page,
// and the wrench in the status screen's header that opens it. The gear on it opens the
// panel's settings.
#include "icons.h"
#include "ui.h"
#include "ui_internal.h"
#include "ui_logic.h"

#include <initializer_list>

static const int TASK_ROWS = 4;               // per page
static const uint32_t TASKS_IDLE_MS = 60000;  // then back to the status screen by itself
static const int ROW_GAP = 2, BUTTON_W = 100; // units

static lv_obj_t *maintBox, *lblTasksCount, *lblTitle, *lblPage, *lblNoTasks;
static lv_obj_t *taskRow[TASK_ROWS], *lblTaskName[TASK_ROWS], *lblTaskDue[TASK_ROWS];
static UiButton btnWrench, btnBack, btnGear, btnPrev, btnNext, btnDone[TASK_ROWS];
static bool showing = false;
static int page = 0, pages = 1;
static uint32_t touchedAt = 0;
static Task shown[MAX_TASKS]; // what the rows show, so "Mark done" acts on the task the person sees
static int shownCount = 0;

static void touched()
{
  touchedAt = millis();
  ui_redraw_soon();
}

static void open_fire(UiButton &)
{
  showing = true;
  page = 0;
  touched();
}

static void back_fire(UiButton &)
{
  showing = false;
  touched();
}

static void page_fire(UiButton &b)
{
  page = (page + b.tag + pages) % pages; // tag: -1 back, +1 on; round at both ends
  touched();
}

static void gear_fire(UiButton &) { ui_open_settings(); }

static void draw_rows(); // below

// "Mark done" resets the task's counter in Bambuddy, so it takes a hold.
static void done_fire(UiButton &b)
{
  int i = page * TASK_ROWS + b.tag;
  if (i >= shownCount) return;
  touchedAt = millis();
  ui_send(Command::PerformTask, shown[i].id);
  draw_rows(); // at once: the finger is still down, which holds back the full redraw
}

void tasks_create(lv_obj_t *screen, lv_obj_t *header)
{
  // Wrench: opens the maintenance screen. Small, so it takes taps from a little around it too.
  ui_button_create(btnWrench, header, open_fire);
  lv_obj_set_size(btnWrench.btn, px(HEADER_ICON_W), px(HEADER_ICON_H));
  lv_obj_set_ext_click_area(btnWrench.btn, px(HEADER_ICON_REACH));
  lv_obj_set_flex_flow(btnWrench.btn, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(btnWrench.btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(btnWrench.btn, px(3), 0);
  lv_obj_add_flag(btnWrench.label, LV_OBJ_FLAG_HIDDEN); // an icon and a count instead of words
  ui_icon(btnWrench.btn, ICON(icon_wrench), C_TEXT);
  lblTasksCount = lv_label_create(btnWrench.btn);
  lv_obj_set_style_text_font(lblTasksCount, ui.small, 0);
  lv_obj_set_style_text_color(lblTasksCount, lv_color_hex(C_TEXT), 0);
  lv_label_set_text(lblTasksCount, "");
  lv_obj_add_flag(btnWrench.btn, LV_OBJ_FLAG_HIDDEN);
  (void)screen;
}

// The screen: a header and four task rows to a page, everything to press TARGET tall. Made when
// it is opened and taken away when it closes, to leave LVGL's small memory to the status screen.
static void build()
{
  if (maintBox) return;
  const int W = ui.uw;
  maintBox = ui_box(lv_screen_active());
  for (UiButton *b : {&btnBack, &btnGear, &btnPrev, &btnNext}) *b = UiButton();
  for (UiButton &b : btnDone) b = UiButton();
  auto head = [&](UiButton &b, int x, int w, const char *text, void (*fire)(UiButton &), int tag = 0) {
    ui_button_create(b, maintBox, fire, tag);
    lv_obj_set_pos(b.btn, px(x), px(ROW_GAP));
    lv_obj_set_size(b.btn, px(w), px(TARGET));
    ui_button_set(b, text, Kind::Quiet, false);
  };
  head(btnBack, 8, 72, LV_SYMBOL_LEFT " Back", back_fire);
  head(btnGear, 86, TARGET, LV_SYMBOL_SETTINGS, gear_fire);
  ui_button_set(btnGear, LV_SYMBOL_SETTINGS, Kind::Quiet, true); // it restarts the panel: a hold
  head(btnPrev, W - 8 - TARGET - 36 - TARGET, TARGET, LV_SYMBOL_LEFT, page_fire, -1);
  head(btnNext, W - 8 - TARGET, TARGET, LV_SYMBOL_RIGHT, page_fire, 1);
  lblPage = ui_label(maintBox, ui.small, C_DIM, W - 8 - TARGET - 36, ROW_GAP + 15, 36);
  lv_obj_set_style_text_align(lblPage, LV_TEXT_ALIGN_CENTER, 0);
  lblTitle = ui_label(maintBox, ui.small, C_TEXT, 140, ROW_GAP + 15, W - 150);

  for (int row = 0; row < TASK_ROWS; row++) {
    lv_obj_t *r = taskRow[row] = ui_plain(maintBox);
    lv_obj_set_pos(r, px(8), px(ROW_GAP + (row + 1) * (TARGET + ROW_GAP)));
    lv_obj_set_size(r, px(W - 16), px(TARGET));
    lv_obj_set_style_bg_color(r, lv_color_hex(C_ROW), 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(r, px(6), 0);
    lv_obj_set_style_border_side(r, LV_BORDER_SIDE_LEFT, 0); // coloured edge = how urgent
    lv_obj_set_style_border_width(r, px(4), 0);
    const int text = W - 16 - 4 - 8 - 8 - BUTTON_W;
    lblTaskName[row] = ui_label(r, ui.small, C_TEXT, 8, 5, text);
    lblTaskDue[row] = ui_label(r, ui.small, C_DIM, 8, 24, text);
    ui_button_create(btnDone[row], r, done_fire, row);
    lv_obj_set_size(btnDone[row].btn, px(BUTTON_W), px(TARGET));
    lv_obj_align(btnDone[row].btn, LV_ALIGN_RIGHT_MID, 0, 0);
  }
  lblNoTasks = ui_label(maintBox, ui.small, C_DIM, 10, (ui.uh - 240) / 2 + 120, W - 20);
  lv_obj_set_style_text_align(lblNoTasks, LV_TEXT_ALIGN_CENTER, 0);
}

bool tasks_showing() { return showing; }

void tasks_close()
{
  showing = false;
  if (!maintBox) return;
  lv_obj_delete_async(maintBox); // not at once: Back and the gear are its own buttons
  maintBox = nullptr;
}

bool tasks_idle_timeout()
{
  if (!showing || millis() - touchedAt <= TASKS_IDLE_MS) return false;
  tasks_close();
  return true;
}

static void draw_rows()
{
  if (!maintBox) return;
  char buf[40];
  for (int row = 0; row < TASK_ROWS; row++) {
    int i = page * TASK_ROWS + row;
    if (i >= shownCount) {
      lv_obj_add_flag(taskRow[row], LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    lv_obj_remove_flag(taskRow[row], LV_OBJ_FLAG_HIDDEN);
    const Task &t = shown[i];
    uint32_t edge = t.level == 2 ? C_RED : t.level == 1 ? C_AMBER : C_GREY;
    uint32_t text = t.level == 2 ? C_BAD_TEXT : t.level == 1 ? C_WARN_TEXT : C_DIM;
    ui_set_text(lblTaskName[row], t.name);
    format_due(buf, sizeof(buf), t);
    ui_set_text(lblTaskDue[row], buf);
    lv_obj_set_style_text_color(lblTaskDue[row], lv_color_hex(text), 0);
    lv_obj_set_style_border_color(taskRow[row], lv_color_hex(edge), 0);
    // Green only where something is due, so the eye lands there first.
    bool under_way = ui_pending(Command::PerformTask, t.id);
    // The answer shows here, in the row that was pressed, until the list has been read again.
    const char *words = !under_way ? "Mark done" : ui_answered() ? LV_SYMBOL_OK " Done" : "Saving...";
    ui_button_set(btnDone[row], words, t.level > 0 ? Kind::Go : Kind::Quiet, true, Icon::None, t.id);
    ui_button_enable(btnDone[row], !ui_busy());
  }
}

void tasks_draw(const Snapshot &s)
{
  build();
  shownCount = s.taskCount;
  for (int i = 0; i < s.taskCount; i++) shown[i] = s.tasks[i];

  pages = (s.taskCount + TASK_ROWS - 1) / TASK_ROWS;
  if (pages < 1) pages = 1;
  if (page >= pages) page = 0;

  // With one page there is nothing to turn, and room for the title instead.
  char buf[16];
  snprintf(buf, sizeof(buf), "%d/%d", page + 1, pages);
  lv_label_set_text(lblPage, buf);
  for (lv_obj_t *o : {btnPrev.btn, btnNext.btn, lblPage}) {
    if (pages > 1) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
  }
  lv_label_set_text(lblTitle, pages > 1 ? "" : "Maintenance");

  if (s.taskCount == 0) {
    lv_label_set_text(lblNoTasks, s.tasksKnown ? "No maintenance tasks for this printer." : "Bambuddy hasn't sent the tasks yet.");
    lv_obj_remove_flag(lblNoTasks, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(lblNoTasks, LV_OBJ_FLAG_HIDDEN);
  }
  draw_rows();
}

// Quiet when all is fine; an outline with a count when something is due soon; filled when due.
// Amber, not red: red is for errors and for Stop.
void tasks_draw_badge(const Snapshot &s)
{
  if (!s.tasksKnown) {
    lv_obj_add_flag(btnWrench.btn, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_obj_remove_flag(btnWrench.btn, LV_OBJ_FLAG_HIDDEN);
  int n = s.tasksDue + s.tasksWarn;
  char buf[12] = "";
  if (n > 0) snprintf(buf, sizeof(buf), "%d", n);
  lv_label_set_text(lblTasksCount, buf);
  lv_obj_set_style_bg_color(btnWrench.btn, lv_color_hex(s.tasksDue ? C_AMBER : C_CHIP), 0);
  lv_obj_set_style_border_color(btnWrench.btn, lv_color_hex(C_AMBER), 0);
  lv_obj_set_style_border_width(btnWrench.btn, !s.tasksDue && s.tasksWarn ? px(2) : 0, 0);
}
