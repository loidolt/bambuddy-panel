// Per-device settings, kept in flash (NVS) and entered through the setup portal.
#pragma once
#include <Arduino.h>
#include "touch_cal.h"

struct Settings {
  String url;        // Bambuddy base URL, e.g. http://192.168.1.50:3700
  String key;        // Bambuddy API key, scoped to this printer
  int printerId = 0; // Bambuddy printer id

  bool complete() const { return url.length() > 0 && key.length() > 0 && printerId > 0; }
};

extern Settings settings;

void settings_load();
void settings_save();

bool settings_load_touch(TouchCal &c); // false when the panel was never calibrated
void settings_save_touch(const TouchCal &c);
