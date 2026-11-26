#include "settings.h"
#include <Preferences.h>
#include <ArduinoJson.h>

Settings settings;

static const char *NS = "panel";

// A touch calibration is only right for the rotation it was made in.
#ifdef PANEL_ROTATION
static const uint8_t ROTATION = PANEL_ROTATION;
#else
static const uint8_t ROTATION = 0;
#endif

void settings_load()
{
  Preferences p;
  if (!p.begin(NS, true)) return;
  if (p.isKey("config-v1")) {
    JsonDocument record;
    if (!deserializeJson(record, p.getString("config-v1")) &&
        record["url"].is<const char *>() && record["key"].is<const char *>() && record["printer"].is<int>()) {
      settings.url = record["url"].as<String>();
      settings.key = record["key"].as<String>();
      settings.printerId = record["printer"];
    }
    return;
  }
  settings.url = p.getString("url", "");
  settings.key = p.getString("key", "");
  settings.printerId = p.getInt("printer", 0);
  p.end();
  if (settings.complete()) settings_save(settings);
}

// One NVS commit binds the credential to its destination, including after a power loss.
bool settings_save(const Settings &candidate)
{
  if (!candidate.complete()) return false;
  JsonDocument record;
  record["url"] = candidate.url;
  record["key"] = candidate.key;
  record["printer"] = candidate.printerId;
  String encoded;
  if (record.overflowed() || serializeJson(record, encoded) != measureJson(record)) return false;
  Preferences p;
  if (!p.begin(NS, false) || p.putString("config-v1", encoded) != encoded.length()) return false;
  settings = candidate;
  for (auto key : {"url", "key", "printer"}) if (p.isKey(key)) p.remove(key);
  return true;
}

void settings_request_setup()
{
  Preferences p;
  p.begin(NS, false);
  p.putBool("setup", true);
  p.end();
}

bool settings_take_setup_request()
{
  Preferences p;
  p.begin(NS, false);
  bool asked = p.getBool("setup", false);
  if (asked) p.remove("setup");
  p.end();
  return asked;
}

bool settings_load_touch(TouchCal &c)
{
  Preferences p;
  p.begin(NS, true);
  bool have = p.getBool("tcal", false) && p.getUChar("trot", 0xFF) == ROTATION;
  if (have) {
    c.x0 = p.getShort("tx0", 0);
    c.x1 = p.getShort("tx1", 0);
    c.y0 = p.getShort("ty0", 0);
    c.y1 = p.getShort("ty1", 0);
    c.swap = p.getBool("tswap", false);
  }
  p.end();
  return have;
}

void settings_save_touch(const TouchCal &c)
{
  Preferences p;
  p.begin(NS, false);
  p.putShort("tx0", c.x0);
  p.putShort("tx1", c.x1);
  p.putShort("ty0", c.y0);
  p.putShort("ty1", c.y1);
  p.putBool("tswap", c.swap);
  p.putUChar("trot", ROTATION);
  p.putBool("tcal", true);
  p.end();
}
