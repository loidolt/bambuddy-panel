#include "bambuddy_parse.h"

#include <climits>
#include <cmath>
#include <cstring>

const char *const QUEUE_GATE_MESSAGE = "Previous print failed or was aborted";

// ASCII for U+00C0..U+017F (Latin-1 Supplement letters and Latin Extended-A), two characters
// per code point; a space in the second place means "one character".
static const char LATIN[] =
    "A A A A A A AEC E E E E I I I I D N O O O O O x O U U U U Y THss" // C0-DF
    "a a a a a a aec e e e e i i i i d n o o o o o / o u u u u y thy " // E0-FF
    "A a A a A a C c C c C c C c D d D d E e E e E e E e E e G g G g " // 100-11F
    "G g G g H h H h I i I i I i I i I i IJijJ j K k k L l L l L l L " // 120-13F
    "l L l N n N n N n n N n O o O o O o OEoeR r R r R r S s S s S s " // 140-15F
    "S s T t T t T t U u U u U u U u U u U u W w Y y Y Z z Z z Z z s "; // 160-17F

void copy_ascii(char *dst, size_t size, const char *src)
{
  if (!size) return;
  size_t o = 0;
  const unsigned char *p = (const unsigned char *)(src ? src : "");
  while (*p) {
    char out[3] = {0, 0, 0};
    if (*p < 0x80) {
      out[0] = (*p >= 0x20 && *p != 0x7F) ? (char)*p : ' ';
      p++;
    } else {
      // Decode one UTF-8 sequence; a broken one counts as one unknown character.
      int len = (*p & 0xE0) == 0xC0 ? 2 : (*p & 0xF0) == 0xE0 ? 3 : (*p & 0xF8) == 0xF0 ? 4 : 1;
      uint32_t cp = len == 2 ? (*p & 0x1F) : len == 3 ? (*p & 0x0F) : (*p & 0x07);
      int i = 1;
      for (; i < len && (p[i] & 0xC0) == 0x80; i++) cp = (cp << 6) | (p[i] & 0x3F);
      if (i < len) len = i; // cut short: skip what was there
      p += len;
      if (len == 1) cp = 0;
      if (cp >= 0xC0 && cp <= 0x17F) {
        const char *pair = LATIN + (cp - 0xC0) * 2;
        out[0] = pair[0];
        if (pair[1] != ' ') out[1] = pair[1];
      } else if (cp == 0xA0) {
        out[0] = ' ';
      } else if (cp == 0x2018 || cp == 0x2019) {
        out[0] = '\'';
      } else if (cp == 0x201C || cp == 0x201D) {
        out[0] = '"';
      } else if (cp == 0x2013 || cp == 0x2014) {
        out[0] = '-';
      } else {
        out[0] = '?';
      }
    }
    size_t n = strlen(out);
    if (o + n >= size) break; // whole replacements only
    memcpy(dst + o, out, n);
    o += n;
  }
  dst[o] = 0;
}

// "Dragon_plate_1.gcode.3mf" -> "Dragon_plate_1": the extension only costs screen space.
static void strip_extension(char *name)
{
  for (const char *ext : {".gcode.3mf", ".3mf", ".gcode"}) {
    size_t n = strlen(name), e = strlen(ext);
    if (n > e && strcmp(name + n - e, ext) == 0) {
      name[n - e] = 0;
      return;
    }
  }
}

// A number from the JSON, whether sent as an integer or a float; `fallback` for anything else
// (null, a string, NaN, infinity), and never outside [lo, hi].
static float number(JsonVariantConst v, float fallback, float lo, float hi)
{
  if (!v.is<float>()) return fallback;
  float f = v.as<float>();
  if (!std::isfinite(f)) return fallback;
  return f < lo ? lo : f > hi ? hi : f;
}

static int whole(JsonVariantConst v, int lo, int hi) { return (int)number(v, 0, (float)lo, (float)hi); }

void status_filter(JsonDocument &filter)
{
  for (const char *k : {"name", "connected", "state", "current_print", "subtask_name", "progress",
                        "remaining_time", "layer_num", "total_layers", "chamber_light",
                        "awaiting_plate_clear", "stg_cur_name"})
    filter[k] = true;
  filter["temperatures"]["nozzle"] = true;
  filter["temperatures"]["bed"] = true;
  filter["temperatures"]["chamber"] = true;
  filter["hms_errors"][0]["description"] = true;
  filter["hms_errors"][0]["full_code"] = true;
}

void parse_status(JsonDocument &doc, Snapshot &s)
{
  s.connected = doc["connected"] | false;
  copy_ascii(s.name, sizeof(s.name), doc["name"]);
  copy_ascii(s.state, sizeof(s.state), doc["state"]);
  const char *job = doc["subtask_name"];
  if (!job || !*job) job = doc["current_print"];
  copy_ascii(s.job, sizeof(s.job), job);
  strip_extension(s.job);
  copy_ascii(s.stage, sizeof(s.stage), doc["stg_cur_name"]);
  s.progress = number(doc["progress"], 0, 0, 100);
  s.remainingMin = whole(doc["remaining_time"], 0, 100000);
  s.layer = whole(doc["layer_num"], 0, 1000000);
  s.totalLayers = whole(doc["total_layers"], 0, 1000000);
  s.nozzle = number(doc["temperatures"]["nozzle"], 0, -100, 1000);
  s.bed = number(doc["temperatures"]["bed"], 0, -100, 1000);
  s.chamber = number(doc["temperatures"]["chamber"], 0, -100, 1000);
  s.light = doc["chamber_light"] | false;
  s.awaitingPlateClear = doc["awaiting_plate_clear"] | false;

  JsonArray errors = doc["hms_errors"];
  s.errorCount = errors.size();
  s.error[0] = 0;
  if (s.errorCount > 0) {
    const char *text = errors[0]["description"];
    if (!text || !*text) text = errors[0]["full_code"];
    copy_ascii(s.error, sizeof(s.error), text);
  }
}

void queue_filter(JsonDocument &filter)
{
  for (const char *k : {"id", "position", "status", "printer_id", "manual_start", "archive_name",
                        "library_file_name", "print_time_seconds", "error_message"})
    filter[0][k] = true;
}

void parse_queue(JsonDocument &doc, int printerId, Snapshot &s)
{
  s.queued = 0;
  s.held = 0;
  s.nextId = 0;
  s.nextName[0] = 0;
  s.nextManual = false;
  s.nextAssigned = false;
  s.nextSeconds = 0;

  int best = INT_MAX;
  for (JsonObject item : doc.as<JsonArray>()) {
    const char *status = item["status"] | "pending";
    if (strcmp(status, "skipped") == 0) {
      const char *why = item["error_message"] | "";
      if (strcmp(why, QUEUE_GATE_MESSAGE) == 0) s.held++;
      continue;
    }
    if (strcmp(status, "pending") != 0) continue;
    s.queued++;
    int pos = whole(item["position"], INT_MIN / 2, INT_MAX / 2);
    if (pos >= best) continue;
    best = pos;
    s.nextId = item["id"] | 0;
    s.nextManual = item["manual_start"] | false;
    s.nextAssigned = (item["printer_id"] | 0) == printerId;
    s.nextSeconds = whole(item["print_time_seconds"], 0, 10000000);
    const char *name = item["library_file_name"];
    if (!name || !*name) name = item["archive_name"];
    copy_ascii(s.nextName, sizeof(s.nextName), name);
    strip_extension(s.nextName);
  }
}

void last_print_filter(JsonDocument &filter)
{
  for (const char *k : {"id", "printer_id", "run_id", "status", "confirm_requested", "user_verdict"})
    filter[0][k] = true;
}

void parse_last_print(JsonDocument &doc, int printerId, Snapshot &s)
{
  s.verdictPending = false;
  s.archiveId = 0;
  s.runId = 0;
  for (JsonObject a : doc.as<JsonArray>()) {
    if ((a["printer_id"] | 0) != printerId) continue;
    s.archiveId = a["id"] | 0;
    s.runId = a["run_id"] | 0;
    // As Bambuddy's own printer card: a finished print that asked, and nobody has answered yet
    // (here, in Bambuddy, or from a notification). Failed and cancelled prints are not asked.
    const char *status = a["status"] | "";
    s.verdictPending = strcmp(status, "completed") == 0 && (a["confirm_requested"] | false) &&
                       a["user_verdict"].isNull();
  }
}

void tasks_filter(JsonDocument &filter)
{
  for (const char *k : {"id", "maintenance_type_name", "enabled", "interval_type", "hours_until_due",
                        "days_until_due", "is_due", "is_warning"})
    filter["maintenance_items"][0][k] = true;
}

// Hours until due, for ordering; a day counts as 24 hours. Rough (print hours against calendar
// days), but it puts the list in the order a person expects.
static float hours_left(const Task &t) { return t.days ? t.left * 24 : t.left; }

// Due first, then due soon, then by time left.
static bool more_urgent(const Task &a, const Task &b)
{
  if (a.level != b.level) return a.level > b.level;
  return hours_left(a) < hours_left(b);
}

// Keeps the MAX_TASKS most urgent tasks, most urgent first.
void parse_tasks(JsonDocument &doc, Snapshot &s)
{
  s.tasksKnown = true;
  s.taskCount = s.tasksDue = s.tasksWarn = 0;

  for (JsonObject item : doc["maintenance_items"].as<JsonArray>()) {
    if (!(item["enabled"] | true)) continue;

    Task t;
    t.id = item["id"] | 0;
    copy_ascii(t.name, sizeof(t.name), item["maintenance_type_name"]);
    t.days = strcmp(item["interval_type"] | "hours", "days") == 0;
    t.left = t.days ? number(item["days_until_due"], 0, -100000, 100000) : number(item["hours_until_due"], 0, -100000, 100000);
    t.level = (item["is_due"] | false) ? 2 : (item["is_warning"] | false) ? 1 : 0;
    if (t.level == 2) s.tasksDue++;
    if (t.level == 1) s.tasksWarn++;

    // Insertion into the sorted list; what falls off the end is the least urgent.
    int at = s.taskCount;
    while (at > 0 && more_urgent(t, s.tasks[at - 1])) at--;
    if (at >= MAX_TASKS) continue;
    int last = s.taskCount < MAX_TASKS ? s.taskCount : MAX_TASKS - 1;
    for (int i = last; i > at; i--) s.tasks[i] = s.tasks[i - 1];
    s.tasks[at] = t;
    if (s.taskCount < MAX_TASKS) s.taskCount++;
  }
}

void settings_filter(JsonDocument &filter) { filter["require_plate_clear"] = true; }

void parse_settings(JsonDocument &doc, Snapshot &s)
{
  // Bambuddy's default is off; the setting may come as a bool or as the string "true".
  JsonVariant v = doc["require_plate_clear"];
  if (v.is<bool>()) s.requirePlateClear = v.as<bool>();
  else if (v.is<const char *>()) s.requirePlateClear = strcmp(v.as<const char *>(), "true") == 0;
  else s.requirePlateClear = false;
}
