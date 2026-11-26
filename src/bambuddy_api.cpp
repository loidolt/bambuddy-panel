#include "bambuddy_api.h"

#include <cctype>
#include <cstdio>
#include <cstring>

bool command_request(const Job &job, int printerId, Request &out)
{
  out = Request();
  if (printerId <= 0) return false;
  const int p = printerId;
  switch (job.cmd) {
  case Command::Pause:       snprintf(out.path, sizeof(out.path), "/api/v1/printers/%d/print/pause", p); break;
  case Command::Resume:      snprintf(out.path, sizeof(out.path), "/api/v1/printers/%d/print/resume", p); break;
  case Command::Stop:        snprintf(out.path, sizeof(out.path), "/api/v1/printers/%d/print/stop", p); break;
  case Command::ClearPlate:  snprintf(out.path, sizeof(out.path), "/api/v1/printers/%d/clear-plate", p); break;
  case Command::ClearErrors: snprintf(out.path, sizeof(out.path), "/api/v1/printers/%d/hms/clear", p); break;
  case Command::ResumeQueue: snprintf(out.path, sizeof(out.path), "/api/v1/queue/printer/%d/resume", p); break;
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
    if (job.arg2 <= 0) return false;
    snprintf(out.body, sizeof(out.body), "{\"user_verdict\":\"%s\",\"user_verdict_source\":\"api\",\"run_id\":%d}",
             verdict, job.arg2);
    break;
  }
  case Command::PerformTask:
    if (job.arg <= 0) return false;
    snprintf(out.path, sizeof(out.path), "/api/v1/maintenance/items/%d/perform", job.arg);
    snprintf(out.body, sizeof(out.body), "{}");
    break;
  case Command::AssignSpool:
    if (job.arg <= 0) return false;
    // "move": the spool leaves any other slot it was in; it is the one in the person's hand.
    snprintf(out.path, sizeof(out.path), "/api/v1/inventory/assignments");
    snprintf(out.body, sizeof(out.body), "{\"spool_id\":%d,\"printer_id\":%d,\"ams_id\":%d,\"tray_id\":%d,\"move\":true}",
             job.arg, p, slot_ams(job.arg2), slot_tray(job.arg2));
    break;
  case Command::UnassignSpool:
    out.method = "DELETE";
    snprintf(out.path, sizeof(out.path), "/api/v1/inventory/assignments/%d/%d/%d", p, slot_ams(job.arg2),
             slot_tray(job.arg2));
    break;
  case Command::LoadSpools: // for the background task, not for Bambuddy
    return false;
  }
  return true;
}

// The API key permission a command needs, as Bambuddy's settings page names it.
static const char *needed_permission(const Job &job)
{
  switch (job.cmd) {
  case Command::ResumeQueue:
  case Command::StartNext:     return "Manage Queue";
  case Command::VerdictGood:
  case Command::VerdictReject: return "Manage Archives";
  case Command::PerformTask:   return "Manage Maintenance";
  case Command::AssignSpool:
  case Command::UnassignSpool: return "Manage Inventory";
  default:                     return "Control Printer";
  }
}

static const char *done_label(const Job &job)
{
  switch (job.cmd) {
  case Command::Pause:         return "Pause requested";
  case Command::Resume:        return "Resume requested";
  case Command::Stop:          return "Stop requested";
  case Command::ClearPlate:    return "Plate cleared";
  case Command::ClearErrors:   return "HMS clear requested";
  case Command::ResumeQueue:   return "Queue resumed";
  case Command::StartNext:     return "Start requested";
  case Command::VerdictGood:   return "Marked good";
  case Command::VerdictReject: return "Marked bad";
  case Command::PerformTask:   return "Marked done";
  case Command::AssignSpool:   return "Spool assigned";
  case Command::UnassignSpool: return "Spool removed";
  case Command::LoadSpools:    return "";
  }
  return "Done";
}

bool command_result(const Job &job, int code, char *out, size_t size, const CommandReply &reply)
{
  if (code >= 200 && code < 300) {
    const char *message = done_label(job);
    if (job.cmd == Command::AssignSpool && reply.configurationPending) message = "Assigned; tray setup pending";
    if (job.cmd == Command::ResumeQueue && reply.noQueueChanges) message = "No held jobs to restore";
    snprintf(out, size, "%s", message);
    return true;
  }
  if (code == 0) snprintf(out, size, "Nothing to act on");
  else if (code == 401) snprintf(out, size, "Bambuddy refused the API key");
  else if (code == 403) snprintf(out, size, "Access denied (%s)", needed_permission(job));
  else if (code == 404) snprintf(out, size, "Bambuddy no longer has it");
  else if (code == 409 && job.cmd == Command::StartNext) {
    const char *why = !strcmp(reply.conflict, "unmatched_filament") ? "No matching filament loaded" :
                      !strcmp(reply.conflict, "insufficient_filament") ? "Not enough filament loaded" : "Start refused; check Bambuddy";
    snprintf(out, size, "%s", why);
  }
  else if (code == 400 || code == 409) snprintf(out, size, "The printer isn't ready for that");
  else if (code >= 500) snprintf(out, size, "Bambuddy had an error (%d)", code);
  else if (code == ERR_STALE) snprintf(out, size, "Status changed. Check and retry.");
  else if (code == ERR_NO_WIFI) snprintf(out, size, "WiFi is down");
  else if (code < 0) snprintf(out, size, "No answer from Bambuddy");
  else snprintf(out, size, "Bambuddy answered %d", code);
  return false;
}

void describe_offline(int code, int printerId, char *out, size_t size)
{
  switch (code) {
  case 401: snprintf(out, size, "Bambuddy refused the API key. It may have been deleted or changed."); return;
  // Bambuddy answers 404, not 403, for a printer the key is not limited to.
  case 403: snprintf(out, size, "The API key lacks Read Status, or its owner may not see printers."); return;
  case 404: snprintf(out, size, "No printer with id %d, or the API key is limited to others.", printerId); return;
  case ERR_HTTPS: snprintf(out, size, "Use an http:// address; https isn't supported."); return;
  case ERR_BAD_URL: snprintf(out, size, "The Bambuddy address isn't valid."); return;
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
  // "Http://", as a phone's keyboard starts it: the scheme is matched in lower case.
  for (char *c = url, *end = strstr(url, "://"); end && c < end; c++) *c = (char)tolower((unsigned char)*c);
  for (;;) {
    n = strlen(url);
    if (n > 0 && url[n - 1] == '/') url[n - 1] = 0;
    else if (ends_with(url, "/api/v1")) url[n - 7] = 0;
    else break;
  }
}
