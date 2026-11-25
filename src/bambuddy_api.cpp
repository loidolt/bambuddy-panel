#include "bambuddy_api.h"

#include <cctype>
#include <cstdio>
#include <cstring>

bool command_request(const Job &job, int printerId, Request &out)
{
  out = Request();
  const int p = printerId;
  switch (job.cmd) {
  case Command::Pause:       snprintf(out.path, sizeof(out.path), "/api/v1/printers/%d/print/pause", p); break;
  case Command::Resume:      snprintf(out.path, sizeof(out.path), "/api/v1/printers/%d/print/resume", p); break;
  case Command::Stop:        snprintf(out.path, sizeof(out.path), "/api/v1/printers/%d/print/stop", p); break;
  case Command::ClearPlate:  snprintf(out.path, sizeof(out.path), "/api/v1/printers/%d/clear-plate", p); break;
  case Command::ClearErrors: snprintf(out.path, sizeof(out.path), "/api/v1/printers/%d/hms/clear", p); break;
  case Command::ResumeQueue: snprintf(out.path, sizeof(out.path), "/api/v1/queue/printer/%d/resume", p); break;
  case Command::SetLight:
    snprintf(out.path, sizeof(out.path), "/api/v1/printers/%d/chamber-light?on=%s", p, job.arg ? "true" : "false");
    break;
  case Command::StartNext:
    if (job.arg <= 0) return false;
    snprintf(out.path, sizeof(out.path), "/api/v1/queue/%d/start", job.arg);
    break;
  case Command::VerdictGood:
  case Command::VerdictReject: {
    if (job.arg <= 0) return false;
    out.method = "PATCH";
    snprintf(out.path, sizeof(out.path), "/api/v1/archives/%d", job.arg);
    const char *verdict = job.cmd == Command::VerdictGood ? "good" : "reject";
    if (job.arg2 > 0)
      snprintf(out.body, sizeof(out.body), "{\"user_verdict\":\"%s\",\"user_verdict_source\":\"api\",\"run_id\":%d}",
               verdict, job.arg2);
    else
      snprintf(out.body, sizeof(out.body), "{\"user_verdict\":\"%s\",\"user_verdict_source\":\"api\"}", verdict);
    break;
  }
  case Command::PerformTask:
    if (job.arg <= 0) return false;
    snprintf(out.path, sizeof(out.path), "/api/v1/maintenance/items/%d/perform", job.arg);
    snprintf(out.body, sizeof(out.body), "{}");
    break;
  }
  return true;
}

static const char *done_label(const Job &job)
{
  switch (job.cmd) {
  case Command::Pause:         return "Paused";
  case Command::Resume:        return "Resumed";
  case Command::Stop:          return "Stopped";
  case Command::ClearPlate:    return "Plate cleared";
  case Command::ClearErrors:   return "Errors cleared";
  case Command::ResumeQueue:   return "Queue resumed";
  case Command::SetLight:      return job.arg ? "Light on" : "Light off";
  case Command::StartNext:     return "Starting";
  case Command::VerdictGood:   return "Marked good";
  case Command::VerdictReject: return "Marked bad";
  case Command::PerformTask:   return "Marked done";
  }
  return "Done";
}

void command_result(const Job &job, int code, char *out, size_t size)
{
  if (code == 0) snprintf(out, size, "Nothing to send");
  else if (code >= 200 && code < 300) snprintf(out, size, "%s", done_label(job));
  else if (code == 401) snprintf(out, size, "API key refused");
  else if (code == 403) snprintf(out, size, "Not allowed (API key)");
  else if (code == 404) snprintf(out, size, "Not found in Bambuddy");
  else if (code == 409 && job.cmd == Command::StartNext) snprintf(out, size, "Filament short; see Bambuddy");
  else if (code == 400 || code == 409) snprintf(out, size, "Printer not ready (%d)", code);
  else if (code >= 500) snprintf(out, size, "Bambuddy error (%d)", code);
  else if (code == ERR_NO_WIFI) snprintf(out, size, "WiFi is down");
  else if (code < 0) snprintf(out, size, "No answer from Bambuddy");
  else snprintf(out, size, "Failed (%d)", code);
}

void describe_offline(int code, int printerId, char *out, size_t size)
{
  static const char *SETTINGS_HINT = "\nHold the screen at start-up to change the settings.";
  switch (code) {
  case 401: snprintf(out, size, "Bambuddy refused the API key.%s", SETTINGS_HINT); return;
  case 403: snprintf(out, size, "The API key may not read printer %d.%s", printerId, SETTINGS_HINT); return;
  case 404: snprintf(out, size, "Bambuddy has no printer with id %d.%s", printerId, SETTINGS_HINT); return;
  case ERR_HTTPS: snprintf(out, size, "Use an http:// address; https isn't supported.%s", SETTINGS_HINT); return;
  case ERR_BAD_URL: snprintf(out, size, "The Bambuddy address isn't valid.%s", SETTINGS_HINT); return;
  case ERR_NO_WIFI: snprintf(out, size, "WiFi is down. Retrying..."); return;
  case ERR_BAD_JSON: snprintf(out, size, "Bambuddy's answer was cut off. Retrying..."); return;
  case -11: snprintf(out, size, "Bambuddy didn't answer in time. Retrying..."); return;
  case -1: snprintf(out, size, "Can't connect to Bambuddy. Retrying..."); return;
  default: break;
  }
  if (code < 0) snprintf(out, size, "Can't reach Bambuddy (%d). Retrying...", code);
  else if (code >= 500) snprintf(out, size, "Bambuddy had an error (%d). Retrying...", code);
  else snprintf(out, size, "Bambuddy answered %d. Retrying...", code);
}

static bool ends_with(const char *s, const char *tail)
{
  size_t n = strlen(s), t = strlen(tail);
  return n >= t && strcmp(s + n - t, tail) == 0;
}

void normalize_url(char *url, size_t size)
{
  if (!size) return;
  // Trim spaces at both ends.
  size_t start = 0, n = strlen(url);
  while (start < n && isspace((unsigned char)url[start])) start++;
  while (n > start && isspace((unsigned char)url[n - 1])) n--;
  memmove(url, url + start, n - start);
  url[n - start] = 0;
  if (!url[0]) return;

  if (!strstr(url, "://")) {
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "http://%s", url);
    snprintf(url, size, "%s", tmp);
  }
  for (;;) {
    n = strlen(url);
    if (n > 0 && url[n - 1] == '/') url[n - 1] = 0;
    else if (ends_with(url, "/api/v1")) url[n - 7] = 0;
    else break;
  }
}
