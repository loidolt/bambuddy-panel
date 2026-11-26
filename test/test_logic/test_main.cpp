// Tests for the parts with no hardware in them. Run with: pio test -e test
//
// The JSON samples follow what Bambuddy 1.3 returns, trimmed, plus fields the panel must ignore.
#include <unity.h>

#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "bambuddy_api.h"
#include "bambuddy_parse.h"
#include "poller.h"
#include "setup_page.h"
#include "touch_cal.h"
#include "ui_logic.h"

static uint32_t now = 0;
uint32_t millis() { return now; }

void setUp() { now = 0; }
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
  TEST_ASSERT_TRUE(s.awaitingPlateClear);
  TEST_ASSERT_EQUAL_INT(0, s.errorCount);
  TEST_ASSERT_EQUAL_STRING("", s.stage); // null becomes empty, not a crash
}

static void test_status_error_and_fallbacks()
{
  const char *json = R"({
    "name": "P1S", "connected": true, "state": "PAUSE", "subtask_name": null,
    "current_print": "Gears.3mf", "progress": 42.4, "remaining_time": 83,
    "hms_errors": [{"code": "0x700", "full_code": "0700200000020001", "severity": 1, "description": null,
                    "actions": ["retry"]},
                   {"code": "0x701", "full_code": "0701200000020001", "severity": 2, "description": "Second"}]
  })";
  Snapshot s;
  read(json, status_filter, [&](JsonDocument &d) { parse_status(d, s); });
  TEST_ASSERT_EQUAL_STRING("Gears", s.job);                // falls back to current_print
  TEST_ASSERT_EQUAL_INT(83, s.remainingMin);
  TEST_ASSERT_EQUAL_INT(2, s.errorCount);
  TEST_ASSERT_EQUAL_STRING("0700200000020001", s.errors[0]); // no description: show the code
  TEST_ASSERT_EQUAL_STRING("Second", s.errors[1]);           // the next one can be read too
  TEST_ASSERT_FALSE(s.awaitingPlateClear);
}

// Bambuddy's own printer card leaves these out (filterKnownHMSErrors); so must the panel, or it
// shows "Error" on a printer that has none.
static void test_notices_and_unknown_codes_are_not_errors()
{
  const char *json = R"({"connected": true, "state": "RUNNING", "hms_errors": [
    {"full_code": "0300960000030001", "severity": 3, "description": "The top cover is open.", "actions": []},
    {"full_code": "0C00001B", "severity": 3, "description": null},
    {"full_code": "0500400000020001", "severity": 0, "description": "Invalid level"}
  ]})";
  Snapshot s;
  read(json, status_filter, [&](JsonDocument &d) { parse_status(d, s); });
  TEST_ASSERT_EQUAL_INT(0, s.errorCount);
  TEST_ASSERT_EQUAL_STRING("", s.errors[0]);

  // A prompt from print_error (8 characters) at the same level counts, and is the one shown
  // although a notice comes first. So does a fault from a Bambuddy that sends no severity.
  json = R"({"connected": true, "state": "PAUSE", "hms_errors": [
    {"full_code": "0300960000030001", "severity": 3, "description": "The top cover is open."},
    {"full_code": "0C008001", "severity": 3, "description": "Unable to start drying."},
    {"full_code": "0700200000020001", "description": "Old server"}
  ]})";
  read(json, status_filter, [&](JsonDocument &d) { parse_status(d, s); });
  TEST_ASSERT_EQUAL_INT(2, s.errorCount);
  TEST_ASSERT_EQUAL_STRING("Unable to start drying.", s.errors[0]);
}

static void test_long_file_names_lose_the_extension_first()
{
  // Short enough to fit without its extension, too long to fit with it: nothing of
  // ".gcode.3mf" may be left at the end.
  std::string base(58, 'a');
  std::string doc = R"({"connected": true, "state": "RUNNING", "subtask_name": ")" + base + R"(.gcode.3mf"})";
  const char *json = doc.c_str();
  Snapshot s;
  read(json, status_filter, [&](JsonDocument &d) { parse_status(d, s); });
  TEST_ASSERT_EQUAL_STRING(base.c_str(), s.job);
  json = R"({"connected": true, "state": "RUNNING", "subtask_name": "Grüße.gcode"})";
  read(json, status_filter, [&](JsonDocument &d) { parse_status(d, s); });
  TEST_ASSERT_EQUAL_STRING("Grusse", s.job);
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

static void test_status_odd_numbers_are_tamed()
{
  // A float where an integer is expected, values out of range, and strings where numbers go.
  const char *json = R"({"connected": true, "state": "RUNNING", "progress": 150, "remaining_time": 83.7,
    "layer_num": "12", "total_layers": -5, "temperatures": {"nozzle": 1e30, "bed": null}})";
  Snapshot s;
  read(json, status_filter, [&](JsonDocument &d) { parse_status(d, s); });
  TEST_ASSERT_EQUAL_FLOAT(100, s.progress);
  TEST_ASSERT_EQUAL_INT(83, s.remainingMin);
  TEST_ASSERT_EQUAL_INT(0, s.layer);
  TEST_ASSERT_EQUAL_INT(0, s.totalLayers);
  TEST_ASSERT_EQUAL_FLOAT(1000, s.nozzle);
  TEST_ASSERT_EQUAL_FLOAT(0, s.bed);
}

// ---------------------------------------------------------------- text the fonts can draw

static void test_text_becomes_ascii()
{
  char out[64];
  copy_ascii(out, sizeof(out), "Drachen f\xC3\xBCr J\xC3\xBCrgen \xC3\x98\x34\x30 Stra\xC3\x9F" "e");
  TEST_ASSERT_EQUAL_STRING("Drachen fur Jurgen O40 Strasse", out);
  copy_ascii(out, sizeof(out), "\xE6\x89\x93\xE5\x8D\xB0 \xE2\x80\x9Cok\xE2\x80\x9D \xE2\x80\x93 x"); // CJK, quotes, dash
  TEST_ASSERT_EQUAL_STRING("?? \"ok\" - x", out);
  copy_ascii(out, sizeof(out), "tab\there\xC3"); // a control character, and a cut-off sequence
  TEST_ASSERT_EQUAL_STRING("tab here?", out);
  copy_ascii(out, sizeof(out), nullptr);
  TEST_ASSERT_EQUAL_STRING("", out);
}

static void test_text_is_cut_on_whole_characters()
{
  char out[6];
  copy_ascii(out, sizeof(out), "abcd\xC3\x9F"); // the sharp s becomes "ss", which doesn't fit whole
  TEST_ASSERT_EQUAL_STRING("abcd", out);
  copy_ascii(out, sizeof(out), "abcdefgh");
  TEST_ASSERT_EQUAL_STRING("abcde", out);
}

// ---------------------------------------------------------------- queue

static void test_queue_picks_lowest_position()
{
  const char *json = R"([
    {"id": 7, "position": 3, "status": "pending", "printer_id": 1, "manual_start": false, "library_file_name": "Later.gcode.3mf", "print_time_seconds": 600},
    {"id": 9, "position": 1, "status": "pending", "printer_id": 1, "manual_start": true, "library_file_name": null, "archive_name": "First", "print_time_seconds": 11520},
    {"id": 8, "position": 2, "status": "pending", "printer_id": 1, "manual_start": false, "library_file_name": "Middle.3mf"}
  ])";
  Snapshot s;
  read(json, queue_filter, [&](JsonDocument &d) { parse_queue(d, 1, s); });
  TEST_ASSERT_EQUAL_INT(3, s.queued);
  TEST_ASSERT_EQUAL_INT(9, s.nextId);
  TEST_ASSERT_TRUE(s.nextManual);
  TEST_ASSERT_TRUE(s.nextAssigned);
  TEST_ASSERT_EQUAL_STRING("First", s.nextName); // falls back to the archive name
  TEST_ASSERT_EQUAL_INT(11520, s.nextSeconds);
}

static void test_queue_lists_what_comes_after_and_why_it_waits()
{
  const char *json = R"([
    {"id": 5, "position": 9, "status": "pending", "printer_id": 1, "library_file_name": "Fourth.3mf", "print_time_seconds": 60},
    {"id": 7, "position": 3, "status": "pending", "printer_id": 1, "library_file_name": "Third.gcode.3mf", "print_time_seconds": 600},
    {"id": 9, "position": 1, "status": "pending", "printer_id": 1, "library_file_name": "First.3mf", "print_time_seconds": 120,
     "waiting_reason": "Waiting for filament: PETG black", "filament_short": true, "created_by_username": "anna"},
    {"id": 8, "position": 2, "status": "pending", "printer_id": 1, "library_file_name": "Second.3mf", "print_time_seconds": 300},
    {"id": 3, "position": 0, "status": "printing", "printer_id": 1, "library_file_name": "Now.3mf", "created_by_username": "J\u00fcrgen"},
    {"id": 2, "position": 0, "status": "printing", "printer_id": 4, "library_file_name": "Elsewhere.3mf", "created_by_username": "bob"}
  ])";
  Snapshot s;
  read(json, queue_filter, [&](JsonDocument &d) { parse_queue(d, 1, s); });
  TEST_ASSERT_EQUAL_INT(4, s.queued); // the one printing is not waiting
  TEST_ASSERT_EQUAL_INT(9, s.nextId);
  TEST_ASSERT_EQUAL_STRING("Waiting for filament: PETG black", s.nextWhy);
  TEST_ASSERT_EQUAL_STRING("anna", s.nextBy);
  TEST_ASSERT_EQUAL_INT(2, s.moreCount);
  TEST_ASSERT_EQUAL_STRING("Second", s.more[0].name);
  TEST_ASSERT_EQUAL_INT(300, s.more[0].seconds);
  TEST_ASSERT_EQUAL_STRING("Third", s.more[1].name);
  TEST_ASSERT_EQUAL_STRING("Jurgen", s.jobBy); // this printer's print, not the other printer's
}

static void test_queue_says_filament_is_short_when_bambuddy_gives_no_words()
{
  const char *json = R"([{"id": 9, "position": 1, "status": "pending", "printer_id": 1, "library_file_name": "A.3mf",
                          "waiting_reason": null, "filament_short": true}])";
  Snapshot s;
  read(json, queue_filter, [&](JsonDocument &d) { parse_queue(d, 1, s); });
  TEST_ASSERT_EQUAL_STRING("Not enough filament loaded", s.nextWhy);
  TEST_ASSERT_EQUAL_STRING("", s.nextBy);
  TEST_ASSERT_EQUAL_INT(0, s.moreCount);
}

static void test_queue_empty_clears_previous()
{
  Snapshot s;
  s.nextId = 5;
  s.queued = 2;
  s.held = 3;
  strcpy(s.nextName, "old");
  strcpy(s.nextWhy, "old");
  strcpy(s.jobBy, "old");
  s.moreCount = 2;
  read("[]", queue_filter, [&](JsonDocument &d) { parse_queue(d, 1, s); });
  TEST_ASSERT_EQUAL_INT(0, s.queued);
  TEST_ASSERT_EQUAL_INT(0, s.held);
  TEST_ASSERT_EQUAL_INT(0, s.nextId);
  TEST_ASSERT_EQUAL_STRING("", s.nextName);
  TEST_ASSERT_EQUAL_STRING("", s.nextWhy);
  TEST_ASSERT_EQUAL_STRING("", s.jobBy);
  TEST_ASSERT_EQUAL_INT(0, s.moreCount);
}

static void test_queue_counts_held_and_any_model_items()
{
  const char *json = R"([
    {"id": 1, "position": 1, "status": "skipped", "printer_id": 1, "error_message": "Previous print failed or was aborted"},
    {"id": 2, "position": 2, "status": "skipped", "printer_id": 1, "error_message": "Filament mismatch"},
    {"id": 3, "position": 3, "status": "skipped", "printer_id": 1, "error_message": "Previous print failed or was aborted"},
    {"id": 4, "position": 4, "status": "pending", "printer_id": null, "manual_start": true, "archive_name": "Any X1C job"}
  ])";
  Snapshot s;
  read(json, queue_filter, [&](JsonDocument &d) { parse_queue(d, 1, s); });
  TEST_ASSERT_EQUAL_INT(2, s.held);    // only what "resume queue" puts back
  TEST_ASSERT_EQUAL_INT(1, s.queued);  // skipped items aren't queued
  TEST_ASSERT_EQUAL_INT(4, s.nextId);
  TEST_ASSERT_FALSE(s.nextAssigned);   // "any X1C": the panel must not start it on this printer
}

// ---------------------------------------------------------------- outcome question

static const char *LAST_PRINTS = R"([
  {"id": 15, "printer_id": 1, "run_id": 26, "status": "completed", "confirm_requested": true, "user_verdict": null},
  {"id": 16, "printer_id": 2, "run_id": 30, "status": "completed", "confirm_requested": true, "user_verdict": "good"},
  {"id": 17, "printer_id": 3, "run_id": 31, "status": "completed", "confirm_requested": false, "user_verdict": null},
  {"id": 18, "printer_id": 5, "run_id": 40, "status": "failed", "confirm_requested": true, "user_verdict": null},
  {"id": 19, "printer_id": 6, "run_id": 41, "status": "printing", "confirm_requested": true, "user_verdict": null}
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

static void test_verdict_not_asked_for_failed_or_running_prints()
{
  Snapshot s;
  read(LAST_PRINTS, last_print_filter, [&](JsonDocument &d) { parse_last_print(d, 5, s); });
  TEST_ASSERT_FALSE(s.verdictPending); // Bambuddy only asks about completed prints
  read(LAST_PRINTS, last_print_filter, [&](JsonDocument &d) { parse_last_print(d, 6, s); });
  TEST_ASSERT_FALSE(s.verdictPending);
}

// ---------------------------------------------------------------- plate-clear setting

static void test_plate_clear_setting()
{
  Snapshot s;
  TEST_ASSERT_TRUE(s.requirePlateClear); // assumed on until read
  read(R"({"require_plate_clear": false, "currency": "EUR"})", settings_filter, [&](JsonDocument &d) { parse_settings(d, s); });
  TEST_ASSERT_FALSE(s.requirePlateClear);
  read(R"({"require_plate_clear": true})", settings_filter, [&](JsonDocument &d) { parse_settings(d, s); });
  TEST_ASSERT_TRUE(s.requirePlateClear);
  read(R"({"require_plate_clear": "true"})", settings_filter, [&](JsonDocument &d) { parse_settings(d, s); });
  TEST_ASSERT_TRUE(s.requirePlateClear);
  read(R"({})", settings_filter, [&](JsonDocument &d) { parse_settings(d, s); });
  TEST_ASSERT_FALSE(s.requirePlateClear); // Bambuddy's default
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
    {"id": 6, "maintenance_type_name": "In 60 hours", "enabled": true, "interval_type": "hours", "hours_until_due": 60, "days_until_due": null, "is_due": false, "is_warning": false}
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

static void test_tasks_without_a_time_go_last()
{
  const char *json = R"({"maintenance_items": [
    {"id": 1, "maintenance_type_name": "No time", "enabled": true, "interval_type": "days", "days_until_due": null, "is_due": false, "is_warning": false},
    {"id": 2, "maintenance_type_name": "Far off", "enabled": true, "interval_type": "hours", "hours_until_due": 442, "is_due": false, "is_warning": false}
  ]})";
  Snapshot s;
  read(json, tasks_filter, [&](JsonDocument &d) { parse_tasks(d, s); });
  TEST_ASSERT_EQUAL_INT(2, s.taskCount);
  TEST_ASSERT_EQUAL_INT(2, s.tasks[0].id);
  TEST_ASSERT_EQUAL_INT(1, s.tasks[1].id);
  char out[40];
  format_due(out, sizeof(out), s.tasks[1]);
  TEST_ASSERT_EQUAL_STRING("Not due", out); // no time to give, but never an empty line

  // An answer that isn't the task list leaves what was known alone.
  read(R"({"detail": "oops"})", tasks_filter, [&](JsonDocument &d) { parse_tasks(d, s); });
  TEST_ASSERT_EQUAL_INT(2, s.taskCount);
  Snapshot fresh;
  read(R"({"detail": "oops"})", tasks_filter, [&](JsonDocument &d) { parse_tasks(d, fresh); });
  TEST_ASSERT_FALSE(fresh.tasksKnown);
}

static void test_tasks_close_together_keep_their_order()
{
  // Fractions of an hour apart, far out: a single float sort key used to lose these.
  const char *json = R"({"maintenance_items": [
    {"id": 1, "enabled": true, "interval_type": "hours", "hours_until_due": 900.1, "is_due": false, "is_warning": false},
    {"id": 2, "enabled": true, "interval_type": "hours", "hours_until_due": 900.05, "is_due": false, "is_warning": false}
  ]})";
  Snapshot s;
  read(json, tasks_filter, [&](JsonDocument &d) { parse_tasks(d, s); });
  TEST_ASSERT_EQUAL_INT(2, s.tasks[0].id);
  TEST_ASSERT_EQUAL_INT(1, s.tasks[1].id);
}

static void test_tasks_keep_the_most_urgent_when_too_many()
{
  // 20 tasks, due in 20 h down to 1 h: the 12 kept must be the 1..12 h ones, in order.
  std::string json = "{\"maintenance_items\":[";
  for (int i = 20; i >= 1; i--) {
    char item[160];
    snprintf(item, sizeof(item), "%s{\"id\":%d,\"maintenance_type_name\":\"T%d\",\"enabled\":true,"
             "\"interval_type\":\"hours\",\"hours_until_due\":%d,\"is_due\":false,\"is_warning\":false}",
             i == 20 ? "" : ",", i, i, i);
    json += item;
  }
  json += "]}";
  Snapshot s;
  read(json.c_str(), tasks_filter, [&](JsonDocument &d) { parse_tasks(d, s); });
  TEST_ASSERT_EQUAL_INT(MAX_TASKS, s.taskCount);
  for (int i = 0; i < MAX_TASKS; i++) TEST_ASSERT_EQUAL_INT(i + 1, s.tasks[i].id);
}

// ---------------------------------------------------------------- requests

static void expect_request(Job job, const char *method, const char *path, const char *body)
{
  Request r;
  TEST_ASSERT_TRUE(command_request(job, 3, r));
  TEST_ASSERT_EQUAL_STRING(method, r.method);
  TEST_ASSERT_EQUAL_STRING(path, r.path);
  TEST_ASSERT_EQUAL_STRING(body, r.body);
}

static void test_requests_match_bambuddy_api()
{
  expect_request({Command::Pause, 0, 0}, "POST", "/api/v1/printers/3/print/pause", "");
  expect_request({Command::Resume, 0, 0}, "POST", "/api/v1/printers/3/print/resume", "");
  expect_request({Command::Stop, 0, 0}, "POST", "/api/v1/printers/3/print/stop", "");
  expect_request({Command::ClearPlate, 0, 0}, "POST", "/api/v1/printers/3/clear-plate", "");
  expect_request({Command::ClearErrors, 0, 0}, "POST", "/api/v1/printers/3/hms/clear", "");
  expect_request({Command::ResumeQueue, 0, 0}, "POST", "/api/v1/queue/printer/3/resume", "");
  expect_request({Command::StartNext, 42, 0}, "POST", "/api/v1/queue/42/start", "");
  expect_request({Command::VerdictGood, 15, 26}, "PATCH", "/api/v1/archives/15",
                 R"({"user_verdict":"good","user_verdict_source":"api","run_id":26})");
  expect_request({Command::VerdictReject, 15, 0}, "PATCH", "/api/v1/archives/15",
                 R"({"user_verdict":"reject","user_verdict_source":"api"})");
  expect_request({Command::PerformTask, 7, 0}, "POST", "/api/v1/maintenance/items/7/perform", "{}");
}

static void test_requests_need_an_id()
{
  Request r;
  TEST_ASSERT_FALSE(command_request({Command::StartNext, 0, 0}, 3, r));
  TEST_ASSERT_FALSE(command_request({Command::VerdictGood, 0, 26}, 3, r));
  TEST_ASSERT_FALSE(command_request({Command::PerformTask, 0, 0}, 3, r));
}

static void test_command_results_are_plain()
{
  char out[40];
  TEST_ASSERT_TRUE(command_result({Command::StartNext, 42, 0}, 200, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("Starting", out);
  TEST_ASSERT_FALSE(command_result({Command::StartNext, 42, 0}, 409, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("Not enough filament loaded", out);
  command_result({Command::Pause, 0, 0}, 409, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("The printer isn't ready for that", out); // no bare status codes
  TEST_ASSERT_FALSE(command_result({Command::Pause, 0, 0}, ERR_NO_WIFI, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("WiFi is down", out);
  command_result({Command::Pause, 0, 0}, -11, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("No answer from Bambuddy", out);
  TEST_ASSERT_TRUE(command_result({Command::PerformTask, 7, 0}, 204, out, sizeof(out)));
}

// "Not allowed" names the permission to tick in Bambuddy, for every command.
static void test_refused_command_names_the_permission()
{
  char out[40];
  const struct {
    Command cmd;
    const char *text;
  } cases[] = {
      {Command::Pause, "API key lacks Control Printer"},      {Command::Stop, "API key lacks Control Printer"},
      {Command::ClearPlate, "API key lacks Control Printer"}, {Command::ClearErrors, "API key lacks Control Printer"},
      {Command::StartNext, "API key lacks Manage Queue"},     {Command::ResumeQueue, "API key lacks Manage Queue"},
      {Command::VerdictGood, "API key lacks Manage Archives"}, {Command::PerformTask, "API key lacks Manage Maintenance"},
  };
  for (const auto &c : cases) {
    command_result({c.cmd, 1, 0}, 403, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING(c.text, out);
    TEST_ASSERT_TRUE(strlen(out) < sizeof(Snapshot().toast)); // fits the Snapshot's field whole
  }
}

static void test_offline_text_names_the_cause()
{
  char out[160];
  describe_offline(404, 7, out, sizeof(out));
  TEST_ASSERT_NOT_NULL(strstr(out, "printer with id 7"));
  TEST_ASSERT_NOT_NULL(strstr(out, "limited")); // Bambuddy says 404 for a printer outside the key's list
  describe_offline(403, 7, out, sizeof(out));
  TEST_ASSERT_NOT_NULL(strstr(out, "Read Status"));
  TEST_ASSERT_TRUE(strlen(out) < sizeof(out) - 1);
  describe_offline(ERR_HTTPS, 7, out, sizeof(out));
  TEST_ASSERT_NOT_NULL(strstr(out, "http://"));
  describe_offline(-1, 7, out, sizeof(out));
  TEST_ASSERT_NULL(strstr(out, "-1")); // no bare error numbers for the common cases
}

static void test_urls_are_cleaned_up()
{
  struct {
    const char *in, *out;
  } cases[] = {
      {"  http://10.0.0.5:3700/  ", "http://10.0.0.5:3700"},
      {"10.0.0.5:3700", "http://10.0.0.5:3700"},
      {"http://host:3700/api/v1/", "http://host:3700"},
      {"https://host", "https://host"},
      {"Http://Host:3700", "http://Host:3700"}, // a phone capitalises the first letter
      {"HTTPS://host/", "https://host"},
      {"", ""},
  };
  for (auto &c : cases) {
    char url[128];
    snprintf(url, sizeof(url), "%s", c.in);
    normalize_url(url, sizeof(url));
    TEST_ASSERT_EQUAL_STRING(c.out, url);
  }
}

// ---------------------------------------------------------------- polling round

// A small inventory, for the spool list tests and the pretend Bambuddy below.
static const char *SPOOLS = R"([
  {"id": 1, "material": "PLA", "brand": "Bambu Lab", "color_name": "Jade White", "rgba": "FFFFFFFF", "label_weight": 1000, "weight_used": 100},
  {"id": 2, "material": "PETG", "brand": "Prusament", "color_name": "Galaxy Black", "rgba": "101010FF", "label_weight": 1000, "weight_used": 1200},
  {"id": 3, "material": "pla", "brand": "eSun", "subtype": "Silk", "color_name": null, "rgba": "nonsense"},
  {"id": 4, "material": "PETG", "brand": null, "color_name": "Gr\u00fcn", "rgba": "00FF00"},
  {"id": 0, "material": "PLA"}
])";

struct FakeBambuddy {
  std::vector<std::string> asked;
  std::string status = STATUS_FINISHED;
  int statusCode = 200;
  std::string lastPrints = LAST_PRINTS;
  std::string queue = "[]";
  std::map<std::string, int> fail; // part of a path -> the code that request gets instead of 200
};

static int fake_fetch(const char *path, JsonDocument &doc, const JsonDocument &filter, void *ctx)
{
  FakeBambuddy &b = *(FakeBambuddy *)ctx;
  b.asked.push_back(path);
  std::string p = path;
  for (auto &f : b.fail)
    if (p.find(f.first) != std::string::npos) return f.second;
  const char *json = "{}";
  if (p.find("/status") != std::string::npos) {
    if (b.statusCode != 200) return b.statusCode;
    json = b.status.c_str();
  } else if (p.find("/queue/") == 0 || p.find("/api/v1/queue/") == 0) {
    json = b.queue.c_str();
  } else if (p.find("last-per-printer") != std::string::npos) {
    json = b.lastPrints.c_str();
  } else if (p.find("/inventory/spools") != std::string::npos) {
    json = SPOOLS;
  } else if (p.find("/inventory/assignments") != std::string::npos) {
    json = "[]";
  } else if (p.find("/maintenance/") != std::string::npos) {
    json = R"({"maintenance_items": []})";
  } else if (p.find("/settings/") != std::string::npos) {
    json = R"({"require_plate_clear": false})";
  }
  deserializeJson(doc, json, DeserializationOption::Filter(filter));
  return 200;
}

static void test_poll_round_asks_in_order()
{
  FakeBambuddy b;
  Poller p;
  p.printerId = 1;
  Snapshot s;
  poll_once(p, s, fake_fetch, &b);
  const char *expected[] = {"/api/v1/printers/1/status", "/api/v1/queue/?status=pending,skipped,printing&printer_id=1",
                            "/api/v1/archives/last-per-printer", "/api/v1/maintenance/printers/1",
                            "/api/v1/settings/", "/api/v1/inventory/assignments?printer_id=1"};
  TEST_ASSERT_EQUAL_size_t(6, b.asked.size()); // this printer has an AMS, so its spools are asked for
  for (int i = 0; i < 6; i++) TEST_ASSERT_EQUAL_STRING(expected[i], b.asked[i].c_str());
  TEST_ASSERT_EQUAL_UINT32(1, s.seq);
  TEST_ASSERT_EQUAL_INT(1, s.printerId);
  TEST_ASSERT_FALSE(s.requirePlateClear);
  TEST_ASSERT_TRUE(s.verdictPending);

  // Within the minute, maintenance, settings and the spool assignments aren't read again.
  b.asked.clear();
  now = 30000;
  poll_once(p, s, fake_fetch, &b);
  TEST_ASSERT_EQUAL_size_t(3, b.asked.size());
  now = 61000;
  b.asked.clear();
  poll_once(p, s, fake_fetch, &b);
  TEST_ASSERT_EQUAL_size_t(6, b.asked.size());
}

static void test_poll_round_stops_when_bambuddy_is_away()
{
  FakeBambuddy b;
  b.statusCode = 401;
  Poller p;
  p.printerId = 1;
  Snapshot s;
  poll_once(p, s, fake_fetch, &b);
  TEST_ASSERT_EQUAL_size_t(1, b.asked.size());
  TEST_ASSERT_FALSE(s.reachable);
  TEST_ASSERT_EQUAL_INT(401, s.httpCode);
}

static void test_one_missed_answer_is_not_offline()
{
  FakeBambuddy b;
  Poller p;
  p.printerId = 1;
  Snapshot s;
  poll_once(p, s, fake_fetch, &b);
  TEST_ASSERT_TRUE(s.reachable);

  b.statusCode = -11; // a timeout: the last status stays up
  poll_once(p, s, fake_fetch, &b);
  TEST_ASSERT_TRUE(s.reachable);
  TEST_ASSERT_EQUAL_STRING("FINISH", s.state);
  TEST_ASSERT_EQUAL_UINT32(2, s.seq);
  poll_once(p, s, fake_fetch, &b); // the second in a row
  TEST_ASSERT_FALSE(s.reachable);
  TEST_ASSERT_EQUAL_INT(-11, s.httpCode);

  b.statusCode = 200; // an answer in between starts the count again
  poll_once(p, s, fake_fetch, &b);
  b.statusCode = -1;
  poll_once(p, s, fake_fetch, &b);
  TEST_ASSERT_TRUE(s.reachable);
  b.statusCode = 200;
  poll_once(p, s, fake_fetch, &b);
  b.statusCode = -1;
  poll_once(p, s, fake_fetch, &b);
  TEST_ASSERT_TRUE(s.reachable);

  b.statusCode = 401; // Bambuddy answered, and said no: shown at once
  poll_once(p, s, fake_fetch, &b);
  TEST_ASSERT_FALSE(s.reachable);
}

static int count_asked(const FakeBambuddy &b, const char *part)
{
  int n = 0;
  for (auto &a : b.asked) n += a.find(part) != std::string::npos;
  return n;
}

// Status answers but another request doesn't: the screen keeps what it last knew.
static void test_failed_side_requests_keep_the_last_known()
{
  FakeBambuddy b;
  b.queue = R"([{"id": 9, "position": 1, "status": "pending", "printer_id": 1, "manual_start": true,
                 "library_file_name": "Next.3mf", "print_time_seconds": 600}])";
  Poller p;
  p.printerId = 1;
  Snapshot s;
  poll_once(p, s, fake_fetch, &b);
  TEST_ASSERT_EQUAL_INT(9, s.nextId);
  TEST_ASSERT_TRUE(s.verdictPending);
  TEST_ASSERT_TRUE(s.tasksKnown);

  b.fail = {{"/queue/", 500}, {"last-per-printer", -11}};
  now = 4000;
  poll_once(p, s, fake_fetch, &b);
  TEST_ASSERT_TRUE(s.reachable);
  TEST_ASSERT_EQUAL_INT(9, s.nextId);
  TEST_ASSERT_EQUAL_STRING("Next", s.nextName);
  TEST_ASSERT_TRUE(s.verdictPending);
  TEST_ASSERT_EQUAL_UINT32(2, s.seq);
}

// A failed maintenance or settings request is tried again after its minute, not on every poll.
static void test_failed_slow_requests_wait_their_minute()
{
  FakeBambuddy b;
  b.fail = {{"/maintenance/", 403}, {"/settings/", 500}};
  Poller p;
  p.printerId = 1;
  Snapshot s;
  poll_once(p, s, fake_fetch, &b);
  TEST_ASSERT_FALSE(s.tasksKnown);
  TEST_ASSERT_TRUE(s.requirePlateClear); // never read: the gate stays offered (see bambuddy.h)
  now = 4000;
  poll_once(p, s, fake_fetch, &b);
  TEST_ASSERT_EQUAL_INT(1, count_asked(b, "/maintenance/"));
  TEST_ASSERT_EQUAL_INT(1, count_asked(b, "/settings/"));

  b.fail.clear();
  now = 61000;
  poll_once(p, s, fake_fetch, &b);
  TEST_ASSERT_EQUAL_INT(2, count_asked(b, "/maintenance/"));
  TEST_ASSERT_TRUE(s.tasksKnown);
  TEST_ASSERT_FALSE(s.requirePlateClear);
}

// millis() starts again from 0 after 49.7 days; the slow requests must neither stop nor flood.
static void test_slow_requests_survive_the_clock_wrapping()
{
  FakeBambuddy b;
  Poller p;
  p.printerId = 1;
  Snapshot s;
  now = 0xFFFFFF00u;
  poll_once(p, s, fake_fetch, &b);
  TEST_ASSERT_EQUAL_INT(1, count_asked(b, "/maintenance/"));
  now = 0x00001000u; // wrapped, about 4 s later
  poll_once(p, s, fake_fetch, &b);
  TEST_ASSERT_EQUAL_INT(1, count_asked(b, "/maintenance/"));
  now = 0x00001000u + 60000;
  poll_once(p, s, fake_fetch, &b);
  TEST_ASSERT_EQUAL_INT(2, count_asked(b, "/maintenance/"));
  TEST_ASSERT_EQUAL_INT(2, count_asked(b, "/settings/"));
}

static void test_unreadable_status_is_offline_with_its_cause()
{
  FakeBambuddy b;
  b.statusCode = ERR_BAD_JSON;
  Poller p;
  p.printerId = 1;
  Snapshot s;
  poll_once(p, s, fake_fetch, &b);
  TEST_ASSERT_FALSE(s.reachable);
  TEST_ASSERT_EQUAL_INT(ERR_BAD_JSON, s.httpCode);
  TEST_ASSERT_EQUAL_UINT32(1, s.seq); // the screen still hears about it
  char out[160];
  describe_offline(s.httpCode, 1, out, sizeof(out));
  TEST_ASSERT_NOT_NULL(strstr(out, "cut off"));
}

static void test_no_outcome_question_while_a_print_is_under_way()
{
  for (const char *state : {"RUNNING", "PAUSE", "PREPARE", "SLICING"}) {
    FakeBambuddy b;
    b.status = std::string(R"({"connected": true, "state": ")") + state + R"("})";
    Poller p;
    p.printerId = 1;
    Snapshot s;
    s.verdictPending = true; // left over from the print before
    poll_once(p, s, fake_fetch, &b);
    TEST_ASSERT_FALSE_MESSAGE(s.verdictPending, state);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, count_asked(b, "last-per-printer"), state);
  }
}

// Only a refused answer for this very print stops the question.
static void test_question_stays_unless_this_print_was_refused()
{
  struct {
    Job job;
    int code;
    bool stillAsked;
  } cases[] = {
      {{Command::VerdictGood, 15, 26}, 401, false},  // the key is refused altogether
      {{Command::VerdictReject, 15, 26}, 403, false},
      {{Command::VerdictGood, 15, 26}, 200, true},   // answered: Bambuddy will say so itself
      {{Command::VerdictGood, 15, 26}, 500, true},   // worth another try
      {{Command::VerdictGood, 15, 99}, 403, true},   // another run of the same file
      {{Command::VerdictGood, 14, 26}, 403, true},   // another print
      {{Command::ClearPlate, 15, 26}, 403, true},    // not an answer at all
  };
  for (auto &c : cases) {
    FakeBambuddy b;
    Poller p;
    p.printerId = 1;
    Snapshot s;
    poller_after_command(p, c.job, c.code);
    poll_once(p, s, fake_fetch, &b);
    TEST_ASSERT_EQUAL_MESSAGE(c.stillAsked, s.verdictPending, "see the case's comment");
  }
}

static void test_poll_round_skips_outcome_while_printing()
{
  FakeBambuddy b;
  b.status = R"({"connected": true, "state": "RUNNING"})";
  Poller p;
  p.printerId = 1;
  Snapshot s;
  s.verdictPending = true;
  poll_once(p, s, fake_fetch, &b);
  for (auto &a : b.asked) TEST_ASSERT_EQUAL(std::string::npos, a.find("last-per-printer"));
  TEST_ASSERT_FALSE(s.verdictPending);
}

static void test_refused_answer_stops_the_question()
{
  FakeBambuddy b;
  Poller p;
  p.printerId = 1;
  Snapshot s;
  poll_once(p, s, fake_fetch, &b);
  TEST_ASSERT_TRUE(s.verdictPending);
  poller_after_command(p, {Command::VerdictGood, 15, 26}, 403);
  poll_once(p, s, fake_fetch, &b);
  TEST_ASSERT_FALSE(s.verdictPending); // the key may not answer: don't keep asking
}

static void test_task_done_reads_the_list_again()
{
  FakeBambuddy b;
  Poller p;
  p.printerId = 1;
  Snapshot s;
  poll_once(p, s, fake_fetch, &b);
  now = 5000;
  poller_after_command(p, {Command::PerformTask, 4, 0}, 200);
  b.asked.clear();
  poll_once(p, s, fake_fetch, &b);
  bool readTasks = false;
  for (auto &a : b.asked) readTasks |= a.find("/maintenance/") != std::string::npos;
  TEST_ASSERT_TRUE(readTasks);
}

// ---------------------------------------------------------------- what the screen shows

static Snapshot idle_printer()
{
  Snapshot s;
  s.reachable = true;
  s.connected = true;
  strcpy(s.state, "IDLE");
  return s;
}

static void test_view_for_each_state()
{
  ViewOptions o;
  Snapshot s;
  TEST_ASSERT_EQUAL(View::Offline, pick_view(s, o));
  s = idle_printer();
  TEST_ASSERT_EQUAL(View::Idle, pick_view(s, o));
  for (const char *st : {"RUNNING", "PREPARE", "SLICING"}) {
    strcpy(s.state, st);
    TEST_ASSERT_EQUAL(View::Printing, pick_view(s, o));
  }
  strcpy(s.state, "PAUSE");
  TEST_ASSERT_EQUAL(View::Paused, pick_view(s, o));
  s = idle_printer();
  s.connected = false;
  TEST_ASSERT_EQUAL(View::PrinterOffline, pick_view(s, o));
  s = idle_printer();
  s.errorCount = 1;
  TEST_ASSERT_EQUAL(View::Error, pick_view(s, o));
  s = idle_printer();
  s.held = 2;
  TEST_ASSERT_EQUAL(View::QueueHeld, pick_view(s, o));
}

// Bambuddy going quiet keeps the last status up; Bambuddy saying no does not.
static void test_silence_keeps_the_last_status_for_a_while()
{
  ViewOptions o;
  Snapshot s = idle_printer();
  s.everReachable = true;
  strcpy(s.state, "RUNNING");
  s.reachable = false;
  s.httpCode = -11;
  s.lostSec = 40;
  TEST_ASSERT_TRUE(link_lost(s));
  TEST_ASSERT_EQUAL(View::Printing, pick_view(s, o));
  char out[48];
  format_lost(out, sizeof(out), s);
  TEST_ASSERT_EQUAL_STRING("No answer from Bambuddy for 40 s", out);
  s.lostSec = 200;
  format_lost(out, sizeof(out), s);
  TEST_ASSERT_EQUAL_STRING("No answer from Bambuddy for 3 min", out);

  s.lostSec = LINK_LOST_GIVE_UP_S;
  TEST_ASSERT_FALSE(link_lost(s));
  TEST_ASSERT_EQUAL(View::Offline, pick_view(s, o));

  s.lostSec = 5;
  for (int code : {401, 403, 404, ERR_HTTPS, ERR_BAD_URL}) {
    s.httpCode = code;
    TEST_ASSERT_FALSE(link_lost(s));
    TEST_ASSERT_EQUAL(View::CheckSettings, pick_view(s, o));
  }
  s.httpCode = -1;
  s.everReachable = false; // never answered: there is no status to keep up
  TEST_ASSERT_EQUAL(View::Offline, pick_view(s, o));
}

static void test_poll_round_counts_the_silence()
{
  FakeBambuddy b;
  Poller p;
  p.printerId = 1;
  Snapshot s;
  now = 1000;
  poll_once(p, s, fake_fetch, &b);
  TEST_ASSERT_TRUE(s.everReachable);
  TEST_ASSERT_EQUAL_INT(0, s.lostSec);
  b.statusCode = -1;
  now = 9000;
  poll_once(p, s, fake_fetch, &b);
  now = 43000;
  poll_once(p, s, fake_fetch, &b);
  TEST_ASSERT_FALSE(s.reachable);
  TEST_ASSERT_EQUAL_INT(42, s.lostSec);
  TEST_ASSERT_EQUAL_STRING("FINISH", s.state); // the last status is still there to show
  b.statusCode = 200;
  poll_once(p, s, fake_fetch, &b);
  TEST_ASSERT_EQUAL_INT(0, s.lostSec);
  now = 0;
}

static void test_plate_gate_follows_bambuddy_setting()
{
  ViewOptions o;
  Snapshot s = idle_printer();
  strcpy(s.state, "FINISH");
  s.awaitingPlateClear = true;
  TEST_ASSERT_EQUAL(View::PlateClear, pick_view(s, o));
  s.requirePlateClear = false; // Bambuddy's default: the gate is raised but nobody needs it
  TEST_ASSERT_EQUAL(View::Idle, pick_view(s, o));
}

static void test_plate_gate_shown_with_printer_switched_off()
{
  ViewOptions o;
  Snapshot s = idle_printer();
  s.connected = false; // Auto Power Off
  s.awaitingPlateClear = true;
  TEST_ASSERT_EQUAL(View::PlateClear, pick_view(s, o));
  Action a[MAX_ACTIONS];
  int n = pick_actions(View::PlateClear, s, a);
  TEST_ASSERT_EQUAL_INT(1, n);
  TEST_ASSERT_EQUAL(Command::ClearPlate, a[0].cmd);
  TEST_ASSERT_TRUE(a[0].hold);
}

static void test_outcome_before_plate_and_skip_passes_it()
{
  Snapshot s = idle_printer();
  strcpy(s.state, "FINISH");
  s.awaitingPlateClear = true;
  s.verdictPending = true;
  s.archiveId = 15;
  s.runId = 26;
  ViewOptions o;
  TEST_ASSERT_EQUAL(View::Verdict, pick_view(s, o));
  o.skippedArchive = 15;
  o.skippedRun = 26;
  TEST_ASSERT_EQUAL(View::PlateClear, pick_view(s, o));
  s.runId = 27; // the next print asks again
  TEST_ASSERT_EQUAL(View::Verdict, pick_view(s, o));
}

static void test_start_only_for_jobs_assigned_here()
{
  ViewOptions o;
  Snapshot s = idle_printer();
  s.nextId = 42;
  s.nextManual = true;
  s.nextAssigned = false; // "any X1C": starting it could send it to another printer
  TEST_ASSERT_EQUAL(View::Idle, pick_view(s, o));
  s.nextAssigned = true;
  TEST_ASSERT_EQUAL(View::Ready, pick_view(s, o));
  Action a[MAX_ACTIONS];
  int n = pick_actions(View::Ready, s, a);
  TEST_ASSERT_EQUAL_INT(1, n);
  TEST_ASSERT_EQUAL(Command::StartNext, a[0].cmd);
  TEST_ASSERT_EQUAL_INT(42, a[0].arg); // the item shown, not whatever is first when it runs
  TEST_ASSERT_TRUE(a[0].hold);
}

static void test_resume_queue_only_when_held()
{
  Snapshot s = idle_printer();
  s.errorCount = 1;
  Action a[MAX_ACTIONS];
  int n = pick_actions(View::Error, s, a);
  TEST_ASSERT_EQUAL_INT(1, n);
  TEST_ASSERT_EQUAL(Command::ClearErrors, a[0].cmd);
  s.held = 2;
  n = pick_actions(View::Error, s, a);
  TEST_ASSERT_EQUAL_INT(2, n);
  TEST_ASSERT_EQUAL(Command::ResumeQueue, a[1].cmd);
  TEST_ASSERT_EQUAL(Kind::Quiet, a[1].kind); // beside another button it is the quiet one
  s.errorCount = 0;
  n = pick_actions(View::QueueHeld, s, a);
  TEST_ASSERT_EQUAL_INT(1, n);
  TEST_ASSERT_EQUAL(Command::ResumeQueue, a[0].cmd);
  TEST_ASSERT_EQUAL_INT(0, pick_actions(View::Idle, s, a)); // nothing to do: no button
}

// Anything that starts a print or can't be undone takes a hold; the rest is a tap.
static void test_costly_actions_are_held()
{
  Snapshot s = idle_printer();
  s.nextId = 1;
  s.held = 1;
  Action a[MAX_ACTIONS];
  for (View v : {View::Printing, View::Paused, View::PlateClear, View::Error, View::Ready, View::QueueHeld, View::Idle}) {
    int n = pick_actions(v, s, a);
    for (int i = 0; i < n; i++) {
      bool costly = a[i].cmd == Command::Stop || a[i].cmd == Command::ClearPlate || a[i].cmd == Command::StartNext ||
                    a[i].cmd == Command::ResumeQueue;
      TEST_ASSERT_EQUAL(costly, a[i].hold);
      TEST_ASSERT_NOT_NULL(a[i].pending); // every command says what is under way
    }
  }
}

// The plate button starts the next print when one is waiting to start by itself, and says so.
static void test_plate_button_says_when_it_starts_a_print()
{
  Snapshot s = idle_printer();
  s.awaitingPlateClear = true;
  Action a[MAX_ACTIONS];
  pick_actions(View::PlateClear, s, a);
  TEST_ASSERT_NULL(strstr(a[0].text, "start"));
  s.nextId = 42;
  pick_actions(View::PlateClear, s, a);
  TEST_ASSERT_NOT_NULL(strstr(a[0].text, "start the next print"));
  s.nextManual = true; // it waits for its own start
  pick_actions(View::PlateClear, s, a);
  TEST_ASSERT_NULL(strstr(a[0].text, "start"));
  s.nextManual = false;
  strcpy(s.nextWhy, "Waiting for filament: PETG"); // Bambuddy is holding it back
  pick_actions(View::PlateClear, s, a);
  TEST_ASSERT_NULL(strstr(a[0].text, "start"));
}

static void test_words_of_each_view()
{
  Face f;
  Snapshot s = idle_printer();
  strcpy(s.state, "PREPARE");
  strcpy(s.stage, "Auto bed leveling");
  s.remainingMin = 125;
  pick_face(View::Printing, s, f);
  TEST_ASSERT_EQUAL_STRING("Preparing", f.chip);
  TEST_ASSERT_EQUAL_STRING("Auto bed leveling", f.headline); // the stage, not "0%"
  TEST_ASSERT_EQUAL_STRING("About 2h 05m once it starts", f.sub);
  s.stage[0] = 0;
  pick_face(View::Printing, s, f);
  TEST_ASSERT_EQUAL_STRING("Getting ready", f.headline);

  strcpy(s.state, "PAUSE");
  s.progress = 42.9f;
  pick_face(View::Paused, s, f);
  TEST_ASSERT_EQUAL_STRING("Paused at 42%", f.headline);

  s = idle_printer();
  strcpy(s.state, "FAILED");
  s.progress = 37;
  pick_face(View::PlateClear, s, f);
  TEST_ASSERT_EQUAL_STRING("Failed", f.chip); // the chip is the state; the button is the instruction
  TEST_ASSERT_EQUAL(Tone::Danger, f.chipTone);
  TEST_ASSERT_EQUAL_STRING("Failed at 37%", f.headline);
  TEST_ASSERT_EQUAL_STRING("Nothing else queued", f.sub);
  s.nextId = 4;
  pick_face(View::PlateClear, s, f);
  TEST_ASSERT_EQUAL_STRING("Starts when the plate is clear:", f.sub);

  s = idle_printer();
  s.nextId = 4;
  s.nextManual = s.nextAssigned = true;
  strcpy(s.nextName, "Dragon_plate_1");
  s.nextSeconds = 3 * 3600 + 12 * 60;
  strcpy(s.nextBy, "anna");
  pick_face(View::Ready, s, f);
  TEST_ASSERT_EQUAL_STRING("Dragon_plate_1", f.headline); // what is about to start, not "Ready to start"
  TEST_ASSERT_TRUE(f.headlineIsName);
  TEST_ASSERT_EQUAL_STRING("3h 12m, queued by anna", f.sub);

  s = idle_printer();
  strcpy(s.state, "FAILED");
  strcpy(s.job, "Gears");
  s.held = 1;
  pick_face(View::QueueHeld, s, f);
  TEST_ASSERT_EQUAL_STRING("Queue held", f.chip);
  TEST_ASSERT_EQUAL_STRING("1 job held", f.headline);
  TEST_ASSERT_EQUAL_STRING("after Gears failed", f.sub);
}

// An idle printer with a job waiting says what the job is waiting for.
static void test_idle_says_what_the_next_job_waits_for()
{
  Face f;
  Snapshot s = idle_printer();
  pick_face(View::Idle, s, f);
  TEST_ASSERT_EQUAL_STRING("Nothing queued", f.headline);
  s.nextId = 4;
  s.nextAssigned = true;
  pick_face(View::Idle, s, f);
  TEST_ASSERT_EQUAL_STRING("Starting soon", f.headline); // accepted, Bambuddy hasn't sent it yet
  strcpy(s.nextWhy, "Waiting for filament: PETG black");
  pick_face(View::Idle, s, f);
  TEST_ASSERT_EQUAL_STRING("Waiting", f.headline);
  TEST_ASSERT_EQUAL_STRING("Waiting for filament: PETG black", f.sub);
  TEST_ASSERT_EQUAL(Tone::Caution, f.subTone);
  s.nextWhy[0] = 0;
  s.nextAssigned = false;
  pick_face(View::Idle, s, f);
  TEST_ASSERT_NOT_NULL(strstr(f.sub, "Bambuddy picks"));
}

// ---------------------------------------------------------------- text on the screen

static void test_progress_never_reads_done_early()
{
  Snapshot s;
  s.progress = 99.6f;
  TEST_ASSERT_EQUAL_INT(99, progress_percent(s));
  s.remainingMin = 83;
  char out[40];
  format_left(out, sizeof(out), s);
  TEST_ASSERT_EQUAL_STRING("1h 23m left", out);
  s.remainingMin = 26 * 60 + 5;
  format_left(out, sizeof(out), s);
  TEST_ASSERT_EQUAL_STRING("26h 05m left", out); // always "left": a bare time could be time spent
}

static void test_next_line_puts_the_count_before_the_name()
{
  char out[96];
  Snapshot s;
  format_next(out, sizeof(out), s);
  TEST_ASSERT_EQUAL_STRING("", out);
  s.nextId = 4;
  s.queued = 1;
  s.nextSeconds = 47 * 60;
  strcpy(s.nextName, "Clips");
  format_next(out, sizeof(out), s);
  TEST_ASSERT_EQUAL_STRING("Next (47m): Clips", out);
  s.queued = 3;
  format_next(out, sizeof(out), s);
  TEST_ASSERT_EQUAL_STRING("Next of 3 (47m): Clips", out);
}

static void test_durations()
{
  char out[16];
  format_duration(out, sizeof(out), 45);
  TEST_ASSERT_EQUAL_STRING("45m", out);
  format_duration(out, sizeof(out), 65);
  TEST_ASSERT_EQUAL_STRING("1h 05m", out);
  format_duration(out, sizeof(out), -3);
  TEST_ASSERT_EQUAL_STRING("0m", out);
}

static void test_due_text()
{
  char out[40];
  Task t;
  t.level = 2;
  t.left = -3.2f;
  format_due(out, sizeof(out), t);
  TEST_ASSERT_EQUAL_STRING("Overdue by 3 print hours", out); // print hours, not hours on the clock
  t.left = -0.4f;
  format_due(out, sizeof(out), t);
  TEST_ASSERT_EQUAL_STRING("Due now", out);
  t.level = 1;
  t.left = 0.4f; // never "Due in 0"
  format_due(out, sizeof(out), t);
  TEST_ASSERT_EQUAL_STRING("Due now", out);
  t.left = 1.2f;
  format_due(out, sizeof(out), t);
  TEST_ASSERT_EQUAL_STRING("Due in 1 print hour", out);
  t.level = 0;
  t.left = 0.4f;
  format_due(out, sizeof(out), t);
  TEST_ASSERT_EQUAL_STRING("Due within the hour", out);
  t.days = true;
  t.left = 1;
  format_due(out, sizeof(out), t);
  TEST_ASSERT_EQUAL_STRING("Due in 1 day", out);
  t.left = 21;
  format_due(out, sizeof(out), t);
  TEST_ASSERT_EQUAL_STRING("Due in 21 days", out);
  t.left = -412;
  format_due(out, sizeof(out), t);
  TEST_ASSERT_EQUAL_STRING("Overdue by 412 days", out);
  t.timed = false; // Bambuddy sent no time: never an empty line
  format_due(out, sizeof(out), t);
  TEST_ASSERT_EQUAL_STRING("Not due", out);
}

static void test_temperatures()
{
  char out[80];
  Snapshot s = idle_printer();
  s.nozzle = 219.6f;
  s.bed = 60;
  s.chamber = 38;
  format_temps(out, sizeof(out), s);
  TEST_ASSERT_EQUAL_STRING("Nozzle 220\xC2\xB0   Bed 60\xC2\xB0   Chamber 38\xC2\xB0", out);
  s.chamber = 0; // no chamber sensor
  format_temps(out, sizeof(out), s);
  TEST_ASSERT_EQUAL_STRING("Nozzle 220\xC2\xB0   Bed 60\xC2\xB0", out);
  s.connected = false; // nothing to report from a printer that is off
  format_temps(out, sizeof(out), s);
  TEST_ASSERT_EQUAL_STRING("", out);
}

// ---------------------------------------------------------------- filament

static void test_status_lists_the_slots()
{
  const char *json = R"({
    "name": "X1C", "connected": true, "state": "IDLE",
    "ams": [{"id": 0, "tray": [{"id": 0, "tray_type": "PETG", "tray_color": "1A1A1AFF"},
                               {"id": 1, "tray_type": "PLA", "tray_color": "FFFFFFFF"},
                               {"id": 2, "tray_type": null, "tray_color": null},
                               {"id": 3, "tray_type": "", "tray_color": "00000000"}]},
            {"id": 128, "tray": [{"id": 0, "tray_type": "PA-CF", "tray_color": "#202020"}]}],
    "vt_tray": [{"id": 254, "tray_type": "TPU", "tray_color": "FF0000FF"}, {"id": 255}, {"id": 250, "tray_type": "PLA"}]
  })";
  Snapshot s;
  read(json, status_filter, [&](JsonDocument &d) { parse_status(d, s); });
  TEST_ASSERT_EQUAL_INT(7, s.slotCount); // rack inputs (250) are not slots the panel offers
  TEST_ASSERT_EQUAL_STRING("PETG", s.slots[0].type);
  TEST_ASSERT_EQUAL_HEX32(0x1A1A1A, s.slots[0].rgb);
  TEST_ASSERT_EQUAL_STRING("", s.slots[2].type); // nothing in it
  TEST_ASSERT_EQUAL_INT(128, s.slots[4].ams);
  TEST_ASSERT_EQUAL_HEX32(0x202020, s.slots[4].rgb);
  TEST_ASSERT_EQUAL_INT(EXTERNAL_AMS, s.slots[5].ams); // tray 254 is Bambuddy's slot 0 of unit 255
  TEST_ASSERT_EQUAL_INT(0, s.slots[5].tray);
  TEST_ASSERT_EQUAL_INT(1, s.slots[6].tray);

  char out[12];
  format_slot(out, sizeof(out), s.slots[1]);
  TEST_ASSERT_EQUAL_STRING("A2", out);
  format_slot(out, sizeof(out), s.slots[4]);
  TEST_ASSERT_EQUAL_STRING("HT-A", out);
  format_slot(out, sizeof(out), s.slots[5]);
  TEST_ASSERT_EQUAL_STRING("Ext", out);
  format_slot(out, sizeof(out), s.slots[6]);
  TEST_ASSERT_EQUAL_STRING("Ext 2", out);

  // A printer with no AMS answer keeps no slots from before.
  read(R"({"name": "A1", "connected": true, "state": "IDLE"})", status_filter, [&](JsonDocument &d) { parse_status(d, s); });
  TEST_ASSERT_EQUAL_INT(0, s.slotCount);
}

static void test_assignments_for_this_printer()
{
  const char *json = R"([
    {"id": 1, "spool_id": 40, "printer_id": 1, "ams_id": 0, "tray_id": 1,
     "spool": {"id": 40, "material": "PLA", "brand": "Bambu Lab", "color_name": "Jade White", "label_weight": 1000, "weight_used": 360.4}},
    {"id": 2, "spool_id": 41, "printer_id": 2, "ams_id": 0, "tray_id": 0, "spool": {"brand": "Other", "color_name": "Printer"}},
    {"id": 3, "spool_id": 42, "printer_id": 1, "ams_id": 255, "tray_id": 0, "spool": {"brand": null, "subtype": "CF", "color_name": null}}
  ])";
  Snapshot s;
  read(json, assignments_filter, [&](JsonDocument &d) { parse_assignments(d, 1, s); });
  TEST_ASSERT_TRUE(s.assignedKnown);
  TEST_ASSERT_EQUAL_INT(2, s.assignedCount);
  TEST_ASSERT_EQUAL_INT(40, s.assigned[0].spoolId);
  TEST_ASSERT_EQUAL_STRING("Bambu Lab Jade White", s.assigned[0].name);
  TEST_ASSERT_EQUAL_INT(640, s.assigned[0].remainG);
  TEST_ASSERT_EQUAL_INT(EXTERNAL_AMS, s.assigned[1].ams);
  TEST_ASSERT_EQUAL_STRING("CF", s.assigned[1].name); // the subtype stands in for a missing colour
  TEST_ASSERT_EQUAL_INT(-1, s.assigned[1].remainG);   // Bambuddy didn't say

  Slot slot;
  slot.ams = 0;
  slot.tray = 1;
  TEST_ASSERT_NOT_NULL(assignment_of(s, slot));
  slot.tray = 2;
  TEST_ASSERT_NULL(assignment_of(s, slot));

  // Not a list (an error body): what was known stays.
  read(R"({"detail": "nope"})", assignments_filter, [&](JsonDocument &d) { parse_assignments(d, 1, s); });
  TEST_ASSERT_EQUAL_INT(2, s.assignedCount);
}

static void test_spool_list_and_pages()
{
  SpoolStore st;
  JsonDocument f, doc;
  spools_filter(f);
  TEST_ASSERT_FALSE(deserializeJson(doc, SPOOLS, DeserializationOption::Filter(f)));
  TEST_ASSERT_TRUE(parse_spools(doc, st));
  TEST_ASSERT_EQUAL(SpoolState::Ready, st.state);
  TEST_ASSERT_EQUAL_INT(4, st.count); // the one without an id is left out
  TEST_ASSERT_EQUAL_STRING("Bambu Lab Jade White", st.items[0].name);
  TEST_ASSERT_EQUAL_INT(900, st.items[0].remainG);
  TEST_ASSERT_EQUAL_INT(0, st.items[1].remainG);  // used more than it held: empty, not negative
  TEST_ASSERT_EQUAL_STRING("eSun Silk", st.items[2].name);
  TEST_ASSERT_EQUAL_HEX32(0, st.items[2].rgb);    // unreadable colour
  TEST_ASSERT_EQUAL_INT(-1, st.items[2].remainG);
  TEST_ASSERT_EQUAL_STRING("Grun", st.items[3].name);
  TEST_ASSERT_EQUAL_HEX32(0x00FF00, st.items[3].rgb);

  char materials[4][SPOOL_MATERIAL];
  TEST_ASSERT_EQUAL_INT(2, spools_materials(st, materials, 4)); // "PLA" and "pla" are one
  TEST_ASSERT_EQUAL_STRING("PLA", materials[0]);
  TEST_ASSERT_EQUAL_STRING("PETG", materials[1]);

  Spool page[4];
  int total;
  TEST_ASSERT_EQUAL_INT(2, spools_page(st, "PLA", 0, page, 4, total));
  TEST_ASSERT_EQUAL_INT(2, total);
  TEST_ASSERT_EQUAL_INT(3, page[1].id);
  TEST_ASSERT_EQUAL_INT(1, spools_page(st, "", 3, page, 4, total)); // all, from the fourth
  TEST_ASSERT_EQUAL_INT(4, total);
  TEST_ASSERT_EQUAL_INT(4, page[0].id);
  TEST_ASSERT_EQUAL_INT(0, spools_page(st, "ABS", 0, page, 4, total));

  SpoolStore copy;
  TEST_ASSERT_TRUE(spools_copy(st, copy));
  TEST_ASSERT_EQUAL_INT(4, copy.count);
  TEST_ASSERT_EQUAL_STRING("Grun", copy.items[3].name);
  free(st.items);
  free(copy.items);
}

static void test_spool_requests()
{
  expect_request({Command::AssignSpool, 40, slot_code(0, 1)}, "POST", "/api/v1/inventory/assignments",
                 R"({"spool_id":40,"printer_id":3,"ams_id":0,"tray_id":1,"move":true})");
  expect_request({Command::AssignSpool, 123456, slot_code(255, 1)}, "POST", "/api/v1/inventory/assignments",
                 R"({"spool_id":123456,"printer_id":3,"ams_id":255,"tray_id":1,"move":true})");
  expect_request({Command::UnassignSpool, 0, slot_code(128, 0)}, "DELETE", "/api/v1/inventory/assignments/3/128/0", "");
  Request r;
  TEST_ASSERT_FALSE(command_request({Command::AssignSpool, 0, slot_code(0, 1)}, 3, r));
  TEST_ASSERT_FALSE(command_request({Command::LoadSpools, 0, 0}, 3, r)); // never sent to Bambuddy
  char out[40];
  command_result({Command::AssignSpool, 40, 1}, 403, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("API key lacks Manage Inventory", out);
  TEST_ASSERT_TRUE(command_result({Command::UnassignSpool, 0, 1}, 200, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("Spool removed", out);
}

// The spool list is read when asked for, once; assignments once a minute and after a change,
// and only for a printer with slots.
static void test_poll_round_reads_filament_when_it_should()
{
  FakeBambuddy b;
  b.status = R"({"name": "X1C", "connected": true, "state": "IDLE",
                 "ams": [{"id": 0, "tray": [{"id": 0, "tray_type": "PLA", "tray_color": "FFFFFFFF"}]}]})";
  Poller p;
  p.printerId = 1;
  SpoolStore st;
  p.spools = &st;
  Snapshot s;
  now = 1000;
  poll_once(p, s, fake_fetch, &b);
  TEST_ASSERT_EQUAL_INT(1, count_asked(b, "/inventory/assignments?printer_id=1"));
  TEST_ASSERT_EQUAL_INT(0, count_asked(b, "/inventory/spools"));
  b.asked.clear();
  now = 5000;
  poll_once(p, s, fake_fetch, &b);
  TEST_ASSERT_EQUAL_INT(0, count_asked(b, "/inventory/"));

  poller_after_command(p, {Command::LoadSpools, 0, 0}, 0);
  TEST_ASSERT_EQUAL(SpoolState::Loading, st.state);
  poll_once(p, s, fake_fetch, &b);
  TEST_ASSERT_EQUAL_INT(1, count_asked(b, "/inventory/spools"));
  TEST_ASSERT_EQUAL(SpoolState::Ready, st.state);
  TEST_ASSERT_EQUAL_INT(4, st.count);
  b.asked.clear();
  poll_once(p, s, fake_fetch, &b);
  TEST_ASSERT_EQUAL_INT(0, count_asked(b, "/inventory/spools")); // once per asking

  poller_after_command(p, {Command::AssignSpool, 1, 0}, 200);
  poll_once(p, s, fake_fetch, &b);
  TEST_ASSERT_EQUAL_INT(1, count_asked(b, "/inventory/assignments")); // read again after the change

  b.fail["/inventory/spools"] = 403;
  poller_after_command(p, {Command::LoadSpools, 0, 0}, 0);
  poll_once(p, s, fake_fetch, &b);
  TEST_ASSERT_EQUAL(SpoolState::Failed, st.state);
  TEST_ASSERT_EQUAL_INT(403, st.code);
  free(st.items);
  now = 0;
}

// ---------------------------------------------------------------- the settings web page

struct TextPage : PageText {
  std::string s;
  using PageText::operator+=;
  PageText &operator+=(const char *text) override
  {
    s += text;
    return *this;
  }
};

static bool has(const TextPage &p, const char *part) { return p.s.find(part) != std::string::npos; }

// What Bambuddy or the person typed goes into the page as text, never as markup.
static void test_settings_page_escapes_what_it_is_given()
{
  static const PageChoice printers[] = {{7, "A1 <mini> & \"co\""}};
  PageData d;
  d.typedCode = "'><script>";
  d.url = "http://x/\"onfocus=\"y";
  d.choices = printers;
  d.choiceCount = 1;
  d.printerId = 7;
  d.printerName = printers[0].name;
  TextPage p;
  setup_page(p, d);
  TEST_ASSERT_FALSE(has(p, "'><script>"));
  TEST_ASSERT_TRUE(has(p, "&#39;&gt;&lt;script&gt;"));
  TEST_ASSERT_FALSE(has(p, "\"onfocus=\"y"));
  TEST_ASSERT_FALSE(has(p, "<mini>"));
  TEST_ASSERT_TRUE(has(p, "A1 &lt;mini&gt; &amp; &quot;co&quot; (id 7)"));
  TEST_ASSERT_TRUE(has(p, "<title>Bambuddy Panel: A1 &lt;mini&gt;")); // which panel this page belongs to
}

static void test_settings_page_states()
{
  PageData d;
  TextPage first;
  setup_page(first, d);
  TEST_ASSERT_TRUE(has(first, "action='/save'"));
  TEST_ASSERT_TRUE(has(first, "from Bambuddy's settings")); // no key yet
  TEST_ASSERT_FALSE(has(first, "action='/finish'"));       // only offered when Bambuddy didn't accept

  d.keySaved = true;
  d.finishAnyway = true;
  TextPage refused;
  setup_page(refused, d);
  TEST_ASSERT_TRUE(has(refused, "saved; leave blank to keep it")); // the key itself is never in the page
  TEST_ASSERT_TRUE(has(refused, "action='/finish'"));

  d.msg = "Saved.";
  d.kind = "ok";
  d.done = true;
  TextPage done;
  setup_page(done, d);
  TEST_ASSERT_TRUE(has(done, "Saved."));
  TEST_ASSERT_FALSE(has(done, "<form")); // nothing left to fill in
}

// ---------------------------------------------------------------- touch calibration

static const int INSET = 20;
static int screenW = 320, screenH = 240; // the maths takes the screen's size; the tests try several

// A pretend touch panel: what it reports for a screen point, with optional mirrored or swapped axes.
static void raw_of(int sx, int sy, bool flipX, bool flipY, bool swap, long &rx, long &ry)
{
  long x = (flipX ? 3700 : 200) + (long)sx * (flipX ? -3500 : 3500) / (screenW - 1);
  long y = (flipY ? 3800 : 240) + (long)sy * (flipY ? -3560 : 3560) / (screenH - 1);
  rx = swap ? y : x;
  ry = swap ? x : y;
}

static void four_taps(bool flipX, bool flipY, bool swap, long rx[4], long ry[4])
{
  const int target[4][2] = {{INSET, INSET},
                            {screenW - 1 - INSET, INSET},
                            {screenW - 1 - INSET, screenH - 1 - INSET},
                            {INSET, screenH - 1 - INSET}};
  for (int i = 0; i < 4; i++) raw_of(target[i][0], target[i][1], flipX, flipY, swap, rx[i], ry[i]);
}

static void every_orientation()
{
  for (int mode = 0; mode < 8; mode++) {
    bool flipX = mode & 1, flipY = mode & 2, swap = mode & 4;
    long rx[4], ry[4];
    four_taps(flipX, flipY, swap, rx, ry);
    TouchCal cal;
    TEST_ASSERT_TRUE(touch_cal_solve(rx, ry, INSET, screenW, screenH, cal));
    TEST_ASSERT_EQUAL(swap, cal.swap);

    // Every point on the screen must land within 2 px of where the finger is.
    for (int sx = 0; sx < screenW; sx += 29)
      for (int sy = 0; sy < screenH; sy += 23) {
        long x, y;
        raw_of(sx, sy, flipX, flipY, swap, x, y);
        int px, py;
        touch_cal_apply(cal, screenW, screenH, (int)x, (int)y, px, py);
        TEST_ASSERT_INT_WITHIN(2, sx, px);
        TEST_ASSERT_INT_WITHIN(2, sy, py);
      }
  }
}

static void test_calibration_every_orientation()
{
  static const int sizes[][2] = {{320, 240}, {480, 320}, {800, 480}, {240, 320}};
  for (auto &size : sizes) {
    screenW = size[0];
    screenH = size[1];
    every_orientation();
  }
  screenW = 320;
  screenH = 240;
}

static void test_calibration_refuses_nonsense()
{
  TouchCal cal;
  long sameX[4] = {2000, 2010, 1990, 2005}, sameY[4] = {2000, 1995, 2005, 2000};
  TEST_ASSERT_FALSE(touch_cal_solve(sameX, sameY, INSET, screenW, screenH, cal)); // one spot tapped four times

  long rx[4], ry[4];
  four_taps(false, false, false, rx, ry);
  rx[2] -= 1200;
  TEST_ASSERT_FALSE(touch_cal_solve(rx, ry, INSET, screenW, screenH, cal)); // one corner far off
}

static void test_touch_is_clamped_to_the_screen()
{
  TouchCal cal = {200, 3700, 240, 3800, false};
  int x, y;
  touch_cal_apply(cal, screenW, screenH, 0, 4095, x, y);
  TEST_ASSERT_EQUAL_INT(0, x);
  TEST_ASSERT_EQUAL_INT(screenH - 1, y);
}

// Capacitive touch: a controller that reports the panel's own portrait grid (240 wide, 320
// tall), on a screen shown in landscape.
static void test_touch_grid_is_turned_to_the_screen()
{
  int x, y;
  // Turned a quarter one way: the portrait grid's top-right corner is the screen's top-left.
  x = 239, y = 0;
  touch_orient(320, 240, true, false, true, x, y);
  TEST_ASSERT_EQUAL_INT(0, x);
  TEST_ASSERT_EQUAL_INT(0, y);
  x = 0, y = 319; // portrait bottom-left = screen bottom-right
  touch_orient(320, 240, true, false, true, x, y);
  TEST_ASSERT_EQUAL_INT(319, x);
  TEST_ASSERT_EQUAL_INT(239, y);
  // Turned the other way: the portrait grid's bottom-left corner is the screen's top-left.
  x = 0, y = 319;
  touch_orient(320, 240, true, true, false, x, y);
  TEST_ASSERT_EQUAL_INT(0, x);
  TEST_ASSERT_EQUAL_INT(0, y);
  // Not turned at all: untouched, but never off the screen.
  x = 100, y = 50;
  touch_orient(320, 240, false, false, false, x, y);
  TEST_ASSERT_EQUAL_INT(100, x);
  TEST_ASSERT_EQUAL_INT(50, y);
  x = 400, y = -3;
  touch_orient(320, 240, false, false, false, x, y);
  TEST_ASSERT_EQUAL_INT(319, x);
  TEST_ASSERT_EQUAL_INT(0, y);
}

int main()
{
  UNITY_BEGIN();
  RUN_TEST(test_status_finished);
  RUN_TEST(test_status_error_and_fallbacks);
  RUN_TEST(test_notices_and_unknown_codes_are_not_errors);
  RUN_TEST(test_long_file_names_lose_the_extension_first);
  RUN_TEST(test_long_names_are_cut_not_overrun);
  RUN_TEST(test_status_odd_numbers_are_tamed);
  RUN_TEST(test_text_becomes_ascii);
  RUN_TEST(test_text_is_cut_on_whole_characters);
  RUN_TEST(test_queue_picks_lowest_position);
  RUN_TEST(test_queue_lists_what_comes_after_and_why_it_waits);
  RUN_TEST(test_queue_says_filament_is_short_when_bambuddy_gives_no_words);
  RUN_TEST(test_queue_empty_clears_previous);
  RUN_TEST(test_queue_counts_held_and_any_model_items);
  RUN_TEST(test_verdict_pending_only_when_asked_and_unanswered);
  RUN_TEST(test_verdict_not_asked_for_failed_or_running_prints);
  RUN_TEST(test_plate_clear_setting);
  RUN_TEST(test_tasks_sorted_by_urgency);
  RUN_TEST(test_tasks_without_a_time_go_last);
  RUN_TEST(test_tasks_close_together_keep_their_order);
  RUN_TEST(test_tasks_keep_the_most_urgent_when_too_many);
  RUN_TEST(test_requests_match_bambuddy_api);
  RUN_TEST(test_requests_need_an_id);
  RUN_TEST(test_command_results_are_plain);
  RUN_TEST(test_refused_command_names_the_permission);
  RUN_TEST(test_offline_text_names_the_cause);
  RUN_TEST(test_urls_are_cleaned_up);
  RUN_TEST(test_poll_round_asks_in_order);
  RUN_TEST(test_poll_round_stops_when_bambuddy_is_away);
  RUN_TEST(test_one_missed_answer_is_not_offline);
  RUN_TEST(test_failed_side_requests_keep_the_last_known);
  RUN_TEST(test_failed_slow_requests_wait_their_minute);
  RUN_TEST(test_slow_requests_survive_the_clock_wrapping);
  RUN_TEST(test_unreadable_status_is_offline_with_its_cause);
  RUN_TEST(test_no_outcome_question_while_a_print_is_under_way);
  RUN_TEST(test_question_stays_unless_this_print_was_refused);
  RUN_TEST(test_poll_round_skips_outcome_while_printing);
  RUN_TEST(test_refused_answer_stops_the_question);
  RUN_TEST(test_task_done_reads_the_list_again);
  RUN_TEST(test_view_for_each_state);
  RUN_TEST(test_silence_keeps_the_last_status_for_a_while);
  RUN_TEST(test_poll_round_counts_the_silence);
  RUN_TEST(test_plate_gate_follows_bambuddy_setting);
  RUN_TEST(test_plate_gate_shown_with_printer_switched_off);
  RUN_TEST(test_outcome_before_plate_and_skip_passes_it);
  RUN_TEST(test_start_only_for_jobs_assigned_here);
  RUN_TEST(test_resume_queue_only_when_held);
  RUN_TEST(test_costly_actions_are_held);
  RUN_TEST(test_plate_button_says_when_it_starts_a_print);
  RUN_TEST(test_words_of_each_view);
  RUN_TEST(test_idle_says_what_the_next_job_waits_for);
  RUN_TEST(test_progress_never_reads_done_early);
  RUN_TEST(test_next_line_puts_the_count_before_the_name);
  RUN_TEST(test_durations);
  RUN_TEST(test_due_text);
  RUN_TEST(test_temperatures);
  RUN_TEST(test_status_lists_the_slots);
  RUN_TEST(test_assignments_for_this_printer);
  RUN_TEST(test_spool_list_and_pages);
  RUN_TEST(test_spool_requests);
  RUN_TEST(test_poll_round_reads_filament_when_it_should);
  RUN_TEST(test_settings_page_escapes_what_it_is_given);
  RUN_TEST(test_settings_page_states);
  RUN_TEST(test_calibration_every_orientation);
  RUN_TEST(test_calibration_refuses_nonsense);
  RUN_TEST(test_touch_is_clamped_to_the_screen);
  RUN_TEST(test_touch_grid_is_turned_to_the_screen);
  return UNITY_END();
}
