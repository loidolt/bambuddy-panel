#include "settings_http.h"
#include <lwip/sockets.h>
#include <esp_timer.h>

namespace {
struct Deadline { int64_t end; };
constexpr int64_t SECOND = 1000000;

int bounded_recv(httpd_handle_t handle, int sock, char *buf, size_t len, int flags)
{
  auto *limit = static_cast<Deadline *>(httpd_sess_get_ctx(handle, sock));
  if (!limit || esp_timer_get_time() >= limit->end) return HTTPD_SOCK_ERR_FAIL;
  int n = recv(sock, buf, len, flags);
  if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return HTTPD_SOCK_ERR_TIMEOUT;
  return n < 0 ? HTTPD_SOCK_ERR_FAIL : n;
}

esp_err_t opened(httpd_handle_t handle, int sock)
{
  auto *limit = static_cast<Deadline *>(malloc(sizeof(Deadline)));
  if (!limit) return ESP_ERR_NO_MEM;
  limit->end = esp_timer_get_time() + 10 * SECOND;
  httpd_sess_set_ctx(handle, sock, limit, free);
  return httpd_sess_set_recv_override(handle, sock, bounded_recv);
}

const char *const names[] = {"code", "url", "key", "printer", "ticket"};
const char *const paths[] = {"/", "/save", "/finish", "/check", "/wifi", "/update"};

int hex_value(char c)
{
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}
}

bool SettingsHttp::fields(const String &encoded)
{
  for (unsigned start = 0; start < encoded.length();) {
    int end = encoded.indexOf('&', start);
    if (end < 0) end = encoded.length();
    int eq = encoded.indexOf('=', start);
    if (eq < 0 || eq >= end) return false;
    String name = encoded.substring(start, eq);
    for (unsigned k = 0; k < sizeof(names) / sizeof(*names); ++k) {
      if (name != names[k]) continue;
      String value;
      if (!value.reserve(end - eq)) return false;
      for (int i = eq + 1; i < end; i++) {
        char c = encoded[i];
        if (c == '+') c = ' ';
        else if (c == '%') {
          if (i + 2 >= end) return false;
          int hi = hex_value(encoded[++i]), lo = hex_value(encoded[++i]);
          if (hi < 0 || lo < 0) return false;
          c = static_cast<char>(hi * 16 + lo);
        }
        if (static_cast<unsigned char>(c) < 32 || c == 127 || !value.concat(c)) return false;
      }
      values[k] = std::move(value);
      break;
    }
    start = end + 1;
  }
  return true;
}

String SettingsHttp::arg(const char *name) const
{
  for (unsigned i = 0; i < sizeof(names) / sizeof(*names); ++i) if (!strcmp(name, names[i])) return values[i];
  return "";
}

void SettingsHttp::send(int status, const char *type, const String &body)
{
  const char *s = status == 200 ? "200 OK" : status == 403 ? "403 Forbidden" :
                  status == 413 ? "413 Content Too Large" : status == 415 ? "415 Unsupported Media Type" :
                  "400 Bad Request";
  httpd_resp_set_status(request, s);
  httpd_resp_set_type(request, type);
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  httpd_resp_set_hdr(request, "Referrer-Policy", "no-referrer");
  httpd_resp_set_hdr(request, "Content-Security-Policy", "frame-ancestors 'none'; base-uri 'none'; form-action 'self'");
  httpd_resp_set_hdr(request, "Connection", "close");
  httpd_resp_send(request, body.c_str(), body.length());
}

int SettingsHttp::read(uint8_t *data, size_t size)
{
  return httpd_req_recv(request, reinterpret_cast<char *>(data), size);
}

void SettingsHttp::allowUpload()
{
  auto *limit = static_cast<Deadline *>(httpd_sess_get_ctx(handle, httpd_req_to_sockfd(request)));
  if (limit) limit->end = esp_timer_get_time() + 120 * SECOND;
}

void SettingsHttp::dispatch()
{
  for (auto &value : values) value = "";
  String uri(request->uri);
  int q = uri.indexOf('?');
  if (q >= 0) {
    if (!fields(uri.substring(q + 1))) return send(400, "text/plain", "Invalid URL fields.");
    uri.remove(q);
  }
  int route = 0;
  while (route < 6 && uri != paths[route]) ++route;
  if (route == 6) return send(400, "text/plain", "Unknown settings page.");
  if (route) {
    char mime[96] = {};
    if (httpd_req_get_hdr_value_str(request, "Content-Type", mime, sizeof(mime)) != ESP_OK)
      return send(415, "text/plain", "Reload the settings page.");
    if (char *params = strchr(mime, ';')) *params = 0;
    if (strcasecmp(mime, route == 5 ? "application/octet-stream" : "application/x-www-form-urlencoded"))
      return send(415, "text/plain", "Reload the settings page.");
    if (route != 5) {
      if (request->content_len > 4096) return send(413, "text/plain", "Settings are too long.");
      String body;
      if (!body.reserve(request->content_len + 1)) return send(400, "text/plain", "Insufficient memory.");
      char buf[256];
      while (body.length() < request->content_len) {
        size_t left = request->content_len - body.length();
        int n = read(reinterpret_cast<uint8_t *>(buf), std::min(sizeof(buf), left));
        if (n <= 0 || memchr(buf, 0, n) || !body.concat(buf, n)) return send(400, "text/plain", "Incomplete settings.");
      }
      if (!fields(body)) return send(400, "text/plain", "Invalid settings.");
    }
  }
  handlers[route]();
}

esp_err_t SettingsHttp::receive(httpd_req_t *req)
{
  auto &s = *static_cast<SettingsHttp *>(req->user_ctx);
  s.lock();
  if (!s.stopping) {
    s.request = req;
    s.dispatch();
    s.request = nullptr;
  }
  s.unlock();
  return ESP_FAIL; // close this request's socket, including any unread body
}

bool SettingsHttp::begin(Handler root, Handler save, Handler finish, Handler check, Handler wifi, Handler update)
{
  Handler routes[] = {root, save, finish, check, wifi, update};
  memcpy(handlers, routes, sizeof(routes));
  mutex = xSemaphoreCreateMutex();
  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.stack_size = 12288;
  cfg.open_fn = opened;
  cfg.recv_wait_timeout = cfg.send_wait_timeout = 3;
  cfg.max_open_sockets = 3;
  cfg.lru_purge_enable = true;
  if (!mutex || httpd_start(&handle, &cfg) != ESP_OK) return false;
  for (int i = 0; i < 6; i++) {
    httpd_uri_t uri{};
    uri.uri = paths[i];
    uri.method = i ? HTTP_POST : HTTP_GET;
    uri.handler = receive;
    uri.user_ctx = this;
    if (httpd_register_uri_handler(handle, &uri) != ESP_OK) return false;
  }
  return true;
}

void SettingsHttp::stop()
{
  if (mutex) {
    lock();
    closeRequests();
    unlock();
  }
  if (handle) httpd_stop(handle);
  if (mutex) vSemaphoreDelete(mutex);
}
