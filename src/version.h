// The firmware's version, as shown in the serial log, on the settings page and to Bambuddy
// (User-Agent). A release build gets it from its tag (.github/workflows/release.yml builds
// with -DFW_VERSION=\"1.2.3\"); any other build is "dev".
#pragma once

#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif

#define FW_USER_AGENT "bambuddy-panel/" FW_VERSION
