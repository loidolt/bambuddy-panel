// Bambuddy's spool inventory, as much of it as the panel needs to offer a spool for a slot.
// No network code here: the panel, the simulator and the unit tests share it.
#pragma once
#include <ArduinoJson.h>
#include <cstddef>
#include <cstdint>

static const int SPOOL_MATERIAL = 12;
static const int MAX_SPOOLS = 150; // more than that are left out, last in Bambuddy's order

struct Spool {
  int id = 0;
  char material[SPOOL_MATERIAL] = ""; // "PETG"
  char name[36] = "";                 // brand and colour: "Bambu Lab Black"
  uint32_t rgb = 0;
  int remainG = -1;                   // grams left; -1 when Bambuddy doesn't know
};

enum class SpoolState : uint8_t { None, Loading, Ready, Failed };

// The list. `items` is allocated the first time a list is read, and kept.
struct SpoolStore {
  Spool *items = nullptr;
  int count = 0;
  int total = 0;      // how many Bambuddy has, which can be more than were kept
  SpoolState state = SpoolState::None;
  int code = 0;       // the HTTP code or client error when Failed
};

void spools_filter(JsonDocument &filter);
// GET /inventory/spools. False (and nothing changed) when there is no memory for the list.
bool parse_spools(JsonDocument &doc, SpoolStore &st);
// Copies `from` into `to`, which gets its own memory.
bool spools_copy(const SpoolStore &from, SpoolStore &to);

// Up to `max` spools of `material` (empty: all), starting at the `offset`-th such. Returns how
// many it gave; `total` is how many of that material there are.
int spools_page(const SpoolStore &st, const char *material, int offset, Spool *out, int max, int &total);
// The different materials in the list, in the order they first come. Returns how many.
int spools_materials(const SpoolStore &st, char out[][SPOOL_MATERIAL], int max);

// "Bambu Lab Black", from the parts of a spool Bambuddy sends (any may be missing).
void spool_name(char *out, size_t size, const char *brand, const char *subtype, const char *color);
// A colour as Bambuddy sends it, "RRGGBBAA" or "RRGGBB" with or without #. 0 when unreadable.
uint32_t parse_rgb(const char *hex);
