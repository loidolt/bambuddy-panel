// The texts of the full-screen messages at start-up and during setup, in one place so the
// simulator can show each screen with the words the panel really uses (see sim_main.cpp).
// Those with a % are snprintf formats; the comment says what goes in.
#pragma once

#define MSG_STARTING_TITLE "Starting"
#define MSG_STARTING_BODY "Touch the screen now for setup."

#define MSG_SETUP_TITLE "Setup"
#define MSG_SETUP_HOLD_BODY "Let go now for WiFi and Bambuddy setup.\nKeep holding 4 seconds for touch calibration."
#define MSG_SETUP_NO_WIFI_SAVED "WiFi isn't set up yet. Restarting..."
#define MSG_SETUP_WIFI_FORGOTTEN "WiFi forgotten. Restarting into WiFi setup..."
#define MSG_SETUP_NOT_ON_WIFI "Not on the WiFi, so the Bambuddy settings can't be entered. Restarting..."

// The setup network: a QR code that joins it, and the same to type by hand.
#define MSG_WIFI_SETUP_TITLE "WiFi setup"
#define MSG_WIFI_QR "WIFI:T:WPA;S:%s;P:%s;;" // network, password
#define MSG_WIFI_LABEL_NAME "Scan, or join"
#define MSG_WIFI_LABEL_PASSWORD "password"
#define MSG_WIFI_NOTE "then choose your WiFi"

#define MSG_CONNECTING_TITLE "Connecting"
#define MSG_CONNECTING_WIFI "Joining WiFi..."
#define MSG_CONNECTING_BAMBUDDY "Asking Bambuddy..."

// The settings page: a QR code that opens it with the code filled in, and the same to type.
#define MSG_WEB_FIRST_TITLE "Bambuddy setup"
#define MSG_WEB_LATER_TITLE "Settings"
#define MSG_WEB_QR "http://%s/?code=%s" // address, code
#define MSG_WEB_LABEL_ADDRESS "Scan, or open"
#define MSG_WEB_LABEL_CODE "code"
#define MSG_WEB_CANCEL "Cancel"
#define MSG_WEB_CALIBRATE "Calibrate touch"

#define MSG_UPDATING_TITLE "Updating"
#define MSG_UPDATING_BODY "Don't unplug."

#define MSG_CAL_TITLE "Calibration"
#define MSG_CAL_KEEP_SAVED "Keeping the saved calibration."
#define MSG_CAL_KEEP_STANDARD "Using standard values.\nHold the screen at start-up to calibrate."
#define MSG_CAL_NO_TOUCH "No touch. %s" // one of the two MSG_CAL_KEEP_ texts
#define MSG_CAL_GIVING_UP "Giving up. %s" // as above
#define MSG_CAL_SAVED "Saved."
#define MSG_CAL_RETRY "Those taps didn't line up.\nTry again."
