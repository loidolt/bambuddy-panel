// What the panel asks Bambuddy for, and how it words the answers. No network code here, so
// the panel, the simulator and the unit tests all build requests the same way.
#pragma once
#include <cstddef>
#include "bambuddy.h"

// Client-side errors, next to HTTPClient's own negative codes (-1 .. -11).
static const int ERR_NO_WIFI = -100;
static const int ERR_BAD_URL = -101;
static const int ERR_BAD_JSON = -102; // unreadable or cut-off answer
static const int ERR_HTTPS = -103;    // the panel speaks plain HTTP only
static const int ERR_NO_MEMORY = -104; // no room for what Bambuddy sent

struct Request {
  const char *method = "POST";
  char path[96] = ""; // after the base URL, starting with /api/v1
  char body[112] = ""; // JSON, or empty for no body
};

// Builds the request for a command. False when there is nothing to send (no id to act on).
bool command_request(const Job &job, int printerId, Request &out);

// The notice after a command: what happened, in a few words that fit the screen. True when
// it worked.
bool command_result(const Job &job, int code, char *out, size_t size);

// The full-screen text while the status request fails.
void describe_offline(int code, int printerId, char *out, size_t size);

// Cleans up a Bambuddy URL typed into the setup form: trims spaces, adds http:// when no
// scheme is given, and drops trailing slashes and a trailing /api/v1.
void normalize_url(char *url, size_t size);
