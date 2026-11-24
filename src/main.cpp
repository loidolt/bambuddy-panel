// Bambuddy Panel: shows one printer's Bambuddy status and offers the few actions
// you want while standing at the printer.
#include <Arduino.h>
#include <ArduinoOTA.h>
#include <WiFi.h>
#include <WiFiManager.h>

#include "bambuddy.h"
#include "calibrate.h"
#include "display.h"
#include "settings.h"
#include "ui.h"

static const char *SETUP_AP = "BambuddyPanel-Setup";

static bool portalSaved = false;

// Joins WiFi, or opens the setup portal (an access point with a web form) when there is
// no WiFi yet, the settings are incomplete, or `force` is set.
static void connect(bool force)
{
  WiFiManager wm;
  char printer[8];
  snprintf(printer, sizeof(printer), "%d", settings.printerId);

  WiFiManagerParameter pUrl("url", "Bambuddy URL (http://host:port)", settings.url.c_str(), 96);
  WiFiManagerParameter pKey("key", "Bambuddy API key", settings.key.c_str(), 128);
  WiFiManagerParameter pPrinter("printer", "Bambuddy printer id", settings.printerId ? printer : "", 6);
  wm.addParameter(&pUrl);
  wm.addParameter(&pKey);
  wm.addParameter(&pPrinter);

  wm.setAPCallback([](WiFiManager *) {
    ui_message("Setup", "Join WiFi \"BambuddyPanel-Setup\" on a phone,\nthen open 192.168.4.1");
  });
  wm.setSaveParamsCallback([]() { portalSaved = true; });
  wm.setConfigPortalTimeout(300); // then reboot and try the saved WiFi again
  wm.setConnectTimeout(20);

  bool ok = (force || !settings.complete()) ? wm.startConfigPortal(SETUP_AP) : wm.autoConnect(SETUP_AP);

  if (portalSaved) {
    settings.url = pUrl.getValue();
    settings.key = pKey.getValue();
    settings.printerId = atoi(pPrinter.getValue());
    settings_save();
  }
  if (!ok || !settings.complete()) {
    ui_message("Setup", "Not finished. Restarting...");
    delay(2000);
    ESP.restart();
  }
}

static void start_ota()
{
#ifdef OTA_PASSWORD
  char host[28];
  snprintf(host, sizeof(host), "bambuddy-panel-%d", settings.printerId);
  ArduinoOTA.setHostname(host);
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.onStart([]() { ui_message("Updating", "Don't unplug."); });
  ArduinoOTA.begin();
#endif
}

void setup()
{
  Serial.begin(115200);
  display_begin();
  ui_begin();
  ui_message("Starting", "Touch the screen now for setup.");
  settings_load();

  TouchCal cal;
  bool calibrate = !settings_load_touch(cal); // a new panel calibrates on first boot
  if (!calibrate) display_set_touch_cal(cal);

  // A touch during the first two seconds asks for setup. Which one is chosen by how long
  // the finger stays, not where it is, so this works even when the calibration is far off.
  bool touched = false, force = false;
  for (uint32_t t = millis(); millis() - t < 2000 && !touched; delay(20)) touched = display_touched();
  if (touched) {
    ui_message("Setup", "Let go now for WiFi setup.\nKeep holding for touch calibration.");
    uint32_t t = millis(), lastDown = t;
    while (millis() - t < 4000 && millis() - lastDown < 300) { // 300 ms rides out a shaky finger
      if (display_touched()) lastDown = millis();
      delay(20);
    }
    if (millis() - lastDown < 300) calibrate = true;
    else force = true;
  }
  if (calibrate) calibrate_run();

  ui_message("Connecting", "Joining WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  connect(force);

  start_ota();
  ui_message("Connecting", "Asking Bambuddy...");
  bambuddy_begin();
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
