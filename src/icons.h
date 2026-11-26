// Icons drawn by tools/make_icons.py: 8-bit alpha masks, coloured by the UI. Each comes at one
// pixel per unit (320x240 panels) and, as _x133, at 1.33 (480x320), so none is ever stretched.
#pragma once
#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const lv_image_dsc_t icon_thumb_up, icon_thumb_up_x133;
extern const lv_image_dsc_t icon_thumb_down, icon_thumb_down_x133;
extern const lv_image_dsc_t icon_wrench, icon_wrench_x133;
extern const lv_image_dsc_t icon_spool, icon_spool_x133;

#ifdef __cplusplus
}
#endif

// The drawing of an icon made for this screen's scale (ui_internal.h has `ui`).
#define ICON(name) (ui.scale > 300 ? &name##_x133 : &name)
