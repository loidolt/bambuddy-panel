// Made-up printer states for the simulator, one per situation the screen must handle.
#pragma once
#include "bambuddy.h"

struct Scenario {
  const char *name; // also the screenshot file name
  Snapshot (*make)();
};

extern const Scenario scenarios[];
extern const int scenarioCount;
