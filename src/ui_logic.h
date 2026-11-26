// What the status screen shows and offers, worked out from a Snapshot. No LVGL in here, so the
// unit tests cover it; ui.cpp only draws what these functions decide.
#pragma once
#include <cstddef>
#include "bambuddy.h"

// What the screen is about right now; decides the words, the buttons and the LED.
enum class View {
  Offline,        // Bambuddy has not answered for a long while, or never has
  CheckSettings,  // Bambuddy said no, or the address can't work: waiting won't help
  PrinterOffline,
  Printing,       // and getting ready to (see preparing())
  Paused,
  Verdict,
  PlateClear,
  Error,
  Ready,
  QueueHeld,
  Idle,
};

// Choices made on this panel that the Snapshot doesn't carry.
struct ViewOptions {
  // The outcome question the person skipped; it stays away for that print.
  int skippedArchive = 0, skippedRun = 0;
};

// While Bambuddy is silent the last status stays on screen this long before the panel gives
// up on it and says it is offline.
static const int LINK_LOST_GIVE_UP_S = 300;

View pick_view(const Snapshot &s, const ViewOptions &o);
// Bambuddy has gone quiet but the last status is still being shown: nothing can be pressed.
bool link_lost(const Snapshot &s);
// A print is being got ready (heating, levelling), not printing yet.
bool preparing(const Snapshot &s);
// Clearing the plate will start the next job without anyone asking again.
bool next_starts_by_itself(const Snapshot &s);

// Colours by what they mean; ui.cpp has the values.
enum class Tone : uint8_t { Plain, Dim, Go, Neutral, Caution, Danger, Quiet };

// The words of a view. Empty strings are not drawn.
struct Face {
  const char *chip = "";
  Tone chipTone = Tone::Quiet;
  char headline[72] = "";
  Tone headlineTone = Tone::Plain;
  bool headlineIsName = false; // a file name: smaller type, may take two lines
  char sub[96] = "";
  Tone subTone = Tone::Dim;
};

void pick_face(View v, const Snapshot &s, Face &out);

enum class Kind : uint8_t { Go, Neutral, Stop, Quiet };
enum class Icon : uint8_t { None, Pause, Play, Stop, Check };

struct Action {
  const char *text;
  const char *pending; // while Bambuddy has the command and hasn't answered
  Command cmd;
  bool hold;           // must be held: it starts a print or can't be undone
  Kind kind;
  Icon icon;
  int arg, arg2;       // the ids as shown, sent with the command
};

static const int MAX_ACTIONS = 2;

// The buttons for a view, the main action first. Returns how many.
int pick_actions(View v, const Snapshot &s, Action out[MAX_ACTIONS]);

void format_duration(char *out, size_t size, int minutes);  // "1h 05m", "45m"
void format_left(char *out, size_t size, const Snapshot &s); // "1h 23m left"
void format_due(char *out, size_t size, const Task &t);       // "Due in 8 print hours", "Overdue by 3 days"
void format_temps(char *out, size_t size, const Snapshot &s);  // empty when the printer is off
void format_next(char *out, size_t size, const Snapshot &s);   // "Next: name (3h 12m), +2 more"
void format_lost(char *out, size_t size, const Snapshot &s);   // "No answer from Bambuddy for 40 s"

void format_slot(char *out, size_t size, const Slot &slot);     // "A2", "HT-A", "Ext"
// "640 g left", "Empty"; nothing when Bambuddy doesn't know.
void format_remaining(char *out, size_t size, int grams);
// The inventory spool Bambuddy has in a slot, or nullptr.
const Assignment *assignment_of(const Snapshot &s, const Slot &slot);

// The progress bar and percentage: whole percent, rounded down, so 99.6 never reads as done.
int progress_percent(const Snapshot &s);
