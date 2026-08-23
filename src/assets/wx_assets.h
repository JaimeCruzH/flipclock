/* generado por tools/emit_icons.py - no editar */
#pragma once
#include <lvgl.h>
#include "../weather_src.h"

#define WX_ICON_BIG   104
#define WX_ICON_SMALL 44

const lv_image_dsc_t *wx_icon_big(wx_cond_t c, bool is_day);
const lv_image_dsc_t *wx_icon_small(wx_cond_t c, bool is_day);
