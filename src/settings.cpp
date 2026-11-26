#include "settings.h"
#include "bambuddy_api.h"
#include <Preferences.h>

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
  p.begin(NS, true);
  settings.url = p.getString("url", "");
  settings.key = p.getString("key", "");
  settings.printerId = p.getInt("printer", 0);
  p.end();
}

void settings_save()
{
  char url[128];
  snprintf(url, sizeof(url), "%s", settings.url.c_str());
  normalize_url(url, sizeof(url));
  settings.url = url;
  settings.key.trim();

  Preferences p;
  p.begin(NS, false);
  p.putString("url", settings.url);
  p.putString("key", settings.key);
  p.putInt("printer", settings.printerId);
  p.end();
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
