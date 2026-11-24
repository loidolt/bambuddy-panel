#include "settings.h"
#include <Preferences.h>

Settings settings;

static const char *NS = "panel";

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
  settings.url.trim();
  settings.key.trim();
  while (settings.url.endsWith("/")) settings.url.remove(settings.url.length() - 1);

  Preferences p;
  p.begin(NS, false);
  p.putString("url", settings.url);
  p.putString("key", settings.key);
  p.putInt("printer", settings.printerId);
  p.end();
}

bool settings_load_touch(TouchCal &c)
{
  Preferences p;
  p.begin(NS, true);
  bool have = p.getBool("tcal", false);
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
  p.putBool("tcal", true);
  p.end();
}
