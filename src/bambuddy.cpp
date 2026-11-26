#include "bambuddy.h"
#include "bambuddy_api.h"
#include "poller.h"
#include "settings.h"
#include "http_json.h"
#include "ui_logic.h"
#include "version.h"

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
struct QueuedCommand {
  Job job;
  CommandTarget expected;
  uint32_t request, queuedAt;
};
// The spool list: the task's own, and the copy the screen reads rows from (under `lock`).
static SpoolStore taskSpools, sharedSpools;

static void publish_spools()
{
  xSemaphoreTake(lock, portMAX_DELAY);
  if (!spools_copy(taskSpools, sharedSpools)) {
    sharedSpools.state = SpoolState::Failed;
    sharedSpools.code = ERR_NO_MEMORY;
  }
  xSemaphoreGive(lock);
}

static bool begin_request(HTTPClient &http, WiFiClient &client, const char *path)
{
  String url = settings.url + path;
  if (!http.begin(client, url)) return false;
  http.useHTTP10(true); // no chunked encoding, so the body can be parsed straight off the socket
  http.setConnectTimeout(3000);
  http.setTimeout(5000);
  http.setUserAgent(FW_USER_AGENT); // so Bambuddy's log says which firmware asked
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
  shared.answeredRequest = local.answeredRequest;
  xSemaphoreGive(lock);
}

static int send_request(const Job &job, const Request &r, CommandReply &reply)
{
  if (int err = precheck()) return err;
  ResponseClient client;
  HTTPClient http;
  if (!begin_request(http, client, r.path)) return ERR_BAD_URL;
  if (r.body[0]) http.addHeader("Content-Type", "application/json");
  int code = http.sendRequest(r.method, (uint8_t *)r.body, strlen(r.body));
  if (client.exceeded()) code = ERR_BAD_JSON;
  if ((code == 409 && job.cmd == Command::StartNext) ||
      (code == 200 && (job.cmd == Command::AssignSpool || job.cmd == Command::ResumeQueue)) ||
      (code == 201 && job.cmd == Command::AssignSpool)) {
    JsonDocument filter;
    filter["detail"]["code"] = true;
    for (const char *key : {"pending_config", "restored", "acknowledged"}) filter[key] = true;
    ResponseDocument doc;
    if (read_json(http, doc, filter) == 200) {
      snprintf(reply.conflict, sizeof(reply.conflict), "%s", doc["detail"]["code"] | "");
      reply.configurationPending = doc["pending_config"] | false;
      reply.noQueueChanges = (doc["restored"] | -1) == 0 && (doc["acknowledged"] | -1) == 0;
    }
  }
  http.end();
  return code;
}

static int fetch(const char *path, JsonDocument &doc, const JsonDocument &filter, void *)
{
  if (int err = precheck()) return err;
  ResponseClient client;
  HTTPClient http;
  if (!begin_request(http, client, path)) return ERR_BAD_URL;
  int code = http.GET();
  if (client.exceeded()) code = ERR_BAD_JSON;
  if (code == 200) code = read_json(http, doc, filter);
  http.end();
  return code;
}

static void process_command(const QueuedCommand &command)
{
  const Job &job = command.job;
  // Read the relevant records again before acting. Only this task owns the HTTP client.
  if (job.cmd == Command::PerformTask) poller.tasksStale = true;
  if (job.cmd == Command::AssignSpool || job.cmd == Command::UnassignSpool) poller.assignedStale = true;
  if (job.cmd == Command::ClearPlate || job.cmd == Command::StartNext) poller.settingsStale = true;
  poll_once(poller, local, fetch, nullptr);
  const CommandTarget current = command_target(job, local);
  int code = ERR_STALE;
  Request r;
  CommandReply reply;
  if (current.printer && current == command.expected && millis() - command.queuedAt < 20000 &&
      command_request(job, settings.printerId, r)) {
    code = send_request(job, r, reply);
    Serial.printf("[bambuddy] %s %s -> %d\n", r.method, r.path, code);
  }
  local.toastBad = !command_result(job, code, local.toast, sizeof(local.toast), reply);
  local.answeredRequest = command.request;
  ++local.toastSeq;
  publish_toast();
  poller_after_command(poller, job, code);
  vTaskDelay(pdMS_TO_TICKS(SETTLE_MS));
}

static void task(void *)
{
  uint32_t healthAt = 0;
  for (;;) {
    QueuedCommand command;
    bool received = xQueueReceive(commands, &command, local.seq ? pdMS_TO_TICKS(POLL_MS) : 0) == pdTRUE;
    bool loadSpools = received && command.job.cmd == Command::LoadSpools;
    if (loadSpools) {
      poller_after_command(poller, command.job, 0);
      publish_spools();
    } else if (received) {
      process_command(command);
    }
    poll_once(poller, local, fetch, nullptr);
    if (loadSpools) publish_spools();
    else if (received) local.settledRequest = command.request;

    xSemaphoreTake(lock, portMAX_DELAY);
    shared = local;
    xSemaphoreGive(lock);

    if (millis() - healthAt >= HEALTH_LOG_MS) {
      healthAt = millis();
      Serial.printf("[health] fw %s, stack left %u B, heap free %u B, lowest %u B, biggest block %u B\n",
                    FW_VERSION, (unsigned)uxTaskGetStackHighWaterMark(nullptr), (unsigned)ESP.getFreeHeap(),
                    (unsigned)ESP.getMinFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
    }
  }
}

bool bambuddy_begin()
{
  poller.printerId = settings.printerId;
  poller.spools = &taskSpools;
  lock = xSemaphoreCreateMutex();
  commands = xQueueCreate(4, sizeof(QueuedCommand));
  if (lock && commands && xTaskCreatePinnedToCore(task, "bambuddy", 8192, nullptr, 1, nullptr, 0) == pdPASS)
    return true;
  if (commands) vQueueDelete(commands);
  if (lock) vSemaphoreDelete(lock);
  commands = nullptr;
  lock = nullptr;
  return false;
}

bool bambuddy_send(Command c, int arg, int arg2, CommandTarget target, uint32_t request)
{
  QueuedCommand queued = {{c, arg, arg2}, target, request, millis()};
  return commands && xQueueSend(commands, &queued, 0) == pdTRUE;
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
