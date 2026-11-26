// The Bambuddy settings page: served on the panel's own address once it is on the WiFi, so it
// can be filled in from any browser on the network, with time to go and copy the API key.
#pragma once

enum class WebSetup {
  Done,       // saved, and Bambuddy answered (or "finish anyway" was chosen)
  ChangeWifi, // the saved WiFi was forgotten; restart to open the WiFi setup network
  Idle,       // nobody saved for a while, or Cancel on the panel; the settings are as they were
  Calibrate,  // "Calibrate touch" on the panel; come back here afterwards
  Updated,    // new firmware was uploaded and checked; restart to run it
};

// Blocks until one of the above. `firstTime`: the settings aren't complete, so there is no
// time limit and no way to finish without them.
WebSetup setup_web_run(bool firstTime);
