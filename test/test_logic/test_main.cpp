// Tests for the parts with no hardware in them. Run with: pio test -e test
//
// The JSON samples follow what Bambuddy 1.3 returns, trimmed, plus fields the panel must ignore.
#include <unity.h>

#include <cstdlib>
#include <cstring>

#include "bambuddy_parse.h"
#include "board.h"
#include "touch_cal.h"

void setUp() {}
void tearDown() {}

// Reads JSON the way the panel does: through the filter, then the parser.
template <class Filter, class Parse>
static void read(const char *json, Filter filter, Parse parse)
{
  JsonDocument f, doc;
  filter(f);
  DeserializationError err = deserializeJson(doc, json, DeserializationOption::Filter(f));
  TEST_ASSERT_FALSE_MESSAGE(err, err.c_str());
  parse(doc);
}

// ---------------------------------------------------------------- status

static const char *STATUS_FINISHED = R"({
  "id": 1, "name": "X1C", "connected": true, "state": "FINISH",
  "current_print": "Spool_plate_1", "subtask_name": "Spool_plate_1.gcode.3mf",
  "progress": 100.0, "remaining_time": 0, "layer_num": 203, "total_layers": 203,
  "temperatures": {"bed": 27.0, "bed_target": 0.0, "nozzle": 30.4, "nozzle_target": 0.0, "chamber": 29.0},
  "hms_errors": [], "chamber_light": true, "awaiting_plate_clear": true, "stg_cur_name": null,
  "ams": [{"id": 0, "tray": [{"id": 0, "tray_type": "PETG"}]}], "wifi_signal": -60
})";

static void test_status_finished()
{
  Snapshot s;
  read(STATUS_FINISHED, status_filter, [&](JsonDocument &d) { parse_status(d, s); });
  TEST_ASSERT_TRUE(s.connected);
  TEST_ASSERT_EQUAL_STRING("X1C", s.name);
  TEST_ASSERT_EQUAL_STRING("FINISH", s.state);
  TEST_ASSERT_EQUAL_STRING("Spool_plate_1", s.job); // extension dropped
  TEST_ASSERT_EQUAL_INT(203, s.totalLayers);
  TEST_ASSERT_EQUAL_FLOAT(30.4f, s.nozzle);
  TEST_ASSERT_TRUE(s.light);
  TEST_ASSERT_TRUE(s.awaitingPlateClear);
  TEST_ASSERT_EQUAL_INT(0, s.errorCount);
  TEST_ASSERT_EQUAL_STRING("", s.stage); // null becomes empty, not a crash
}

static void test_status_error_and_fallbacks()
{
  const char *json = R"({
    "name": "P1S", "connected": true, "state": "PAUSE", "subtask_name": null,
    "current_print": "Gears.3mf", "progress": 42.4, "remaining_time": 83,
    "hms_errors": [{"code": "0x700", "full_code": "0700_2000_0002_0001", "description": null},
                   {"code": "0x701", "full_code": "0701", "description": "Second"}]
  })";
  Snapshot s;
  read(json, status_filter, [&](JsonDocument &d) { parse_status(d, s); });
  TEST_ASSERT_EQUAL_STRING("Gears", s.job);                // falls back to current_print
  TEST_ASSERT_EQUAL_INT(83, s.remainingMin);
  TEST_ASSERT_EQUAL_INT(2, s.errorCount);
  TEST_ASSERT_EQUAL_STRING("0700_2000_0002_0001", s.error); // no description: show the code
  TEST_ASSERT_FALSE(s.light);                              // missing fields take safe defaults
  TEST_ASSERT_FALSE(s.awaitingPlateClear);
}

static void test_long_names_are_cut_not_overrun()
{
  const char *json = R"({"name": "A printer with a very long name indeed", "connected": true, "state": "IDLE",
    "subtask_name": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"})";
  Snapshot s;
  read(json, status_filter, [&](JsonDocument &d) { parse_status(d, s); });
  TEST_ASSERT_EQUAL_size_t(sizeof(s.name) - 1, strlen(s.name));
  TEST_ASSERT_EQUAL_size_t(sizeof(s.job) - 1, strlen(s.job));
}

// ---------------------------------------------------------------- queue

static void test_queue_picks_lowest_position()
{
  const char *json = R"([
    {"id": 7, "position": 3, "manual_start": false, "library_file_name": "Later.gcode.3mf", "print_time_seconds": 600},
    {"id": 9, "position": 1, "manual_start": true, "library_file_name": null, "archive_name": "First", "print_time_seconds": 11520},
    {"id": 8, "position": 2, "manual_start": false, "library_file_name": "Middle.3mf"}
  ])";
  Snapshot s;
  read(json, queue_filter, [&](JsonDocument &d) { parse_queue(d, s); });
  TEST_ASSERT_EQUAL_INT(3, s.queued);
  TEST_ASSERT_EQUAL_INT(9, s.nextId);
  TEST_ASSERT_TRUE(s.nextManual);
  TEST_ASSERT_EQUAL_STRING("First", s.nextName); // falls back to the archive name
  TEST_ASSERT_EQUAL_INT(11520, s.nextSeconds);
}

static void test_queue_empty_clears_previous()
{
  Snapshot s;
  s.nextId = 5;
  s.queued = 2;
  strcpy(s.nextName, "old");
  read("[]", queue_filter, [&](JsonDocument &d) { parse_queue(d, s); });
  TEST_ASSERT_EQUAL_INT(0, s.queued);
  TEST_ASSERT_EQUAL_INT(0, s.nextId);
  TEST_ASSERT_EQUAL_STRING("", s.nextName);
}

// ---------------------------------------------------------------- outcome question

static const char *LAST_PRINTS = R"([
  {"id": 15, "printer_id": 1, "run_id": 26, "status": "completed", "confirm_requested": true, "user_verdict": null},
  {"id": 16, "printer_id": 2, "run_id": 30, "status": "completed", "confirm_requested": true, "user_verdict": "good"},
  {"id": 17, "printer_id": 3, "run_id": 31, "status": "completed", "confirm_requested": false, "user_verdict": null}
])";

static void test_verdict_pending_only_when_asked_and_unanswered()
{
  Snapshot s;
  read(LAST_PRINTS, last_print_filter, [&](JsonDocument &d) { parse_last_print(d, 1, s); });
  TEST_ASSERT_TRUE(s.verdictPending);
  TEST_ASSERT_EQUAL_INT(15, s.archiveId);
  TEST_ASSERT_EQUAL_INT(26, s.runId);

  read(LAST_PRINTS, last_print_filter, [&](JsonDocument &d) { parse_last_print(d, 2, s); });
  TEST_ASSERT_FALSE(s.verdictPending); // already answered

  read(LAST_PRINTS, last_print_filter, [&](JsonDocument &d) { parse_last_print(d, 3, s); });
  TEST_ASSERT_FALSE(s.verdictPending); // never asked

  read(LAST_PRINTS, last_print_filter, [&](JsonDocument &d) { parse_last_print(d, 4, s); });
  TEST_ASSERT_FALSE(s.verdictPending); // no print for this printer
  TEST_ASSERT_EQUAL_INT(0, s.archiveId);
}

// ---------------------------------------------------------------- maintenance

static void test_tasks_sorted_by_urgency()
{
  const char *json = R"({"printer_id": 1, "due_count": 1, "warning_count": 1, "maintenance_items": [
    {"id": 1, "maintenance_type_name": "Far off", "enabled": true, "interval_type": "hours", "hours_until_due": 442.9, "is_due": false, "is_warning": false},
    {"id": 2, "maintenance_type_name": "Soon", "enabled": true, "interval_type": "hours", "hours_until_due": 8.0, "is_due": false, "is_warning": true},
    {"id": 3, "maintenance_type_name": "Switched off", "enabled": false, "interval_type": "hours", "hours_until_due": -50, "is_due": true, "is_warning": false},
    {"id": 4, "maintenance_type_name": "Overdue", "enabled": true, "interval_type": "hours", "hours_until_due": -3.2, "is_due": true, "is_warning": false},
    {"id": 5, "maintenance_type_name": "In two days", "enabled": true, "interval_type": "days", "days_until_due": 2, "hours_until_due": 0, "is_due": false, "is_warning": false},
    {"id": 6, "maintenance_type_name": "In 60 hours", "enabled": true, "interval_type": "hours", "hours_until_due": 60, "is_due": false, "is_warning": false}
  ]})";
  Snapshot s;
  read(json, tasks_filter, [&](JsonDocument &d) { parse_tasks(d, s); });
  TEST_ASSERT_TRUE(s.tasksKnown);
  TEST_ASSERT_EQUAL_INT(5, s.taskCount); // the disabled one is left out
  TEST_ASSERT_EQUAL_INT(1, s.tasksDue);
  TEST_ASSERT_EQUAL_INT(1, s.tasksWarn);
  const int expected[] = {4, 2, 5, 6, 1}; // due, due soon, then by time left (2 days = 48 h)
  for (int i = 0; i < 5; i++) TEST_ASSERT_EQUAL_INT(expected[i], s.tasks[i].id);
  TEST_ASSERT_TRUE(s.tasks[2].days);
  TEST_ASSERT_EQUAL_FLOAT(2, s.tasks[2].left);
}

static void test_tasks_keep_the_most_urgent_when_too_many()
{
  // 20 tasks, due in 20 h down to 1 h: the 12 kept must be the 1..12 h ones, in order.
  char json[4096] = "{\"maintenance_items\":[";
  for (int i = 20; i >= 1; i--) {
    char item[160];
    snprintf(item, sizeof(item), "%s{\"id\":%d,\"maintenance_type_name\":\"T%d\",\"enabled\":true,"
             "\"interval_type\":\"hours\",\"hours_until_due\":%d,\"is_due\":false,\"is_warning\":false}",
             i == 20 ? "" : ",", i, i, i);
    strcat(json, item);
  }
  strcat(json, "]}");
  Snapshot s;
  read(json, tasks_filter, [&](JsonDocument &d) { parse_tasks(d, s); });
  TEST_ASSERT_EQUAL_INT(MAX_TASKS, s.taskCount);
  for (int i = 0; i < MAX_TASKS; i++) TEST_ASSERT_EQUAL_INT(i + 1, s.tasks[i].id);
}

// ---------------------------------------------------------------- touch calibration

static const int INSET = 20;

// A pretend touch panel: what it reports for a screen point, with optional mirrored or swapped axes.
static void raw_of(int sx, int sy, bool flipX, bool flipY, bool swap, long &rx, long &ry)
{
  long x = (flipX ? 3700 : 200) + (long)sx * (flipX ? -3500 : 3500) / (SCREEN_W - 1);
  long y = (flipY ? 3800 : 240) + (long)sy * (flipY ? -3560 : 3560) / (SCREEN_H - 1);
  rx = swap ? y : x;
  ry = swap ? x : y;
}

static void four_taps(bool flipX, bool flipY, bool swap, long rx[4], long ry[4])
{
  const int target[4][2] = {{INSET, INSET},
                            {SCREEN_W - 1 - INSET, INSET},
                            {SCREEN_W - 1 - INSET, SCREEN_H - 1 - INSET},
                            {INSET, SCREEN_H - 1 - INSET}};
  for (int i = 0; i < 4; i++) raw_of(target[i][0], target[i][1], flipX, flipY, swap, rx[i], ry[i]);
}

static void test_calibration_every_orientation()
{
  for (int mode = 0; mode < 8; mode++) {
    bool flipX = mode & 1, flipY = mode & 2, swap = mode & 4;
    long rx[4], ry[4];
    four_taps(flipX, flipY, swap, rx, ry);
    TouchCal cal;
    TEST_ASSERT_TRUE(touch_cal_solve(rx, ry, INSET, cal));
    TEST_ASSERT_EQUAL(swap, cal.swap);

    // Every point on the screen must land within 2 px of where the finger is.
    for (int sx = 0; sx < SCREEN_W; sx += 29)
      for (int sy = 0; sy < SCREEN_H; sy += 23) {
        long x, y;
        raw_of(sx, sy, flipX, flipY, swap, x, y);
        int px, py;
        touch_cal_apply(cal, (int)x, (int)y, px, py);
        TEST_ASSERT_INT_WITHIN(2, sx, px);
        TEST_ASSERT_INT_WITHIN(2, sy, py);
      }
  }
}

static void test_calibration_refuses_nonsense()
{
  TouchCal cal;
  long sameX[4] = {2000, 2010, 1990, 2005}, sameY[4] = {2000, 1995, 2005, 2000};
  TEST_ASSERT_FALSE(touch_cal_solve(sameX, sameY, INSET, cal)); // one spot tapped four times

  long rx[4], ry[4];
  four_taps(false, false, false, rx, ry);
  rx[2] -= 1200;
  TEST_ASSERT_FALSE(touch_cal_solve(rx, ry, INSET, cal)); // one corner far off
}

static void test_touch_is_clamped_to_the_screen()
{
  TouchCal cal = {200, 3700, 240, 3800, false};
  int x, y;
  touch_cal_apply(cal, 0, 4095, x, y);
  TEST_ASSERT_EQUAL_INT(0, x);
  TEST_ASSERT_EQUAL_INT(SCREEN_H - 1, y);
}

int main()
{
  UNITY_BEGIN();
  RUN_TEST(test_status_finished);
  RUN_TEST(test_status_error_and_fallbacks);
  RUN_TEST(test_long_names_are_cut_not_overrun);
  RUN_TEST(test_queue_picks_lowest_position);
  RUN_TEST(test_queue_empty_clears_previous);
  RUN_TEST(test_verdict_pending_only_when_asked_and_unanswered);
  RUN_TEST(test_tasks_sorted_by_urgency);
  RUN_TEST(test_tasks_keep_the_most_urgent_when_too_many);
  RUN_TEST(test_calibration_every_orientation);
  RUN_TEST(test_calibration_refuses_nonsense);
  RUN_TEST(test_touch_is_clamped_to_the_screen);
  return UNITY_END();
}
