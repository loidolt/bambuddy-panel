#include "poller.h"
#include "bambuddy_api.h"
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
    if (s.httpCode == 200) {
      p.misses = 0;
      p.answeredAt = millis();
      s.reachable = s.everReachable = true;
      s.lostSec = 0;
      parse_status(doc, s);
    } else {
      // No answer at all (a timeout, WiFi gone for a moment) is forgiven once while the last
      // status is still on screen. An answer that says no, or an address that can't work, is
      // shown at once: waiting won't change it.
      bool noAnswer = s.httpCode < 0 && s.httpCode != ERR_HTTPS && s.httpCode != ERR_BAD_URL;
      if (!(noAnswer && s.reachable && ++p.misses < OFFLINE_AFTER)) s.reachable = false;
      if (s.everReachable) s.lostSec = (int)((millis() - p.answeredAt) / 1000);
      s.seq++;
      return;
    }
  }

  {
    // Pending items (the printer's own and "any <model>" ones; Bambuddy adds those itself), and
    // the ones it skipped after a failed print, which "resume queue" would put back.
    JsonDocument filter, doc;
    queue_filter(filter);
    snprintf(path, sizeof(path), "/api/v1/queue/?status=pending,skipped,printing&printer_id=%d", p.printerId);
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
    if (fetch(path, doc, filter, ctx) == 200) parse_tasks(doc, s);
    // Also after a failure: try again in a minute, not on every poll.
    p.tasksStale = false;
    p.tasksPolledAt = millis();
  }

  if (due(p.settingsStale, p.settingsPolledAt, SETTINGS_POLL_MS)) {
    JsonDocument filter, doc;
    settings_filter(filter);
    if (fetch("/api/v1/settings/", doc, filter, ctx) == 200) parse_settings(doc, s);
    p.settingsStale = false;
    p.settingsPolledAt = millis();
  }

  // Only for a printer that has somewhere to put a spool.
  if (s.slotCount > 0 && due(p.assignedStale, p.assignedPolledAt, ASSIGNED_POLL_MS)) {
    JsonDocument filter, doc;
    assignments_filter(filter);
    snprintf(path, sizeof(path), "/api/v1/inventory/assignments?printer_id=%d", p.printerId);
    if (fetch(path, doc, filter, ctx) == 200) parse_assignments(doc, p.printerId, s);
    p.assignedStale = false;
    p.assignedPolledAt = millis();
  }

  if (p.spoolsWanted && p.spools) {
    p.spoolsWanted = false;
    JsonDocument filter, doc;
    spools_filter(filter);
    int code = fetch("/api/v1/inventory/spools", doc, filter, ctx);
    if (code != 200 || !parse_spools(doc, *p.spools)) {
      p.spools->state = SpoolState::Failed;
      p.spools->code = code == 200 ? ERR_NO_MEMORY : code;
    }
  }

  s.seq++;
}

void poller_after_command(Poller &p, const Job &job, int code)
{
  if (job.cmd == Command::PerformTask) p.tasksStale = true;
  if (job.cmd == Command::AssignSpool || job.cmd == Command::UnassignSpool) p.assignedStale = true;
  if (job.cmd == Command::LoadSpools) {
    p.spoolsWanted = true;
    if (p.spools) p.spools->state = SpoolState::Loading;
  }
  bool answer = job.cmd == Command::VerdictGood || job.cmd == Command::VerdictReject;
  if (answer && (code == 401 || code == 403)) {
    p.refusedArchive = job.arg;
    p.refusedRun = job.arg2;
  }
}
