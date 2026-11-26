#include "ui_logic.h"
#include "bambuddy_api.h"

#include <cstdio>
#include <cstring>

static bool is(const Snapshot &s, const char *state) { return strcmp(s.state, state) == 0; }

static bool active(const Snapshot &s)
{
  return is(s, "RUNNING") || is(s, "PREPARE") || is(s, "SLICING") || is(s, "PAUSE");
}

bool preparing(const Snapshot &s) { return is(s, "PREPARE") || is(s, "SLICING"); }

// An answer that says no, or an address that can't work. Asking again changes nothing.
static bool settings_are_wrong(int code)
{
  return code == 401 || code == 403 || code == 404 || code == ERR_HTTPS || code == ERR_BAD_URL;
}

bool link_lost(const Snapshot &s)
{
  return !s.reachable && s.everReachable && !settings_are_wrong(s.httpCode) && s.lostSec < LINK_LOST_GIVE_UP_S;
}

bool next_starts_by_itself(const Snapshot &s) { return s.nextId != 0 && !s.nextManual && !s.nextWhy[0]; }

View pick_view(const Snapshot &s, const ViewOptions &o)
{
  if (!s.reachable) {
    if (settings_are_wrong(s.httpCode)) return View::CheckSettings;
    if (!link_lost(s)) return View::Offline;
    // else: the last status, with a band over it (ui.cpp)
  }
  if (s.connected && is(s, "PAUSE")) return View::Paused;
  if (s.connected && active(s)) return View::Printing;
  if (s.connected && s.sending) return View::Sending;
  // Bambuddy-side questions come next. They don't need the printer: with Auto Power Off it is
  // switched off exactly when the plate wants clearing.
  bool skipped = s.archiveId == o.skippedArchive && s.runId == o.skippedRun;
  if (s.verdictPending && !skipped) return View::Verdict; // before the plate: clearing it can answer for you
  if (s.awaitingPlateClear && s.requirePlateClear) return View::PlateClear;
  if (!s.connected) return View::PrinterOffline;
  if (s.errorCount > 0) return View::Error;
  if (s.nextId != 0 && s.nextManual && s.nextAssigned) return View::Ready;
  if (s.held > 0) return View::QueueHeld;
  return View::Idle;
}

void pick_face(View v, const Snapshot &s, Face &f)
{
  f = Face();
  char dur[16];
  switch (v) {
  case View::Printing:
    f.chipTone = Tone::Neutral;
    if (!preparing(s)) {
      f.chip = "Printing"; // the headline is the progress, which ui.cpp draws
      break;
    }
    f.chip = "Preparing";
    snprintf(f.headline, sizeof(f.headline), "%s", s.stage[0] ? s.stage : "Getting ready");
    if (s.remainingMin > 0) {
      format_duration(dur, sizeof(dur), s.remainingMin);
      snprintf(f.sub, sizeof(f.sub), "About %s once it starts", dur);
    }
    break;
  case View::Sending:
    f.chip = "Starting";
    f.chipTone = Tone::Neutral;
    snprintf(f.headline, sizeof(f.headline), "%s", s.runningName[0] ? s.runningName : "A print");
    f.headlineIsName = true;
    snprintf(f.sub, sizeof(f.sub), "Sent to the printer. Waiting for it to begin.");
    break;
  case View::Paused:
    f.chip = "Paused";
    f.chipTone = Tone::Caution;
    snprintf(f.headline, sizeof(f.headline), "Paused at %d%%", progress_percent(s));
    break;
  case View::Verdict:
    f.chip = "Finished";
    f.chipTone = Tone::Go;
    snprintf(f.headline, sizeof(f.headline), "How did it print?");
    break;
  case View::PlateClear: {
    bool failed = is(s, "FAILED");
    f.chip = failed ? "Failed" : "Finished";
    f.chipTone = failed ? Tone::Danger : Tone::Go;
    f.headlineTone = f.chipTone;
    if (failed) snprintf(f.headline, sizeof(f.headline), "Failed at %d%%", progress_percent(s));
    else snprintf(f.headline, sizeof(f.headline), "Print finished");
    if (!s.connected && s.nextId == 0) snprintf(f.sub, sizeof(f.sub), "Printer is switched off");
    else if (s.nextId == 0) snprintf(f.sub, sizeof(f.sub), "Nothing else queued");
    else if (next_starts_by_itself(s)) snprintf(f.sub, sizeof(f.sub), "Starts when the plate is clear:");
    else snprintf(f.sub, sizeof(f.sub), "Next, not starting by itself:");
    break;
  }
  case View::Error:
    f.chip = "Error"; // the error's own text is the headline, in its box
    f.chipTone = Tone::Danger;
    break;
  case View::Ready:
    f.chip = "Ready";
    f.chipTone = Tone::Go;
    snprintf(f.headline, sizeof(f.headline), "%s", s.nextName);
    f.headlineIsName = true;
    format_duration(dur, sizeof(dur), s.nextSeconds / 60);
    if (s.nextBy[0]) snprintf(f.sub, sizeof(f.sub), "%s, queued by %s", dur, s.nextBy);
    else snprintf(f.sub, sizeof(f.sub), "%s", dur);
    break;
  case View::QueueHeld:
    f.chip = "Queue held";
    f.chipTone = Tone::Caution;
    f.headlineTone = Tone::Caution;
    snprintf(f.headline, sizeof(f.headline), "%d job%s held", s.held, s.held == 1 ? "" : "s");
    if (is(s, "FAILED") && s.job[0]) snprintf(f.sub, sizeof(f.sub), "after %s failed", s.job);
    else snprintf(f.sub, sizeof(f.sub), "after a failed print");
    break;
  case View::PrinterOffline:
    f.chip = "Offline";
    f.headlineTone = Tone::Dim;
    snprintf(f.headline, sizeof(f.headline), "Printer offline");
    snprintf(f.sub, sizeof(f.sub), "Bambuddy can't reach it. Switched off?");
    break;
  case View::Idle:
    f.chip = "Idle";
    f.headlineTone = Tone::Dim;
    if (s.nextId == 0) {
      snprintf(f.headline, sizeof(f.headline), "Nothing queued");
    } else if (s.nextWhy[0]) {
      snprintf(f.headline, sizeof(f.headline), "Waiting");
      snprintf(f.sub, sizeof(f.sub), "%s", s.nextWhy);
      f.subTone = Tone::Caution;
    } else if (!s.nextAssigned) {
      // A job for "any X1C": Bambuddy decides which printer runs it.
      snprintf(f.headline, sizeof(f.headline), "Waiting");
      snprintf(f.sub, sizeof(f.sub), "For any printer of this model: Bambuddy picks one");
    } else {
      snprintf(f.headline, sizeof(f.headline), "Starting soon");
    }
    break;
  default: // Offline and CheckSettings are message screens with words of their own
    break;
  }
}

int pick_actions(View v, const Snapshot &s, Action out[MAX_ACTIONS])
{
  int n = 0;
  auto add = [&](Action a) {
    if (n < MAX_ACTIONS) out[n++] = a;
  };
  const Action stop = {"Hold to stop", "Stopping...", Command::Stop, true, Kind::Stop, Icon::Stop, 0, 0};
  // Resuming the queue can start a print, so it is held like Start. Beside another button it
  // is the quiet one.
  auto resumeQueue = [&](bool main) {
    return Action{main ? "Hold to resume queue" : "Hold to\nresume queue", "Resuming...", Command::ResumeQueue,
                  true, main ? Kind::Neutral : Kind::Quiet, main ? Icon::Play : Icon::None, 0, 0};
  };

  switch (v) {
  case View::Printing:
    add({"Pause", "Pausing...", Command::Pause, false, Kind::Neutral, Icon::Pause, 0, 0});
    add(stop);
    break;
  case View::Paused:
    add({"Resume", "Resuming...", Command::Resume, false, Kind::Go, Icon::Play, 0, 0});
    add(stop);
    break;
  case View::PlateClear:
    // Says what holding it does: with a job waiting to start by itself, it starts that job.
    add({next_starts_by_itself(s) ? "Hold: plate is clear,\nstart the next print" : "Hold when plate is clear",
         "Clearing...", Command::ClearPlate, true, Kind::Go, Icon::Check, 0, 0});
    break;
  case View::Error:
    add({"Clear error", "Clearing...", Command::ClearErrors, false, Kind::Neutral, Icon::None, 0, 0});
    if (s.held > 0) add(resumeQueue(false));
    break;
  case View::Ready:
    add({"Hold to start", "Starting...", Command::StartNext, true, Kind::Go, Icon::Play, s.nextId, 0});
    if (s.held > 0) add(resumeQueue(false));
    break;
  case View::QueueHeld:
    add(resumeQueue(true));
    break;
  default: // nothing to do here: Idle, the offline views, Verdict (which has its own buttons)
    break;
  }
  return n;
}

void format_duration(char *out, size_t size, int minutes)
{
  if (minutes < 0) minutes = 0;
  if (minutes >= 60) snprintf(out, size, "%dh %02dm", minutes / 60, minutes % 60);
  else snprintf(out, size, "%dm", minutes);
}

int progress_percent(const Snapshot &s)
{
  int p = (int)s.progress; // rounded down
  return p < 0 ? 0 : p > 100 ? 100 : p;
}

void format_left(char *out, size_t size, const Snapshot &s)
{
  char dur[16];
  format_duration(dur, sizeof(dur), s.remainingMin);
  snprintf(out, size, "%s left", dur); // always "left": a bare time could be time spent
}

void format_due(char *out, size_t size, const Task &t)
{
  if (!t.timed) {
    snprintf(out, size, "%s", t.level == 2 ? "Due now" : t.level == 1 ? "Due soon" : "Not due");
    return;
  }
  // Print hours and calendar days are different clocks; say which.
  float a = t.left < 0 ? -t.left : t.left;
  int n = (int)(a + 0.5f);
  const char *unit = t.days ? (n == 1 ? "day" : "days") : (n == 1 ? "print hour" : "print hours");
  if (a < 1 || n == 0) snprintf(out, size, "%s", t.level > 0 || t.left <= 0 ? "Due now" : t.days ? "Due today" : "Due within the hour");
  else if (t.left < 0) snprintf(out, size, "Overdue by %d %s", n, unit);
  else snprintf(out, size, "Due in %d %s", n, unit);
}

static int rounded(float f) { return (int)(f < 0 ? f - 0.5f : f + 0.5f); }

void format_temps(char *out, size_t size, const Snapshot &s)
{
  if (!s.connected) {
    out[0] = 0;
    return;
  }
  // Printers without a chamber sensor report 0; leave the chamber out for them.
  if (rounded(s.chamber) > 0)
    snprintf(out, size, "Nozzle %d\xC2\xB0   Bed %d\xC2\xB0   Chamber %d\xC2\xB0", rounded(s.nozzle), rounded(s.bed),
             rounded(s.chamber));
  else
    snprintf(out, size, "Nozzle %d\xC2\xB0   Bed %d\xC2\xB0", rounded(s.nozzle), rounded(s.bed));
}

// The count goes first, where it can't be cut off by a long name.
void format_next(char *out, size_t size, const Snapshot &s)
{
  if (s.nextId == 0) {
    out[0] = 0;
    return;
  }
  char dur[16];
  format_duration(dur, sizeof(dur), s.nextSeconds / 60);
  if (s.queued > 1) snprintf(out, size, "Next of %d (%s): %s", s.queued, dur, s.nextName);
  else snprintf(out, size, "Next (%s): %s", dur, s.nextName);
}

void format_lost(char *out, size_t size, const Snapshot &s)
{
  if (s.lostSec < 90) snprintf(out, size, "No answer from Bambuddy for %d s", s.lostSec);
  else snprintf(out, size, "No answer from Bambuddy for %d min", s.lostSec / 60);
}

// As Bambuddy and the printer's own screen name them: AMS A to D with trays 1 to 4, an AMS HT
// by its letter, and the external holder or holders.
void format_slot(char *out, size_t size, const Slot &slot)
{
  if (slot.ams == EXTERNAL_AMS) snprintf(out, size, slot.tray == 0 ? "Ext" : "Ext %d", slot.tray + 1);
  else if (slot.ams >= 128) snprintf(out, size, "HT-%c", 'A' + (slot.ams - 128) % 26);
  else snprintf(out, size, "%c%d", 'A' + slot.ams % 26, slot.tray + 1);
}

void format_remaining(char *out, size_t size, int grams)
{
  if (grams < 0) out[0] = 0;
  else if (grams == 0) snprintf(out, size, "Empty");
  else snprintf(out, size, "%d g left", grams);
}

const Assignment *assignment_of(const Snapshot &s, const Slot &slot)
{
  for (int i = 0; i < s.assignedCount; i++)
    if (s.assigned[i].ams == slot.ams && s.assigned[i].tray == slot.tray) return &s.assigned[i];
  return nullptr;
}
