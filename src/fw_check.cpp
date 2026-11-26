#include "fw_check.h"

#include <cstdio>
#include <cstring>

static const char MARKER[] = FW_BOARD_MARKER;
static const uint8_t MARKER_LEN = sizeof(MARKER) - 1;

static bool name_char(uint8_t b)
{
  return (b >= 'a' && b <= 'z') || (b >= '0' && b <= '9') || b == '-' || b == '_';
}

// How much of the marker the stream spells after `b`, given it spelled `matched` before.
static uint8_t marker_step(uint8_t matched, uint8_t b)
{
  // The longest start of the marker that ends with the bytes seen plus this one. Mostly the
  // first try; the rest is for a marker that begins inside something marker-like ("bambamb...").
  for (uint8_t keep = matched < MARKER_LEN ? matched : MARKER_LEN - 1;; keep--) {
    if ((uint8_t)MARKER[keep] == b && memcmp(MARKER, MARKER + matched - keep, keep) == 0) return keep + 1;
    if (keep == 0) return 0;
  }
}

void fw_check_feed(FwCheck &c, const uint8_t *data, size_t len)
{
  if (len && c.size == 0) c.firstByte = data[0];
  c.size += len;
  if (c.found) return;
  for (size_t i = 0; i < len; i++) {
    uint8_t b = data[i];
    if (c.inName) {
      if (b == ';' && c.nameLen > 0) {
        c.board[c.nameLen] = 0;
        c.found = true;
        return;
      }
      if (name_char(b) && c.nameLen < sizeof(c.board) - 1) c.board[c.nameLen++] = (char)b;
      else c.inName = false; // the marker alone, as in this file's own code: keep looking
    }
    c.matched = marker_step(c.matched, b);
    if (c.matched == MARKER_LEN) {
      c.inName = true;
      c.nameLen = 0;
    }
  }
}

FwVerdict fw_check_start(const FwCheck &c)
{
  if (c.size == 0) return FwVerdict::Empty;
  if (c.firstByte == 0xFF) return FwVerdict::FullBin;
  if (c.firstByte != 0xE9) return FwVerdict::NotImage;
  return FwVerdict::Ok;
}

FwVerdict fw_check_end(const FwCheck &c, const char *thisBoard, size_t maxSize)
{
  FwVerdict v = fw_check_start(c);
  if (v != FwVerdict::Ok) return v;
  if (c.size > maxSize) return FwVerdict::TooBig;
  if (!c.found) return FwVerdict::NoStamp;
  if (strcmp(c.board, thisBoard) != 0) return FwVerdict::OtherBoard;
  return FwVerdict::Ok;
}

void fw_refusal(char *out, size_t size, FwVerdict v, const FwCheck &c, const char *thisBoard)
{
  switch (v) {
  case FwVerdict::Ok: snprintf(out, size, "%s", ""); break;
  case FwVerdict::Empty: snprintf(out, size, "Choose the firmware file first."); break;
  case FwVerdict::FullBin:
    snprintf(out, size,
             "That looks like a -full.bin, which is for flashing over USB. Pick the -app.bin: "
             "bambuddy-panel-&lt;version&gt;-%s-app.bin.",
             thisBoard);
    break;
  case FwVerdict::NotImage:
    snprintf(out, size, "That file isn't Bambuddy Panel firmware. Pick bambuddy-panel-&lt;version&gt;-%s-app.bin.",
             thisBoard);
    break;
  case FwVerdict::TooBig: snprintf(out, size, "That file is too big for this panel."); break;
  case FwVerdict::NoStamp:
    snprintf(out, size,
             "That file doesn't say which board it is for, so it isn't Bambuddy Panel firmware this panel "
             "can check. This panel is %s.",
             thisBoard);
    break;
  case FwVerdict::OtherBoard:
    snprintf(out, size, "That file is for %s; this panel is %s. Pick bambuddy-panel-&lt;version&gt;-%s-app.bin.",
             c.board, thisBoard, thisBoard);
    break;
  }
}
