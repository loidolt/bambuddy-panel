// Talks to Bambuddy on a background task: polls one printer's status and queue,
// and sends the few commands the panel offers. The UI only ever reads a Snapshot.
#pragma once
#include <Arduino.h>

// One maintenance task for this printer, as Bambuddy tracks it.
struct Task {
  uint32_t lastPerformed = 0;
  int id = 0;
  char name[28] = "";
  uint8_t level = 0;  // 0 fine, 1 due soon, 2 due
  bool days = false;  // counted in calendar days, else in print hours
  float left = 0;     // days or hours until due; negative when overdue
  bool timed = true;  // false when Bambuddy sent no time: `left` means nothing then
};

static const int MAX_TASKS = 12;
static const int MAX_ERRORS = 3; // kept to read on the screen; the count can be higher
static const int MAX_MORE = 2;   // queued jobs shown after the next one

// A place a spool can sit: a tray of an AMS, or an external holder. `ams` is Bambuddy's own
// number for the unit (0-3 an AMS, 128 and up an AMS HT, 255 the external holders).
struct Slot {
  uint8_t ams = 0, tray = 0;
  char type[12] = "";   // what the printer says is in it ("PETG"); empty when nothing is
  bool empty = false;   // explicitly absent according to the printer
  uint32_t rgb = 0;     // its colour, as the printer has it
};
static const int MAX_SLOTS = 18; // four AMS of four, and two external
static const int EXTERNAL_AMS = 255;

// A spool from Bambuddy's inventory that Bambuddy has put in one of this printer's slots.
struct Assignment {
  uint8_t ams = 0, tray = 0;
  int spoolId = 0;
  char name[36] = "";   // "Bambu Lab Black", as the spool list words it
  int remainG = -1;     // grams left; -1 when Bambuddy doesn't know
};

// One command's two numbers hold a slot as one: see Command::AssignSpool.
inline int slot_code(int ams, int tray) { return ams * 16 + tray; }
inline int slot_ams(int code) { return code / 16; }
inline int slot_tray(int code) { return code % 16; }

// A queued job after the next one: just enough to list it.
struct QueuedJob {
  char name[48] = "";
  int seconds = 0;
};

struct Snapshot {
  uint32_t seq = 0;        // goes up on every poll; the UI redraws when it changes
  int printerId = 0;       // the Bambuddy printer this panel shows
  bool reachable = false;  // Bambuddy answered the last status request (or missed only one)
  bool everReachable = false; // it has answered at least once: the fields below hold a real status
  int lostSec = 0;         // seconds since it last answered, while it doesn't
  int httpCode = 0;        // last status HTTP code (or a negative client error)

  bool connected = false;  // Bambuddy is connected to the printer
  char name[24] = "";
  char state[16] = "";     // RUNNING, PAUSE, FINISH, FAILED, IDLE, PREPARE, SLICING, ...
  char job[64] = "";
  int currentArchive = 0;
  uint32_t fileIdentity = 0; // full server filename, before shortening it for display
  char stage[32] = "";     // current stage name, when the printer reports one
  float progress = 0;      // 0..100
  int remainingMin = 0;    // minutes, as the printer reports it
  int layer = 0;
  int totalLayers = 0;
  float nozzle = 0, bed = 0, chamber = 0;
  bool awaitingPlateClear = false;
  uint32_t errorIdentity = 0;
  int errorCount = 0;      // HMS errors that count as a problem (see hms_counts)
  char errors[MAX_ERRORS][96] = {}; // the first few, description or code

  // Bambuddy's "require plate clear" setting. Its plate-clear gate is only shown when this is
  // on and has been read successfully.
  bool settingsKnown = false, queueKnown = false, lastPrintKnown = false;
  bool requirePlateClear = false;

  int queued = 0;          // pending queue items for this printer (its own and "any <model>")
  int nextId = 0;          // first pending item, 0 when none
  bool nextManual = false; // it waits for a manual start
  bool nextAssigned = false; // it is assigned to this printer, not to "any <model>"
  char nextName[64] = "";
  int nextSeconds = 0;
  char nextWhy[72] = "";   // Bambuddy's reason the next job hasn't started, when it gives one
  char nextBy[20] = "";    // who queued it, when Bambuddy knows
  QueuedJob more[MAX_MORE]; // the jobs after it, in order
  int moreCount = 0;
  char jobBy[20] = "";     // who queued the print under way, when it came from the queue
  // The queue item Bambuddy counts as printing on this printer. Bambuddy says so from the
  // moment it has sent the job, which is before the printer says it is doing anything.
  int runningId = 0;
  char runningName[64] = "";
  bool sending = false;    // that job has been sent and the printer has not taken it up yet
  uint32_t heldIdentity = 0;
  int held = 0;            // items Bambuddy skipped because the previous print failed

  // Bambuddy's "ask for outcome": the last print waits for a good / bad answer.
  bool verdictPending = false;
  int archiveId = 0;       // the print to answer for
  int runId = 0;

  // Maintenance, most urgent first. Refreshed about once a minute.
  bool tasksKnown = false; // Bambuddy has answered at least once
  Task tasks[MAX_TASKS];
  int taskCount = 0;
  int tasksDue = 0;
  int tasksWarn = 0;

  // Filament: the printer's slots, and which inventory spool Bambuddy has in each.
  Slot slots[MAX_SLOTS];
  int slotCount = 0;
  Assignment assigned[MAX_SLOTS];
  int assignedCount = 0;
  bool assignedKnown = false; // Bambuddy has answered about assignments at least once

  uint32_t answeredRequest = 0, settledRequest = 0;
  uint32_t toastSeq = 0;   // goes up when a command finishes
  char toast[40] = "";     // its result, to show briefly
  bool toastBad = false;   // the command failed
};

enum class Command : uint8_t {
  Pause,
  Resume,
  Stop,
  ClearPlate,
  ClearErrors,
  ResumeQueue,
  StartNext,     // arg: the queue item shown on screen
  VerdictGood,   // arg: archive id, arg2: run id (0 when none)
  VerdictReject, // as VerdictGood
  PerformTask,   // arg: the task's id
  AssignSpool,   // arg: the spool's id, arg2: slot_code() of where it goes
  UnassignSpool, // arg2: slot_code() of the slot to empty
  LoadSpools,    // not sent anywhere: asks the background task to read the spool list again
};

// A command and the ids it acts on, as the screen showed them when it was pressed.
struct Job {
  Command cmd;
  int arg;
  int arg2;
};

struct CommandTarget {
  int printer, item, related;
  uint32_t file;
  int next;
  bool operator==(const CommandTarget &other) const {
    return printer == other.printer && item == other.item && related == other.related && file == other.file && next == other.next;
  }
};

bool bambuddy_begin(); // starts the background task; call once, after WiFi is set up
// Queues a command and returns at once. False when the queue is full.
bool bambuddy_send(Command c, int arg = 0, int arg2 = 0, CommandTarget target = {}, uint32_t request = 0);
void bambuddy_snapshot(Snapshot &out);

// The spool list is too big to hand around whole: the background task keeps it, and the
// screen asks for the few rows it is showing. Send Command::LoadSpools to have it read.
#include "spools.h"
// Up to `max` spools of `material` (empty: all), starting at the `offset`-th such. Returns how
// many it gave; `total` is how many there are, `state` how the reading stands.
int bambuddy_spools(const char *material, int offset, Spool *out, int max, int &total, SpoolState &state);
int bambuddy_spool_materials(char out[][SPOOL_MATERIAL], int max); // the materials there are, in order
