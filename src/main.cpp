// Bambuddy Panel: shows one printer's Bambuddy status and offers the few actions
// you want while standing at the printer.
#include <Arduino.h>
#include <ArduinoOTA.h>
#include <WiFi.h>
#include <WiFiManager.h>

#include "bambuddy.h"
#include "calibrate.h"
#include "display.h"
#include "messages.h"
#include "settings.h"
#include "setup_web.h"
#include "ui.h"
#include "version.h"

static const char *SETUP_AP = "BambuddyPanel-Setup";
static const uint32_t PORTAL_TIMEOUT_S = 600;
static const uint32_t WIFI_WAIT_MS = 20000;

static char apPassword[15];

// The setup portal is a web server on an access point. WiFiManager serves more than the
// setup form: a firmware upload page, "erase WiFi settings", "restart" and device info, with no password of
// their own. The panel answers those paths first, so WiFiManager's handlers never run.
static void block_extra_pages(WiFiManager &wm)
{
  for (const char *path : {"/update", "/u", "/erase", "/restart", "/info", "/param", "/paramsave"})
    wm.server->on(path, HTTP_ANY, [&wm]() { wm.server->send(404, "text/plain", "Not available"); });
}

// The WiFi setup portal: an access point with a password shown on the screen, so only someone
// standing at the panel can join it. It asks for the WiFi and nothing else: the Bambuddy
// settings come after, on a page on the panel's own address (setup_web.cpp), because a phone's
// captive-portal window forgets what was typed in it as soon as you switch away to copy a key.
// True when the panel joined the WiFi.
static bool run_wifi_portal()
{
  WiFiManager wm;
  std::vector<const char *> menu = {"wifi", "exit"};
  wm.setMenu(menu);
  wm.setWebServerCallback([&wm]() { block_extra_pages(wm); });
  // The network list takes a few seconds to scan and the page shows nothing until it's done:
  // say so on the button that was pressed.
  wm.setCustomHeadElement(
      "<script>addEventListener('submit',function(e){var f=e.target,b=f.querySelector('button');"
      "if(!b)return;b.disabled=true;b.textContent=/\\/wifi$/.test(f.action)?'Looking for networks...'"
      ":/\\/exit$/.test(f.action)?'Closing...':'Joining...';});</script>");

  // Three groups of four, new every time, from letters and digits that can't be mistaken for
  // each other: about 59 bits, so recording someone joining and guessing at home gets nowhere.
  static const char ALPHABET[] = "abcdefghjkmnpqrstuvwxyz23456789";
  for (size_t i = 0; i < sizeof(apPassword) - 1; i++)
    apPassword[i] = i % 5 == 4 ? '-' : ALPHABET[esp_random() % (sizeof(ALPHABET) - 1)];
  apPassword[sizeof(apPassword) - 1] = 0;
  wm.setAPCallback([](WiFiManager *) {
    char join[96]; // what a phone's camera needs to join the network by itself
    snprintf(join, sizeof(join), MSG_WIFI_QR, SETUP_AP, apPassword);
    CodeScreen c;
    c.title = MSG_WIFI_SETUP_TITLE;
    c.qr = join;
    c.label1 = MSG_WIFI_LABEL_NAME;
    c.value1 = SETUP_AP;
    c.label2 = MSG_WIFI_LABEL_PASSWORD;
    c.value2 = apPassword;
    c.note = MSG_WIFI_NOTE;
    ui_code(c);
  });
  wm.setConfigPortalTimeout(PORTAL_TIMEOUT_S);
  wm.setConnectTimeout(20);

  // Returns once the WiFi is joined; a wrong password keeps the portal open for another try.
  bool joined = wm.startConfigPortal(SETUP_AP, apPassword);
  memset(apPassword, 0, sizeof(apPassword));
  return joined;
}

static bool join_saved_wifi()
{
  if (WiFi.status() == WL_CONNECTED) return true;
  ui_message(MSG_CONNECTING_TITLE, MSG_CONNECTING_WIFI, true);
  WiFi.begin();
  for (uint32_t t = millis(); millis() - t < WIFI_WAIT_MS && WiFi.status() != WL_CONNECTED; delay(10))
    display_loop(); // keeps the spinner turning
  return WiFi.status() == WL_CONNECTED;
}

static void restart_unfinished(const char *why)
{
  ui_message(MSG_SETUP_TITLE, why);
  delay(3000);
  ESP.restart();
}

// Joins the WiFi, and runs setup when it is asked for (`force`) or isn't finished. Setup never
// opens because the WiFi happens to be down on a panel that was set up: the panel then keeps
// trying in the background, and the status screen says so.
static void connect(bool force)
{
  WiFiManager probe;
  if (!probe.getWiFiIsSaved() && !run_wifi_portal()) restart_unfinished(MSG_SETUP_NO_WIFI_SAVED);
  bool up = join_saved_wifi();
  if (!force && settings.complete()) return;

  if (!up) {
    // Setup was asked for, or isn't finished, and the saved WiFi doesn't answer: let it be changed.
    if (!run_wifi_portal()) {
      if (!settings.complete()) restart_unfinished(MSG_SETUP_NOT_ON_WIFI);
      return; // set up already: carry on, retrying the WiFi
    }
  }
  for (;;) {
    switch (setup_web_run(!settings.complete())) {
    case WebSetup::ChangeWifi:
      restart_unfinished(MSG_SETUP_WIFI_FORGOTTEN);
      return;
    case WebSetup::Calibrate:
      calibrate_run(true);
      continue; // and back to the settings screen it was asked from
    case WebSetup::Updated: // the screen still says "Updating"
      delay(300);
      ESP.restart();
      return;
    case WebSetup::Idle:
    case WebSetup::Done:
      return;
    }
  }
}

// "Open settings" and the gear on the running panel end here: the settings page and the poller
// are not run side by side, so the panel starts again and goes where it was asked to.
void panel_restart(bool intoSettings)
{
  if (intoSettings) settings_request_setup();
  delay(400); // let the screen say so
  ESP.restart();
}

static void start_ota()
{
#ifdef OTA_PASSWORD
  static_assert(sizeof(OTA_PASSWORD) > 8, "OTA_PASSWORD needs at least 8 characters");
  char host[28];
  snprintf(host, sizeof(host), "bambuddy-panel-%d", settings.printerId);
  ArduinoOTA.setHostname(host);
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.onStart([]() { ui_message(MSG_UPDATING_TITLE, MSG_UPDATING_BODY); });
  ArduinoOTA.begin();
#endif
}

void setup()
{
  Serial.begin(115200);
  Serial.printf("\n[boot] Bambuddy Panel %s\n", FW_VERSION);
  display_begin();
  ui_begin();
  settings_load();
  // Asked for from the running panel (the gear, or "Open settings"): go straight there.
  const bool asked = settings_take_setup_request();
  // A new panel goes into setup anyway; asking it to touch the screen would only confuse.
  const bool offerSetup = settings.complete() && !asked;
  if (offerSetup) ui_message(MSG_STARTING_TITLE, MSG_STARTING_BODY);

  TouchCal cal;
  bool haveCal = settings_load_touch(cal);
  if (haveCal) display_set_touch_cal(cal);
  const bool resistive = display_needs_calibration();
  bool calibrate = resistive && !haveCal; // a new resistive panel calibrates on first boot

  // A touch during the first two seconds asks for setup. On a resistive panel, which setup is
  // chosen by how long the finger stays, not where it is, so this works even when the
  // calibration is far off.
  bool touched = false, force = asked;
  for (uint32_t t = millis(); offerSetup && millis() - t < 2000 && !touched; delay(20)) touched = display_touched();
  if (touched && !resistive) {
    force = true;
  } else if (touched) {
    ui_message(MSG_SETUP_TITLE, MSG_SETUP_HOLD_BODY);
    uint32_t t = millis(), lastDown = t;
    while (millis() - t < 4000 && millis() - lastDown < 300) { // 300 ms rides out a shaky finger
      if (display_touched()) lastDown = millis();
      delay(20);
    }
    if (millis() - lastDown < 300) calibrate = true;
    else force = true;
  }
  if (calibrate) calibrate_run(haveCal);

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  connect(force);

  start_ota();
  ui_message(MSG_CONNECTING_TITLE, MSG_CONNECTING_BAMBUDDY, true);
  if (!bambuddy_begin()) ui_message("Out of memory", "Restart the panel to reconnect.");
}

void loop()
{
  static Snapshot snap;
  static uint32_t last = 0;
  if (millis() - last >= 100) {
    last = millis();
    bambuddy_snapshot(snap);
    ui_update(snap);
  }
  display_loop();
#ifdef OTA_PASSWORD
  ArduinoOTA.handle();
#endif
  delay(5);
}
