#include "setup_web.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <WiFi.h>

#include "bambuddy_api.h"
#include "display.h"
#include "messages.h"
#include "settings.h"
#include "setup_page.h"
#include "ui.h"

static const uint32_t IDLE_MS = 10 * 60 * 1000; // a panel that was set up already goes back to work
static const int MAX_WRONG = 10;                // wrong codes before the panel shows a new one

static WebServer *server;
static char code[10];   // "abcd-efgh", shown on the panel's screen only
static int wrong = 0;
static WebSetup result;
static bool finished = false;
static bool first = false;
static uint32_t lastSave = 0;

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
  p.reserve(7000);
  StringPage out(p);
  PageData d;
  d.msg = msg.c_str();
  d.kind = kind;
  d.typedCode = typedCode.c_str();
  d.url = settings.url.c_str();
  d.keySaved = settings.key.length() > 0;
  d.printerId = settings.printerId;
  d.choices = choices;
  d.choiceCount = choiceCount;
  d.finishAnyway = finishAnyway;
  d.done = done;
  for (int i = 0; i < choiceCount; i++)
    if (choices[i].id == settings.printerId) d.printerName = choices[i].name;
  setup_page(out, d);
  server->sendHeader("Cache-Control", "no-store");
  server->send(200, "text/html", p);
}

// Asks Bambuddy for the printer's status with the saved settings, as the panel will.
static int check_bambuddy()
{
  if (settings.url.startsWith("https://")) return ERR_HTTPS;
  WiFiClient client;
  HTTPClient http;
  char path[48];
  snprintf(path, sizeof(path), "/api/v1/printers/%d/status", settings.printerId);
  if (!http.begin(client, settings.url + path)) return ERR_BAD_URL;
  http.setConnectTimeout(3000);
  http.setTimeout(5000);
  http.addHeader("X-API-Key", settings.key);
  http.addHeader("Accept", "application/json");
  int c = http.GET();
  http.end();
  return c;
}

// Reads the printers a key may see into `choices`. Takes a plain list of printers, or one
// wrapped in {"printers": [...]} or {"items": [...]}.
static int fetch_printers(const String &url, const String &key)
{
  choiceCount = 0;
  if (url.startsWith("https://")) return ERR_HTTPS;
  WiFiClient client;
  HTTPClient http;
  if (!http.begin(client, url + "/api/v1/printers/")) return ERR_BAD_URL;
  http.useHTTP10(true);
  http.setConnectTimeout(3000);
  http.setTimeout(5000);
  http.addHeader("X-API-Key", key);
  http.addHeader("Accept", "application/json");
  int c = http.GET();
  if (c == 200) {
    JsonDocument filter;
    filter[0]["id"] = true;
    filter[0]["name"] = true;
    filter["printers"][0]["id"] = true;
    filter["printers"][0]["name"] = true;
    filter["items"][0]["id"] = true;
    filter["items"][0]["name"] = true;
    JsonDocument doc;
    if (deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter))) c = ERR_BAD_JSON;
    JsonArrayConst list = doc.is<JsonArrayConst>()       ? doc.as<JsonArrayConst>()
                          : doc["printers"].is<JsonArrayConst>() ? doc["printers"].as<JsonArrayConst>()
                                                                 : doc["items"].as<JsonArrayConst>();
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
             settings.printerId);
    break;
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
static void handle_root() { send_page("", "", server->arg("code"), false); }

static void handle_save()
{
  String typed = server->arg("code");
  if (!code_ok()) {
    send_page("That code doesn't match the one on the panel's screen.", "e", "", false);
    return;
  }
  lastSave = millis();

  char url[128];
  snprintf(url, sizeof(url), "%s", server->arg("url").c_str());
  normalize_url(url, sizeof(url));
  bool moved = settings.url != url; // the saved one is normalised too
  String key = server->arg("key");
  key.trim();
  bool keyDropped = false;
  settings.url = url;
  if (key.length()) settings.key = key;
  else if (moved && settings.key.length()) {
    settings.key = ""; // or whoever is at the panel could have the key sent to their own server
    keyDropped = true;
  }
  settings.printerId = server->arg("printer").toInt();

  // With an address and key, list the printers the key may see, so the page can offer them by
  // name. It also tells, before the status check, whether the address and key work at all.
  int listed = settings.url.length() && settings.key.length() ? fetch_printers(settings.url, settings.key) : 0;
  if (settings.printerId > 0 && choiceCount) {
    bool known = false;
    for (int i = 0; i < choiceCount; i++) known |= choices[i].id == settings.printerId;
    if (!known) settings.printerId = 0; // not one the key may see: choose again
  }
  if (settings.printerId <= 0 && choiceCount == 1) settings.printerId = choices[0].id; // the only one
  settings_save();

  String missing;
  auto need = [&missing](const char *what) {
    if (missing.length()) missing += ", ";
    missing += what;
  };
  if (!settings.url.length()) need("the Bambuddy address");
  if (!settings.key.length()) need(keyDropped ? "the API key (again, for the new address)" : "the API key");
  if (settings.printerId <= 0) need(choiceCount ? "the printer (choose it below)" : "the printer id");
  if (missing.length()) {
    Serial.printf("[setup] saved, still missing: %s\n", missing.c_str());
    String msg = "Saved. Still needed: " + missing + ".";
    if (listed != 0 && listed != 200) msg += " Listing the printers failed: " + describe_check(listed, true);
    send_page(msg, "e", typed, false);
    return;
  }

  int c = check_bambuddy();
  Serial.printf("[setup] saved, Bambuddy check -> %d\n", c);
  if (c == 200) {
    send_page("Saved, and Bambuddy answered. The panel is starting; you can close this page.", "ok", "", false, true);
    result = WebSetup::Done;
    finished = true;
    return;
  }
  send_page("Saved, but " + describe_check(c) + " Fix it and save again, or use the settings as they are "
                                                 "if Bambuddy is only down for now.",
            "e", typed, true);
}

// Tries an address and key without saving them, and answers with the printers the key may see.
static void handle_check()
{
  JsonDocument out;
  out["ok"] = false;
  out["printers"].to<JsonArray>();
  out["selected"] = settings.printerId;
  auto reply = [&out]() {
    String body;
    serializeJson(out, body);
    server->sendHeader("Cache-Control", "no-store");
    server->send(200, "application/json", body);
  };
  if (!code_ok()) {
    out["msg"] = "That code doesn't match the one on the panel's screen.";
    return reply();
  }
  char url[128];
  snprintf(url, sizeof(url), "%s", server->arg("url").c_str());
  normalize_url(url, sizeof(url));
  String key = server->arg("key");
  key.trim();
  // A blank key means the saved one, but only at the address it was saved with.
  if (!key.length() && settings.key.length() && settings.url == url) key = settings.key;
  if (!url[0]) {
    out["msg"] = "Enter the Bambuddy address first.";
    return reply();
  }
  if (!key.length()) {
    out["msg"] = settings.key.length() ? "A new address needs the API key typed in again." : "Enter the API key first.";
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
  out["msg"] = choiceCount == 0   ? "The key works, but it may not see any printers."
               : choiceCount == 1 ? "The key works."
                                  : "The key works. Choose the printer below.";
  reply();
}

static void handle_finish()
{
  if (!code_ok()) {
    send_page("That code doesn't match the one on the panel's screen.", "e", "", true);
    return;
  }
  if (!settings.complete()) {
    send_page("The address, API key and printer id are all needed first.", "e", server->arg("code"), false);
    return;
  }
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

WebSetup setup_web_run(bool firstTime)
{
  first = firstTime;
  finished = false;
  result = WebSetup::Idle;
  lastSave = millis();
  new_code();
  choiceCount = 0;

  WebServer web(80);
  server = &web;
  web.on("/", HTTP_GET, handle_root);
  web.on("/save", HTTP_POST, handle_save);
  web.on("/finish", HTTP_POST, handle_finish);
  web.on("/check", HTTP_POST, handle_check);
  web.on("/wifi", HTTP_POST, handle_wifi);
  web.onNotFound([]() {
    server->sendHeader("Location", "/");
    server->send(303);
  });
  web.begin();
  show_screen();
  Serial.printf("[setup] settings page at http://%s\n", WiFi.localIP().toString().c_str());

  IPAddress shown = WiFi.localIP();
  while (!finished) {
    web.handleClient();
    display_loop(); // the buttons on the panel's screen
    int pressed = ui_message_pressed();
    if (pressed == 1) break; // Cancel: as if nobody had saved
    if (pressed == 2) {
      result = WebSetup::Calibrate;
      break;
    }
    if (!first && millis() - lastSave > IDLE_MS) break;
    if (WiFi.localIP() != shown) { // a new lease: the address on the screen was wrong
      shown = WiFi.localIP();
      show_screen();
    }
    delay(2);
  }
  // Let the last answer reach the browser before the server goes.
  for (uint32_t t = millis(); millis() - t < 300;) {
    web.handleClient();
    delay(2);
  }
  web.stop();
  server = nullptr;
  memset(code, 0, sizeof(code));

  if (result == WebSetup::ChangeWifi) {
    WiFi.disconnect(true, true); // and erase the saved network
    delay(200);
  }
  return result;
}
