#include "bambuddy.h"
#include "bambuddy_parse.h"
#include "settings.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>

static const uint32_t POLL_MS = 4000;
static const uint32_t TASKS_POLL_MS = 60000; // maintenance changes slowly

struct Job {
  Command cmd;
  int arg;
};

static uint32_t tasksPolledAt = 0;
static bool tasksStale = true;

static Snapshot shared;
static SemaphoreHandle_t lock;
static QueueHandle_t commands;

static String api(const String &path) { return settings.url + "/api/v1" + path; }

static void prepare(HTTPClient &http)
{
  http.useHTTP10(true); // no chunked encoding, so the body can be parsed straight off the socket
  http.setConnectTimeout(3000);
  http.setTimeout(5000);
  http.addHeader("X-API-Key", settings.key);
  http.addHeader("Accept", "application/json");
}

// GET a JSON document, keeping only the fields named in `filter` (the full status is far
// larger than this board should hold). Returns the HTTP code; `doc` is valid on 200.
static int get_json(const String &path, JsonDocument &doc, const JsonDocument &filter)
{
  if (WiFi.status() != WL_CONNECTED) return -100;
  WiFiClient client;
  HTTPClient http;
  if (!http.begin(client, api(path))) return -101;
  prepare(http);
  int code = http.GET();
  if (code == 200) {
    DeserializationError err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    if (err) code = -102;
  }
  http.end();
  return code;
}

static int post(const String &path)
{
  if (WiFi.status() != WL_CONNECTED) return -100;
  WiFiClient client;
  HTTPClient http;
  if (!http.begin(client, api(path))) return -101;
  prepare(http);
  int code = http.POST("");
  http.end();
  return code;
}

static int send_json(const char *method, const String &path, const String &json)
{
  if (WiFi.status() != WL_CONNECTED) return -100;
  WiFiClient client;
  HTTPClient http;
  if (!http.begin(client, api(path))) return -101;
  prepare(http);
  http.addHeader("Content-Type", "application/json");
  int code = http.sendRequest(method, json);
  http.end();
  return code;
}

static void copy(char *dst, size_t size, const char *src) { strlcpy(dst, src ? src : "", size); }

static void poll_status(Snapshot &s)
{
  JsonDocument filter, doc;
  status_filter(filter);
  s.httpCode = get_json("/printers/" + String(settings.printerId) + "/status", doc, filter);
  s.reachable = s.httpCode == 200;
  if (s.reachable) parse_status(doc, s);
}

static void poll_queue(Snapshot &s)
{
  JsonDocument filter, doc;
  queue_filter(filter);
  int code = get_json("/queue/?status=pending&printer_id=" + String(settings.printerId), doc, filter);
  if (code == 200) parse_queue(doc, s); // else keep the last known queue; status decides "reachable"
}

// Is Bambuddy waiting for a good / bad answer on the last print? Not asked while a print is
// under way: the question belongs to a finished print.
static void poll_last_print(Snapshot &s)
{
  if (!strcmp(s.state, "RUNNING") || !strcmp(s.state, "PAUSE") || !strcmp(s.state, "PREPARE")) {
    s.verdictPending = false;
    return;
  }
  JsonDocument filter, doc;
  last_print_filter(filter);
  if (get_json("/archives/last-per-printer", doc, filter) == 200) parse_last_print(doc, settings.printerId, s);
}

static void poll_tasks(Snapshot &s)
{
  if (!tasksStale && millis() - tasksPolledAt < TASKS_POLL_MS) return;
  JsonDocument filter, doc;
  tasks_filter(filter);
  if (get_json("/maintenance/printers/" + String(settings.printerId), doc, filter) == 200) {
    parse_tasks(doc, s);
    tasksStale = false;
  }
  tasksPolledAt = millis(); // also after a failure: try again in a minute, not every poll
}

static int send_verdict(const Snapshot &s, const char *verdict)
{
  String body = String("{\"user_verdict\":\"") + verdict + "\",\"user_verdict_source\":\"api\"";
  if (s.runId) body += ",\"run_id\":" + String(s.runId);
  body += "}";
  return send_json("PATCH", "/archives/" + String(s.archiveId), body);
}

static void run(Job job, Snapshot &s)
{
  Command c = job.cmd;
  String printer = "/printers/" + String(settings.printerId);
  const char *label = "";
  int code = 0;
  switch (c) {
  case Command::Pause:       label = "Paused";        code = post(printer + "/print/pause"); break;
  case Command::Resume:      label = "Resumed";       code = post(printer + "/print/resume"); break;
  case Command::Stop:        label = "Stopped";       code = post(printer + "/print/stop"); break;
  case Command::ClearPlate:  label = "Plate cleared"; code = post(printer + "/clear-plate"); break;
  case Command::ClearErrors: label = "Errors cleared"; code = post(printer + "/hms/clear"); break;
  case Command::ResumeQueue: label = "Queue resumed"; code = post("/queue/printer/" + String(settings.printerId) + "/resume"); break;
  case Command::ToggleLight:
    label = s.light ? "Light off" : "Light on";
    code = post(printer + "/chamber-light?on=" + (s.light ? "false" : "true"));
    break;
  case Command::StartNext:
    if (s.nextId == 0) return;
    label = "Starting";
    code = post("/queue/" + String(s.nextId) + "/start");
    break;
  case Command::VerdictGood:
  case Command::VerdictReject:
    if (s.archiveId == 0) return;
    label = c == Command::VerdictGood ? "Marked good" : "Marked bad";
    code = send_verdict(s, c == Command::VerdictGood ? "good" : "reject");
    break;
  case Command::PerformTask:
    label = "Marked done";
    code = send_json("POST", "/maintenance/items/" + String(job.arg) + "/perform", "{}");
    tasksStale = true; // read the list again right away
    break;
  }

  if (code >= 200 && code < 300) copy(s.toast, sizeof(s.toast), label);
  else if (code == 401 || code == 403) copy(s.toast, sizeof(s.toast), "Not allowed (API key)");
  else snprintf(s.toast, sizeof(s.toast), "Failed (%d)", code);
  s.toastSeq++;
  Serial.printf("[bambuddy] command %d -> %d\n", (int)c, code);
}

static void task(void *)
{
  Snapshot local; // worked on here, then published in one go
  for (;;) {
    // Sleeps for a poll interval, or wakes at once when the UI sends a command.
    Job c;
    if (xQueueReceive(commands, &c, pdMS_TO_TICKS(local.seq == 0 ? 1 : POLL_MS)) == pdTRUE) {
      run(c, local);
      vTaskDelay(pdMS_TO_TICKS(400)); // give the printer a moment before re-reading
    }

    poll_status(local);
    if (local.reachable) poll_queue(local);
    if (local.reachable) poll_last_print(local);
    if (local.reachable) poll_tasks(local);
    local.seq++;

    xSemaphoreTake(lock, portMAX_DELAY);
    shared = local;
    xSemaphoreGive(lock);
  }
}

void bambuddy_begin()
{
  lock = xSemaphoreCreateMutex();
  commands = xQueueCreate(4, sizeof(Job));
  xTaskCreatePinnedToCore(task, "bambuddy", 8192, nullptr, 1, nullptr, 0);
}

void bambuddy_send(Command c, int arg)
{
  Job job = {c, arg};
  if (commands) xQueueSend(commands, &job, 0);
}

void bambuddy_snapshot(Snapshot &out)
{
  if (!lock) return;
  xSemaphoreTake(lock, portMAX_DELAY);
  out = shared;
  xSemaphoreGive(lock);
}
