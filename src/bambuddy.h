// Talks to Bambuddy on a background task: polls one printer's status and queue,
// and sends the few commands the panel offers. The UI only ever reads a Snapshot.
#pragma once
#include <Arduino.h>

// One maintenance task for this printer, as Bambuddy tracks it.
struct Task {
  int id = 0;
  char name[28] = "";
  uint8_t level = 0;  // 0 fine, 1 due soon, 2 due
  bool days = false;  // counted in calendar days, else in print hours
  float left = 0;     // days or hours until due; negative when overdue
};

static const int MAX_TASKS = 12;

struct Snapshot {
  uint32_t seq = 0;        // goes up on every poll; the UI redraws when it changes
  int printerId = 0;       // the Bambuddy printer this panel shows
  bool reachable = false;  // Bambuddy answered the last status request
  int httpCode = 0;        // last status HTTP code (or a negative client error)

  bool connected = false;  // Bambuddy is connected to the printer
  char name[24] = "";
  char state[16] = "";     // RUNNING, PAUSE, FINISH, FAILED, IDLE, PREPARE, SLICING, ...
  char job[64] = "";
  char stage[32] = "";     // current stage name, when the printer reports one
  float progress = 0;      // 0..100
  int remainingMin = 0;    // minutes, as the printer reports it
  int layer = 0;
  int totalLayers = 0;
  float nozzle = 0, bed = 0, chamber = 0;
  bool light = false;
  bool awaitingPlateClear = false;
  int errorCount = 0;
  char error[96] = "";     // first HMS error, description or code

  // Bambuddy's "require plate clear" setting. Its plate-clear gate is only shown when this is
  // on; until the setting has been read the panel assumes it is.
  bool requirePlateClear = true;

  int queued = 0;          // pending queue items for this printer (its own and "any <model>")
  int nextId = 0;          // first pending item, 0 when none
  bool nextManual = false; // it waits for a manual start
  bool nextAssigned = false; // it is assigned to this printer, not to "any <model>"
  char nextName[64] = "";
  int nextSeconds = 0;
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

  uint32_t toastSeq = 0;   // goes up when a command finishes
  char toast[40] = "";     // its result, to show briefly
};

enum class Command : uint8_t {
  Pause,
  Resume,
  Stop,
  ClearPlate,
  SetLight,      // arg: 1 on, 0 off
  ClearErrors,
  ResumeQueue,
  StartNext,     // arg: the queue item shown on screen
  VerdictGood,   // arg: archive id, arg2: run id (0 when none)
  VerdictReject, // as VerdictGood
  PerformTask,   // arg: the task's id
};

// A command and the ids it acts on, as the screen showed them when it was pressed.
struct Job {
  Command cmd;
  int arg;
  int arg2;
};

void bambuddy_begin(); // starts the background task; call once, after WiFi is set up
// Queues a command and returns at once. False when the queue is full.
bool bambuddy_send(Command c, int arg = 0, int arg2 = 0);
void bambuddy_snapshot(Snapshot &out);
