#include "bambuddy_parse.h"
#include "spools.h"

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

// As copy_ascii, for the first `len` bytes of `src`.
static void copy_ascii_n(char *dst, size_t size, const char *src, size_t len)
{
  if (!size) return;
  size_t o = 0;
  const unsigned char *p = (const unsigned char *)(src ? src : "");
  const unsigned char *end = p + (src ? len : 0);
  while (p < end && *p) {
    char out[3] = {0, 0, 0};
    if (*p < 0x80) {
      out[0] = (*p >= 0x20 && *p != 0x7F) ? (char)*p : ' ';
      p++;
    } else {
      // Decode one UTF-8 sequence; a broken one counts as one unknown character.
      int len = (*p & 0xE0) == 0xC0 ? 2 : (*p & 0xF0) == 0xE0 ? 3 : (*p & 0xF8) == 0xF0 ? 4 : 1;
      uint32_t cp = len == 2 ? (*p & 0x1F) : len == 3 ? (*p & 0x0F) : (*p & 0x07);
      int i = 1;
      for (; i < len && p + i < end && (p[i] & 0xC0) == 0x80; i++) cp = (cp << 6) | (p[i] & 0x3F);
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

void copy_ascii(char *dst, size_t size, const char *src) { copy_ascii_n(dst, size, src, src ? strlen(src) : 0); }

// A file name for the screen. "Dragon_plate_1.gcode.3mf" -> "Dragon_plate_1": the extension
// only costs screen space. It comes off before the name is cut to fit, so a long name never
// ends in half an extension.
static void copy_name(char *dst, size_t size, const char *src)
{
  size_t n = src ? strlen(src) : 0;
  for (const char *ext : {".gcode.3mf", ".3mf", ".gcode"}) {
    size_t e = strlen(ext);
    if (n > e && memcmp(src + n - e, ext, e) == 0) {
      n -= e;
      break;
    }
  }
  copy_ascii_n(dst, size, src, n);
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
                        "remaining_time", "layer_num", "total_layers", "awaiting_plate_clear",
                        "stg_cur_name"})
    filter[k] = true;
  filter["temperatures"]["nozzle"] = true;
  filter["temperatures"]["bed"] = true;
  filter["temperatures"]["chamber"] = true;
  for (const char *k : {"description", "full_code", "severity", "actions"}) filter["hms_errors"][0][k] = true;
  // The slots a spool can sit in: the trays of each AMS and the external holders.
  filter["ams"][0]["id"] = true;
  for (const char *k : {"id", "tray_type", "tray_color"}) {
    filter["ams"][0]["tray"][0][k] = true;
    filter["vt_tray"][0][k] = true;
  }
}

// Whether a fault counts as a problem: the rule of Bambuddy's own printer card
// (filterKnownHMSErrors) and of its notifications. The printer's list also holds notices such
// as "the top cover is open", which can stay for a whole print, and codes Bambu publishes no
// text for, such as the echo after a cancelled print; neither is an error to clear.
static bool hms_counts(JsonObjectConst e)
{
  int severity = e["severity"].is<int>() ? e["severity"].as<int>() : 1; // an older Bambuddy sends none
  if (severity < 1) return false;
  if (e["actions"].as<JsonArrayConst>().size() > 0) return true; // it offers buttons: always shown
  const char *text = e["description"] | "";
  const char *code = e["full_code"] | "";
  bool notice = severity == 3 && strlen(code) == 16; // an hms[] entry at "notification" level
  return *text && !notice;
}

static void parse_slots(JsonDocument &doc, Snapshot &s); // below

void parse_status(JsonDocument &doc, Snapshot &s)
{
  s.connected = doc["connected"] | false;
  copy_ascii(s.name, sizeof(s.name), doc["name"]);
  copy_ascii(s.state, sizeof(s.state), doc["state"]);
  const char *job = doc["subtask_name"];
  if (!job || !*job) job = doc["current_print"];
  copy_name(s.job, sizeof(s.job), job);
  copy_ascii(s.stage, sizeof(s.stage), doc["stg_cur_name"]);
  s.progress = number(doc["progress"], 0, 0, 100);
  s.remainingMin = whole(doc["remaining_time"], 0, 100000);
  s.layer = whole(doc["layer_num"], 0, 1000000);
  s.totalLayers = whole(doc["total_layers"], 0, 1000000);
  s.nozzle = number(doc["temperatures"]["nozzle"], 0, -100, 1000);
  s.bed = number(doc["temperatures"]["bed"], 0, -100, 1000);
  s.chamber = number(doc["temperatures"]["chamber"], 0, -100, 1000);
  s.awaitingPlateClear = doc["awaiting_plate_clear"] | false;
  parse_slots(doc, s);

  s.errorCount = 0;
  for (auto &e : s.errors) e[0] = 0;
  for (JsonObjectConst e : doc["hms_errors"].as<JsonArrayConst>()) {
    if (!hms_counts(e)) continue;
    int at = s.errorCount++;
    if (at >= MAX_ERRORS) continue; // counted, but only the first few can be read on the screen
    const char *text = e["description"];
    if (!text || !*text) text = e["full_code"];
    copy_ascii(s.errors[at], sizeof(s.errors[at]), text);
  }
}

// The printer's slots, AMS trays first, in the order Bambuddy sends them.
static void parse_slots(JsonDocument &doc, Snapshot &s)
{
  s.slotCount = 0;
  auto add = [&](int ams, int tray, JsonObjectConst t) {
    if (s.slotCount >= MAX_SLOTS || ams < 0 || ams > 255 || tray < 0 || tray > 15) return;
    Slot &slot = s.slots[s.slotCount++];
    slot = Slot();
    slot.ams = (uint8_t)ams;
    slot.tray = (uint8_t)tray;
    copy_ascii(slot.type, sizeof(slot.type), t["tray_type"] | "");
    slot.rgb = parse_rgb(t["tray_color"]);
  };
  for (JsonObjectConst unit : doc["ams"].as<JsonArrayConst>()) {
    int ams = unit["id"] | -1;
    for (JsonObjectConst t : unit["tray"].as<JsonArrayConst>()) add(ams, t["id"] | -1, t);
  }
  // The external holders come as trays 254 and 255; Bambuddy's assignments call them slot 0
  // and 1 of unit 255.
  for (JsonObjectConst t : doc["vt_tray"].as<JsonArrayConst>()) {
    int id = t["id"] | 254;
    if (id == 254 || id == 255) add(EXTERNAL_AMS, id - 254, t);
  }
}

void assignments_filter(JsonDocument &filter)
{
  for (const char *k : {"printer_id", "ams_id", "tray_id", "spool_id"}) filter[0][k] = true;
  for (const char *k : {"brand", "subtype", "color_name", "label_weight", "weight_used"}) filter[0]["spool"][k] = true;
}

void parse_assignments(JsonDocument &doc, int printerId, Snapshot &s)
{
  if (!doc.is<JsonArrayConst>()) return; // not the answer expected: keep what was known
  s.assignedKnown = true;
  s.assignedCount = 0;
  for (JsonObjectConst a : doc.as<JsonArrayConst>()) {
    if ((a["printer_id"] | printerId) != printerId || s.assignedCount >= MAX_SLOTS) continue;
    int ams = a["ams_id"] | -1, tray = a["tray_id"] | -1;
    if (ams < 0 || ams > 255 || tray < 0 || tray > 15) continue;
    Assignment &as = s.assigned[s.assignedCount++];
    as = Assignment();
    as.ams = (uint8_t)ams;
    as.tray = (uint8_t)tray;
    as.spoolId = a["spool_id"] | 0;
    JsonObjectConst sp = a["spool"];
    spool_name(as.name, sizeof(as.name), sp["brand"], sp["subtype"], sp["color_name"]);
    if (sp["label_weight"].is<float>() && sp["weight_used"].is<float>()) {
      float left = sp["label_weight"].as<float>() - sp["weight_used"].as<float>();
      as.remainG = left < 0 ? 0 : (int)(left + 0.5f);
    }
  }
}

void queue_filter(JsonDocument &filter)
{
  for (const char *k : {"id", "position", "status", "printer_id", "manual_start", "archive_name",
                        "library_file_name", "print_time_seconds", "error_message", "waiting_reason",
                        "filament_short", "created_by_username"})
    filter[0][k] = true;
}

// The file a queue item prints, as a name for the screen.
static const char *item_name(JsonObjectConst item)
{
  const char *name = item["library_file_name"];
  if (!name || !*name) name = item["archive_name"];
  return name;
}

void parse_queue(JsonDocument &doc, int printerId, Snapshot &s)
{
  s.queued = 0;
  s.held = 0;
  s.nextId = 0;
  s.nextName[0] = s.nextWhy[0] = s.nextBy[0] = s.jobBy[0] = 0;
  s.nextManual = false;
  s.nextAssigned = false;
  s.nextSeconds = 0;
  s.moreCount = 0;

  // The pending items with the lowest positions, in order: the next one and those after it.
  static const int KEEP = 1 + MAX_MORE;
  JsonObjectConst first[KEEP];
  int pos[KEEP], kept = 0;

  for (JsonObjectConst item : doc.as<JsonArrayConst>()) {
    const char *status = item["status"] | "pending";
    if (strcmp(status, "skipped") == 0) {
      const char *why = item["error_message"] | "";
      if (strcmp(why, QUEUE_GATE_MESSAGE) == 0) s.held++;
      continue;
    }
    if (strcmp(status, "printing") == 0) {
      // The print under way, when it came from the queue: who put it there.
      if ((item["printer_id"] | 0) == printerId) copy_ascii(s.jobBy, sizeof(s.jobBy), item["created_by_username"]);
      continue;
    }
    if (strcmp(status, "pending") != 0) continue;
    s.queued++;
    int p = whole(item["position"], INT_MIN / 2, INT_MAX / 2);
    int at = kept;
    while (at > 0 && p < pos[at - 1]) at--; // equal positions keep the order Bambuddy sent
    if (at >= KEEP) continue;
    int last = kept < KEEP ? kept : KEEP - 1;
    for (int i = last; i > at; i--) {
      first[i] = first[i - 1];
      pos[i] = pos[i - 1];
    }
    first[at] = item;
    pos[at] = p;
    if (kept < KEEP) kept++;
  }

  if (kept == 0) return;
  JsonObjectConst next = first[0];
  s.nextId = next["id"] | 0;
  s.nextManual = next["manual_start"] | false;
  s.nextAssigned = (next["printer_id"] | 0) == printerId;
  s.nextSeconds = whole(next["print_time_seconds"], 0, 10000000);
  copy_name(s.nextName, sizeof(s.nextName), item_name(next));
  copy_ascii(s.nextBy, sizeof(s.nextBy), next["created_by_username"]);
  copy_ascii(s.nextWhy, sizeof(s.nextWhy), next["waiting_reason"]);
  // Bambuddy held it for filament but gave no words for it: say that much.
  if (!s.nextWhy[0] && (next["filament_short"] | false)) copy_ascii(s.nextWhy, sizeof(s.nextWhy), "Not enough filament loaded");
  for (int i = 1; i < kept; i++) {
    QueuedJob &j = s.more[s.moreCount++];
    copy_name(j.name, sizeof(j.name), item_name(first[i]));
    j.seconds = whole(first[i]["print_time_seconds"], 0, 10000000);
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
// A task Bambuddy gave no time for goes last among its equals.
static float hours_left(const Task &t) { return !t.timed ? 1e9f : t.days ? t.left * 24 : t.left; }

// Due first, then due soon, then by time left.
static bool more_urgent(const Task &a, const Task &b)
{
  if (a.level != b.level) return a.level > b.level;
  return hours_left(a) < hours_left(b);
}

// Keeps the MAX_TASKS most urgent tasks, most urgent first.
void parse_tasks(JsonDocument &doc, Snapshot &s)
{
  JsonArray items = doc["maintenance_items"];
  if (items.isNull()) return; // not the answer expected: keep what was known
  s.tasksKnown = true;
  s.taskCount = s.tasksDue = s.tasksWarn = 0;

  for (JsonObject item : items) {
    if (!(item["enabled"] | true)) continue;

    Task t;
    t.id = item["id"] | 0;
    copy_ascii(t.name, sizeof(t.name), item["maintenance_type_name"]);
    t.days = strcmp(item["interval_type"] | "hours", "days") == 0;
    JsonVariantConst left = item[t.days ? "days_until_due" : "hours_until_due"];
    t.timed = left.is<float>();
    t.left = number(left, 0, -100000, 100000);
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
