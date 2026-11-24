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

void parse_status(JsonDocument &doc, Snapshot &s); // GET /printers/{id}/status
void parse_queue(JsonDocument &doc, Snapshot &s);  // GET /queue/?status=pending&printer_id={id}
void parse_tasks(JsonDocument &doc, Snapshot &s);  // GET /maintenance/printers/{id}
void parse_last_print(JsonDocument &doc, int printerId, Snapshot &s); // GET /archives/last-per-printer
