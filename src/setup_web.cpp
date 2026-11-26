#include "setup_web.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Update.h>
#include "settings_http.h"
#include "http_json.h"
#include <WiFi.h>
#include <esp_ota_ops.h>

#include "bambuddy_api.h"
#include "board.h"
#include "display.h"
#include "fw_check.h"
#include "messages.h"
#include "settings.h"
#include "setup_page.h"
#include "ui.h"
#include "version.h"

static const uint32_t IDLE_MS = 10 * 60 * 1000; // a panel that was set up already goes back to work
static const int MAX_WRONG = 10;                // wrong codes before the panel shows a new one

static SettingsHttp *server;
static Settings draft;
static bool offlineSave = false;
static int draftTicket = 0;
static char code[10];   // "abcd-efgh", shown on the panel's screen only
static int wrong = 0;
static WebSetup result;
static bool finished = false;
static bool first = false;
static uint32_t lastSave = 0; // or the last piece of a firmware upload

// This build's name, as an uploaded firmware file is searched for it (fw_check.h). The name is
// read out of this text when the page starts, which is also what keeps the text in the firmware.
static const char BOARD_STAMP[] = FW_BOARD_MARKER BOARD_ENV ";";
static char thisBoard[sizeof(((FwCheck *)nullptr)->board)];
static_assert(sizeof(BOARD_ENV) <= sizeof(thisBoard), "the build's name is too long for the board check");

// The printers the saved key may see, read from Bambuddy after a Save, so the page can offer
// them by name instead of asking for an id. Empty until then, or when Bambuddy didn't list any.
static const int MAX_CHOICES = 16;
static PageChoice choices[MAX_CHOICES];
static int choiceCount = 0;

// Letters and digits that can't be mistaken for each other on the small screen.
static void new_code()
{
  static const char ALPHABET[] = "abcdefghjkmnpqrstuvwxyz23456789";
  for (size_t i = 0; i < sizeof(code) - 1; i++)
    code[i] = i == 4 ? '-' : ALPHABET[esp_random() % (sizeof(ALPHABET) - 1)];
  code[sizeof(code) - 1] = 0;
  wrong = 0;
}

static void show_screen()
{
  // The QR code opens the page with the code filled in: scanning it from the screen shows the
  // same thing reading the code off the screen does, that the person is at the panel.
  String address = WiFi.localIP().toString();
  char link[80];
  snprintf(link, sizeof(link), MSG_WEB_QR, address.c_str(), code);
  CodeScreen c;
  c.title = first ? MSG_WEB_FIRST_TITLE : MSG_WEB_LATER_TITLE;
  c.qr = link;
  c.label1 = MSG_WEB_LABEL_ADDRESS;
  c.value1 = address.c_str();
  c.label2 = MSG_WEB_LABEL_CODE;
  c.value2 = code;
  ui_code(c);
  // A panel that is set up can go back to work without a Save; a new one has nowhere to go.
  if (!first) ui_message_buttons(MSG_WEB_CANCEL, display_needs_calibration() ? MSG_WEB_CALIBRATE : nullptr);
}

// The code from the form, compared without stopping at the first difference.
static bool code_ok()
{
  String given = server->arg("code");
  given.trim();
  given.toLowerCase();
  if (given.length() == 8 && given.indexOf('-') < 0) given = given.substring(0, 4) + "-" + given.substring(4);
  uint8_t diff = given.length() != strlen(code);
  for (size_t i = 0; i < strlen(code); i++) diff |= (uint8_t)(i < given.length() ? given[i] : 0) ^ (uint8_t)code[i];
  if (!diff) {
    wrong = 0;
    return true;
  }
  delay(1000); // slows guessing to one a second
  if (++wrong >= MAX_WRONG) {
    new_code();
    show_screen();
  }
  return false;
}

static String esc(const String &s)
{
  String o;
  o.reserve(s.length() + 8);
  for (char c : s) {
    switch (c) {
    case '&': o += "&amp;"; break;
    case '<': o += "&lt;"; break;
    case '>': o += "&gt;"; break;
    case '"': o += "&quot;"; break;
    case '\'': o += "&#39;"; break;
    default: o += c;
    }
  }
  return o;
}

struct StringPage : PageText {
  String &s;
  explicit StringPage(String &to) : s(to) {}
  using PageText::operator+=;
  PageText &operator+=(const char *text) override
  {
    s += text;
    return *this;
  }
};

// kind: "" plain, "e" error, "ok" success. `finishAnyway` offers to keep settings Bambuddy
// didn't accept (it may just be down). The page itself is in setup_page.cpp.
static void send_page(const String &msg, const char *kind, const String &typedCode, bool finishAnyway, bool done = false)
{
  String p;
  StringPage out(p);
  PageData d;
  if (!done && typedCode.isEmpty()) {
    d.locked = true;
    setup_page(out, d);
    return server->send(403, "text/html", p);
  }
  p.reserve(8200);
  d.msg = msg.c_str();
  d.kind = kind;
  d.typedCode = typedCode.c_str();
  d.url = draft.url.c_str();
  d.keySaved = draft.key.length() > 0;
  d.printerId = draft.printerId;
  d.choices = choices;
  d.choiceCount = choiceCount;
  d.finishAnyway = finishAnyway;
  d.ticket = draftTicket;
  d.done = done;
  d.version = FW_VERSION;
  d.board = thisBoard;
  for (int i = 0; i < choiceCount; i++)
    if (choices[i].id == draft.printerId) d.printerName = choices[i].name;
  setup_page(out, d);
  server->send(200, "text/html", p);
}

// Asks Bambuddy for the printer's status with the saved settings, as the panel will.
static int check_bambuddy()
{
  if (draft.url.startsWith("https://")) return ERR_HTTPS;
  ResponseClient client;
  HTTPClient http;
  char path[48];
  snprintf(path, sizeof(path), "/api/v1/printers/%d/status", draft.printerId);
  if (!http.begin(client, draft.url + path)) return ERR_BAD_URL;
  http.useHTTP10(true);
  http.setConnectTimeout(3000);
  http.setTimeout(5000);
  http.setUserAgent(FW_USER_AGENT);
  http.addHeader("X-API-Key", draft.key);
  http.addHeader("Accept", "application/json");
  int c = http.GET();
  if (client.exceeded()) c = ERR_BAD_JSON;
  if (c == 200) {
    JsonDocument filter;
    filter["connected"] = true;
    ResponseDocument doc;
    c = read_json(http, doc, filter);
    if (c == 200 && !doc["connected"].is<bool>()) c = ERR_BAD_JSON;
  }
  http.end();
  return c;
}

// Bambuddy returns a JSON array. Keep the first 16 suggestions and allow any numeric id.
static int fetch_printers(const String &url, const String &key)
{
  choiceCount = 0;
  if (url.startsWith("https://")) return ERR_HTTPS;
  ResponseClient client;
  HTTPClient http;
  if (!http.begin(client, url + "/api/v1/printers/")) return ERR_BAD_URL;
  http.useHTTP10(true);
  http.setConnectTimeout(3000);
  http.setTimeout(5000);
  http.setUserAgent(FW_USER_AGENT);
  http.addHeader("X-API-Key", key);
  http.addHeader("Accept", "application/json");
  int c = http.GET();
  if (client.exceeded()) c = ERR_BAD_JSON;
  if (c == 200) {
    JsonDocument filter;
    filter[0]["id"] = true;
    filter[0]["name"] = true;
    ResponseDocument doc;
    c = read_json(http, doc, filter);
    if (c == 200 && !doc.is<JsonArrayConst>()) c = ERR_BAD_JSON;
    JsonArrayConst list = c == 200 ? doc.as<JsonArrayConst>() : JsonArrayConst();
    for (JsonObjectConst p : list) {
      if (choiceCount >= MAX_CHOICES) break;
      int id = p["id"] | 0;
      if (id <= 0) continue;
      choices[choiceCount].id = id;
      snprintf(choices[choiceCount].name, sizeof(choices[choiceCount].name), "%s", p["name"] | "Printer");
      choiceCount++;
    }
  }
  http.end();
  return c;
}

static String describe_check(int c, bool listing = false)
{
  if (listing && c == 404) return "That address answered, but not like Bambuddy does. Check the port.";
  if (listing && c == 403) return "The API key lacks Read Status, or its owner may not see printers.";
  char out[160];
  switch (c) {
  case 401: snprintf(out, sizeof(out), "Bambuddy refused the API key."); break;
  case 403: snprintf(out, sizeof(out), "The API key lacks Read Status, or its owner may not see printers."); break;
  case 404:
    snprintf(out, sizeof(out), "Bambuddy has no printer %d, or the API key is limited to other printers.",
             draft.printerId);
    break;
  case ERR_BAD_JSON: return "The server did not return a recognizable Bambuddy response.";
  case ERR_NO_MEMORY: return "The response was too large for this panel.";
  case ERR_HTTPS: snprintf(out, sizeof(out), "Use an http:// address; the panel doesn't speak https."); break;
  case ERR_BAD_URL: snprintf(out, sizeof(out), "That address isn't valid."); break;
  case -1: snprintf(out, sizeof(out), "Nothing answered at that address. Check the host and port."); break;
  case -11: snprintf(out, sizeof(out), "Bambuddy didn't answer in time."); break;
  default:
    if (c < 0) snprintf(out, sizeof(out), "Couldn't reach Bambuddy (error %d).", c);
    else snprintf(out, sizeof(out), "Bambuddy answered %d.", c);
  }
  return esc(out);
}

// The link in the panel's QR code carries the code, so it needn't be typed.
static void handle_root()
{
  String typed = server->arg("code");
  send_page("", "", typed.length() && code_ok() ? typed : String(), false);
}

static bool form_url(char (&url)[128])
{
  String raw = server->arg("url");
  raw.trim();
  if (raw.length() + (raw.indexOf("://") < 0 ? 7 : 0) >= sizeof(url)) return false;
  raw.toCharArray(url, sizeof(url));
  normalize_url(url, sizeof(url));
  if (!url[0]) return true;
  if (strncmp(url, "http://", 7) || !url[7] || url[7] == '/' || strpbrk(url, " @?#\\"))
    return false;
  return true;
}

static bool commit_draft(const String &typed)
{
  if (settings_save(draft)) return true;
  send_page("Could not write settings to flash. Check storage and try again.", "e", typed, offlineSave);
  return false;
}

static void handle_save()
{
  String typed = server->arg("code");
  if (!code_ok()) {
    send_page("That code doesn't match the one on the panel's screen.", "e", "", false);
    return;
  }
  lastSave = millis();

  char url[128];
  offlineSave = false;
  draftTicket = (esp_random() & 0x3fffffff) + 1;
  if (!form_url(url)) return send_page("Enter a valid HTTP server address of at most 127 characters.", "e", typed, false);
  bool moved = draft.url != url; // the saved one is normalised too
  String key = server->arg("key");
  key.trim();
  bool keyDropped = false;
  draft.url = url;
  if (key.length()) draft.key = key;
  else if (moved && draft.key.length()) {
    draft.key = ""; // or whoever is at the panel could have the key sent to their own server
    keyDropped = true;
  }
  String printer = server->arg("printer");
  const char *start = printer.c_str();
  char *end = nullptr;
  errno = 0;
  long id = strtol(start, &end, 10);
  draft.printerId = !errno && end != start && !*end && id > 0 && id <= INT_MAX ? (int)id : 0;
  choiceCount = 0;

  // With an address and key, list the printers the key may see, so the page can offer them by
  // name. It also tells, before the status check, whether the address and key work at all.
  int listed = draft.url.length() && draft.key.length() ? fetch_printers(draft.url, draft.key) : 0;
  if (draft.printerId <= 0 && choiceCount == 1) draft.printerId = choices[0].id;

  String missing;
  auto need = [&missing](const char *what) {
    if (missing.length()) missing += ", ";
    missing += what;
  };
  if (!draft.url.length()) need("the Bambuddy address");
  if (!draft.key.length()) need(keyDropped ? "the API key (again, for the new address)" : "the API key");
  if (draft.printerId <= 0) need(choiceCount ? "the printer (choose it below)" : "the printer id");
  if (missing.length()) {
    String msg = "Still needed: " + missing + ".";
    if (listed != 0 && listed != 200) msg += " Listing the printers failed: " + describe_check(listed, true);
    send_page(msg, "e", typed, false);
    return;
  }

  int c = check_bambuddy();
  Serial.printf("[setup] Bambuddy check -> %d\n", c);
  if (c == 200) {
    if (!commit_draft(typed)) return;
    send_page("Saved, and Bambuddy answered. The panel is starting; you can close this page.", "ok", "", false, true);
    result = WebSetup::Done;
    finished = true;
    return;
  }
  offlineSave = c == -1 || c == -11 || (c >= 500 && c <= 599);
  send_page(describe_check(c) + " Your previous settings are still saved.", "e", typed, offlineSave);
}

// Tries an address and key without saving them, and answers with the printers the key may see.
static void handle_check()
{
  JsonDocument out;
  out["ok"] = false;
  out["printers"].to<JsonArray>();
  auto reply = [&out]() {
    String body;
    serializeJson(out, body);
    server->send(200, "application/json", body);
  };
  if (!code_ok()) {
    out["msg"] = "That code doesn't match the one on the panel's screen.";
    return reply();
  }
  char url[128];
  out["selected"] = draft.printerId;
  if (!form_url(url)) {
    out["msg"] = "Enter a valid HTTP address of at most 127 characters.";
    return reply();
  }
  String key = server->arg("key");
  key.trim();
  // A blank key means the saved one, but only at the address it was saved with.
  if (!key.length() && draft.key.length() && draft.url == url) key = draft.key;
  if (!url[0]) {
    out["msg"] = "Enter the Bambuddy address first.";
    return reply();
  }
  if (!key.length()) {
    out["msg"] = draft.key.length() ? "A new address needs the API key typed in again." : "Enter the API key first.";
    return reply();
  }
  int c = fetch_printers(url, key);
  Serial.printf("[setup] key check -> %d, %d printers\n", c, choiceCount);
  if (c != 200) {
    out["msg"] = describe_check(c, true);
    return reply();
  }
  out["ok"] = true;
  JsonArray list = out["printers"].to<JsonArray>();
  for (int i = 0; i < choiceCount; i++) {
    JsonObject o = list.add<JsonObject>();
    o["id"] = choices[i].id;
    o["name"] = choices[i].name;
  }
  out["msg"] = choiceCount == 0   ? "Bambuddy returned no printers."
               : choiceCount == 1 ? "Bambuddy listed one printer."
                                  : "Choose a listed printer or enter its id.";
  reply();
}

static void handle_finish()
{
  if (!code_ok()) {
    send_page("That code doesn't match the one on the panel's screen.", "e", "", true);
    return;
  }
  if (!offlineSave || server->arg("ticket") != String(draftTicket) || !draft.complete()) {
    send_page("Save the current settings and review the connection result first.", "e", server->arg("code"), false);
    return;
  }
  if (!commit_draft(server->arg("code"))) return;
  send_page("The panel is starting with these settings; you can close this page.", "ok", "", false, true);
  result = WebSetup::Done;
  finished = true;
}

static void handle_wifi()
{
  if (!code_ok()) {
    send_page("That code doesn't match the one on the panel's screen.", "e", "", false);
    return;
  }
  send_page("The panel forgets this WiFi and restarts. Join its network <b>BambuddyPanel-Setup</b> "
            "with the password on its screen.",
            "ok", "", false, true);
  result = WebSetup::ChangeWifi;
  finished = true;
}

// ---- firmware update: a file uploaded from the page into the spare application slot

static void handle_update()
{
  if (!code_ok()) return send_page("", "e", "", false);
  String typed = server->arg("code");
  const esp_partition_t *slot = esp_ota_get_next_update_partition(nullptr);
  const size_t length = server->contentLength();
  if (!slot || !length || length > slot->size) {
    return send_page("This file is empty or exceeds the available application slot.", "e", typed, false);
  }
  server->allowUpload();
  FwCheck check;
  FwVerdict verdict = FwVerdict::Ok;
  String error;
  uint8_t chunk[1024];
  bool writing = false;
  while (check.size < length) {
    int n = server->read(chunk, std::min(sizeof(chunk), length - check.size));
    if (n <= 0) { error = "Upload interrupted."; break; }
    lastSave = millis();
    fw_check_feed(check, chunk, n);
    if (!writing) {
      verdict = fw_check_start(check);
      if (verdict != FwVerdict::Ok) break;
      if (!Update.begin(length)) { error = Update.errorString(); break; }
      writing = true;
      ui_message(MSG_UPDATING_TITLE, MSG_UPDATING_BODY);
    }
    if (Update.write(chunk, n) != (size_t)n) { error = Update.errorString(); break; }
    display_loop();
  }
  if (writing && error.isEmpty()) {
    verdict = fw_check_end(check, thisBoard, slot->size);
    if (verdict == FwVerdict::Ok) {
      if (Update.end()) {
        send_page("Updated. The panel will restart.", "ok", "", false, true);
        result = WebSetup::Updated;
        finished = true;
        return;
      }
      error = Update.errorString();
    }
  }
  if (writing) Update.abort();
  show_screen();
  char why[240] = {};
  fw_refusal(why, sizeof(why), verdict, check, thisBoard);
  send_page(error.length() ? esc(error) : String(why), "e", typed, false);
}

WebSetup setup_web_run(bool firstTime)
{
  first = firstTime;
  finished = false;
  result = WebSetup::Idle;
  lastSave = millis();
  new_code();
  choiceCount = 0;
  snprintf(thisBoard, sizeof(thisBoard), "%.*s", (int)(sizeof(BOARD_STAMP) - sizeof(FW_BOARD_MARKER) - 1),
           BOARD_STAMP + sizeof(FW_BOARD_MARKER) - 1);
  draft = settings;
  offlineSave = false;
  SettingsHttp web;
  server = &web;
  show_screen();
  if (!web.begin(handle_root, handle_save, handle_finish, handle_check, handle_wifi, handle_update)) {
    web.stop();
    server = nullptr;
    ui_message("Settings unavailable", "Restart the panel to retry.");
    delay(2000);
    if (first) ESP.restart();
    return WebSetup::Idle;
  }
  Serial.printf("[setup] settings page at http://%s\n", WiFi.localIP().toString().c_str());

  IPAddress shown = WiFi.localIP();
  for (;;) {
    web.lock();
    if (!finished) {
      display_loop();
      int pressed = ui_message_pressed();
      if (pressed == 2) result = WebSetup::Calibrate;
      finished = pressed == 1 || pressed == 2 || (!first && millis() - lastSave > IDLE_MS);
      if (!finished && WiFi.localIP() != shown) {
        shown = WiFi.localIP();
        show_screen();
      }
    }
    bool exitSetup = finished;
    if (exitSetup) web.closeRequests();
    web.unlock();
    if (exitSetup) break;
    delay(2);
  }
  web.stop();
  server = nullptr;
  draft = Settings();
  memset(code, 0, sizeof(code));

  if (result == WebSetup::ChangeWifi) {
    WiFi.disconnect(true, true); // and erase the saved network
    delay(200);
  }
  return result;
}
