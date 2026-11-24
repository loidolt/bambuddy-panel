#include "bambuddy_parse.h"

#include <climits>
#include <cstring>

static void copy(char *dst, size_t size, const char *src)
{
  snprintf(dst, size, "%s", src ? src : "");
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

void queue_filter(JsonDocument &filter)
{
  for (const char *k : {"id", "position", "manual_start", "archive_name", "library_file_name", "print_time_seconds"})
    filter[0][k] = true;
}

void parse_status(JsonDocument &doc, Snapshot &s)
{
  s.connected = doc["connected"] | false;
  copy(s.name, sizeof(s.name), doc["name"]);
  copy(s.state, sizeof(s.state), doc["state"]);
  const char *job = doc["subtask_name"];
  if (!job || !*job) job = doc["current_print"];
  copy(s.job, sizeof(s.job), job);
  strip_extension(s.job);
  copy(s.stage, sizeof(s.stage), doc["stg_cur_name"]);
  s.progress = doc["progress"] | 0.0f;
  s.remainingMin = doc["remaining_time"] | 0;
  s.layer = doc["layer_num"] | 0;
  s.totalLayers = doc["total_layers"] | 0;
  s.nozzle = doc["temperatures"]["nozzle"] | 0.0f;
  s.bed = doc["temperatures"]["bed"] | 0.0f;
  s.chamber = doc["temperatures"]["chamber"] | 0.0f;
  s.light = doc["chamber_light"] | false;
  s.awaitingPlateClear = doc["awaiting_plate_clear"] | false;

  JsonArray errors = doc["hms_errors"];
  s.errorCount = errors.size();
  s.error[0] = 0;
  if (s.errorCount > 0) {
    const char *text = errors[0]["description"];
    if (!text || !*text) text = errors[0]["full_code"];
    copy(s.error, sizeof(s.error), text);
  }
}

void parse_queue(JsonDocument &doc, Snapshot &s)
{
  JsonArray items = doc.as<JsonArray>();
  s.queued = items.size();
  s.nextId = 0;
  s.nextName[0] = 0;
  s.nextManual = false;
  s.nextSeconds = 0;

  int best = INT_MAX;
  for (JsonObject item : items) {
    int pos = item["position"] | 0;
    if (pos >= best) continue;
    best = pos;
    s.nextId = item["id"] | 0;
    s.nextManual = item["manual_start"] | false;
    s.nextSeconds = item["print_time_seconds"] | 0;
    const char *name = item["library_file_name"];
    if (!name || !*name) name = item["archive_name"];
    copy(s.nextName, sizeof(s.nextName), name);
    strip_extension(s.nextName);
  }
}

void last_print_filter(JsonDocument &filter)
{
  for (const char *k : {"id", "printer_id", "run_id", "confirm_requested", "user_verdict"})
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
    // Asked for, and nobody has answered yet (here, in Bambuddy, or from a notification).
    s.verdictPending = (a["confirm_requested"] | false) && a["user_verdict"].isNull();
  }
}

void tasks_filter(JsonDocument &filter)
{
  for (const char *k : {"id", "maintenance_type_name", "enabled", "interval_type", "hours_until_due",
                        "days_until_due", "is_due", "is_warning"})
    filter["maintenance_items"][0][k] = true;
}

// Keeps the MAX_TASKS most urgent tasks, most urgent first: due, then due soon, then by time
// left. A day counts as 24 hours for the ordering; rough (print hours against calendar days),
// but it puts the list in the order a person expects.
void parse_tasks(JsonDocument &doc, Snapshot &s)
{
  float keys[MAX_TASKS];
  s.tasksKnown = true;
  s.taskCount = s.tasksDue = s.tasksWarn = 0;

  for (JsonObject item : doc["maintenance_items"].as<JsonArray>()) {
    if (!(item["enabled"] | true)) continue;

    Task t;
    t.id = item["id"] | 0;
    copy(t.name, sizeof(t.name), item["maintenance_type_name"]);
    t.days = strcmp(item["interval_type"] | "hours", "days") == 0;
    t.left = t.days ? (item["days_until_due"] | 0.0f) : (item["hours_until_due"] | 0.0f);
    t.level = (item["is_due"] | false) ? 2 : (item["is_warning"] | false) ? 1 : 0;
    if (t.level == 2) s.tasksDue++;
    if (t.level == 1) s.tasksWarn++;

    float key = (2 - t.level) * 1e6f + (t.days ? t.left * 24 : t.left); // lower is more urgent

    int at = s.taskCount;
    while (at > 0 && keys[at - 1] > key) at--;
    if (at >= MAX_TASKS) continue;
    int last = s.taskCount < MAX_TASKS ? s.taskCount : MAX_TASKS - 1;
    for (int i = last; i > at; i--) {
      s.tasks[i] = s.tasks[i - 1];
      keys[i] = keys[i - 1];
    }
    s.tasks[at] = t;
    keys[at] = key;
    if (s.taskCount < MAX_TASKS) s.taskCount++;
  }
}
