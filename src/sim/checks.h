// Looks over what is on the simulator's screen for things a person would trip on: text that
// was cut off or lies on top of something else, and buttons too small for a finger. Run on
// every screenshot (see sim_main.cpp); the findings go into checks.txt next to the pictures.
#pragma once
#include <string>

void checks_begin(int screenW, int screenH);
void checks_run(const char *shot); // records what it finds under the screenshot's name
// Draws a frame around every touch target, with its size, over the screen; and takes it away.
void checks_overlay_show();
void checks_overlay_hide();
bool checks_write(const std::string &path);
int checks_problems(); // findings that are problems, not just sizes for the record
