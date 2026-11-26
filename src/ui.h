// The panel's screens: a status screen whose words and buttons follow the printer's state, the
// maintenance screen (ui_tasks.cpp), message screens for start-up and setup, and touch
// calibration.
#pragma once
#include "bambuddy.h"

void ui_begin();

// A full-screen message; draws at once. `waiting` adds a spinner, which only turns while
// display_loop() is being called.
void ui_message(const char *title, const char *body, bool waiting = false);

// A message screen for something to join or open from a phone: a QR code, and beside it the
// same thing as text to type, in two labelled lines. Draws at once.
struct CodeScreen {
  const char *title = "";
  const char *qr = "";     // what the QR code holds
  const char *label1 = "", *value1 = "";
  const char *label2 = "", *value2 = "";
  const char *note = "";   // a last small line
};
void ui_code(const CodeScreen &c);

// Buttons along the bottom of the message or code screen now showing; nullptr for none. They
// only work while display_loop() is being called.
void ui_message_buttons(const char *a, const char *b = nullptr);
int ui_message_pressed(); // 1 or 2, once, after that button was tapped; else 0

void ui_calibrate(int step, int total, int x, int y); // a cross to tap at x,y; draws at once
void ui_update(const Snapshot &s);                    // call often; redraws only on change

// Restarts the panel, into its settings screen or into normal running. Not part of the UI:
// main.cpp has the panel's, the simulator its own.
void panel_restart(bool intoSettings);
