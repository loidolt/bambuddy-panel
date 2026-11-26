#pragma once
#include <HTTPClient.h>
#include "bambuddy_api.h"
#include "response_json.h"

// HTTPClient reads response headers one byte at a time through Stream. Bound that
// phase too, including responses that never finish a header line.
class ResponseClient : public WiFiClient {
  const uint32_t started = millis();
  size_t headerBytes = 0;
  bool expired = false;
  bool allowed() {
    if (millis() - started >= 15000 || headerBytes >= 8192) {
      if (!expired) stop();
      expired = true;
    }
    return !expired;
  }
public:
  using WiFiClient::read;
  int available() override { return allowed() ? WiFiClient::available() : 0; }
  int read() override {
    if (!allowed()) return -1;
    int c = WiFiClient::read();
    if (c >= 0) ++headerBytes;
    return c;
  }
  bool exceeded() const { return expired; }
};

// HTTP/1.0 clients receive an unchunked JSON body. ArduinoJson reads incrementally.
class ResponseReader {
  WiFiClient &client;
  uint32_t started = millis();
  size_t remaining;
  uint8_t buffer[512];
  size_t pos = 0, count = 0;
public:
  ResponseReader(WiFiClient &stream, size_t budget) : client(stream), remaining(budget) {}
  int read() {
    if (pos < count) return buffer[pos++];
    while (remaining && millis() - started < 10000) {
      if (client.available()) {
        const int n = client.read(buffer, remaining < sizeof(buffer) ? remaining : sizeof(buffer));
        if (n <= 0) return -1;
        remaining -= n;
        count = n;
        pos = 1;
        return buffer[0];
      }
      if (!client.connected()) break;
      delay(1);
    }
    return -1;
  }
  size_t readBytes(char *out, size_t n) {
    size_t i = 0;
    while (i < n) {
      int c = read();
      if (c < 0) break;
      out[i++] = static_cast<char>(c);
    }
    return i;
  }
};

inline int read_json(HTTPClient &http, JsonDocument &doc, const JsonDocument &filter) {
  constexpr int maxBytes = 512 * 1024;
  int size = http.getSize();
  if (size > maxBytes) return ERR_BAD_JSON;
  ResponseReader reader(http.getStream(), size >= 0 ? size : maxBytes);
  auto error = deserializeJson(doc, reader, DeserializationOption::Filter(filter));
  if (error == DeserializationError::NoMemory) return ERR_NO_MEMORY;
  return error ? ERR_BAD_JSON : 200;
}
