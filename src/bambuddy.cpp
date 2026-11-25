#include "bambuddy.h"
#include "bambuddy_api.h"
#include "poller.h"
#include "settings.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>

static const uint32_t POLL_MS = 4000;
static const uint32_t HEALTH_LOG_MS = 60000;

static Snapshot shared;
static SemaphoreHandle_t lock;
static QueueHandle_t commands;
static Poller poller;
static Snapshot local; // worked on by the background task, then published in one go

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
  Request r;
  int code = command_request(job, settings.printerId, r) ? send_request(r) : 0;
  command_result(job, code, local.toast, sizeof(local.toast));
  local.toastSeq++;
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
    // Sleeps for a poll interval, or wakes at once when the UI sends a command.
    Job job;
    if (xQueueReceive(commands, &job, pdMS_TO_TICKS(local.seq == 0 ? 1 : POLL_MS)) == pdTRUE) {
      run(job);
      drain();
      vTaskDelay(pdMS_TO_TICKS(400)); // give the printer a moment before re-reading
    }

    poll_once(poller, local, fetch, nullptr);

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
  lock = xSemaphoreCreateMutex();
  commands = xQueueCreate(4, sizeof(Job));
  xTaskCreatePinnedToCore(task, "bambuddy", 8192, nullptr, 1, nullptr, 0);
}

bool bambuddy_send(Command c, int arg, int arg2)
{
  Job job = {c, arg, arg2};
  return commands && xQueueSend(commands, &job, 0) == pdTRUE;
}

void bambuddy_snapshot(Snapshot &out)
{
  if (!lock) return;
  xSemaphoreTake(lock, portMAX_DELAY);
  out = shared;
  xSemaphoreGive(lock);
}
