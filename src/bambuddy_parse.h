// Turns Bambuddy's JSON into a Snapshot. No network code here, so the desktop
// simulator runs exactly the same parsing as the panel.
#pragma once
#include <ArduinoJson.h>
#include "bambuddy.h"

// The fields to keep from each response; everything else is skipped while reading.
void status_filter(JsonDocument &filter);
void queue_filter(JsonDocument &filter);
void last_print_filter(JsonDocument &filter);
void tasks_filter(JsonDocument &filter);
void settings_filter(JsonDocument &filter);
void assignments_filter(JsonDocument &filter);

void parse_status(JsonDocument &doc, Snapshot &s); // GET /printers/{id}/status
// GET /queue/?status=pending,skipped,printing&printer_id={id}
void parse_queue(JsonDocument &doc, int printerId, Snapshot &s);
void parse_tasks(JsonDocument &doc, Snapshot &s);  // GET /maintenance/printers/{id}
void parse_last_print(JsonDocument &doc, int printerId, Snapshot &s); // GET /print-log/?printer_id=&limit=1
void parse_settings(JsonDocument &doc, Snapshot &s); // GET /settings/
// GET /inventory/assignments?printer_id={id}
void parse_assignments(JsonDocument &doc, int printerId, Snapshot &s);

// The message Bambuddy's scheduler puts on queue items it skips after a failed print; the
// "resume queue" request puts exactly these back.
extern const char *const QUEUE_GATE_MESSAGE;

// Copies UTF-8 text as plain ASCII, which is all the panel's fonts can draw: accented Latin
// letters lose their accents (e -> e, ss for the sharp s), anything else becomes '?'.
// Never cuts a character in half.
void copy_ascii(char *dst, size_t size, const char *src);
