#include "ui_logic.h"

#include <cstdio>
#include <cstring>

static bool is(const Snapshot &s, const char *state) { return strcmp(s.state, state) == 0; }

static bool active(const Snapshot &s)
{
  return is(s, "RUNNING") || is(s, "PREPARE") || is(s, "SLICING") || is(s, "PAUSE");
}

View pick_view(const Snapshot &s, const ViewOptions &o)
{
  if (!s.reachable) return View::Offline;
  if (s.connected && is(s, "PAUSE")) return View::Paused;
  if (s.connected && active(s)) return View::Printing;
  // Bambuddy-side questions come next. They don't need the printer: with Auto Power Off it is
  // switched off exactly when the plate wants clearing.
  bool later = s.archiveId == o.laterArchive && s.runId == o.laterRun;
  if (s.verdictPending && !later) return View::Verdict; // before the plate: clearing it can answer for you
  if (s.awaitingPlateClear && s.requirePlateClear) return View::PlateClear;
  if (!s.connected) return View::PrinterOffline;
  if (s.errorCount > 0) return View::Error;
  if (s.nextId != 0 && s.nextManual && s.nextAssigned) return View::Ready;
  return View::Idle;
}

int pick_actions(View v, const Snapshot &s, Action out[MAX_ACTIONS])
{
  int n = 0;
  auto add = [&](Action a) {
    if (n < MAX_ACTIONS) out[n++] = a;
  };
  // Shows the light's state, not the action: lit when on, grey when off. Sends the opposite.
  const Action light = {s.light ? "Light\nON" : "Light\nOFF", Command::SetLight, false,
                        s.light ? Look::LightOn : Look::LightOff, s.light ? 0 : 1, 0};
  const Action resumeQueue = {"Resume\nqueue", Command::ResumeQueue, false, Look::Blue, 0, 0};

  switch (v) {
  case View::Printing:
    add({"Pause", Command::Pause, false, Look::Amber, 0, 0});
    add({"Hold to stop", Command::Stop, true, Look::Red, 0, 0});
    add(light);
    break;
  case View::Paused:
    add({"Resume", Command::Resume, false, Look::Green, 0, 0});
    add({"Hold to stop", Command::Stop, true, Look::Red, 0, 0});
    add(light);
    break;
  case View::PlateClear:
    add({"Hold when plate\nis clear", Command::ClearPlate, true, Look::Green, 0, 0});
    if (s.connected) add(light); // a printer that is switched off has no light to switch
    break;
  case View::Error:
    add({"Clear error", Command::ClearErrors, false, Look::Amber, 0, 0});
    if (s.held > 0) add(resumeQueue);
    add(light);
    break;
  case View::Ready:
    add({"Hold to start", Command::StartNext, true, Look::Green, s.nextId, 0});
    if (s.held > 0) add(resumeQueue);
    add(light);
    break;
  case View::Idle:
    if (s.held > 0) add(resumeQueue);
    add(light);
    break;
  default: // Offline, PrinterOffline, Verdict (which has its own buttons)
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

void format_progress(char *out, size_t size, const Snapshot &s)
{
  char dur[16];
  format_duration(dur, sizeof(dur), s.remainingMin);
  // "left" is dropped for very long prints so the time itself always fits.
  snprintf(out, size, s.remainingMin >= 600 ? "%d%%   %s" : "%d%%   %s left", progress_percent(s), dur);
}

void format_due(char *out, size_t size, const Task &t)
{
  const char *unit = t.days ? (t.left >= 1.5f || t.left <= -1.5f ? "days" : "day") : "h";
  int n = (int)(t.left < 0 ? -t.left + 0.5f : t.left + 0.5f);
  if (t.level == 2 && (t.left > -1 || n == 0)) snprintf(out, size, "Due now");
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
