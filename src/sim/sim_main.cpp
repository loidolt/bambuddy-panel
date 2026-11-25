// Desktop simulator: runs the panel's real UI code in a window the size of a panel's screen.
//
//   program                 interactive, made-up states (Left/Right to switch, Q to quit)
//   program --live          interactive, read-only status from a real Bambuddy
//                           (BAMBUDDY_URL, BAMBUDDY_KEY, BAMBUDDY_PRINTER in the environment)
//   program --shots <dir>   no window; writes one .bmp per made-up state
//   --size <W>x<H>          with any of the above: another screen than 320x240, e.g. 480x320
//
// Buttons work with the mouse. Commands change the made-up state and print the request the
// panel would send; nothing is ever sent anywhere, in live mode either.
//
// What is the panel's own code here: ui*.cpp, the parsing, the request building and the polling
// round (poller.cpp). What is stood in for: the display and touch drivers, the background task
// and HTTP (curl, in this thread), the LED, and Bambuddy itself in made-up mode.
#include <SDL2/SDL.h>
#include <lvgl.h>

#include <unistd.h>

#include <chrono>
#include <cctype>
#include <string>
#include <vector>

#include "bambuddy_api.h"
#include "bambuddy_parse.h"
#include "display.h"
#include "poller.h"
#include "scenarios.h"
#include "ui.h"

static Snapshot cur;
static int screenW = 320, screenH = 240; // --size
static bool live = false;
static bool ledR, ledG, ledB;

// ---------------------------------------------------------------- what the panel code calls

uint32_t millis()
{
  using namespace std::chrono;
  static const auto start = steady_clock::now();
  return (uint32_t)duration_cast<milliseconds>(steady_clock::now() - start).count();
}

void display_led(bool r, bool g, bool b)
{
  ledR = r;
  ledG = g;
  ledB = b;
}

static void toast(const char *text)
{
  snprintf(cur.toast, sizeof(cur.toast), "%s", text);
  cur.toastSeq++;
}

static void start_next()
{
  snprintf(cur.state, sizeof(cur.state), "RUNNING");
  snprintf(cur.job, sizeof(cur.job), "%s", cur.nextName);
  cur.progress = 0;
  cur.layer = 0;
  cur.remainingMin = cur.nextSeconds / 60;
  cur.nextId = 0;
  cur.queued = cur.queued > 0 ? cur.queued - 1 : 0;
}

// Pretends to be Bambuddy: applies the command to the made-up state.
static void apply(const Job &job)
{
  switch (job.cmd) {
  case Command::Pause:  snprintf(cur.state, sizeof(cur.state), "PAUSE"); break;
  case Command::Resume: snprintf(cur.state, sizeof(cur.state), "RUNNING"); break;
  case Command::Stop:
    snprintf(cur.state, sizeof(cur.state), "FAILED");
    cur.awaitingPlateClear = true; // Bambuddy raises the gate after any print that ends
    break;
  case Command::ClearPlate:
    cur.awaitingPlateClear = false;
    if (cur.nextId != 0 && !cur.nextManual) start_next();
    else snprintf(cur.state, sizeof(cur.state), "IDLE");
    break;
  case Command::SetLight:      cur.light = job.arg != 0; break;
  case Command::ClearErrors:   cur.errorCount = 0; break;
  case Command::ResumeQueue:   cur.queued += cur.held; cur.held = 0; break;
  case Command::StartNext:     start_next(); break;
  case Command::VerdictGood:
  case Command::VerdictReject: cur.verdictPending = false; break;
  case Command::PerformTask:
    cur.tasksDue = cur.tasksWarn = 0;
    for (int i = 0; i < cur.taskCount; i++) {
      Task &t = cur.tasks[i];
      if (t.id == job.arg) {
        t.level = 0;
        t.left = t.days ? 30 : 100;
      }
      if (t.level == 2) cur.tasksDue++;
      if (t.level == 1) cur.tasksWarn++;
    }
    break;
  }
}

bool bambuddy_send(Command c, int arg, int arg2)
{
  Job job = {c, arg, arg2};
  Request r;
  bool any = command_request(job, live ? atoi(getenv("BAMBUDDY_PRINTER")) : 1, r);
  printf("[sim] would send: %s %s %s\n", any ? r.method : "(nothing)", r.path, r.body);
  if (live) {
    toast("Simulator: not sent");
  } else {
    if (any) apply(job);
    char text[sizeof(cur.toast)];
    command_result(job, any ? 200 : 0, text, sizeof(text));
    toast(text);
  }
  cur.seq++;
  return true;
}

// ---------------------------------------------------------------- live mode (read-only)

static std::string env(const char *name) { const char *v = getenv(name); return v ? v : ""; }

// The values go into a shell command, so only what a URL and a number can hold is let through.
static bool safe_url(const std::string &s)
{
  for (char c : s)
    if (!isalnum((unsigned char)c) && !strchr(":/._-[]", c)) return false;
  return s.rfind("http://", 0) == 0;
}

static bool digits(const std::string &s)
{
  if (s.empty()) return false;
  for (char c : s)
    if (!isdigit((unsigned char)c)) return false;
  return true;
}

// The API key goes to curl in a header file only this user can read, never on the command line.
static char headerFile[64] = "";

static void remove_header_file()
{
  if (headerFile[0]) unlink(headerFile);
}

static bool write_header_file()
{
  snprintf(headerFile, sizeof(headerFile), "/tmp/bambuddy-sim-XXXXXX");
  int fd = mkstemp(headerFile); // created 0600
  if (fd < 0) return false;
  atexit(remove_header_file);
  std::string line = "X-API-Key: " + env("BAMBUDDY_KEY") + "\n";
  bool ok = write(fd, line.data(), line.size()) == (ssize_t)line.size();
  close(fd);
  return ok;
}

// GET through curl; returns the HTTP code and fills `doc` on 200. Same contract as the panel's.
static int fetch(const char *path, JsonDocument &doc, const JsonDocument &filter, void *)
{
  std::string cmd = std::string("curl -s -m 4 -w '\\n%{http_code}' -H @") + headerFile + " '" +
                    env("BAMBUDDY_URL") + path + "'";
  FILE *p = popen(cmd.c_str(), "r");
  if (!p) return -1;
  std::string out;
  char buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), p)) > 0) out.append(buf, n);
  pclose(p);

  size_t nl = out.rfind('\n');
  if (nl == std::string::npos) return -1;
  int code = atoi(out.c_str() + nl + 1);
  out.resize(nl);
  if (code == 200 && deserializeJson(doc, out, DeserializationOption::Filter(filter))) return ERR_BAD_JSON;
  return code == 0 ? -1 : code;
}

static Poller poller;

// ---------------------------------------------------------------- screenshots

// As on the panel: LVGL draws a tenth of the screen at a time, and each strip is "sent" to
// the frame, so strip rendering gets exercised too. Both hold 16-bit pixels.
static std::vector<uint16_t> frame, strip;

static void flush_strip(lv_display_t *d, const lv_area_t *a, uint8_t *px)
{
  int w = lv_area_get_width(a);
  for (int y = a->y1; y <= a->y2; y++)
    memcpy(&frame[y * screenW + a->x1], px + (y - a->y1) * w * 2, w * 2);
  lv_display_flush_ready(d);
}

static bool write_bmp(const std::string &path)
{
  FILE *f = fopen(path.c_str(), "wb");
  if (!f) return false;
  const uint32_t row = (screenW * 3 + 3) & ~3u, size = 54 + row * screenH; // rows pad to 4 bytes
  uint8_t h[54] = {'B', 'M'};
  auto put = [&](int at, uint32_t v) { memcpy(h + at, &v, 4); };
  put(2, size);
  put(10, 54);
  put(14, 40);
  put(18, screenW);
  put(22, screenH);
  h[26] = 1;
  h[28] = 24;
  put(34, row * screenH);
  fwrite(h, 1, 54, f);

  const uint8_t pad[3] = {0, 0, 0};
  for (int y = screenH - 1; y >= 0; y--) {
    for (int x = 0; x < screenW; x++) {
      uint16_t p = frame[y * screenW + x];
      uint8_t r = (p >> 11) & 31, g = (p >> 5) & 63, b = p & 31;
      uint8_t bgr[3] = {(uint8_t)(b << 3 | b >> 2), (uint8_t)(g << 2 | g >> 4), (uint8_t)(r << 3 | r >> 2)};
      fwrite(bgr, 1, 3, f);
    }
    fwrite(pad, 1, row - screenW * 3, f);
  }
  fclose(f);
  return true;
}

// A pretend finger for screenshots of pressed buttons.
static bool fingerDown = false;
static int fingerX = 0, fingerY = 0;

static const int TOAST_WAIT_MS = 2700; // a toast is gone after this

static bool save(const std::string &dir, const char *name)
{
  std::string path = dir + "/" + name + ".bmp";
  if (!write_bmp(path)) {
    fprintf(stderr, "can't write %s\n", path.c_str());
    return false;
  }
  lv_mem_monitor_t m;
  lv_mem_monitor(&m);
  printf("%s  led=%c%c%c  lvgl mem peak %u of %u\n", path.c_str(), ledR ? 'R' : '-', ledG ? 'G' : '-',
         ledB ? 'B' : '-', (unsigned)m.max_used, (unsigned)m.total_size);
  return true;
}

static void spin(int ms)
{
  for (uint32_t t = millis(); millis() - t < (uint32_t)ms;) {
    ui_update(cur);
    lv_timer_handler();
    SDL_Delay(5);
  }
}

// Holds the finger at a point for a while, as LVGL would see a real press.
static void press(lv_display_t *disp, int x, int y, int ms)
{
  fingerX = x;
  fingerY = y;
  fingerDown = true;
  spin(ms);
  lv_refr_now(disp);
}

static void release()
{
  fingerDown = false;
  spin(400); // let the release animation finish
}

// A quick tap, then a redraw as the panel's loop would do it.
static void tap(lv_display_t *disp, lv_point_t at)
{
  press(disp, at.x, at.y, 120);
  fingerDown = false;
  spin(400); // until the button has faded back, so the picture doesn't depend on timing
  ui_update(cur);
  lv_refr_now(disp);
}

// The first button on show under `o` whose label has `text` in it; with no text, the first
// that holds an icon. Buttons are looked up by what they say, not where they are, so the
// screenshots work on a screen of any size.
static lv_obj_t *find_button(lv_obj_t *o, const char *text)
{
  if (lv_obj_check_type(o, &lv_button_class) && lv_obj_is_visible(o)) {
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) {
      lv_obj_t *c = lv_obj_get_child(o, i);
      if (text ? lv_obj_check_type(c, &lv_label_class) && strstr(lv_label_get_text(c), text) != nullptr
               : lv_obj_check_type(c, &lv_image_class))
        return o;
    }
  }
  for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++)
    if (lv_obj_t *found = find_button(lv_obj_get_child(o, i), text)) return found;
  return nullptr;
}

// The middle of that button. Stops the run when the screen has no such button: the
// screenshots would be of something else than their names say.
static lv_point_t button(const char *text)
{
  lv_obj_update_layout(lv_screen_active());
  lv_obj_t *b = find_button(lv_screen_active(), text);
  if (!b) {
    fprintf(stderr, "no button \"%s\" on the screen\n", text ? text : "(icon)");
    exit(1);
  }
  lv_area_t a;
  lv_obj_get_coords(b, &a);
  return {(a.x1 + a.x2) / 2, (a.y1 + a.y2) / 2};
}

static int run_shots(const std::string &dir)
{
  frame.resize(screenW * screenH);
  strip.resize(screenW * (screenH / 10));
  lv_display_t *disp = lv_display_create(screenW, screenH);
  lv_display_set_flush_cb(disp, flush_strip);
  lv_display_set_buffers(disp, strip.data(), nullptr, strip.size() * 2, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_indev_t *indev = lv_indev_create(); // the UI asks whether a finger is down
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, [](lv_indev_t *, lv_indev_data_t *d) {
    d->point.x = fingerX;
    d->point.y = fingerY;
    d->state = fingerDown ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
  });

  ui_begin();
  for (int i = 0; i < scenarioCount; i++) {
    cur = scenarios[i].make();
    cur.seq = i + 1;
    ui_update(cur);
    lv_refr_now(disp);
    if (!save(dir, scenarios[i].name)) return 1;
  }

  // Let the last scenario's toast time out so it isn't in the pictures below.
  spin(TOAST_WAIT_MS);

  // Maintenance: open it with the wrench, page once, go back.
  cur = scenarios[0].make();
  cur.seq = scenarioCount + 2;
  ui_update(cur);
  tap(disp, button(nullptr)); // the wrench
  if (!save(dir, "maintenance")) return 1;
  tap(disp, button("More"));
  if (!save(dir, "maintenance-page-2")) return 1;
  tap(disp, button("Back"));
  if (!save(dir, "maintenance-closed")) return 1;

  ui_calibrate(2, 4, screenW - 1 - screenH / 12, screenH / 12); // as calibrate.cpp places it
  if (!save(dir, "calibrate")) return 1;

  // Pressed buttons: a tap button and a hold button, on the first scenario.
  cur = scenarios[0].make();
  cur.seq = scenarioCount + 3;
  ui_update(cur);
  lv_refr_now(disp);
  lv_point_t at = button("Pause");
  press(disp, at.x, at.y, 300);
  if (!save(dir, "pressed-tap")) return 1;
  release();
  cur = scenarios[0].make(); // the tap paused the made-up print; start over
  cur.seq = 1000;
  spin(50);
  at = button("Hold to stop");
  press(disp, at.x, at.y, 600);
  if (!save(dir, "pressed-hold")) return 1;
  release();

  // "Later" on the outcome question: the plate-clear screen comes up for that print.
  for (int i = 0; i < scenarioCount; i++)
    if (!strcmp(scenarios[i].name, "verdict")) cur = scenarios[i].make();
  cur.seq = 2000;
  spin(TOAST_WAIT_MS);
  tap(disp, button("Later"));
  spin(50);
  lv_refr_now(disp);
  if (!save(dir, "verdict-later")) return 1;
  return 0;
}

// ---------------------------------------------------------------- interactive window

static int scenario = 0;

static void load_scenario(int i)
{
  scenario = (i + scenarioCount) % scenarioCount;
  uint32_t seq = cur.seq, toasts = cur.toastSeq;
  cur = scenarios[scenario].make();
  cur.seq = seq + 1;
  if (cur.toastSeq == 0) cur.toastSeq = toasts; // no new toast
  else cur.toastSeq = toasts + 1;
}

static bool pressed(SDL_Scancode key)
{
  static bool was[SDL_NUM_SCANCODES];
  bool now = SDL_GetKeyboardState(nullptr)[key];
  bool edge = now && !was[key];
  was[key] = now;
  return edge;
}

static void update_title(lv_display_t *disp)
{
  static std::string shown;
  std::string led = ledG && ledB ? "cyan" : ledR && ledG ? "yellow" : ledR && ledB ? "purple" : ledR ? "red" : ledG ? "green" : ledB ? "blue" : "off";
  std::string title = std::string("Bambuddy Panel - ") + (live ? "live" : scenarios[scenario].name) + " - LED " + led;
  if (title == shown) return;
  shown = title;
  lv_sdl_window_set_title(disp, title.c_str());
}

static int run_window()
{
  lv_display_t *disp = lv_sdl_window_create(screenW, screenH);
  lv_sdl_window_set_zoom(disp, screenW <= 480 ? 2 : 1); // small panels are shown at 2x
  lv_sdl_mouse_create();
  ui_begin();

  if (live) ui_message("Connecting", "Asking Bambuddy...");
  else load_scenario(0);

  uint32_t lastPoll = 0;
  for (;;) {
    if (live) {
      if (lastPoll == 0 || millis() - lastPoll >= 4000) {
        poll_once(poller, cur, fetch, nullptr); // blocks the window for a moment; the panel polls in the background
        lastPoll = millis() | 1;
      }
    } else {
      if (pressed(SDL_SCANCODE_RIGHT)) load_scenario(scenario + 1);
      if (pressed(SDL_SCANCODE_LEFT)) load_scenario(scenario - 1);
    }
    if (pressed(SDL_SCANCODE_Q)) return 0;

    ui_update(cur);
    update_title(disp);
    lv_timer_handler();
    SDL_Delay(5);
  }
}

int main(int argc, char **argv)
{
  std::string shots;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--live") live = true;
    else if (a == "--shots" && i + 1 < argc) shots = argv[++i];
    else if (a == "--size" && i + 1 < argc && sscanf(argv[i + 1], "%dx%d", &screenW, &screenH) == 2 &&
             screenW >= 160 && screenW <= 1920 && screenH >= 120 && screenH <= 1080) i++;
    else {
      fprintf(stderr, "usage: %s [--live | --shots <dir>] [--size <W>x<H>]\n", argv[0]);
      return 2;
    }
  }
  if (live) {
    if (env("BAMBUDDY_URL").empty() || env("BAMBUDDY_KEY").empty() || env("BAMBUDDY_PRINTER").empty()) {
      fprintf(stderr, "--live needs BAMBUDDY_URL, BAMBUDDY_KEY and BAMBUDDY_PRINTER in the environment\n");
      return 2;
    }
    char url[256];
    snprintf(url, sizeof(url), "%s", env("BAMBUDDY_URL").c_str());
    normalize_url(url, sizeof(url));
    setenv("BAMBUDDY_URL", url, 1);
    if (!safe_url(url) || !digits(env("BAMBUDDY_PRINTER"))) {
      fprintf(stderr, "BAMBUDDY_URL must be an http:// address and BAMBUDDY_PRINTER a number\n");
      return 2;
    }
    if (env("BAMBUDDY_KEY").find_first_of("\r\n") != std::string::npos || !write_header_file()) {
      fprintf(stderr, "can't pass the API key to curl\n");
      return 2;
    }
    poller.printerId = atoi(env("BAMBUDDY_PRINTER").c_str());
  }

  lv_init();
  lv_tick_set_cb(millis);
  return shots.empty() ? run_window() : run_shots(shots);
}
