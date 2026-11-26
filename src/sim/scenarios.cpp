#include "scenarios.h"
#include "bambuddy_parse.h"

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

static void add_slot(Snapshot &s, int ams, int tray, const char *type, uint32_t rgb)
{
  Slot &slot = s.slots[s.slotCount++];
  slot.ams = (uint8_t)ams;
  slot.tray = (uint8_t)tray;
  SET(slot.type, type);
  slot.rgb = rgb;
}

static void assign(Snapshot &s, int ams, int tray, int spool, const char *name, int grams)
{
  Assignment &a = s.assigned[s.assignedCount++];
  a.ams = (uint8_t)ams;
  a.tray = (uint8_t)tray;
  a.spoolId = spool;
  SET(a.name, name);
  a.remainG = grams;
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
  s.printerId = 1;
  s.reachable = s.everReachable = true;
  s.httpCode = 200;
  s.connected = true;
  SET(s.name, "X1C");
  SET(s.state, "IDLE");
  s.nozzle = 28;
  s.bed = 26;
  s.chamber = 27;
  // One AMS and the external holder; Bambuddy has an inventory spool in two of the slots.
  add_slot(s, 0, 0, "PETG", 0x101010);
  add_slot(s, 0, 1, "PLA", 0xF5F5F0);
  add_slot(s, 0, 2, "PLA", 0xC12E1F);
  add_slot(s, 0, 3, "", 0);
  add_slot(s, EXTERNAL_AMS, 0, "TPU", 0x2F6FDB);
  s.assignedKnown = true;
  assign(s, 0, 0, 12, "Prusament Galaxy Black", 640);
  assign(s, 0, 1, 3, "Bambu Lab Jade White", 212);
  return s;
}

static void queue_next(Snapshot &s, const char *name, int seconds, int queued, bool manual)
{
  s.nextId = 42;
  s.queued = queued;
  s.nextManual = manual;
  s.nextAssigned = true;
  s.nextSeconds = seconds;
  SET(s.nextName, name);
  s.moreCount = queued > 3 ? 2 : queued > 1 ? queued - 1 : 0;
  SET(s.more[0].name, "Cable_clips_x24");
  s.more[0].seconds = 47 * 60;
  SET(s.more[1].name, "Spool_holder_v3_with_a_longer_name_than_fits");
  s.more[1].seconds = 5 * 3600 + 40 * 60;
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

// Names as Bambuddy sends them, through the same ASCII conversion the panel uses.
static Snapshot printing_accents()
{
  Snapshot s = printing();
  copy_ascii(s.name, sizeof(s.name), "Werkstatt Süd");
  copy_ascii(s.job, sizeof(s.job), "Drachen_für_Jürgen_Ø40_plate_1");
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
  SET(s.errors[0], "AMS1 slot 2 filament has run out. Please insert a new filament.");
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
  SET(s.errors[0], "The nozzle is clogged. Please check the hotend and extruder.");
  s.held = 1; // the job after it, skipped because this print failed
  return s;
}

// With Auto Power Off the printer is switched off by the time the plate wants clearing.
static Snapshot finished_printer_off()
{
  Snapshot s = finished();
  s.connected = false;
  s.nozzle = s.bed = s.chamber = 0;
  s.nextId = 0;
  s.queued = 0;
  return s;
}

// Bambuddy skipped two jobs because the print before them failed.
static Snapshot queue_held()
{
  Snapshot s = base();
  SET(s.job, "Fidget_gears_plate_2");
  s.held = 2;
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

// A printer without a chamber sensor.
static Snapshot idle_no_chamber()
{
  Snapshot s = idle();
  SET(s.name, "A1 mini");
  s.chamber = 0;
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
  s.printerId = 1;
  s.httpCode = -1;
  return s;
}

static Snapshot key_refused()
{
  Snapshot s;
  s.printerId = 1;
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

// ---- states that stress the layout rather than show a situation

// Every text field as long as it can be, at once.
static Snapshot everything_long()
{
  Snapshot s = printing_long_text();
  SET(s.name, "Workshop H2D left bench");
  SET(s.job, "A_very_long_job_name_that_fills_the_whole_field_to_the_end_p12");
  SET(s.stage, "Heating the chamber and waiting");
  s.errorCount = 3;
  SET(s.errors[0], "AMS1 slot 2 filament has run out and the spare spool could not be loaded. Please check it");
  SET(s.errors[1], "The nozzle is clogged. Please check the hotend and extruder.");
  SET(s.errors[2], "0C00800000020001");
  SET(s.jobBy, "a_long_user_name_19");
  return s;
}

// Several errors at once: only the first has room.
static Snapshot error_many()
{
  Snapshot s = error();
  s.errorCount = 4;
  SET(s.errors[0], "The heatbed temperature is abnormal; the sensor may have an open circuit or the cable");
  SET(s.errors[1], "The nozzle is clogged. Please check the hotend and extruder.");
  SET(s.errors[2], "AMS1 slot 2 filament has run out. Please insert a new filament.");
  return s;
}

// A print that takes days, on a printer that reports no layers.
static Snapshot printing_days()
{
  Snapshot s = printing();
  s.progress = 3.4f;
  s.remainingMin = 101 * 60 + 7;
  s.layer = s.totalLayers = 0;
  s.nextId = 0;
  s.queued = 0;
  SET(s.stage, "Printing");
  return s;
}

// Preparing with a job waiting behind it: stage and "Next" want the same line.
static Snapshot preparing_queued()
{
  Snapshot s = preparing();
  queue_next(s, "Articulated_dragon_plate_1", 3 * 3600 + 12 * 60, 3, false);
  return s;
}

// The next job is for "any X1C": shown, but not this panel's to start.
static Snapshot idle_unassigned_next()
{
  Snapshot s = idle();
  queue_next(s, "Cable_clips_x24", 47 * 60, 2, true);
  s.nextAssigned = false;
  return s;
}

static Snapshot ready_queue_held()
{
  Snapshot s = ready();
  s.held = 2;
  return s;
}

// ---- what Bambuddy says about the queue

static Snapshot printing_queued_by()
{
  Snapshot s = printing();
  SET(s.jobBy, "anna");
  return s;
}

// Bambuddy holds the job back and says why; who queued it is known.
static Snapshot ready_waiting_filament()
{
  Snapshot s = ready();
  SET(s.nextBy, "anna");
  SET(s.nextWhy, "Waiting for filament: PETG black, or any PETG in slot 3");
  return s;
}

static Snapshot ready_queued_more()
{
  Snapshot s = idle();
  queue_next(s, "Articulated_dragon_plate_1_with_supports_v2", 3 * 3600 + 12 * 60, 3, true);
  SET(s.nextBy, "anna");
  return s;
}

static Snapshot idle_waiting_filament()
{
  Snapshot s = idle();
  queue_next(s, "Articulated_dragon_plate_1", 3 * 3600 + 12 * 60, 3, false);
  SET(s.nextWhy, "Waiting for filament: PETG black");
  return s;
}

// Accepted, and Bambuddy has not sent it to the printer yet.
static Snapshot idle_starting_soon()
{
  Snapshot s = idle();
  queue_next(s, "Articulated_dragon_plate_1", 3 * 3600 + 12 * 60, 1, false);
  return s;
}

// Bambuddy has sent the job; the printer still shows the last print as finished.
static Snapshot sending()
{
  Snapshot s = idle();
  SET(s.state, "FINISH");
  SET(s.job, "Fidget_gears_plate_2");
  s.runningId = 42;
  SET(s.runningName, "Articulated_dragon_plate_1");
  s.sending = true;
  queue_next(s, "Cable_clips_x24", 47 * 60, 1, false);
  return s;
}

// The plate gate with a job that needs its own start: clearing the plate starts nothing.
static Snapshot finished_next_manual()
{
  Snapshot s = finished();
  s.nextManual = true;
  return s;
}

// ---- Bambuddy not answering

// One answer missed: the last status stays, the chip goes grey.
static Snapshot printing_one_missed()
{
  Snapshot s = printing();
  s.httpCode = -11;
  return s;
}

// Two and more: the last status stays under a band, and nothing can be pressed.
static Snapshot link_lost()
{
  Snapshot s = printing();
  s.reachable = false;
  s.httpCode = -11;
  s.lostSec = 40;
  return s;
}

static Snapshot link_lost_long()
{
  Snapshot s = link_lost();
  s.lostSec = 400; // past the five minutes: the panel gives up on the old status
  return s;
}

// ---- filament

// Two AMS, an AMS HT and both external holders: more slots than a page, and long names.
static Snapshot slots_many()
{
  Snapshot s = idle();
  s.slotCount = s.assignedCount = 0;
  const uint32_t colours[] = {0x101010, 0xF5F5F0, 0xC12E1F, 0x2F6FDB, 0x1F8A3B, 0xF2C037, 0x8E44AD, 0x7F8C8D};
  for (int ams = 0; ams < 2; ams++)
    for (int tray = 0; tray < 4; tray++) add_slot(s, ams, tray, tray == 3 ? "PETG-CF" : "PLA", colours[ams * 4 + tray]);
  add_slot(s, 128, 0, "PA6-CF", 0x202020);
  add_slot(s, EXTERNAL_AMS, 0, "TPU", 0x2F6FDB);
  add_slot(s, EXTERNAL_AMS, 1, "", 0);
  assign(s, 0, 0, 12, "A_brand_with_a_long_name Midnight Blue", 1000);
  assign(s, 0, 1, 3, "Bambu Lab Jade White", 0);
  assign(s, 128, 0, 30, "Polymaker", -1);
  return s;
}

// A printer with no AMS answer: no spool in the header.
static Snapshot slots_none()
{
  Snapshot s = idle();
  s.slotCount = s.assignedCount = 0;
  return s;
}

// ---- maintenance lists (opened by the screenshot run, see sim_main.cpp)

static Snapshot tasks_none()
{
  Snapshot s = printing();
  s.taskCount = s.tasksDue = s.tasksWarn = 0;
  return s;
}

static Snapshot tasks_unknown()
{
  Snapshot s = tasks_none();
  s.tasksKnown = false;
  return s;
}

static Snapshot tasks_one()
{
  Snapshot s = tasks_none();
  add_task(s, "Clean Build Plate", 1, false, 0.4f);
  return s;
}

// As many as the panel keeps, with names as long as it keeps, and every wording of "due".
static Snapshot tasks_full()
{
  Snapshot s = tasks_none();
  add_task(s, "Replace HEPA and carbon filt", 2, true, -412);
  add_task(s, "Lubricate the Z lead screws", 2, false, -0.4f);
  add_task(s, "Clean Build Plate", 2, false, 0);
  add_task(s, "Check Belt Tension", 1, true, 1);
  add_task(s, "Lubricate Linear Rails", 1, false, 1.2f);
  add_task(s, "Clean Carbon Rods", 1, false, 8.4f);
  add_task(s, "Clean Nozzle/Hotend", 0, false, 42.9f);
  add_task(s, "Check PTFE Tube", 0, false, 1442.9f);
  add_task(s, "Replace Carbon Filter", 0, true, 365);
  add_task(s, "Dry the AMS desiccant", 0, true, 2);
  add_task(s, "Custom task without a time", 1, false, 0);
  s.tasks[10].timed = false;
  add_task(s, "Another without a time", 0, false, 0);
  s.tasks[11].timed = false;
  return s;
}

const Scenario scenarios[] = {
    {"printing", printing},
    {"printing-long-text", printing_long_text},
    {"printing-accents", printing_accents},
    {"preparing", preparing},
    {"paused", paused},
    {"paused-error", paused_error},
    {"verdict", verdict},
    {"finished", finished},
    {"failed", failed},
    {"finished-printer-off", finished_printer_off},
    {"error", error},
    {"ready", ready},
    {"idle", idle},
    {"idle-no-chamber", idle_no_chamber},
    {"queue-held", queue_held},
    {"printer-offline", printer_offline},
    {"offline", offline},
    {"key-refused", key_refused},
    {"everything-long", everything_long},
    {"error-many", error_many},
    {"printing-days", printing_days},
    {"preparing-queued", preparing_queued},
    {"idle-unassigned-next", idle_unassigned_next},
    {"ready-queue-held", ready_queue_held},
    {"printing-queued-by", printing_queued_by},
    {"ready-waiting-filament", ready_waiting_filament},
    {"ready-queued-more", ready_queued_more},
    {"idle-waiting-filament", idle_waiting_filament},
    {"idle-starting-soon", idle_starting_soon},
    {"sending", sending},
    {"finished-next-manual", finished_next_manual},
    {"printing-one-missed", printing_one_missed},
    {"link-lost", link_lost},
    {"link-lost-long", link_lost_long},
    {"slots-many", slots_many},
    {"slots-none", slots_none},
    {"tasks-none", tasks_none},
    {"tasks-unknown", tasks_unknown},
    {"tasks-one", tasks_one},
    {"tasks-full", tasks_full},
    {"toast", toast}, // last: its toast would cover the header of whatever came after
};
const int scenarioCount = sizeof(scenarios) / sizeof(scenarios[0]);
