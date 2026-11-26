// Desktop simulator: runs the panel's real UI code in a window the size of a panel's screen.
//
//   program                 interactive, made-up states (Left/Right to switch, Q to quit)
//   program --live          interactive, read-only status from a real Bambuddy
//                           (BAMBUDDY_URL, BAMBUDDY_KEY, BAMBUDDY_PRINTER in the environment)
//   program --shots <dir>   no window; writes a .bmp of every screen the panel can show, the
//                           same with the touch targets framed (targets/), and checks.txt
//   program --web <dir>     writes the settings web page as the panel would serve it, in each
//                           of its states, as .html files
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
#include <sys/stat.h>
#include <string>
#include <vector>

#include "bambuddy_api.h"
#include "bambuddy_parse.h"
#include "checks.h"
#include "display.h"
#include "messages.h"
#include "poller.h"
#include "scenarios.h"
#include "setup_page.h"
#include "spools.h"
#include "ui.h"

static Snapshot cur;
static int screenW = 320, screenH = 240; // --size
static bool live = false;
static bool ledR, ledG, ledB;

// ---------------------------------------------------------------- what the panel code calls

// For the picture of a dimmed panel: when set, the clock jumps a minute ahead once that many
// milliseconds have passed without a touch, so the screenshots needn't wait it out.
static int restAfterMs = 0;
static uint32_t skipped = 0, quietSince = 0;
static bool fingerDown = false; // a pretend finger, for screenshots of pressed buttons

uint32_t millis()
{
  using namespace std::chrono;
  static const auto start = steady_clock::now();
  uint32_t real = (uint32_t)duration_cast<milliseconds>(steady_clock::now() - start).count();
  if (fingerDown || !restAfterMs) quietSince = real;
  else if (real - quietSince > (uint32_t)restAfterMs) {
    skipped += 61000;
    quietSince = real;
  }
  return real + skipped;
}

void display_led(bool r, bool g, bool b)
{
  ledR = r;
  ledG = g;
  ledB = b;
}

static int backlight = 100;
void display_brightness(uint8_t percent) { backlight = percent; }

void panel_restart(bool intoSettings)
{
  printf("[sim] the panel would restart%s\n", intoSettings ? " into its settings screen" : "");
}

static void toast(const char *text, bool bad)
{
  snprintf(cur.toast, sizeof(cur.toast), "%s", text);
  cur.toastBad = bad;
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

// ---------------------------------------------------------------- made-up inventory

static SpoolStore inventory;
static int spoolsReadyAfterMs = 700; // how long the pretend Bambuddy takes over the list
static uint32_t spoolsAskedAt = 0;
static bool spoolsFail = false;      // for the picture of a list that didn't come

static void make_inventory()
{
  static const struct {
    const char *material, *brand;
  } kinds[] = {{"PLA", "Bambu Lab"}, {"PLA", "Prusament"}, {"PLA", "eSun"},      {"PETG", "Prusament"},
               {"PETG", "Bambu Lab"}, {"ABS", "Bambu Lab"}, {"TPU", "Polymaker"}, {"PA6-CF", "Polymaker"}};
  static const struct {
    const char *name;
    uint32_t rgb;
  } colours[] = {{"Jade White", 0xF5F5F0}, {"Galaxy Black", 0x101010}, {"Fire Engine Red", 0xC12E1F},
                 {"Azure Blue", 0x2F6FDB}, {"Grass Green", 0x1F8A3B},  {"Signal Yellow with a long name", 0xF2C037}};
  if (!inventory.items) inventory.items = (Spool *)calloc(MAX_SPOOLS, sizeof(Spool));
  inventory.count = 0;
  int id = 1;
  for (const auto &k : kinds)
    for (const auto &c : colours) {
      if ((id * 7) % 5 == 0) { // not every brand has every colour
        id++;
        continue;
      }
      Spool &sp = inventory.items[inventory.count++];
      sp = Spool();
      sp.id = id++;
      snprintf(sp.material, sizeof(sp.material), "%s", k.material);
      spool_name(sp.name, sizeof(sp.name), k.brand, nullptr, c.name);
      sp.rgb = c.rgb;
      sp.remainG = id % 9 == 0 ? -1 : id % 11 == 0 ? 0 : (id * 137) % 1000;
    }
  inventory.total = inventory.count;
}

int bambuddy_spools(const char *material, int offset, Spool *out, int max, int &total, SpoolState &state)
{
  state = inventory.state;
  if (state != SpoolState::Ready) {
    total = 0;
    return 0;
  }
  return spools_page(inventory, material, offset, out, max, total);
}

int bambuddy_spool_materials(char out[][SPOOL_MATERIAL], int max)
{
  return inventory.state == SpoolState::Ready ? spools_materials(inventory, out, max) : 0;
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
  case Command::ClearErrors:   cur.errorCount = 0; break;
  case Command::ResumeQueue:   cur.queued += cur.held; cur.held = 0; break;
  case Command::StartNext:     start_next(); break;
  case Command::VerdictGood:
  case Command::VerdictReject: cur.verdictPending = false; break;
  case Command::AssignSpool:
  case Command::UnassignSpool: {
    int ams = slot_ams(job.arg2), tray = slot_tray(job.arg2), n = 0;
    for (int i = 0; i < cur.assignedCount; i++) // out of this slot, and (a move) out of any other
      if (!(cur.assigned[i].ams == ams && cur.assigned[i].tray == tray) && cur.assigned[i].spoolId != job.arg)
        cur.assigned[n++] = cur.assigned[i];
    cur.assignedCount = n;
    if (job.cmd == Command::AssignSpool && n < MAX_SLOTS) {
      Assignment &a = cur.assigned[cur.assignedCount++];
      a = Assignment();
      a.ams = (uint8_t)ams;
      a.tray = (uint8_t)tray;
      a.spoolId = job.arg;
      for (int i = 0; i < inventory.count; i++)
        if (inventory.items[i].id == job.arg) {
          snprintf(a.name, sizeof(a.name), "%s", inventory.items[i].name);
          a.remainG = inventory.items[i].remainG;
        }
    }
    break;
  }
  case Command::LoadSpools: break; // handled where it is sent
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

// A command takes Bambuddy a moment, as it does on the panel: the answer comes after
// `answerAfterMs`, and the made-up state is "read again" a little after that.
static int answerAfterMs = 500;
static const int SETTLE_MS = 400;
static struct {
  bool waiting = false, settling = false;
  Job job;
  uint32_t at = 0;
} sent;

bool bambuddy_send(Command c, int arg, int arg2)
{
  if (c == Command::LoadSpools) { // nothing is sent: the list is read again
    inventory.state = SpoolState::Loading;
    spoolsAskedAt = millis();
    return true;
  }
  if (sent.waiting) return false;
  sent.job = {c, arg, arg2};
  sent.waiting = true;
  sent.at = millis();
  return true;
}

// Call often: answers the command that was sent, when its time has come.
static void bambuddy_tick()
{
  if (inventory.state == SpoolState::Loading && millis() - spoolsAskedAt >= (uint32_t)spoolsReadyAfterMs) {
    if (spoolsFail) inventory.state = SpoolState::Failed;
    else {
      make_inventory();
      inventory.state = SpoolState::Ready;
    }
  }
  if (sent.waiting && millis() - sent.at >= (uint32_t)answerAfterMs) {
    sent.waiting = false;
    Request r;
    bool any = command_request(sent.job, live ? atoi(getenv("BAMBUDDY_PRINTER")) : 1, r);
    printf("[sim] would send: %s %s %s\n", any ? r.method : "(nothing)", r.path, r.body);
    if (live) {
      toast("Simulator: not sent", true);
    } else {
      char text[sizeof(cur.toast)];
      bool ok = command_result(sent.job, any ? 200 : 0, text, sizeof(text));
      toast(text, !ok);
      sent.settling = any;
      sent.at = millis();
    }
  }
  if (sent.settling && millis() - sent.at >= SETTLE_MS) {
    sent.settling = false;
    apply(sent.job);
    cur.seq++;
  }
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

static int fingerX = 0, fingerY = 0; // where the pretend finger is

static const int NOTICE_WAIT_MS = 6200; // a notice is gone after this, a failure's too

static lv_display_t *shotDisplay;

// The screenshot, then the same screen with its touch targets framed (targets/), and the
// checks on what it shows (checks.cpp).
static bool save(const std::string &dir, const char *name)
{
  std::string path = dir + "/" + name + ".bmp";
  if (!write_bmp(path)) {
    fprintf(stderr, "can't write %s\n", path.c_str());
    return false;
  }
  checks_run(name);
  checks_overlay_show();
  lv_refr_now(shotDisplay);
  bool ok = write_bmp(dir + "/targets/" + name + ".bmp");
  checks_overlay_hide();
  lv_refr_now(shotDisplay);
  if (!ok) {
    fprintf(stderr, "can't write into %s/targets\n", dir.c_str());
    return false;
  }
  lv_mem_monitor_t m;
  lv_mem_monitor(&m);
  printf("%s  led=%c%c%c  backlight %d%%  lvgl mem %u now, peak %u of %u\n", path.c_str(), ledR ? 'R' : '-',
         ledG ? 'G' : '-', ledB ? 'B' : '-', backlight, (unsigned)(m.total_size - m.free_size), (unsigned)m.max_used,
         (unsigned)m.total_size);
  fflush(stdout);
  return true;
}

static void spin(int ms)
{
  for (uint32_t t = millis(); millis() - t < (uint32_t)ms;) {
    bambuddy_tick();
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

// The `nth` button on show under `o` that holds an icon, counting from 0, in the order they
// were made: in the status screen's header the spool, then the wrench.
static lv_obj_t *find_icon_button(lv_obj_t *o, int &nth)
{
  if (lv_obj_check_type(o, &lv_button_class) && lv_obj_is_visible(o))
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++)
      if (lv_obj_check_type(lv_obj_get_child(o, i), &lv_image_class) && nth-- == 0) return o;
  for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++)
    if (lv_obj_t *found = find_icon_button(lv_obj_get_child(o, i), nth)) return found;
  return nullptr;
}

static lv_point_t icon_button(int nth, const char *what)
{
  lv_obj_update_layout(lv_screen_active());
  lv_obj_t *b = find_icon_button(lv_screen_active(), nth);
  if (!b) {
    fprintf(stderr, "no %s on the screen\n", what);
    exit(1);
  }
  lv_area_t a;
  lv_obj_get_coords(b, &a);
  return {(a.x1 + a.x2) / 2, (a.y1 + a.y2) / 2};
}
#define SPOOL icon_button(0, "spool button")
#define WRENCH icon_button(1, "wrench")

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

static uint32_t nextSeq()
{
  // Above anything the scenario loop used, and in steps: a command counts the made-up state's
  // number up by one, which must not land on the next number handed out here.
  static uint32_t seq = 100000;
  return seq += 100;
}

static Snapshot scenario_named(const char *name)
{
  for (int i = 0; i < scenarioCount; i++)
    if (!strcmp(scenarios[i].name, name)) return scenarios[i].make();
  fprintf(stderr, "no scenario \"%s\"\n", name);
  exit(1);
}

// A file name from a notice's words: "WiFi is down" -> "wifi-is-down".
static std::string slug(const std::string &text)
{
  std::string s;
  for (char c : text) {
    if (isalnum((unsigned char)c)) s += (char)tolower((unsigned char)c);
    else if (!s.empty() && s.back() != '-') s += '-';
  }
  while (!s.empty() && s.back() == '-') s.pop_back();
  return s;
}

static int run_shots(const std::string &dir)
{
  frame.resize(screenW * screenH);
  strip.resize(screenW * (screenH / 10));
  lv_display_t *disp = shotDisplay = lv_display_create(screenW, screenH);
  mkdir((dir + "/targets").c_str(), 0755);
  checks_begin(screenW, screenH);
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
    lv_timer_handler(); // as the panel's loop does: a screen that was taken away is freed here
    lv_refr_now(disp);
    if (!save(dir, scenarios[i].name)) return 1;
  }

  // Let the last scenario's notice time out so it isn't in the pictures below.
  spin(NOTICE_WAIT_MS);

  // Maintenance: open it with the wrench, turn the page, go back.
  cur = scenarios[0].make();
  cur.seq = nextSeq();
  ui_update(cur);
  tap(disp, WRENCH);
  if (!save(dir, "maintenance")) return 1;
  tap(disp, button(LV_SYMBOL_RIGHT));
  if (!save(dir, "maintenance-page-2")) return 1;
  tap(disp, button("Back"));
  if (!save(dir, "maintenance-closed")) return 1;

  // "Mark done" held part of the way, all the way (Bambuddy has it), and answered.
  answerAfterMs = 1200;
  tap(disp, WRENCH);
  lv_point_t at = button("Mark done");
  press(disp, at.x, at.y, 800);
  if (!save(dir, "maintenance-hold")) return 1;
  release();
  press(disp, at.x, at.y, 1700);
  if (!save(dir, "maintenance-hold-fired")) return 1;
  release();
  spin(1000);
  lv_refr_now(disp);
  if (!save(dir, "maintenance-marked-done")) return 1;
  tap(disp, button("Back"));
  spin(NOTICE_WAIT_MS);

  // The other lists: every page of each. (With the tasks not read yet there is no wrench.)
  for (const char *name : {"tasks-none", "tasks-one", "tasks-full"}) {
    cur = scenario_named(name);
    cur.seq = nextSeq();
    ui_update(cur);
    tap(disp, WRENCH);
    for (int page = 1, pages = (cur.taskCount + 3) / 4; page <= (pages ? pages : 1); page++) {
      char file[48];
      snprintf(file, sizeof(file), "maintenance-%s-%d", name, page);
      if (!save(dir, file)) return 1;
      if (page < pages) tap(disp, button(LV_SYMBOL_RIGHT));
    }
    tap(disp, button("Back"));
  }

  // Filament: the slots, the spool list for one (as it is read, then read), another material,
  // a spool held and assigned, and the slot's own spool taken out.
  cur = scenarios[0].make();
  cur.seq = nextSeq();
  spin(50);
  tap(disp, SPOOL);
  if (!save(dir, "filament-slots")) return 1;
  tap(disp, button("Change"));
  if (!save(dir, "filament-reading")) return 1;
  spin(1000);
  lv_refr_now(disp);
  if (!save(dir, "filament-spools")) return 1;
  tap(disp, button(LV_SYMBOL_RIGHT));
  if (!save(dir, "filament-spools-page-2")) return 1;
  tap(disp, button("PETG"));
  if (!save(dir, "filament-spools-next-material")) return 1;
  answerAfterMs = 1200;
  at = button("Assign");
  press(disp, at.x, at.y, 800);
  if (!save(dir, "filament-assign-hold")) return 1;
  release();
  press(disp, at.x, at.y, 1700);
  if (!save(dir, "filament-assign-fired")) return 1;
  release();
  spin(2200);
  lv_refr_now(disp);
  if (!save(dir, "filament-assigned")) return 1;
  answerAfterMs = 500;
  tap(disp, button("Assign")); // the first slot without a spool
  spin(1000);
  lv_refr_now(disp);
  if (!save(dir, "filament-spools-other-slot")) return 1;
  tap(disp, button(LV_SYMBOL_LEFT " A3"));
  spoolsFail = true;
  tap(disp, button("Change"));
  spin(1000);
  lv_refr_now(disp);
  if (!save(dir, "filament-spools-failed")) return 1;
  spoolsFail = false;
  tap(disp, button(LV_SYMBOL_LEFT " A1"));
  tap(disp, button("Back"));

  cur = scenario_named("slots-many");
  cur.seq = nextSeq();
  spin(50);
  tap(disp, SPOOL);
  if (!save(dir, "filament-slots-many")) return 1;
  tap(disp, button(LV_SYMBOL_RIGHT));
  tap(disp, button(LV_SYMBOL_RIGHT));
  if (!save(dir, "filament-slots-many-3")) return 1;
  tap(disp, button("Back"));

  // Pressed buttons: a tap, and a hold part of the way, all the way, and answered.
  cur = scenarios[0].make();
  cur.seq = nextSeq();
  ui_update(cur);
  lv_refr_now(disp);
  at = button("Pause");
  press(disp, at.x, at.y, 300);
  if (!save(dir, "pressed-tap")) return 1;
  release(); // the tap is sent now; Bambuddy has it for a moment
  lv_refr_now(disp);
  if (!save(dir, "pressed-tap-under-way")) return 1;
  spin(2000 + NOTICE_WAIT_MS);
  cur = scenarios[0].make(); // the tap paused the made-up print; start over
  cur.seq = nextSeq();
  spin(50);
  at = button("Hold to stop");
  press(disp, at.x, at.y, 800);
  if (!save(dir, "pressed-hold")) return 1;
  release();
  lv_refr_now(disp);
  if (!save(dir, "pressed-hold-let-go")) return 1;
  press(disp, at.x, at.y, 1700);
  if (!save(dir, "pressed-hold-fired")) return 1;
  release();
  spin(1000);
  lv_refr_now(disp);
  if (!save(dir, "pressed-hold-answered")) return 1;
  spin(NOTICE_WAIT_MS);
  answerAfterMs = 500;

  // Several errors: a tap on the box shows the next.
  cur = scenario_named("error-many");
  cur.seq = nextSeq();
  spin(50);
  lv_obj_update_layout(lv_screen_active());
  tap(disp, {screenW / 2, screenH * 100 / 240});
  if (!save(dir, "error-many-second")) return 1;

  // "Skip" on the outcome question: the plate-clear screen comes up for that print.
  cur = scenario_named("verdict");
  cur.seq = nextSeq();
  spin(50);
  tap(disp, button("Skip"));
  spin(50);
  lv_refr_now(disp);
  if (!save(dir, "verdict-skipped")) return 1;

  // A dimmed panel: a minute untouched while nothing wants anyone, and the touch that wakes it.
  cur = scenarios[0].make();
  cur.seq = nextSeq();
  spin(50);
  restAfterMs = 300;
  spin(600);
  lv_refr_now(disp);
  if (!save(dir, "dimmed")) return 1;
  at = button("Pause");
  tap(disp, at); // only brightens: nothing is sent
  if (!save(dir, "dimmed-woken")) return 1;
  restAfterMs = 0;

  // Every notice the panel can show: each command with each kind of answer, each wording once.
  struct Notice {
    std::string text;
    bool bad;
  };
  std::vector<Notice> notices = {{"Busy, try again", true}}; // ui.cpp's own, when the queue is full
  for (Command c : {Command::Pause, Command::Resume, Command::Stop, Command::ClearPlate, Command::ClearErrors,
                    Command::ResumeQueue, Command::StartNext, Command::VerdictGood, Command::VerdictReject,
                    Command::PerformTask})
    for (int code : {200, 0, 401, 403, 404, 409, 400, 503, ERR_NO_WIFI, -1, 418}) {
      char text[sizeof(cur.toast)];
      bool ok = command_result({c, 1, 0}, code, text, sizeof(text));
      bool seen = false;
      for (const Notice &n : notices) seen = seen || n.text == text;
      if (!seen) notices.push_back({text, !ok});
    }
  cur = scenario_named("paused");
  for (size_t i = 0; i < notices.size(); i++) {
    snprintf(cur.toast, sizeof(cur.toast), "%s", notices[i].text.c_str());
    cur.toastBad = notices[i].bad;
    cur.toastSeq = 5000 + i;
    cur.seq = nextSeq();
    ui_update(cur);
    lv_refr_now(disp);
    if (!save(dir, ("notice-" + slug(notices[i].text)).c_str())) return 1;
  }
  spin(NOTICE_WAIT_MS);

  // The start-up and setup screens, in the order a new panel shows them, with the panel's own
  // words (messages.h) and made-up values where it fills some in.
  char wifiQr[96], webQr[96], calNone[120], calGiveUp[120];
  snprintf(wifiQr, sizeof(wifiQr), MSG_WIFI_QR, "BambuddyPanel-Setup", "k3mw-9xbq-7hfe");
  snprintf(webQr, sizeof(webQr), MSG_WEB_QR, "192.168.178.123", "abcd-efgh");
  snprintf(calNone, sizeof(calNone), MSG_CAL_NO_TOUCH, MSG_CAL_KEEP_STANDARD);
  snprintf(calGiveUp, sizeof(calGiveUp), MSG_CAL_GIVING_UP, MSG_CAL_KEEP_SAVED);
  CodeScreen wifi, web;
  wifi.title = MSG_WIFI_SETUP_TITLE;
  wifi.qr = wifiQr;
  wifi.label1 = MSG_WIFI_LABEL_NAME;
  wifi.value1 = "BambuddyPanel-Setup";
  wifi.label2 = MSG_WIFI_LABEL_PASSWORD;
  wifi.value2 = "k3mw-9xbq-7hfe";
  wifi.note = MSG_WIFI_NOTE;
  web.title = MSG_WEB_FIRST_TITLE;
  web.qr = webQr;
  web.label1 = MSG_WEB_LABEL_ADDRESS;
  web.value1 = "192.168.178.123";
  web.label2 = MSG_WEB_LABEL_CODE;
  web.value2 = "abcd-efgh";

  ui_message(MSG_STARTING_TITLE, MSG_STARTING_BODY);
  if (!save(dir, "msg-01-starting")) return 1;
  ui_message(MSG_SETUP_TITLE, MSG_SETUP_HOLD_BODY);
  if (!save(dir, "msg-02-setup-hold")) return 1;
  ui_code(wifi);
  if (!save(dir, "msg-03-wifi-setup")) return 1;
  ui_message(MSG_CONNECTING_TITLE, MSG_CONNECTING_WIFI, true);
  if (!save(dir, "msg-04-joining-wifi")) return 1;
  ui_code(web); // a new panel: nothing to cancel back to
  if (!save(dir, "msg-05-web-first")) return 1;
  web.title = MSG_WEB_LATER_TITLE;
  ui_code(web);
  ui_message_buttons(MSG_WEB_CANCEL, MSG_WEB_CALIBRATE);
  if (!save(dir, "msg-06-web-later")) return 1;
  ui_message_buttons(MSG_WEB_CANCEL); // a panel with capacitive touch has nothing to calibrate
  if (!save(dir, "msg-06-web-later-capacitive")) return 1;
  ui_message(MSG_CONNECTING_TITLE, MSG_CONNECTING_BAMBUDDY, true);
  if (!save(dir, "msg-07-asking-bambuddy")) return 1;
  const struct {
    const char *file, *title, *body;
  } messages[] = {
      {"msg-08-no-wifi-saved", MSG_SETUP_TITLE, MSG_SETUP_NO_WIFI_SAVED},
      {"msg-09-not-on-wifi", MSG_SETUP_TITLE, MSG_SETUP_NOT_ON_WIFI},
      {"msg-10-wifi-forgotten", MSG_SETUP_TITLE, MSG_SETUP_WIFI_FORGOTTEN},
      {"msg-11-updating", MSG_UPDATING_TITLE, MSG_UPDATING_BODY},
      {"msg-12-cal-saved", MSG_CAL_TITLE, MSG_CAL_SAVED},
      {"msg-13-cal-retry", MSG_CAL_TITLE, MSG_CAL_RETRY},
      {"msg-14-cal-no-touch", MSG_CAL_TITLE, calNone},
      {"msg-15-cal-giving-up", MSG_CAL_TITLE, calGiveUp},
  };
  for (const auto &m : messages) {
    ui_message(m.title, m.body);
    if (!save(dir, m.file)) return 1;
  }

  // Bambuddy saying no, and Bambuddy silent for too long, with every reason the panel gives.
  for (int code : {401, 403, 404, ERR_HTTPS, ERR_BAD_URL, ERR_NO_WIFI, ERR_BAD_JSON, -11, -1, -5, 502, 302}) {
    char file[32];
    snprintf(file, sizeof(file), code < 0 ? "offline-err%d" : "offline-http-%d", code < 0 ? -code : code);
    cur = Snapshot();
    cur.printerId = 12;
    cur.httpCode = code;
    cur.seq = nextSeq();
    ui_update(cur);
    lv_timer_handler();
    lv_refr_now(disp);
    if (!save(dir, file)) return 1;
  }

  // Touch calibration: the four crosses, placed as calibrate.cpp places them.
  const int inset = screenH / 12;
  const int cross[4][2] = {{inset, inset}, {screenW - 1 - inset, inset}, {screenW - 1 - inset, screenH - 1 - inset},
                           {inset, screenH - 1 - inset}};
  for (int i = 0; i < 4; i++) {
    ui_calibrate(i + 1, 4, cross[i][0], cross[i][1]);
    char file[24];
    snprintf(file, sizeof(file), i == 1 ? "calibrate" : "calibrate-%d", i + 1); // "calibrate" is in the README
    if (!save(dir, file)) return 1;
  }

  if (!checks_write(dir + "/checks.txt")) return 1;
  printf("%d problems found by the checks, see %s/checks.txt\n", checks_problems(), dir.c_str());
  return 0;
}

// ---------------------------------------------------------------- the settings web page

struct StdPage : PageText {
  std::string s;
  using PageText::operator+=;
  PageText &operator+=(const char *text) override
  {
    s += text;
    return *this;
  }
};

// The page in each state setup_web.cpp serves it in. The messages are that file's own words,
// copied: it needs the panel's web server, so it can't be built in here.
static int run_web(const std::string &dir)
{
  static const PageChoice printers[] = {{1, "X1C"}, {2, "Workshop H2D left bench"}, {7, "A1 mini <kitchen>"}};
  struct {
    const char *file;
    PageData d;
  } pages[5];
  pages[0].file = "1-first-run";
  pages[1].file = "2-wrong-code";
  pages[1].d.msg = "That code doesn't match the one on the panel's screen.";
  pages[1].d.kind = "e";
  pages[2].file = "3-saved-not-accepted";
  pages[2].d.msg = "Saved, but Bambuddy refused the API key. Fix it and save again, or use the settings as they "
                   "are if Bambuddy is only down for now.";
  pages[2].d.kind = "e";
  pages[2].d.typedCode = "abcd-efgh";
  pages[2].d.url = "http://192.168.1.50:3700";
  pages[2].d.keySaved = true;
  pages[2].d.printerId = 2;
  pages[2].d.finishAnyway = true;
  pages[3].file = "4-saved";
  pages[3].d.msg = "Saved, and Bambuddy answered. The panel is starting; you can close this page.";
  pages[3].d.kind = "ok";
  pages[3].d.done = true;
  pages[4].file = "5-settings-later";
  for (int i : {3, 4}) {
    pages[i].d.url = "http://192.168.1.50:3700";
    pages[i].d.keySaved = true;
    pages[i].d.printerId = 2;
    pages[i].d.choices = printers;
    pages[i].d.choiceCount = 3;
    pages[i].d.printerName = printers[1].name;
  }
  for (const auto &page : pages) {
    StdPage out;
    setup_page(out, page.d);
    std::string path = dir + "/" + page.file + ".html";
    FILE *f = fopen(path.c_str(), "w");
    if (!f || fwrite(out.s.data(), 1, out.s.size(), f) != out.s.size()) {
      fprintf(stderr, "can't write %s\n", path.c_str());
      return 1;
    }
    fclose(f);
    printf("%s  %u bytes\n", path.c_str(), (unsigned)out.s.size());
  }
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

    bambuddy_tick();
    ui_update(cur);
    update_title(disp);
    lv_timer_handler();
    SDL_Delay(5);
  }
}

int main(int argc, char **argv)
{
  std::string shots, web;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--live") live = true;
    else if (a == "--shots" && i + 1 < argc) shots = argv[++i];
    else if (a == "--web" && i + 1 < argc) web = argv[++i];
    else if (a == "--size" && i + 1 < argc && sscanf(argv[i + 1], "%dx%d", &screenW, &screenH) == 2 &&
             screenW >= 160 && screenW <= 1920 && screenH >= 120 && screenH <= 1080) i++;
    else {
      fprintf(stderr, "usage: %s [--live | --shots <dir> | --web <dir>] [--size <W>x<H>]\n", argv[0]);
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

  if (!web.empty()) return run_web(web);

  lv_init();
  lv_tick_set_cb(millis);
  return shots.empty() ? run_window() : run_shots(shots);
}
