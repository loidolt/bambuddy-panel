// Tests for the parts with no hardware in them. Run with: pio test -e test
//
// The JSON samples follow what Bambuddy 1.3 returns, trimmed, plus fields the panel must ignore.
#include <unity.h>

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "bambuddy_api.h"
#include "bambuddy_parse.h"
#include "board.h"
#include "poller.h"
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

static void test_queue_empty_clears_previous()
{
  Snapshot s;
  s.nextId = 5;
  s.queued = 2;
  s.held = 3;
  strcpy(s.nextName, "old");
  read("[]", queue_filter, [&](JsonDocument &d) { parse_queue(d, 1, s); });
  TEST_ASSERT_EQUAL_INT(0, s.queued);
  TEST_ASSERT_EQUAL_INT(0, s.held);
  TEST_ASSERT_EQUAL_INT(0, s.nextId);
  TEST_ASSERT_EQUAL_STRING("", s.nextName);
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
  expect_request({Command::SetLight, 1, 0}, "POST", "/api/v1/printers/3/chamber-light?on=true", "");
  expect_request({Command::SetLight, 0, 0}, "POST", "/api/v1/printers/3/chamber-light?on=false", "");
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
  command_result({Command::StartNext, 42, 0}, 200, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("Starting", out);
  command_result({Command::StartNext, 42, 0}, 409, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("Filament short; see Bambuddy", out);
  command_result({Command::VerdictGood, 1, 0}, 403, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("Not allowed (API key)", out);
  command_result({Command::Pause, 0, 0}, ERR_NO_WIFI, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("WiFi is down", out);
  command_result({Command::Pause, 0, 0}, -11, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("No answer from Bambuddy", out);
  command_result({Command::SetLight, 0, 0}, 204, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("Light off", out);
}

static void test_offline_text_names_the_cause()
{
  char out[160];
  describe_offline(404, 7, out, sizeof(out));
  TEST_ASSERT_NOT_NULL(strstr(out, "no printer with id 7"));
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

struct FakeBambuddy {
  std::vector<std::string> asked;
  std::string status = STATUS_FINISHED;
  int statusCode = 200;
  std::string lastPrints = LAST_PRINTS;
};

static int fake_fetch(const char *path, JsonDocument &doc, const JsonDocument &filter, void *ctx)
{
  FakeBambuddy &b = *(FakeBambuddy *)ctx;
  b.asked.push_back(path);
  std::string p = path;
  const char *json = "{}";
  if (p.find("/status") != std::string::npos) {
    if (b.statusCode != 200) return b.statusCode;
    json = b.status.c_str();
  } else if (p.find("/queue/") == 0 || p.find("/api/v1/queue/") == 0) {
    json = "[]";
  } else if (p.find("last-per-printer") != std::string::npos) {
    json = b.lastPrints.c_str();
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
  const char *expected[] = {"/api/v1/printers/1/status", "/api/v1/queue/?status=pending,skipped&printer_id=1",
                            "/api/v1/archives/last-per-printer", "/api/v1/maintenance/printers/1",
                            "/api/v1/settings/"};
  TEST_ASSERT_EQUAL_size_t(5, b.asked.size());
  for (int i = 0; i < 5; i++) TEST_ASSERT_EQUAL_STRING(expected[i], b.asked[i].c_str());
  TEST_ASSERT_EQUAL_UINT32(1, s.seq);
  TEST_ASSERT_EQUAL_INT(1, s.printerId);
  TEST_ASSERT_FALSE(s.requirePlateClear);
  TEST_ASSERT_TRUE(s.verdictPending);

  // Within the minute, maintenance and settings aren't read again.
  b.asked.clear();
  now = 30000;
  poll_once(p, s, fake_fetch, &b);
  TEST_ASSERT_EQUAL_size_t(3, b.asked.size());
  now = 61000;
  b.asked.clear();
  poll_once(p, s, fake_fetch, &b);
  TEST_ASSERT_EQUAL_size_t(5, b.asked.size());
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
  TEST_ASSERT_EQUAL_INT(1, n); // no light button for a printer that is off
  TEST_ASSERT_EQUAL(Command::ClearPlate, a[0].cmd);
  TEST_ASSERT_TRUE(a[0].hold);
}

static void test_outcome_before_plate_and_later_skips_it()
{
  Snapshot s = idle_printer();
  strcpy(s.state, "FINISH");
  s.awaitingPlateClear = true;
  s.verdictPending = true;
  s.archiveId = 15;
  s.runId = 26;
  ViewOptions o;
  TEST_ASSERT_EQUAL(View::Verdict, pick_view(s, o));
  o.laterArchive = 15;
  o.laterRun = 26;
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
  TEST_ASSERT_EQUAL_INT(2, n);
  TEST_ASSERT_EQUAL(Command::StartNext, a[0].cmd);
  TEST_ASSERT_EQUAL_INT(42, a[0].arg); // the item shown, not whatever is first when it runs
  TEST_ASSERT_TRUE(a[0].hold);
}

static void test_light_sends_the_opposite_of_what_it_shows()
{
  Snapshot s = idle_printer();
  Action a[MAX_ACTIONS];
  s.light = true;
  pick_actions(View::Idle, s, a);
  TEST_ASSERT_EQUAL(Command::SetLight, a[0].cmd);
  TEST_ASSERT_EQUAL_INT(0, a[0].arg);
  s.light = false;
  pick_actions(View::Idle, s, a);
  TEST_ASSERT_EQUAL_INT(1, a[0].arg);
}

static void test_resume_queue_only_when_held()
{
  Snapshot s = idle_printer();
  s.errorCount = 1;
  Action a[MAX_ACTIONS];
  int n = pick_actions(View::Error, s, a);
  for (int i = 0; i < n; i++) TEST_ASSERT_TRUE(a[i].cmd != Command::ResumeQueue);
  s.held = 2;
  n = pick_actions(View::Error, s, a);
  TEST_ASSERT_EQUAL_INT(3, n);
  TEST_ASSERT_EQUAL(Command::ResumeQueue, a[1].cmd);
  s.errorCount = 0;
  n = pick_actions(View::Idle, s, a);
  TEST_ASSERT_EQUAL(Command::ResumeQueue, a[0].cmd);
}

static void test_costly_actions_are_held()
{
  Snapshot s = idle_printer();
  s.nextId = 1;
  Action a[MAX_ACTIONS];
  for (View v : {View::Printing, View::Paused, View::PlateClear, View::Error, View::Ready, View::Idle}) {
    int n = pick_actions(v, s, a);
    for (int i = 0; i < n; i++) {
      bool costly = a[i].cmd == Command::Stop || a[i].cmd == Command::ClearPlate || a[i].cmd == Command::StartNext;
      TEST_ASSERT_EQUAL(costly, a[i].hold);
    }
  }
}

// ---------------------------------------------------------------- text on the screen

static void test_progress_never_reads_done_early()
{
  Snapshot s;
  s.progress = 99.6f;
  TEST_ASSERT_EQUAL_INT(99, progress_percent(s));
  s.remainingMin = 83;
  char out[40];
  format_progress(out, sizeof(out), s);
  TEST_ASSERT_EQUAL_STRING("99%   1h 23m left", out);
  s.remainingMin = 26 * 60 + 5;
  format_progress(out, sizeof(out), s);
  TEST_ASSERT_EQUAL_STRING("99%   26h 05m", out); // "left" dropped so the time fits
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
  TEST_ASSERT_EQUAL_STRING("Overdue by 3 h", out);
  t.left = -0.4f;
  format_due(out, sizeof(out), t);
  TEST_ASSERT_EQUAL_STRING("Due now", out);
  t.level = 0;
  t.days = true;
  t.left = 1;
  format_due(out, sizeof(out), t);
  TEST_ASSERT_EQUAL_STRING("Due in 1 day", out);
  t.left = 21;
  format_due(out, sizeof(out), t);
  TEST_ASSERT_EQUAL_STRING("Due in 21 days", out);
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
  RUN_TEST(test_status_odd_numbers_are_tamed);
  RUN_TEST(test_text_becomes_ascii);
  RUN_TEST(test_text_is_cut_on_whole_characters);
  RUN_TEST(test_queue_picks_lowest_position);
  RUN_TEST(test_queue_empty_clears_previous);
  RUN_TEST(test_queue_counts_held_and_any_model_items);
  RUN_TEST(test_verdict_pending_only_when_asked_and_unanswered);
  RUN_TEST(test_verdict_not_asked_for_failed_or_running_prints);
  RUN_TEST(test_plate_clear_setting);
  RUN_TEST(test_tasks_sorted_by_urgency);
  RUN_TEST(test_tasks_close_together_keep_their_order);
  RUN_TEST(test_tasks_keep_the_most_urgent_when_too_many);
  RUN_TEST(test_requests_match_bambuddy_api);
  RUN_TEST(test_requests_need_an_id);
  RUN_TEST(test_command_results_are_plain);
  RUN_TEST(test_offline_text_names_the_cause);
  RUN_TEST(test_urls_are_cleaned_up);
  RUN_TEST(test_poll_round_asks_in_order);
  RUN_TEST(test_poll_round_stops_when_bambuddy_is_away);
  RUN_TEST(test_poll_round_skips_outcome_while_printing);
  RUN_TEST(test_refused_answer_stops_the_question);
  RUN_TEST(test_task_done_reads_the_list_again);
  RUN_TEST(test_view_for_each_state);
  RUN_TEST(test_plate_gate_follows_bambuddy_setting);
  RUN_TEST(test_plate_gate_shown_with_printer_switched_off);
  RUN_TEST(test_outcome_before_plate_and_later_skips_it);
  RUN_TEST(test_start_only_for_jobs_assigned_here);
  RUN_TEST(test_light_sends_the_opposite_of_what_it_shows);
  RUN_TEST(test_resume_queue_only_when_held);
  RUN_TEST(test_costly_actions_are_held);
  RUN_TEST(test_progress_never_reads_done_early);
  RUN_TEST(test_durations);
  RUN_TEST(test_due_text);
  RUN_TEST(test_temperatures);
  RUN_TEST(test_calibration_every_orientation);
  RUN_TEST(test_calibration_refuses_nonsense);
  RUN_TEST(test_touch_is_clamped_to_the_screen);
  return UNITY_END();
}
