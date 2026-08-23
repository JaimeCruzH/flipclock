#include "settings_ui.h"
#include "clock_ui.h"
#include "time_src.h"
#include "prefs.h"
#include "esp_bsp.h"

#include <lvgl.h>
#include <stdio.h>

/*
 * Pantalla de ajustes en tres pestanas: HORA (rollers grandes, pensados para el
 * dedo), WIFI (dos campos y el teclado de LVGL) y PANTALLA (brillo y giro de
 * 180 grados). Se crea al abrirla y se destruye al cerrarla: no tiene sentido
 * tener esto ocupando RAM las 24 horas.
 */

static lv_obj_t *s_scr;
static lv_obj_t *s_roll_h, *s_roll_m, *s_roll_d, *s_roll_mo, *s_roll_y;
static lv_obj_t *s_ta_ssid, *s_ta_pass, *s_kb;
static lv_obj_t *s_slider, *s_lbl_bright, *s_sw_flip;

static const char *OPT_HORA  = "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n"
                               "12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23";
static const char *OPT_MES   = "ENE\nFEB\nMAR\nABR\nMAY\nJUN\n"
                               "JUL\nAGO\nSEP\nOCT\nNOV\nDIC";

static void close_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    bsp_display_brightness_set(prefs_get_brightness());   /* ya sin el minimo de UI */
    clock_ui_show();
    lv_obj_delete_async(s_scr);
    s_scr = NULL;
}

static void save_time_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    time_src_set_manual(2020 + (int)lv_roller_get_selected(s_roll_y),
                        1 + (int)lv_roller_get_selected(s_roll_mo),
                        1 + (int)lv_roller_get_selected(s_roll_d),
                        (int)lv_roller_get_selected(s_roll_h),
                        (int)lv_roller_get_selected(s_roll_m));
    close_cb(e);
}

static void save_wifi_cb(lv_event_t *e)
{
    time_src_set_wifi(lv_textarea_get_text(s_ta_ssid),
                      lv_textarea_get_text(s_ta_pass));
    close_cb(e);
}

static void ta_focus_cb(lv_event_t *e)
{
    lv_keyboard_set_textarea(s_kb, lv_event_get_target(e));
    lv_obj_remove_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
}

static void kb_ready_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
}

static lv_obj_t *make_roller(lv_obj_t *parent, const char *opts, int sel, int w)
{
    lv_obj_t *r = lv_roller_create(parent);
    lv_roller_set_options(r, opts, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(r, 3);
    lv_roller_set_selected(r, sel, LV_ANIM_OFF);
    lv_obj_set_width(r, w);
    lv_obj_set_style_text_font(r, &lv_font_montserrat_20, 0);
    return r;
}

static lv_obj_t *make_button(lv_obj_t *parent, const char *txt, lv_event_cb_t cb)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_size(b, 110, 44);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, txt);
    lv_obj_center(l);
    return b;
}

static void build_tab_hora(lv_obj_t *tab)
{
    struct tm t;
    time_src_now(&t);

    /* Los dias se generan a 31 fijos: mktime normaliza un 31 de febrero, asi
     * que como mucho el ajuste cae en el 3 de marzo, no en una fecha invalida. */
    static char opt_dia[31 * 3 + 1];
    static char opt_ano[16 * 5 + 1];
    static char opt_min[60 * 3 + 1];
    char *p = opt_dia;
    for (int i = 1; i <= 31; i++) p += sprintf(p, i == 1 ? "%d" : "\n%d", i);
    p = opt_ano;
    for (int i = 2020; i <= 2035; i++) p += sprintf(p, i == 2020 ? "%d" : "\n%d", i);
    p = opt_min;
    for (int i = 0; i < 60; i++) p += sprintf(p, i == 0 ? "%02d" : "\n%02d", i);

    lv_obj_t *row = lv_obj_create(tab);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), 130);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    s_roll_h  = make_roller(row, OPT_HORA, t.tm_hour, 62);
    s_roll_m  = make_roller(row, opt_min, t.tm_min, 62);
    s_roll_d  = make_roller(row, opt_dia, t.tm_mday - 1, 62);
    s_roll_mo = make_roller(row, OPT_MES, t.tm_mon, 76);
    s_roll_y  = make_roller(row, opt_ano, t.tm_year + 1900 - 2020, 80);

    lv_obj_t *bar = lv_obj_create(tab);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, lv_pct(100), 50);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    make_button(bar, "Guardar", save_time_cb);
    make_button(bar, "Cancelar", close_cb);
}

static void build_tab_wifi(lv_obj_t *tab)
{
    char ssid[33];
    time_src_get_ssid(ssid, sizeof(ssid));

    s_ta_ssid = lv_textarea_create(tab);
    lv_textarea_set_one_line(s_ta_ssid, true);
    lv_textarea_set_placeholder_text(s_ta_ssid, "SSID");
    lv_textarea_set_text(s_ta_ssid, ssid);
    lv_obj_set_width(s_ta_ssid, lv_pct(92));
    lv_obj_align(s_ta_ssid, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_add_event_cb(s_ta_ssid, ta_focus_cb, LV_EVENT_FOCUSED, NULL);

    s_ta_pass = lv_textarea_create(tab);
    lv_textarea_set_one_line(s_ta_pass, true);
    lv_textarea_set_password_mode(s_ta_pass, true);
    lv_textarea_set_placeholder_text(s_ta_pass, "Clave WiFi");
    lv_obj_set_width(s_ta_pass, lv_pct(92));
    lv_obj_align(s_ta_pass, LV_ALIGN_TOP_MID, 0, 46);
    lv_obj_add_event_cb(s_ta_pass, ta_focus_cb, LV_EVENT_FOCUSED, NULL);

    lv_obj_t *bar = lv_obj_create(tab);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, lv_pct(100), 50);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 96);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    make_button(bar, "Conectar", save_wifi_cb);
    make_button(bar, "Cancelar", close_cb);
}

static void brightness_cb(lv_event_t *e)
{
    int v = (int)lv_slider_get_value(lv_event_get_target(e));
    prefs_set_brightness(v);

    /* En vivo, pero sin dejar la pantalla a oscuras mientras se ajusta: aqui
     * nunca se baja del minimo de UI. El valor real se aplica al salir. */
    bsp_display_brightness_set(v < PREFS_BRIGHTNESS_UI_MIN ? PREFS_BRIGHTNESS_UI_MIN : v);

    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", v);
    lv_label_set_text(s_lbl_bright, buf);
}

static void flip_cb(lv_event_t *e)
{
    bool on = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    prefs_set_flipped(on);
    bsp_display_set_flipped(on);
}

static void build_tab_pantalla(lv_obj_t *tab)
{
    lv_obj_t *t = lv_label_create(tab);
    lv_label_set_text(t, "Brillo");
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, 0);

    s_lbl_bright = lv_label_create(tab);
    lv_obj_align(s_lbl_bright, LV_ALIGN_TOP_RIGHT, 0, 0);

    s_slider = lv_slider_create(tab);
    lv_slider_set_range(s_slider, 0, 100);
    lv_slider_set_value(s_slider, prefs_get_brightness(), LV_ANIM_OFF);
    lv_obj_set_width(s_slider, lv_pct(92));
    lv_obj_align(s_slider, LV_ALIGN_TOP_MID, 0, 30);
    /* barra mas alta de lo normal: se maneja con el dedo, no con un raton */
    lv_obj_set_height(s_slider, 18);
    lv_obj_add_event_cb(s_slider, brightness_cb, LV_EVENT_VALUE_CHANGED, NULL);

    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", prefs_get_brightness());
    lv_label_set_text(s_lbl_bright, buf);

    lv_obj_t *t2 = lv_label_create(tab);
    lv_label_set_text(t2, "Girar 180" LV_SYMBOL_LOOP);
    lv_obj_align(t2, LV_ALIGN_TOP_LEFT, 0, 74);

    s_sw_flip = lv_switch_create(tab);
    lv_obj_set_size(s_sw_flip, 70, 36);
    lv_obj_align(s_sw_flip, LV_ALIGN_TOP_RIGHT, 0, 68);
    if (prefs_get_flipped()) lv_obj_add_state(s_sw_flip, LV_STATE_CHECKED);
    lv_obj_add_event_cb(s_sw_flip, flip_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *bar = lv_obj_create(tab);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, lv_pct(100), 50);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    make_button(bar, "Volver", close_cb);
}

void settings_ui_open(void)
{
    if (s_scr) return;

    /* Si el reloj estaba con el brillo muy bajo (o apagado del todo), subirlo
     * para que se vea la propia pantalla de ajustes. Es lo que hace posible
     * recuperarse de haber dejado el brillo al 0 %. */
    if (prefs_get_brightness() < PREFS_BRIGHTNESS_UI_MIN) {
        bsp_display_brightness_set(PREFS_BRIGHTNESS_UI_MIN);
    }

    s_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(0x1B1B1E), 0);

    lv_obj_t *tabs = lv_tabview_create(s_scr);
    lv_tabview_set_tab_bar_size(tabs, 40);
    lv_obj_set_size(tabs, lv_pct(100), lv_pct(100));

    build_tab_hora(lv_tabview_add_tab(tabs, "HORA"));
    build_tab_wifi(lv_tabview_add_tab(tabs, "WIFI"));
    build_tab_pantalla(lv_tabview_add_tab(tabs, "PANTALLA"));

    /* El teclado se crea en la pantalla, no en la pestana, para que se dibuje
     * por encima de todo y no lo recorte el contenedor de la pestana. */
    s_kb = lv_keyboard_create(s_scr);
    lv_obj_set_size(s_kb, lv_pct(100), lv_pct(48));
    lv_obj_align(s_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_kb, kb_ready_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(s_kb, kb_ready_cb, LV_EVENT_CANCEL, NULL);

    lv_screen_load(s_scr);
}
