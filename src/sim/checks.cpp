#include "checks.h"

#include <lvgl.h>
#include <src/widgets/label/lv_label_private.h>

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <set>
#include <vector>

// A finger needs about this much, on the smallest panel that has this many pixels.
static const float MIN_TARGET_MM = 7.0f;
static const uint32_t DOT_END_NONE = 0xFFFFFFFF; // LVGL's "this label was not shortened"

static int scrW, scrH;
static float mmPerPx = 0; // on the smallest panel with this resolution; 0 when no board has it
static float diagonal = 0;

struct Finding {
  std::string shot, text;
  bool problem;
};
static std::vector<Finding> findings;
static std::vector<lv_obj_t *> overlay;

struct Item {
  lv_obj_t *obj;
  lv_area_t area;
  std::string what;
};
static std::vector<Item> content, targets;

void checks_begin(int w, int h)
{
  scrW = w;
  scrH = h;
  // The smallest panel sold with each resolution (README, "Other boards").
  if (w == 320 && h == 240) diagonal = 2.4f;
  else if (w == 480 && h == 320) diagonal = 3.5f;
  if (diagonal > 0) mmPerPx = diagonal * 25.4f / std::sqrt((float)(w * w + h * h));
}

static void add(const char *shot, bool problem, const char *fmt, ...)
{
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  findings.push_back({shot, buf, problem});
}

// What a label says, short enough for a report line, on one line.
static std::string quote(const char *text)
{
  std::string s;
  for (const char *p = text; *p && s.size() < 36; p++) s += *p == '\n' ? ' ' : *p;
  if (strlen(text) > 36) s += "...";
  return "\"" + s + "\"";
}

static std::string describe(lv_obj_t *o)
{
  if (lv_obj_check_type(o, &lv_label_class)) return quote(lv_label_get_text(o));
  for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++)
    if (lv_obj_check_type(lv_obj_get_child(o, i), &lv_image_class)) return "an icon button";
  for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) {
    lv_obj_t *c = lv_obj_get_child(o, i);
    if (lv_obj_check_type(c, &lv_label_class) && lv_label_get_text(c)[0]) return "button " + quote(lv_label_get_text(c));
  }
  if (lv_obj_check_type(o, &lv_bar_class)) return "the progress bar";
  if (lv_obj_check_type(o, &lv_image_class)) return "an icon";
  return lv_obj_check_type(o, &lv_button_class) ? "an icon button" : "an object";
}

static bool is_ancestor(lv_obj_t *a, lv_obj_t *of)
{
  for (lv_obj_t *p = lv_obj_get_parent(of); p; p = lv_obj_get_parent(p))
    if (p == a) return true;
  return false;
}

static bool inside(const lv_area_t &a, const lv_area_t &outer)
{
  return a.x1 >= outer.x1 && a.y1 >= outer.y1 && a.x2 <= outer.x2 && a.y2 <= outer.y2;
}

static bool overlap(const lv_area_t &a, const lv_area_t &b)
{
  return a.x1 <= b.x2 && b.x1 <= a.x2 && a.y1 <= b.y2 && b.y1 <= a.y2;
}

// The pixels a label's text really covers: the label can be wider than what it says.
static lv_area_t text_area(lv_obj_t *label)
{
  lv_area_t box;
  lv_obj_get_content_coords(label, &box);
  lv_point_t size;
  lv_text_get_size(&size, lv_label_get_text(label), lv_obj_get_style_text_font(label, 0),
                   lv_obj_get_style_text_letter_space(label, 0), lv_obj_get_style_text_line_space(label, 0),
                   lv_area_get_width(&box), LV_TEXT_FLAG_NONE);
  lv_area_t a = box;
  lv_text_align_t align = lv_obj_get_style_text_align(label, 0);
  if (align == LV_TEXT_ALIGN_CENTER) a.x1 = box.x1 + (lv_area_get_width(&box) - size.x) / 2;
  else if (align == LV_TEXT_ALIGN_RIGHT) a.x1 = box.x2 - size.x + 1;
  a.x2 = a.x1 + size.x - 1;
  a.y2 = a.y1 + size.y - 1; // the full text, also where the label is too short to show it
  return a;
}

static void collect(lv_obj_t *o, const char *shot)
{
  if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return;

  if (lv_obj_check_type(o, &lv_label_class)) {
    const char *text = lv_label_get_text(o);
    if (text[0]) {
      lv_area_t a = text_area(o), box, screen = {0, 0, scrW - 1, scrH - 1};
      lv_obj_get_coords(o, &box);
      if (((lv_label_t *)o)->dot_end != DOT_END_NONE) add(shot, true, "cut short with dots: %s", quote(text).c_str());
      else if (a.y2 > box.y2 || a.x2 > box.x2 + 1) add(shot, true, "does not fit its label: %s", quote(text).c_str());
      if (!inside(a, screen)) add(shot, true, "runs off the screen: %s", quote(text).c_str());
      lv_obj_t *parent = lv_obj_get_parent(o);
      lv_area_t pa;
      lv_obj_get_coords(parent, &pa);
      if (lv_obj_check_type(parent, &lv_button_class) && !inside(a, pa))
        add(shot, true, "wider or taller than its button: %s", quote(text).c_str());
      a.y2 = LV_MIN(a.y2, box.y2);
      content.push_back({o, a, describe(o)});
    }
  } else if (lv_obj_check_type(o, &lv_bar_class) || lv_obj_check_type(o, &lv_button_class)) {
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    content.push_back({o, a, describe(o)});
  }

  if (lv_obj_has_flag(o, LV_OBJ_FLAG_CLICKABLE) && lv_obj_get_event_count(o) > 0) {
    lv_area_t a;
    lv_obj_get_click_area(o, &a);
    targets.push_back({o, a, describe(o)});
  }

  for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) collect(lv_obj_get_child(o, i), shot);
}

void checks_run(const char *shot)
{
  content.clear();
  targets.clear();
  lv_obj_update_layout(lv_screen_active());
  collect(lv_screen_active(), shot);
  // The toast lies over the header on purpose and is not compared with what is under it.

  for (size_t i = 0; i < content.size(); i++)
    for (size_t j = i + 1; j < content.size(); j++) {
      const Item &a = content[i], &b = content[j];
      if (is_ancestor(a.obj, b.obj) || is_ancestor(b.obj, a.obj)) continue;
      if (overlap(a.area, b.area)) add(shot, true, "overlap: %s and %s", a.what.c_str(), b.what.c_str());
    }

  for (size_t i = 0; i < targets.size(); i++) {
    const Item &t = targets[i];
    int w = lv_area_get_width(&t.area), h = lv_area_get_height(&t.area);
    if (mmPerPx > 0) {
      float wm = w * mmPerPx, hm = h * mmPerPx;
      bool small = wm < MIN_TARGET_MM || hm < MIN_TARGET_MM;
      add(shot, small, "target %s: %.1f x %.1f mm on a %.1f\" panel%s", t.what.c_str(), wm, hm, diagonal,
          small ? " (under 7 mm)" : "");
    } else {
      add(shot, false, "target %s: %d x %d px", t.what.c_str(), w, h);
    }
    for (size_t j = i + 1; j < targets.size(); j++)
      if (overlap(t.area, targets[j].area) && !is_ancestor(t.obj, targets[j].obj) && !is_ancestor(targets[j].obj, t.obj))
        add(shot, true, "touch areas overlap: %s and %s", t.what.c_str(), targets[j].what.c_str());
  }
}

void checks_overlay_show()
{
  for (const Item &t : targets) {
    lv_obj_t *frame = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(frame);
    lv_obj_remove_flag(frame, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(frame, t.area.x1, t.area.y1);
    lv_obj_set_size(frame, lv_area_get_width(&t.area), lv_area_get_height(&t.area));
    float wm = lv_area_get_width(&t.area) * mmPerPx, hm = lv_area_get_height(&t.area) * mmPerPx;
    bool small = mmPerPx > 0 && (wm < MIN_TARGET_MM || hm < MIN_TARGET_MM);
    lv_color_t c = lv_color_hex(small ? 0xFF4081 : 0x00E5FF);
    lv_obj_set_style_border_color(frame, c, 0);
    lv_obj_set_style_border_width(frame, 1, 0);
    overlay.push_back(frame);

    lv_obj_t *l = lv_label_create(lv_layer_top());
    char buf[32];
    if (mmPerPx > 0) snprintf(buf, sizeof(buf), "%.1fx%.1f", wm, hm);
    else snprintf(buf, sizeof(buf), "%dx%d", (int)lv_area_get_width(&t.area), (int)lv_area_get_height(&t.area));
    lv_label_set_text(l, buf);
    lv_obj_set_style_text_color(l, lv_color_black(), 0);
    lv_obj_set_style_bg_color(l, c, 0);
    lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
    lv_obj_set_pos(l, t.area.x1, t.area.y1);
    overlay.push_back(l);
  }
}

void checks_overlay_hide()
{
  for (lv_obj_t *o : overlay) lv_obj_delete(o);
  overlay.clear();
}

int checks_problems()
{
  int n = 0;
  for (const Finding &f : findings) n += f.problem;
  return n;
}

// Each shot's findings, problems marked with "!". Then every distinct problem once, with how
// many screenshots show it: the list to work through.
bool checks_write(const std::string &path)
{
  FILE *f = fopen(path.c_str(), "w");
  if (!f) return false;
  fprintf(f, "%dx%d", scrW, scrH);
  if (mmPerPx > 0) fprintf(f, ", sizes in mm as on a %.1f\" panel (%.3f mm per pixel)", diagonal, mmPerPx);
  fprintf(f, "\n\n== distinct problems\n");
  std::set<std::string> seen;
  for (const Finding &a : findings) {
    if (!a.problem || !seen.insert(a.text).second) continue;
    int n = 0;
    std::string first;
    for (const Finding &b : findings)
      if (b.problem && b.text == a.text && n++ == 0) first = b.shot;
    fprintf(f, "%3d shot%s, first %s: %s\n", n, n == 1 ? " " : "s", first.c_str(), a.text.c_str());
  }
  if (seen.empty()) fprintf(f, "none\n");
  fprintf(f, "\n== by screenshot\n");
  std::string shot;
  for (const Finding &a : findings) {
    if (a.shot != shot) fprintf(f, "\n%s\n", (shot = a.shot).c_str());
    fprintf(f, "  %c %s\n", a.problem ? '!' : ' ', a.text.c_str());
  }
  fclose(f);
  return true;
}
