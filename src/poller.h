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

struct Poller {
  int printerId = 0;
  bool tasksStale = true;
  uint32_t tasksPolledAt = 0;
  bool settingsStale = true;
  uint32_t settingsPolledAt = 0;
  // An outcome answer Bambuddy refused (no permission): stop asking for that print.
  int refusedArchive = 0, refusedRun = 0;
};

// Reads status, then (when Bambuddy answers) the queue, the last print, and, now and then,
// the maintenance tasks and the plate-clear setting. Bumps s.seq.
void poll_once(Poller &p, Snapshot &s, FetchFn fetch, void *ctx);

// After a command came back with `code`: marks what must be read again soon.
void poller_after_command(Poller &p, const Job &job, int code);
