// Desktop simulator: runs the panel's real ui.cpp in a 320x240 window.
//
//   program                 interactive, made-up states (Left/Right to switch, Q to quit)
//   program --live          interactive, read-only status from a real Bambuddy
//                           (BAMBUDDY_URL, BAMBUDDY_KEY, BAMBUDDY_PRINTER in the environment)
//   program --shots <dir>   no window; writes one .bmp per made-up state
//
// Buttons work with the mouse. Commands change the made-up state; they are never sent anywhere.
#include <SDL2/SDL.h>
#include <lvgl.h>

#include <chrono>
#include <string>

#include "bambuddy_parse.h"
#include "board.h"
#include "display.h"
#include "scenarios.h"
#include "ui.h"

static Snapshot cur;
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
void bambuddy_send(Command c, int arg)
{
  if (live) {
    toast("Simulator: not sent");
    return;
  }
  switch (c) {
  case Command::Pause:       snprintf(cur.state, sizeof(cur.state), "PAUSE"); toast("Paused"); break;
  case Command::Resume:      snprintf(cur.state, sizeof(cur.state), "RUNNING"); toast("Resumed"); break;
  case Command::Stop:
    snprintf(cur.state, sizeof(cur.state), "FAILED");
    cur.awaitingPlateClear = true;
    toast("Stopped");
    break;
  case Command::ClearPlate:
    cur.awaitingPlateClear = false;
    toast("Plate cleared");
    if (cur.nextId != 0 && !cur.nextManual) start_next();
    else snprintf(cur.state, sizeof(cur.state), "IDLE");
    break;
  case Command::ToggleLight: cur.light = !cur.light; toast(cur.light ? "Light on" : "Light off"); break;
  case Command::ClearErrors: cur.errorCount = 0; toast("Errors cleared"); break;
  case Command::ResumeQueue: toast("Queue resumed"); break;
  case Command::StartNext:   start_next(); toast("Starting"); break;
  case Command::VerdictGood:   cur.verdictPending = false; toast("Marked good"); break;
  case Command::VerdictReject: cur.verdictPending = false; toast("Marked bad"); break;
  case Command::PerformTask:
    cur.tasksDue = cur.tasksWarn = 0;
    for (int i = 0; i < cur.taskCount; i++) {
      Task &t = cur.tasks[i];
      if (t.id == arg) {
        t.level = 0;
        t.left = t.days ? 30 : 100;
      }
      if (t.level == 2) cur.tasksDue++;
      if (t.level == 1) cur.tasksWarn++;
    }
    toast("Marked done");
    break;
  }
  cur.seq++;
}

// ---------------------------------------------------------------- live mode (read-only)

static std::string env(const char *name) { const char *v = getenv(name); return v ? v : ""; }

// GET through curl; returns the HTTP code and fills `doc` on 200.
static int fetch(const std::string &path, JsonDocument &doc, JsonDocument &filter)
{
  std::string cmd = "curl -s -m 4 -w '\\n%{http_code}' -H \"X-API-Key: $BAMBUDDY_KEY\" '" +
                    env("BAMBUDDY_URL") + "/api/v1" + path + "'";
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
  if (code == 200 && deserializeJson(doc, out, DeserializationOption::Filter(filter))) return -102;
  return code == 0 ? -1 : code;
}

static void poll_live()
{
  std::string printer = env("BAMBUDDY_PRINTER");
  {
    JsonDocument filter, doc;
    status_filter(filter);
    cur.httpCode = fetch("/printers/" + printer + "/status", doc, filter);
    cur.reachable = cur.httpCode == 200;
    if (cur.reachable) parse_status(doc, cur);
  }
  if (cur.reachable) {
    JsonDocument filter, doc;
    queue_filter(filter);
    if (fetch("/queue/?status=pending&printer_id=" + printer, doc, filter) == 200) parse_queue(doc, cur);
  }
  if (cur.reachable) {
    JsonDocument filter, doc;
    tasks_filter(filter);
    if (fetch("/maintenance/printers/" + printer, doc, filter) == 200) parse_tasks(doc, cur);
  }
  if (cur.reachable) {
    JsonDocument filter, doc;
    last_print_filter(filter);
    if (fetch("/archives/last-per-printer", doc, filter) == 200) parse_last_print(doc, atoi(printer.c_str()), cur);
  }
  cur.seq++;
}

// ---------------------------------------------------------------- screenshots

alignas(4) static uint8_t frame[SCREEN_W * SCREEN_H * 2];

static bool write_bmp(const std::string &path)
{
  FILE *f = fopen(path.c_str(), "wb");
  if (!f) return false;
  const uint32_t row = SCREEN_W * 3, size = 54 + row * SCREEN_H;
  uint8_t h[54] = {'B', 'M'};
  auto put = [&](int at, uint32_t v) { memcpy(h + at, &v, 4); };
  put(2, size);
  put(10, 54);
  put(14, 40);
  put(18, SCREEN_W);
  put(22, SCREEN_H);
  h[26] = 1;
  h[28] = 24;
  put(34, row * SCREEN_H);
  fwrite(h, 1, 54, f);

  const uint16_t *px = (const uint16_t *)frame;
  for (int y = SCREEN_H - 1; y >= 0; y--) {
    for (int x = 0; x < SCREEN_W; x++) {
      uint16_t p = px[y * SCREEN_W + x];
      uint8_t r = (p >> 11) & 31, g = (p >> 5) & 63, b = p & 31;
      uint8_t bgr[3] = {(uint8_t)(b << 3 | b >> 2), (uint8_t)(g << 2 | g >> 4), (uint8_t)(r << 3 | r >> 2)};
      fwrite(bgr, 1, 3, f);
    }
  }
  fclose(f);
  return true;
}

// A pretend finger for screenshots of pressed buttons.
static bool fingerDown = false;
static int fingerX = 0, fingerY = 0;

static bool save(const std::string &dir, const char *name)
{
  std::string path = dir + "/" + name + ".bmp";
  if (!write_bmp(path)) {
    fprintf(stderr, "can't write %s\n", path.c_str());
    return false;
  }
  printf("%s  led=%c%c%c\n", path.c_str(), ledR ? 'R' : '-', ledG ? 'G' : '-', ledB ? 'B' : '-');
  return true;
}

// Holds the finger at a point for a while, as LVGL would see a real press.
static void press(lv_display_t *disp, int x, int y, int ms)
{
  fingerX = x;
  fingerY = y;
  fingerDown = true;
  for (uint32_t t = millis(); millis() - t < (uint32_t)ms;) {
    lv_timer_handler();
    SDL_Delay(5);
  }
  lv_refr_now(disp);
}

static void release()
{
  fingerDown = false;
  for (uint32_t t = millis(); millis() - t < 400;) { // let the release animation finish
    lv_timer_handler();
    SDL_Delay(5);
  }
}

// A quick tap, then a redraw as the panel's loop would do it.
static void tap(lv_display_t *disp, int x, int y)
{
  press(disp, x, y, 120);
  fingerDown = false;
  for (uint32_t t = millis(); millis() - t < 120;) {
    lv_timer_handler();
    SDL_Delay(5);
  }
  ui_update(cur);
  lv_refr_now(disp);
}

static int run_shots(const std::string &dir)
{
  lv_display_t *disp = lv_display_create(SCREEN_W, SCREEN_H);
  lv_display_set_flush_cb(disp, [](lv_display_t *d, const lv_area_t *, uint8_t *) { lv_display_flush_ready(d); });
  lv_display_set_buffers(disp, frame, nullptr, sizeof(frame), LV_DISPLAY_RENDER_MODE_DIRECT);
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
  for (uint32_t t = millis(); millis() - t < 2700;) {
    ui_update(cur);
    lv_timer_handler();
    SDL_Delay(20);
  }

  // Maintenance: open it with the wrench, page once, go back.
  cur = scenarios[0].make();
  cur.seq = scenarioCount + 2;
  ui_update(cur);
  tap(disp, 165, 19);
  if (!save(dir, "maintenance")) return 1;
  tap(disp, 270, 20);
  if (!save(dir, "maintenance-page-2")) return 1;
  tap(disp, 44, 20);
  if (!save(dir, "maintenance-closed")) return 1;

  ui_calibrate(2, 4, SCREEN_W - 21, 20);
  if (!save(dir, "calibrate")) return 1;

  // Pressed buttons: a tap button and a hold button, on the first scenario.
  cur = scenarios[0].make();
  cur.seq = scenarioCount + 1;
  ui_update(cur);
  lv_refr_now(disp);
  press(disp, 60, 205, 300);
  if (!save(dir, "pressed-tap")) return 1;
  release();
  press(disp, 178, 205, 600);
  if (!save(dir, "pressed-hold")) return 1;
  fingerDown = false;
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
  lv_display_t *disp = lv_sdl_window_create(SCREEN_W, SCREEN_H);
  lv_sdl_window_set_zoom(disp, 2);
  lv_sdl_mouse_create();
  ui_begin();

  if (live) ui_message("Connecting", "Asking Bambuddy...");
  else load_scenario(0);

  uint32_t lastPoll = 0;
  for (;;) {
    if (live) {
      if (lastPoll == 0 || millis() - lastPoll >= 4000) {
        poll_live();
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
    else {
      fprintf(stderr, "usage: %s [--live | --shots <dir>]\n", argv[0]);
      return 2;
    }
  }
  if (live && (env("BAMBUDDY_URL").empty() || env("BAMBUDDY_KEY").empty() || env("BAMBUDDY_PRINTER").empty())) {
    fprintf(stderr, "--live needs BAMBUDDY_URL, BAMBUDDY_KEY and BAMBUDDY_PRINTER in the environment\n");
    return 2;
  }

  lv_init();
  lv_tick_set_cb(millis);
  return shots.empty() ? run_window() : run_shots(shots);
}
