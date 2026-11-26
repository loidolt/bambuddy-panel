// ESP-IDF owns each request on its server task. A mutex serializes its handlers
// with the setup screen, including all LVGL and SPI access.
#pragma once
#include <Arduino.h>
#include <esp_http_server.h>


class SettingsHttp {
public:
  using Handler = void (*)();
  bool begin(Handler root, Handler save, Handler finish, Handler check, Handler wifi, Handler update);
  void lock() { xSemaphoreTake(mutex, portMAX_DELAY); }
  void unlock() { xSemaphoreGive(mutex); }
  void closeRequests() { stopping = true; } // called with the lock held
  void stop();
  String arg(const char *name) const;
  void send(int status, const char *type, const String &body);
  size_t contentLength() const { return request->content_len; }
  int read(uint8_t *data, size_t size);
  void allowUpload();

private:
  static esp_err_t receive(httpd_req_t *req);
  void dispatch();
  bool fields(const String &encoded);
  httpd_handle_t handle = nullptr;
  httpd_req_t *request = nullptr;
  SemaphoreHandle_t mutex = nullptr;
  bool stopping = false;
  Handler handlers[6]{};
  String values[5];
};
