// Checks on a firmware file uploaded through the settings page (setup_web.cpp), before the
// panel starts from it: is it an application image at all, and is it built for this board?
// A file for another board would boot to a white screen with no way back but USB.
// No Arduino in here, so the unit tests can run it.
#pragma once

#include <cstddef>
#include <cstdint>

// Every firmware image carries this, followed by the name of its build and a ';', e.g.
// "bambuddy-panel-board:cyd-st7789;". The build's name is PlatformIO's env name (BOARD_ENV).
#define FW_BOARD_MARKER "bambuddy-panel-board:"

enum class FwVerdict {
  Ok,
  Empty,      // no file, or an empty one
  FullBin,    // starts with 0xFF: a -full.bin, which begins with blank flash before the bootloader
  NotImage,   // doesn't start like an ESP32 application
  TooBig,     // more than the spare application slot holds
  NoStamp,    // no build name in it: not Bambuddy Panel, or from before the stamp
  OtherBoard, // built for another board
};

// Fed the file piece by piece, in any sizes.
struct FwCheck {
  size_t size = 0;
  uint8_t firstByte = 0;
  uint8_t matched = 0; // how much of the marker the last bytes spell
  bool inName = false; // the marker just went by; the name is being read
  bool found = false;
  uint8_t nameLen = 0;
  char board[32] = ""; // the build the file is for, once found
};

void fw_check_feed(FwCheck &c, const uint8_t *data, size_t len);
// What the first byte says, as soon as there is one: Ok, Empty, FullBin or NotImage.
FwVerdict fw_check_start(const FwCheck &c);
// The verdict on the whole file. `maxSize` is the room in the spare slot.
FwVerdict fw_check_end(const FwCheck &c, const char *thisBoard, size_t maxSize);
// Why a file was refused, as text for the page; "" for Ok. Nothing in it needs escaping: a
// build name read from a file is letters, digits, '-' and '_' only.
void fw_refusal(char *out, size_t size, FwVerdict v, const FwCheck &c, const char *thisBoard);
