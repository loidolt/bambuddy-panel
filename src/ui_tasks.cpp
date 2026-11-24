// The maintenance screen: the printer's maintenance tasks, most urgent first, four per page,
// and the wrench in the status screen's header that opens it.
#include "board.h"
#include "icons.h"
#include "ui_internal.h"
#include "ui_logic.h"

static const int TASK_ROWS = 4;               // per page
static const uint32_t TASKS_IDLE_MS = 30000;  // then back to the status screen by itself
static const int ROW_Y = 50, ROW_H = 44, ROW_GAP = 3;

static lv_obj_t *maintBox, *btnTasks, *lblTasksCount, *btnMore, *lblMore, *lblNoTasks;
static lv_obj_t *taskRow[TASK_ROWS], *lblTaskName[TASK_ROWS], *lblTaskDue[TASK_ROWS], *lblTaskDone[TASK_ROWS],
    *btnTaskDone[TASK_ROWS];
static bool showing = false;
static int page = 0;
static uint32_t touchedAt = 0;
static Task shown[MAX_TASKS]; // what the rows show, so Done acts on the task the person sees
static int shownCount = 0;
static Hold hold;

static void touched()
{
  touchedAt = millis();
  ui_redraw_soon();
}

static void open_cb(lv_event_t *)
{
  showing = true;
  page = 0;
  touched();
}

static void back_cb(lv_event_t *)
{
  showing = false;
  touched();
}

static void more_cb(lv_event_t *)
{
  page++; // wraps to the first page when drawn
  touched();
}

// "Done" resets the task's counter in Bambuddy, so it takes a hold, like Stop.
static void done_cb(lv_event_t *e)
{
  int row = (int)(intptr_t)lv_event_get_user_data(e);
  int i = page * TASK_ROWS + row;
  if (i >= shownCount) return;

  switch (lv_event_get_code(e)) {
  case LV_EVENT_PRESSED:
    hold.start = touchedAt = millis();
    hold.fired = false;
    lv_label_set_text(lblTaskDone[row], "Hold...");
    break;
  case LV_EVENT_PRESSING:
    if (!hold.fired && millis() - hold.start >= HOLD_MS) {
      hold.fired = true;
      lv_label_set_text(lblTaskDone[row], "OK");
      ui_send(Command::PerformTask, shown[i].id);
    }
    break;
  case LV_EVENT_RELEASED:
  case LV_EVENT_PRESS_LOST:
    touchedAt = millis();
    if (!hold.fired) lv_label_set_text(lblTaskDone[row], "Done");
    break;
  default:
    break;
  }
}

void tasks_create(lv_obj_t *screen, lv_obj_t *header)
{
  // Wrench: opens the maintenance screen. Small, so it takes taps from a little around it too.
  btnTasks = ui_button(header, C_CHIP, 144, 4, 42, 30, nullptr);
  lv_obj_set_ext_click_area(btnTasks, 8);
  lv_obj_set_flex_flow(btnTasks, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(btnTasks, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(btnTasks, 3, 0);
  lv_obj_add_event_cb(btnTasks, open_cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *wrench = lv_image_create(btnTasks);
  lv_image_set_src(wrench, &icon_wrench);
  lv_obj_set_style_image_recolor(wrench, lv_color_hex(C_TEXT), 0);
  lv_obj_set_style_image_recolor_opa(wrench, LV_OPA_COVER, 0);
  lblTasksCount = lv_label_create(btnTasks);
  lv_obj_set_style_text_font(lblTasksCount, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(lblTasksCount, lv_color_hex(C_TEXT), 0);
  lv_label_set_text(lblTasksCount, "");
  lv_obj_add_flag(btnTasks, LV_OBJ_FLAG_HIDDEN);

  // The screen: a header and four task rows per page. Every button is at least 44 px tall.
  maintBox = ui_box(screen);
  lv_obj_t *back = ui_button(maintBox, C_GREY, 8, 2, 80, 44, LV_SYMBOL_LEFT " Back");
  lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *title = ui_label(maintBox, &lv_font_montserrat_14, C_TEXT, 92, 16, 128);
  lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(title, "Maintenance");
  btnMore = ui_button(maintBox, C_GREY, 224, 2, 88, 44, "More", &lblMore);
  lv_obj_add_event_cb(btnMore, more_cb, LV_EVENT_CLICKED, nullptr);
  for (int row = 0; row < TASK_ROWS; row++) {
    lv_obj_t *r = taskRow[row] = lv_obj_create(maintBox);
    lv_obj_remove_style_all(r);
    lv_obj_set_pos(r, 8, ROW_Y + row * (ROW_H + ROW_GAP));
    lv_obj_set_size(r, 304, ROW_H);
    lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(r, lv_color_hex(C_ROW), 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(r, 6, 0);
    lv_obj_set_style_border_side(r, LV_BORDER_SIDE_LEFT, 0); // coloured edge = how urgent
    lv_obj_set_style_border_width(r, 4, 0);
    lblTaskName[row] = ui_label(r, &lv_font_montserrat_14, C_TEXT, 8, 4, 196);
    lblTaskDue[row] = ui_label(r, &lv_font_montserrat_14, C_DIM, 8, 23, 196);
    lv_obj_t *done = btnTaskDone[row] = ui_button(r, C_GREY, 212, 0, 92, ROW_H, "Done", &lblTaskDone[row]);
    lv_obj_add_event_cb(done, done_cb, LV_EVENT_ALL, (void *)(intptr_t)row);
  }
  lblNoTasks = ui_label(maintBox, &lv_font_montserrat_14, C_DIM, 10, 110, 300);
  lv_obj_set_style_text_align(lblNoTasks, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_add_flag(maintBox, LV_OBJ_FLAG_HIDDEN);
}

bool tasks_showing() { return showing; }
void tasks_close() { showing = false; }
lv_obj_t *tasks_box() { return maintBox; }

bool tasks_idle_timeout()
{
  if (!showing || millis() - touchedAt <= TASKS_IDLE_MS) return false;
  showing = false;
  return true;
}

void tasks_draw(const Snapshot &s)
{
  shownCount = s.taskCount;
  for (int i = 0; i < s.taskCount; i++) shown[i] = s.tasks[i];

  int pages = (s.taskCount + TASK_ROWS - 1) / TASK_ROWS;
  if (pages < 1) pages = 1;
  if (page >= pages) page = 0;

  char buf[40];
  if (pages > 1) {
    snprintf(buf, sizeof(buf), "More %d/%d", page + 1, pages);
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
    int i = page * TASK_ROWS + row;
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

// Quiet when all is fine, coloured with a count when not.
void tasks_draw_badge(const Snapshot &s)
{
  if (!s.tasksKnown) {
    lv_obj_add_flag(btnTasks, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_obj_remove_flag(btnTasks, LV_OBJ_FLAG_HIDDEN);
  int n = s.tasksDue + s.tasksWarn;
  char buf[12] = "";
  if (n > 0) snprintf(buf, sizeof(buf), "%d", n);
  lv_label_set_text(lblTasksCount, buf);
  lv_obj_set_style_bg_color(btnTasks, lv_color_hex(s.tasksDue ? C_RED : s.tasksWarn ? C_AMBER : C_CHIP), 0);
}
