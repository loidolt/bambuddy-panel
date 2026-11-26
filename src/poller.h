// One round of reading Bambuddy into a Snapshot, and the bookkeeping after a command.
// The transport is passed in, so the panel (HTTPClient), the simulator (curl) and the unit
// tests (canned JSON) all run this same code.
#pragma once
#include <ArduinoJson.h>
#include "bambuddy.h"

// GETs `path` (starting with /api/v1), keeping only the fields in `filter`. Returns the HTTP
// code, or a negative client error; `doc` is valid on 200.
typedef int (*FetchFn)(const char *path, JsonDocument &doc, const JsonDocument &filter, void *ctx);

static const uint32_t TASKS_POLL_MS = 60000;    // maintenance changes slowly
static const uint32_t SETTINGS_POLL_MS = 60000; // so does the plate-clear setting
static const uint32_t ASSIGNED_POLL_MS = 60000; // and which spool is in which slot
// Status requests that must go unanswered in a row before the panel says Bambuddy is away. One
// lost request on WiFi is ordinary, and must not blank the screen for a poll.
static const int OFFLINE_AFTER = 2;

struct Poller {
  int printerId = 0;
  bool tasksStale = true;
  uint32_t tasksPolledAt = 0;
  bool settingsStale = true;
  uint32_t settingsPolledAt = 0;
  // An outcome answer Bambuddy refused (no permission): stop asking for that print.
  int refusedArchive = 0, refusedRun = 0;
  int misses = 0; // status requests in a row with no answer
  uint32_t answeredAt = 0; // when the last status came
  bool assignedStale = true;
  uint32_t assignedPolledAt = 0;
  // The spool list is read when the filament screen asks for it, not on a timer: it is big.
  bool spoolsWanted = false;
  SpoolStore *spools = nullptr; // where it goes; the caller's
};

// Reads status, then (when Bambuddy answers) the queue, the last print, and, now and then,
// the maintenance tasks, the plate-clear setting and the spool assignments; and the spool
// list when it was asked for. Bumps s.seq.
void poll_once(Poller &p, Snapshot &s, FetchFn fetch, void *ctx);

// After a command came back with `code`: marks what must be read again soon.
void poller_after_command(Poller &p, const Job &job, int code);
