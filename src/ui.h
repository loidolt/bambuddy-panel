// The panel's screens. One status screen whose buttons follow the printer's state,
// plus a plain message screen for boot and setup.
#pragma once
#include "bambuddy.h"

void ui_begin();
void ui_message(const char *title, const char *body); // full-screen text; draws at once
void ui_calibrate(int step, int total, int x, int y);  // a cross to tap at x,y; draws at once
void ui_update(const Snapshot &s);                    // call often; redraws only on change
