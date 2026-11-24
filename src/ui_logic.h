// What the status screen shows and offers, worked out from a Snapshot. No LVGL in here, so the
// unit tests cover it; ui.cpp only draws what these functions decide.
#pragma once
#include <cstddef>
#include "bambuddy.h"

// What the screen is about right now; decides the buttons and the LED.
enum class View { Offline, PrinterOffline, Printing, Paused, Verdict, PlateClear, Error, Ready, Idle };

// Choices made on this panel that the Snapshot doesn't carry.
struct ViewOptions {
  // The outcome question the person put off with "Later"; it stays away for that print.
  int laterArchive = 0, laterRun = 0;
};

View pick_view(const Snapshot &s, const ViewOptions &o);

enum class Look : uint8_t { Amber, Red, Green, Blue, LightOn, LightOff };

struct Action {
  const char *text;
  Command cmd;
  bool hold; // must be held: anything that is costly to press by accident
  Look look;
  int arg, arg2; // the ids as shown, sent with the command
};

static const int MAX_ACTIONS = 3;

// The buttons for a view, the main action first. Returns how many.
int pick_actions(View v, const Snapshot &s, Action out[MAX_ACTIONS]);

void format_duration(char *out, size_t size, int minutes);  // "1h 05m", "45m"
void format_progress(char *out, size_t size, const Snapshot &s); // "42%   1h 23m left"
void format_due(char *out, size_t size, const Task &t);       // "Due in 8 h", "Overdue by 3 days"
void format_temps(char *out, size_t size, const Snapshot &s);  // empty when the printer is off

// The progress bar and percentage: whole percent, rounded down, so 99.6 never reads as done.
int progress_percent(const Snapshot &s);
