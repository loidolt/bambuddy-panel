// The filament screens: the printer's slots with the inventory spool Bambuddy has in each, and
// a list of the inventory's spools to put one in a slot. Opened by the spool in the status
// screen's header. Made when opened and taken away when closed, as the maintenance screen is.
#include "icons.h"
#include "ui.h"
#include "ui_internal.h"
#include "ui_logic.h"

#include <cstring>
#include <initializer_list>
#include <strings.h>

static const int ROWS = 4;                    // per page
static const uint32_t IDLE_MS = 60000;        // then back to the status screen by itself
static const int ROW_GAP = 2, BUTTON_W = 72, SWATCH = 26; // units; a narrow button leaves the name room
static const int MAX_MATERIALS = 12;

enum class Mode { Slots, Pick };

static lv_obj_t *box, *lblTitle, *lblPage, *lblNote;
static lv_obj_t *row[ROWS], *swatch[ROWS], *lblLine1[ROWS], *lblLine2[ROWS];
static UiButton btnOpen, btnBack, btnMaterial, btnPrev, btnNext, btnRow[ROWS];
static bool showing = false;
static Mode mode = Mode::Slots;
static int page = 0, pages = 1;
static uint32_t touchedAt = 0;

// What the rows stand for, so a press acts on what the person sees.
static Slot slots[MAX_SLOTS];
static int slotCount = 0;
static Slot picked;                 // the slot a spool is being chosen for
static int pickedSpool = 0;         // the spool Bambuddy has in it now, 0 for none
static char pickedName[36] = "";
static char material[SPOOL_MATERIAL] = ""; // the list shows this material; empty: all
static bool materialChosen = false; // by the person, or once from what is in the slot
static Spool shown[ROWS];
static int rowSpool[ROWS];          // per row: a spool's id, 0 for the "take it out" row, -1 none
static SpoolState drawnState = SpoolState::None;
static bool sentChange = false;     // an assign or remove is with Bambuddy

static void touched()
{
  touchedAt = millis();
  ui_redraw_soon();
}

static void open_fire(UiButton &)
{
  showing = true;
  mode = Mode::Slots;
  page = 0;
  touched();
}

static void back_fire(UiButton &)
{
  if (mode == Mode::Pick) {
    mode = Mode::Slots; // back to the slots, not all the way out
    page = 0;
  } else {
    showing = false;
  }
  touched();
}

static void page_fire(UiButton &b)
{
  page = (page + b.tag + pages) % pages; // tag: -1 back, +1 on; round at both ends
  touched();
}

// The material button steps through "all" and each material there is.
static void material_fire(UiButton &)
{
  char list[MAX_MATERIALS][SPOOL_MATERIAL];
  int n = bambuddy_spool_materials(list, MAX_MATERIALS);
  int at = -1; // "all"
  for (int i = 0; i < n; i++)
    if (strcasecmp(list[i], material) == 0) at = i;
  at = at + 1 < n ? at + 1 : -1;
  snprintf(material, sizeof(material), "%s", at < 0 ? "" : list[at]);
  materialChosen = true;
  page = 0;
  touched();
}

static void draw_rows(const Snapshot *s); // below

static void row_fire(UiButton &b)
{
  touchedAt = millis();
  if (mode == Mode::Slots) {
    int i = page * ROWS + b.tag;
    if (i >= slotCount) return;
    picked = slots[i];
    mode = Mode::Pick;
    page = 0;
    materialChosen = false;
    drawnState = SpoolState::None;
    bambuddy_send(Command::LoadSpools); // a fresh list every time the picker opens
    touched();
    return;
  }
  int id = rowSpool[b.tag];
  if (id < 0) return;
  if (id == 0) ui_send(Command::UnassignSpool, 0, slot_code(picked.ams, picked.tray));
  else ui_send(Command::AssignSpool, id, slot_code(picked.ams, picked.tray));
  sentChange = true;
  draw_rows(nullptr); // at once: the finger is still down, which holds back the full redraw
}

void filament_create(lv_obj_t *header)
{
  // The spool: opens the filament screen. Small, so it takes taps from a little around it too.
  ui_button_create(btnOpen, header, open_fire);
  lv_obj_set_size(btnOpen.btn, px(HEADER_ICON_W), px(HEADER_ICON_H));
  lv_obj_set_ext_click_area(btnOpen.btn, px(HEADER_ICON_REACH));
  lv_obj_add_flag(btnOpen.label, LV_OBJ_FLAG_HIDDEN); // an icon instead of words
  lv_obj_center(ui_icon(btnOpen.btn, ICON(icon_spool), C_TEXT));
  lv_obj_add_flag(btnOpen.btn, LV_OBJ_FLAG_HIDDEN);
}

static void build()
{
  if (box) return;
  const int W = ui.uw;
  box = ui_box(lv_screen_active());
  for (UiButton *b : {&btnBack, &btnMaterial, &btnPrev, &btnNext}) *b = UiButton();
  for (UiButton &b : btnRow) b = UiButton();

  auto head = [&](UiButton &b, int x, int w, const char *text, void (*fire)(UiButton &), int tag = 0) {
    ui_button_create(b, box, fire, tag);
    lv_obj_set_pos(b.btn, px(x), px(ROW_GAP));
    lv_obj_set_size(b.btn, px(w), px(TARGET));
    ui_button_set(b, text, Kind::Quiet, false);
  };
  const int pageX = W - 8 - TARGET - 32 - TARGET;
  head(btnBack, 8, 64, LV_SYMBOL_LEFT " Back", back_fire);
  head(btnMaterial, 78, pageX - 78 - 6, "", material_fire);
  head(btnPrev, pageX, TARGET, LV_SYMBOL_LEFT, page_fire, -1);
  head(btnNext, W - 8 - TARGET, TARGET, LV_SYMBOL_RIGHT, page_fire, 1);
  lblPage = ui_label(box, ui.small, C_DIM, pageX + TARGET, ROW_GAP + 15, 32);
  lv_obj_set_style_text_align(lblPage, LV_TEXT_ALIGN_CENTER, 0);
  lblTitle = ui_label(box, ui.small, C_TEXT, 82, ROW_GAP + 15, pageX - 90);

  for (int i = 0; i < ROWS; i++) {
    lv_obj_t *r = row[i] = ui_plain(box);
    lv_obj_set_pos(r, px(8), px(ROW_GAP + (i + 1) * (TARGET + ROW_GAP)));
    lv_obj_set_size(r, px(W - 16), px(TARGET));
    lv_obj_set_style_bg_color(r, lv_color_hex(C_ROW), 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(r, px(6), 0);
    // The filament's colour. Outlined, so a black one shows against the dark row.
    swatch[i] = ui_plain(r);
    lv_obj_set_pos(swatch[i], px(8), px((TARGET - SWATCH) / 2));
    lv_obj_set_size(swatch[i], px(SWATCH), px(SWATCH));
    lv_obj_set_style_radius(swatch[i], px(4), 0);
    lv_obj_set_style_bg_opa(swatch[i], LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(swatch[i], 1, 0);
    lv_obj_set_style_border_color(swatch[i], lv_color_hex(C_GREY), 0);
    const int textX = 8 + SWATCH + 8, text = W - 16 - textX - 8 - BUTTON_W;
    lblLine1[i] = ui_label(r, ui.small, C_TEXT, textX, 5, text);
    lblLine2[i] = ui_label(r, ui.small, C_DIM, textX, 24, text);
    ui_button_create(btnRow[i], r, row_fire, i);
    lv_obj_set_size(btnRow[i].btn, px(BUTTON_W), px(TARGET));
    lv_obj_align(btnRow[i].btn, LV_ALIGN_RIGHT_MID, 0, 0);
  }
  lblNote = lv_label_create(box);
  lv_obj_set_style_text_font(lblNote, ui.small, 0);
  lv_obj_set_style_text_color(lblNote, lv_color_hex(C_DIM), 0);
  lv_obj_set_style_text_align(lblNote, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_width(lblNote, px(W - 40));
  lv_label_set_long_mode(lblNote, LV_LABEL_LONG_WRAP);
  lv_obj_align(lblNote, LV_ALIGN_CENTER, 0, px(TARGET / 2));
}

bool filament_showing() { return showing; }

void filament_close()
{
  showing = false;
  if (!box) return;
  lv_obj_delete_async(box); // not at once: Back is one of its own buttons
  box = nullptr;
}

// Call often. True when the screen should be drawn again: it closed itself after a while
// untouched, or the spool list it is waiting for has come (or failed).
bool filament_tick()
{
  if (!showing) return false;
  if (millis() - touchedAt > IDLE_MS) {
    filament_close();
    return true;
  }
  if (mode != Mode::Pick) return false;
  Spool none;
  int total;
  SpoolState state;
  bambuddy_spools("", 0, &none, 0, total, state);
  return state != drawnState;
}

static void set_swatch(int i, bool any, uint32_t rgb)
{
  if (any) lv_obj_remove_flag(swatch[i], LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(swatch[i], LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_bg_color(swatch[i], lv_color_hex(rgb), 0);
}

static void hide_row(int i)
{
  lv_obj_add_flag(row[i], LV_OBJ_FLAG_HIDDEN);
  rowSpool[i] = -1;
}

// The slots: what the printer says is in each, and the inventory spool Bambuddy has there.
static void draw_slots(const Snapshot &s)
{
  slotCount = s.slotCount;
  for (int i = 0; i < s.slotCount; i++) slots[i] = s.slots[i];
  pages = (slotCount + ROWS - 1) / ROWS;
  if (pages < 1) pages = 1;
  if (page >= pages) page = 0;

  char buf[64], name[12], left[20];
  for (int i = 0; i < ROWS; i++) {
    int at = page * ROWS + i;
    if (at >= slotCount) {
      hide_row(i);
      continue;
    }
    lv_obj_remove_flag(row[i], LV_OBJ_FLAG_HIDDEN);
    const Slot &slot = slots[at];
    // The slot, what the printer says is in it, and what is left on Bambuddy's spool: the short
    // things on one line, so the spool's name has the other to itself.
    const Assignment *a = assignment_of(s, slot);
    format_slot(name, sizeof(name), slot);
    left[0] = 0;
    if (a) format_remaining(left, sizeof(left), a->remainG);
    snprintf(buf, sizeof(buf), left[0] ? "%s   %s   %s" : "%s   %s", name, slot.type[0] ? slot.type : "empty", left);
    ui_set_text(lblLine1[i], buf);
    set_swatch(i, slot.type[0] != 0, slot.rgb);
    snprintf(buf, sizeof(buf), "%s", a ? a->name : s.assignedKnown ? "No spool assigned" : "");
    ui_set_text(lblLine2[i], buf);
    lv_obj_set_style_text_color(lblLine2[i], lv_color_hex(a ? C_TEXT : C_DIM), 0);
    ui_button_set(btnRow[i], a ? "Change" : "Assign", Kind::Quiet, false, Icon::None, slot_code(slot.ams, slot.tray) + 1);
    ui_button_enable(btnRow[i], !ui_busy());
  }
  lv_label_set_text(lblNote, slotCount ? "" : "This printer reports no filament slots.");
}

// The inventory's spools of one material, with "take it out" first when the slot has a spool.
static void draw_spools()
{
  char list[MAX_MATERIALS][SPOOL_MATERIAL];
  int materials = bambuddy_spool_materials(list, MAX_MATERIALS);
  // Start on what the printer says is in the slot, when the inventory has that material.
  if (!materialChosen && materials > 0) {
    material[0] = 0;
    for (int i = 0; i < materials; i++)
      if (strcasecmp(list[i], picked.type) == 0) snprintf(material, sizeof(material), "%s", list[i]);
    materialChosen = true;
  }

  const int extra = pickedSpool ? 1 : 0; // the "take it out" row
  int total = 0;
  SpoolState state;
  bambuddy_spools(material, 0, shown, 0, total, state);
  drawnState = state;
  pages = (total + extra + ROWS - 1) / ROWS;
  if (pages < 1) pages = 1;
  if (page >= pages) page = 0;

  const char *note = "";
  if (state == SpoolState::None || state == SpoolState::Loading) note = "Reading the spools from Bambuddy...";
  else if (state == SpoolState::Failed) note = "Bambuddy didn't give the spool list. Go back and try again.";
  else if (total == 0 && !extra) note = material[0] ? "No spools of this material in Bambuddy." : "No spools in Bambuddy's inventory.";
  lv_label_set_text(lblNote, note);
  bool ready = state == SpoolState::Ready;

  int offset = page * ROWS - extra;
  int first = offset < 0 ? 0 : offset;
  int got = ready ? bambuddy_spools(material, first, shown, ROWS, total, state) : 0;
  char buf[72], left[20];
  int used = 0;
  for (int i = 0; i < ROWS; i++) {
    int at = page * ROWS + i; // among all rows, the "take it out" row first
    if (!ready || at >= total + extra) {
      hide_row(i);
      continue;
    }
    lv_obj_remove_flag(row[i], LV_OBJ_FLAG_HIDDEN);
    if (at < extra) {
      rowSpool[i] = 0;
      set_swatch(i, false, 0);
      ui_set_text(lblLine1[i], pickedName);
      ui_set_text(lblLine2[i], "in this slot now");
      bool under_way = ui_pending(Command::UnassignSpool, 0);
      ui_button_set(btnRow[i], under_way ? "Saving..." : "Remove", Kind::Quiet, true, Icon::None, 1);
      ui_button_enable(btnRow[i], !ui_busy());
      continue;
    }
    if (used >= got) {
      hide_row(i);
      continue;
    }
    const Spool &sp = shown[used++];
    rowSpool[i] = sp.id;
    set_swatch(i, true, sp.rgb);
    ui_set_text(lblLine1[i], sp.name);
    format_remaining(left, sizeof(left), sp.remainG);
    snprintf(buf, sizeof(buf), left[0] ? "%s, %s" : "%s", sp.material, left);
    ui_set_text(lblLine2[i], buf);
    bool here = sp.id == pickedSpool;
    bool under_way = ui_pending(Command::AssignSpool, sp.id);
    // Putting a spool in a slot changes Bambuddy's records and the printer's slot: a hold.
    ui_button_set(btnRow[i], here ? "In slot" : under_way ? "Saving..." : "Assign", here ? Kind::Quiet : Kind::Go,
                  true, Icon::None, sp.id + 2);
    ui_button_enable(btnRow[i], !here && !ui_busy());
  }
}

static void draw_rows(const Snapshot *s)
{
  if (!box) return;
  if (mode == Mode::Slots && s) draw_slots(*s);
  else if (mode == Mode::Pick) draw_spools();
}

void filament_draw(const Snapshot &s)
{
  build();
  // A change Bambuddy accepted: back to the slots, which will show it.
  if (sentChange && ui_answered()) {
    mode = Mode::Slots;
    page = 0;
  }
  if (!ui_busy() || ui_answered()) sentChange = false;

  if (mode == Mode::Pick) {
    const Assignment *a = assignment_of(s, picked);
    pickedSpool = a ? a->spoolId : 0;
    snprintf(pickedName, sizeof(pickedName), "%s", a ? a->name : "");
  }
  draw_rows(&s);

  // Header: the slots have a title, the spool list a button that steps through the materials.
  char buf[40], name[12];
  bool pick = mode == Mode::Pick;
  // In the spool list Back says which slot the spool is for, and goes back to the slots.
  format_slot(name, sizeof(name), picked);
  snprintf(buf, sizeof(buf), LV_SYMBOL_LEFT " %s", pick ? name : "Back");
  ui_button_set(btnBack, buf, Kind::Quiet, false);
  if (pick) {
    snprintf(buf, sizeof(buf), "%s", material[0] ? material : "All types");
    ui_button_set(btnMaterial, buf, Kind::Quiet, false);
    lv_obj_remove_flag(btnMaterial.btn, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(lblTitle, "");
  } else {
    lv_obj_add_flag(btnMaterial.btn, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(lblTitle, "Filament");
  }
  snprintf(buf, sizeof(buf), "%d/%d", page + 1, pages);
  lv_label_set_text(lblPage, buf);
  for (lv_obj_t *o : {btnPrev.btn, btnNext.btn, lblPage}) {
    if (pages > 1) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
  }
}

// The spool is offered only for a printer that has somewhere to put one.
void filament_draw_badge(const Snapshot &s)
{
  if (s.slotCount > 0) lv_obj_remove_flag(btnOpen.btn, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(btnOpen.btn, LV_OBJ_FLAG_HIDDEN);
}
