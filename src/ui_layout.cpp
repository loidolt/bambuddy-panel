// Sizes for the screen the UI finds itself on: the unit grid and the three text sizes.
#include "ui_internal.h"

UiMetrics ui;

static const int BASE_W = 320, BASE_H = 240; // the screen the layout was drawn for: 1 unit = 1 px

int px(int units)
{
  int v = units * ui.scale;
  return v >= 0 ? (v + 128) / 256 : -((-v + 128) / 256);
}

// The Montserrat sizes built into this firmware (LV_FONT_MONTSERRAT_nn in the build flags),
// smallest first.
static const struct {
  int size;
  const lv_font_t *font;
} built[] = {
#if LV_FONT_MONTSERRAT_12
    {12, &lv_font_montserrat_12},
#endif
#if LV_FONT_MONTSERRAT_14
    {14, &lv_font_montserrat_14},
#endif
#if LV_FONT_MONTSERRAT_16
    {16, &lv_font_montserrat_16},
#endif
#if LV_FONT_MONTSERRAT_18
    {18, &lv_font_montserrat_18},
#endif
#if LV_FONT_MONTSERRAT_20
    {20, &lv_font_montserrat_20},
#endif
#if LV_FONT_MONTSERRAT_22
    {22, &lv_font_montserrat_22},
#endif
#if LV_FONT_MONTSERRAT_24
    {24, &lv_font_montserrat_24},
#endif
#if LV_FONT_MONTSERRAT_26
    {26, &lv_font_montserrat_26},
#endif
#if LV_FONT_MONTSERRAT_28
    {28, &lv_font_montserrat_28},
#endif
#if LV_FONT_MONTSERRAT_30
    {30, &lv_font_montserrat_30},
#endif
#if LV_FONT_MONTSERRAT_32
    {32, &lv_font_montserrat_32},
#endif
#if LV_FONT_MONTSERRAT_34
    {34, &lv_font_montserrat_34},
#endif
#if LV_FONT_MONTSERRAT_36
    {36, &lv_font_montserrat_36},
#endif
#if LV_FONT_MONTSERRAT_38
    {38, &lv_font_montserrat_38},
#endif
#if LV_FONT_MONTSERRAT_40
    {40, &lv_font_montserrat_40},
#endif
#if LV_FONT_MONTSERRAT_42
    {42, &lv_font_montserrat_42},
#endif
#if LV_FONT_MONTSERRAT_44
    {44, &lv_font_montserrat_44},
#endif
#if LV_FONT_MONTSERRAT_46
    {46, &lv_font_montserrat_46},
#endif
#if LV_FONT_MONTSERRAT_48
    {48, &lv_font_montserrat_48},
#endif
};

// The biggest built-in size that is no taller than `units`; the smallest there is when none fits.
static const lv_font_t *font_for(int units)
{
  const int want = px(units), count = sizeof(built) / sizeof(built[0]);
  const lv_font_t *best = built[0].font;
  for (int i = 1; i < count; i++)
    if (built[i].size <= want) best = built[i].font;
  return best;
}

void ui_layout_begin(int w, int h)
{
  ui.w = w;
  ui.h = h;
  // As many pixels per unit as fit both ways, so the 320x240 layout always fits on the screen;
  // what is left over is extra units of width or height, which the screens stretch into.
  int sx = w * 256 / BASE_W, sy = h * 256 / BASE_H;
  ui.scale = sx < sy ? sx : sy;
  ui.uw = w * 256 / ui.scale;
  ui.uh = h * 256 / ui.scale;
  ui.small = font_for(14);
  ui.medium = font_for(20);
  ui.large = font_for(28);
}
