// Stand-in for the Arduino header when building the desktop simulator.
// Only what ui.cpp and the shared headers need.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

uint32_t millis();
