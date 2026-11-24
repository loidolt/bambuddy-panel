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
  bool reachable = false;  // Bambuddy answered the last status request
  int httpCode = 0;        // last status HTTP code (or a negative client error)

  bool connected = false;  // Bambuddy is connected to the printer
  char name[24] = "";
  char state[16] = "";     // RUNNING, PAUSE, FINISH, FAILED, IDLE, PREPARE, ...
  char job[64] = "";
  char stage[32] = "";     // current stage name, when the printer reports one
  float progress = 0;      // 0..100
  int remainingMin = 0;    // assumed minutes, as the printer reports it
  int layer = 0;
  int totalLayers = 0;
  float nozzle = 0, bed = 0, chamber = 0;
  bool light = false;
  bool awaitingPlateClear = false;
  int errorCount = 0;
  char error[96] = "";     // first HMS error, description or code

  int queued = 0;          // pending queue items for this printer
  int nextId = 0;          // first pending item, 0 when none
  bool nextManual = false; // it waits for a manual start
  char nextName[64] = "";
  int nextSeconds = 0;

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
  ToggleLight,
  ClearErrors,
  ResumeQueue,
  StartNext,
  VerdictGood,
  VerdictReject,
  PerformTask, // arg: the task's id
};

void bambuddy_begin();               // starts the background task; WiFi must be up
void bambuddy_send(Command c, int arg = 0); // queues a command; returns at once
void bambuddy_snapshot(Snapshot &out);
