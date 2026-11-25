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
static const uint32_t PORTAL_TIMEOUT_S = 600;
static const uint32_t WIFI_WAIT_MS = 20000;

static bool portalSaved = false;
static char apPassword[9];

// The setup portal is a web server on an access point. WiFiManager serves more than the
// setup form: a firmware upload page, "erase WiFi settings", "restart" and device info, with no password of
// their own. The panel answers those paths first, so WiFiManager's handlers never run.
static void block_extra_pages(WiFiManager &wm)
{
  for (const char *path : {"/update", "/u", "/erase", "/restart", "/info"})
    wm.server->on(path, HTTP_ANY, [&wm]() { wm.server->send(404, "text/plain", "Not available"); });
}

// Opens the setup portal: an access point with a password shown on the screen, so only
// someone standing at the panel can join it. The saved API key is never sent to the browser;
// a blank key field keeps it.
static void run_portal()
{
  WiFiManager wm;
  char printer[8];
  snprintf(printer, sizeof(printer), "%d", settings.printerId);

  WiFiManagerParameter pUrl("url", "Bambuddy URL, e.g. http://192.168.1.50:3700", settings.url.c_str(), 120);
  WiFiManagerParameter pKey("key",
                            settings.key.length() ? "Bambuddy API key (leave blank to keep the saved one)"
                                                  : "Bambuddy API key",
                            "", 128, "type='password' autocomplete='off'");
  WiFiManagerParameter pPrinter("printer", "Bambuddy printer id", settings.printerId ? printer : "", 6,
                                "inputmode='numeric'");
  wm.addParameter(&pUrl);
  wm.addParameter(&pKey);
  wm.addParameter(&pPrinter);
  std::vector<const char *> menu = {"wifi", "param", "exit"};
  wm.setMenu(menu);
  wm.setWebServerCallback([&wm]() { block_extra_pages(wm); });

  // Eight digits, new every time: easy to type on a phone, and nobody can guess it from afar.
  snprintf(apPassword, sizeof(apPassword), "%08lu", (unsigned long)(esp_random() % 100000000UL));
  wm.setAPCallback([](WiFiManager *) {
    char body[160];
    snprintf(body, sizeof(body),
             "On a phone, join WiFi \"%s\"\nwith password %s,\nthen open 192.168.4.1", SETUP_AP, apPassword);
    ui_message("Setup", body);
  });
  wm.setSaveParamsCallback([]() { portalSaved = true; });
  wm.setConfigPortalTimeout(PORTAL_TIMEOUT_S);
  wm.setConnectTimeout(20);

  bool joined = wm.startConfigPortal(SETUP_AP, apPassword);
  memset(apPassword, 0, sizeof(apPassword));

  if (portalSaved) {
    settings.url = pUrl.getValue();
    if (strlen(pKey.getValue())) settings.key = pKey.getValue();
    settings.printerId = atoi(pPrinter.getValue());
    settings_save();
  }
  if (!settings.complete()) {
    ui_message("Setup", "Not finished. Restarting...");
    delay(2000);
    ESP.restart();
  }
  (void)joined; // not joined: the panel keeps trying the saved WiFi below
}

// Joins the saved WiFi. The portal opens only when asked for (or when nothing is set up yet),
// never because the WiFi happens to be down: the panel then keeps trying in the background.
static void connect(bool force)
{
  WiFiManager probe;
  if (force || !settings.complete() || !probe.getWiFiIsSaved()) run_portal();

  if (WiFi.status() != WL_CONNECTED) {
    ui_message("Connecting", "Joining WiFi...");
    WiFi.begin();
    for (uint32_t t = millis(); millis() - t < WIFI_WAIT_MS && WiFi.status() != WL_CONNECTED;) delay(100);
  }
  // Not joined yet: carry on. The status screen says WiFi is down, and the ESP32 keeps retrying.
}

static void start_ota()
{
#ifdef OTA_PASSWORD
  static_assert(sizeof(OTA_PASSWORD) > 8, "OTA_PASSWORD needs at least 8 characters");
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
  bool haveCal = settings_load_touch(cal);
  if (haveCal) display_set_touch_cal(cal);
  const bool resistive = display_needs_calibration();
  bool calibrate = resistive && !haveCal; // a new resistive panel calibrates on first boot

  // A touch during the first two seconds asks for setup. On a resistive panel, which setup is
  // chosen by how long the finger stays, not where it is, so this works even when the
  // calibration is far off.
  bool touched = false, force = false;
  for (uint32_t t = millis(); millis() - t < 2000 && !touched; delay(20)) touched = display_touched();
  if (touched && !resistive) {
    force = true;
  } else if (touched) {
    ui_message("Setup", "Let go now for WiFi and Bambuddy setup.\nKeep holding 4 seconds for touch calibration.");
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
