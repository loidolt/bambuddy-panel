#include "bambuddy.h"
#include "bambuddy_api.h"
#include "poller.h"
#include "settings.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>

static const uint32_t POLL_MS = 4000;
static const uint32_t HEALTH_LOG_MS = 60000;
static const uint32_t SETTLE_MS = 400; // give the printer a moment after a command before re-reading

static Snapshot shared;
static SemaphoreHandle_t lock;
static QueueHandle_t commands;
static Poller poller;
static Snapshot local; // worked on by the background task, then published in one go
static bool commandRan; // since the round of polls began: what it read may be from before
// The spool list: the task's own, and the copy the screen reads rows from (under `lock`).
static SpoolStore taskSpools, sharedSpools;

static void publish_spools()
{
  xSemaphoreTake(lock, portMAX_DELAY);
  spools_copy(taskSpools, sharedSpools);
  xSemaphoreGive(lock);
}

static bool begin_request(HTTPClient &http, WiFiClient &client, const char *path)
{
  String url = settings.url + path;
  if (!http.begin(client, url)) return false;
  http.useHTTP10(true); // no chunked encoding, so the body can be parsed straight off the socket
  http.setConnectTimeout(3000);
  http.setTimeout(5000);
  http.addHeader("X-API-Key", settings.key);
  http.addHeader("Accept", "application/json");
  return true;
}

static int precheck()
{
  if (WiFi.status() != WL_CONNECTED) return ERR_NO_WIFI;
  if (settings.url.startsWith("https://")) return ERR_HTTPS;
  return 0;
}

static void publish_toast()
{
  xSemaphoreTake(lock, portMAX_DELAY);
  memcpy(shared.toast, local.toast, sizeof(shared.toast));
  shared.toastBad = local.toastBad;
  shared.toastSeq = local.toastSeq;
  xSemaphoreGive(lock);
}

static int send_request(const Request &r)
{
  if (int err = precheck()) return err;
  WiFiClient client;
  HTTPClient http;
  if (!begin_request(http, client, r.path)) return ERR_BAD_URL;
  if (r.body[0]) http.addHeader("Content-Type", "application/json");
  int code = http.sendRequest(r.method, (uint8_t *)r.body, strlen(r.body));
  http.end();
  return code;
}

static void run(const Job &job)
{
  if (job.cmd == Command::LoadSpools) { // nothing to send: the next round of polls reads the list
    poller_after_command(poller, job, 0);
    publish_spools(); // "loading", at once
    commandRan = true;
    return;
  }
  Request r;
  int code = command_request(job, settings.printerId, r) ? send_request(r) : 0;
  local.toastBad = !command_result(job, code, local.toast, sizeof(local.toast));
  local.toastSeq++;
  commandRan = true;
  poller_after_command(poller, job, code);
  publish_toast(); // at once, not after the next round of polls
  Serial.printf("[bambuddy] %s %s -> %d\n", r.method, r.path, code);
}

// Runs whatever the UI has asked for, without waiting. Called between requests, so a command
// never waits for a whole round of polls.
static bool drain()
{
  bool any = false;
  Job job;
  while (xQueueReceive(commands, &job, 0) == pdTRUE) {
    run(job);
    any = true;
  }
  return any;
}

static int fetch(const char *path, JsonDocument &doc, const JsonDocument &filter, void *)
{
  drain();
  if (int err = precheck()) return err;
  WiFiClient client;
  HTTPClient http;
  if (!begin_request(http, client, path)) return ERR_BAD_URL;
  int code = http.GET();
  if (code == 200) {
    DeserializationError err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    if (err) code = ERR_BAD_JSON;
  }
  http.end();
  return code;
}

static void task(void *)
{
  uint32_t healthAt = 0;
  for (;;) {
    // Sleeps for a poll interval, or wakes at once when the UI sends a command. A command that
    // ran between the requests of the last round left that round out of date: read again soon.
    Job job;
    uint32_t wait = local.seq == 0 ? 1 : commandRan ? SETTLE_MS : POLL_MS;
    if (xQueueReceive(commands, &job, pdMS_TO_TICKS(wait)) == pdTRUE) {
      run(job);
      drain();
      vTaskDelay(pdMS_TO_TICKS(SETTLE_MS));
    }

    commandRan = false;
    bool readingSpools = poller.spoolsWanted;
    poll_once(poller, local, fetch, nullptr);
    if (readingSpools) publish_spools();

    xSemaphoreTake(lock, portMAX_DELAY);
    shared = local;
    xSemaphoreGive(lock);

    if (millis() - healthAt >= HEALTH_LOG_MS) {
      healthAt = millis();
      Serial.printf("[health] stack left %u B, heap free %u B, lowest %u B, biggest block %u B\n",
                    (unsigned)uxTaskGetStackHighWaterMark(nullptr), (unsigned)ESP.getFreeHeap(),
                    (unsigned)ESP.getMinFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
    }
  }
}

void bambuddy_begin()
{
  poller.printerId = settings.printerId;
  poller.spools = &taskSpools;
  lock = xSemaphoreCreateMutex();
  commands = xQueueCreate(4, sizeof(Job));
  xTaskCreatePinnedToCore(task, "bambuddy", 8192, nullptr, 1, nullptr, 0);
}

bool bambuddy_send(Command c, int arg, int arg2)
{
  Job job = {c, arg, arg2};
  return commands && xQueueSend(commands, &job, 0) == pdTRUE;
}

int bambuddy_spools(const char *material, int offset, Spool *out, int max, int &total, SpoolState &state)
{
  total = 0;
  state = SpoolState::None;
  if (!lock) return 0;
  xSemaphoreTake(lock, portMAX_DELAY);
  state = sharedSpools.state;
  int n = spools_page(sharedSpools, material, offset, out, max, total);
  xSemaphoreGive(lock);
  return n;
}

int bambuddy_spool_materials(char out[][SPOOL_MATERIAL], int max)
{
  if (!lock) return 0;
  xSemaphoreTake(lock, portMAX_DELAY);
  int n = spools_materials(sharedSpools, out, max);
  xSemaphoreGive(lock);
  return n;
}

void bambuddy_snapshot(Snapshot &out)
{
  if (!lock) return;
  xSemaphoreTake(lock, portMAX_DELAY);
  out = shared;
  xSemaphoreGive(lock);
}
