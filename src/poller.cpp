#include "poller.h"
#include "bambuddy_parse.h"

#include <cstdio>
#include <cstring>

static bool is(const Snapshot &s, const char *state) { return strcmp(s.state, state) == 0; }

static bool due(bool stale, uint32_t at, uint32_t every) { return stale || millis() - at >= every; }

void poll_once(Poller &p, Snapshot &s, FetchFn fetch, void *ctx)
{
  char path[96];
  s.printerId = p.printerId;
  {
    JsonDocument filter, doc;
    status_filter(filter);
    snprintf(path, sizeof(path), "/api/v1/printers/%d/status", p.printerId);
    s.httpCode = fetch(path, doc, filter, ctx);
    s.reachable = s.httpCode == 200;
    if (s.reachable) parse_status(doc, s);
  }
  if (!s.reachable) {
    s.seq++;
    return;
  }

  {
    // Pending items (the printer's own and "any <model>" ones; Bambuddy adds those itself), and
    // the ones it skipped after a failed print, which "resume queue" would put back.
    JsonDocument filter, doc;
    queue_filter(filter);
    snprintf(path, sizeof(path), "/api/v1/queue/?status=pending,skipped&printer_id=%d", p.printerId);
    if (fetch(path, doc, filter, ctx) == 200) parse_queue(doc, p.printerId, s); // else keep the last known
  }

  // The outcome question belongs to a finished print; while one is under way there is none.
  if (is(s, "RUNNING") || is(s, "PAUSE") || is(s, "PREPARE") || is(s, "SLICING")) {
    s.verdictPending = false;
  } else {
    JsonDocument filter, doc;
    last_print_filter(filter);
    if (fetch("/api/v1/archives/last-per-printer", doc, filter, ctx) == 200) {
      parse_last_print(doc, p.printerId, s);
      if (s.verdictPending && s.archiveId == p.refusedArchive && s.runId == p.refusedRun) s.verdictPending = false;
    }
  }

  if (due(p.tasksStale, p.tasksPolledAt, TASKS_POLL_MS)) {
    JsonDocument filter, doc;
    tasks_filter(filter);
    snprintf(path, sizeof(path), "/api/v1/maintenance/printers/%d", p.printerId);
    if (fetch(path, doc, filter, ctx) == 200) {
      parse_tasks(doc, s);
      p.tasksStale = false;
    }
    p.tasksPolledAt = millis(); // also after a failure: try again in a minute, not every poll
  }

  if (due(p.settingsStale, p.settingsPolledAt, SETTINGS_POLL_MS)) {
    JsonDocument filter, doc;
    settings_filter(filter);
    if (fetch("/api/v1/settings/", doc, filter, ctx) == 200) {
      parse_settings(doc, s);
      p.settingsStale = false;
    }
    p.settingsPolledAt = millis();
  }

  s.seq++;
}

void poller_after_command(Poller &p, const Job &job, int code)
{
  if (job.cmd == Command::PerformTask) p.tasksStale = true;
  bool answer = job.cmd == Command::VerdictGood || job.cmd == Command::VerdictReject;
  if (answer && (code == 401 || code == 403)) {
    p.refusedArchive = job.arg;
    p.refusedRun = job.arg2;
  }
}
