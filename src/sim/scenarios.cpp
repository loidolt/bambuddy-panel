#include "scenarios.h"

#include <cstdio>

static void set(char *dst, size_t size, const char *src) { snprintf(dst, size, "%s", src); }
#define SET(field, text) set(field, sizeof(field), text)

static void add_task(Snapshot &s, const char *name, int level, bool days, float left)
{
  Task &t = s.tasks[s.taskCount];
  t.id = 100 + s.taskCount;
  SET(t.name, name);
  t.level = level;
  t.days = days;
  t.left = left;
  s.taskCount++;
  if (level == 2) s.tasksDue++;
  if (level == 1) s.tasksWarn++;
}

// A healthy printer, connected and idle, with nothing queued.
static Snapshot base()
{
  Snapshot s;
  s.tasksKnown = true;
  add_task(s, "Clean Build Plate", 2, false, -3.2f);
  add_task(s, "Lubricate Linear Rails", 1, false, 8.4f);
  add_task(s, "Replace Carbon Filter", 0, true, 21);
  add_task(s, "Clean Carbon Rods", 0, false, 42.9f);
  add_task(s, "Clean Nozzle/Hotend", 0, false, 42.9f);
  add_task(s, "Check Belt Tension", 0, false, 142.9f);
  add_task(s, "Check PTFE Tube", 0, false, 442.9f);
  s.reachable = true;
  s.httpCode = 200;
  s.connected = true;
  SET(s.name, "X1C");
  SET(s.state, "IDLE");
  s.nozzle = 28;
  s.bed = 26;
  s.chamber = 27;
  return s;
}

static void queue_next(Snapshot &s, const char *name, int seconds, int queued, bool manual)
{
  s.nextId = 42;
  s.queued = queued;
  s.nextManual = manual;
  s.nextSeconds = seconds;
  SET(s.nextName, name);
}

static Snapshot printing()
{
  Snapshot s = base();
  SET(s.state, "RUNNING");
  SET(s.job, "Fidget_gears_plate_2");
  s.progress = 42;
  s.remainingMin = 83;
  s.layer = 86;
  s.totalLayers = 203;
  s.nozzle = 220;
  s.bed = 60;
  s.chamber = 38;
  s.light = true;
  queue_next(s, "Articulated_dragon_plate_1", 3 * 3600 + 12 * 60, 3, false);
  return s;
}

static Snapshot printing_long_text()
{
  Snapshot s = printing();
  SET(s.name, "Workshop H2D left");
  SET(s.job, "A_very_long_job_name_that_will_not_fit_on_one_line_plate_12");
  s.progress = 99.6f;
  s.remainingMin = 26 * 60 + 5;
  s.layer = 1234;
  s.totalLayers = 1240;
  s.nozzle = 300;
  s.bed = 110;
  s.chamber = 60;
  queue_next(s, "Another_extremely_long_file_name_for_the_next_print_plate_3", 14 * 3600, 5, false);
  return s;
}

static Snapshot preparing()
{
  Snapshot s = base();
  SET(s.state, "PREPARE");
  SET(s.job, "Fidget_gears_plate_2");
  SET(s.stage, "Auto bed leveling");
  s.remainingMin = 125;
  s.totalLayers = 203;
  s.nozzle = 140;
  s.bed = 58;
  s.light = true;
  return s;
}

static Snapshot paused()
{
  Snapshot s = printing();
  SET(s.state, "PAUSE");
  return s;
}

static Snapshot paused_error()
{
  Snapshot s = paused();
  s.errorCount = 1;
  SET(s.error, "AMS1 slot 2 filament has run out. Please insert a new filament.");
  return s;
}

static Snapshot finished()
{
  Snapshot s = base();
  SET(s.state, "FINISH");
  SET(s.job, "Fidget_gears_plate_2");
  s.progress = 100;
  s.layer = s.totalLayers = 203;
  s.awaitingPlateClear = true;
  s.light = true;
  queue_next(s, "Articulated_dragon_plate_1", 3 * 3600 + 12 * 60, 3, false);
  return s;
}

static Snapshot verdict()
{
  Snapshot s = finished();
  s.verdictPending = true;
  s.archiveId = 15;
  s.runId = 26;
  return s;
}

static Snapshot failed()
{
  Snapshot s = base();
  SET(s.state, "FAILED");
  SET(s.job, "Fidget_gears_plate_2");
  s.progress = 37;
  s.awaitingPlateClear = true;
  return s;
}

static Snapshot error()
{
  Snapshot s = base();
  SET(s.state, "FAILED");
  SET(s.job, "Fidget_gears_plate_2");
  s.errorCount = 2;
  SET(s.error, "The nozzle is clogged. Please check the hotend and extruder.");
  queue_next(s, "Articulated_dragon_plate_1", 3 * 3600 + 12 * 60, 1, false);
  return s;
}

static Snapshot ready()
{
  Snapshot s = base();
  queue_next(s, "Articulated_dragon_plate_1", 3 * 3600 + 12 * 60, 1, true);
  return s;
}

// Nothing due: the wrench in the header stays quiet.
static Snapshot idle()
{
  Snapshot s = base();
  s.tasks[0].level = s.tasks[1].level = 0;
  s.tasks[0].left = 17;
  s.tasksDue = s.tasksWarn = 0;
  return s;
}

static Snapshot printer_offline()
{
  Snapshot s = base();
  s.connected = false;
  s.state[0] = 0;
  s.nozzle = s.bed = s.chamber = 0;
  return s;
}

static Snapshot offline()
{
  Snapshot s;
  s.httpCode = -1;
  return s;
}

static Snapshot key_refused()
{
  Snapshot s;
  s.httpCode = 401;
  return s;
}

static Snapshot toast()
{
  Snapshot s = paused();
  s.toastSeq = 1000;
  SET(s.toast, "Paused");
  return s;
}

const Scenario scenarios[] = {
    {"printing", printing},
    {"printing-long-text", printing_long_text},
    {"preparing", preparing},
    {"paused", paused},
    {"paused-error", paused_error},
    {"verdict", verdict},
    {"finished", finished},
    {"failed", failed},
    {"error", error},
    {"ready", ready},
    {"idle", idle},
    {"printer-offline", printer_offline},
    {"offline", offline},
    {"key-refused", key_refused},
    {"toast", toast},
};
const int scenarioCount = sizeof(scenarios) / sizeof(scenarios[0]);
