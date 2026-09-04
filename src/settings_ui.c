#include "settings_ui.h"
#include "clock_ui.h"
#include "time_src.h"
#include "prefs.h"
#include "esp_bsp.h"
#include "night_ui.h"
#include "pomodoro_ui.h"
#include "pomodoro_src.h"
#include "battery_src.h"
#include "power_manager.h"

#include <lvgl.h>
#include <stdio.h>

/*
 * Pantalla de ajustes en cuatro pestanas: HORA (rollers grandes, pensados para
 * el dedo), WIFI (dos campos y el teclado de LVGL), PANTALLA (brillo y giro de
 * 180 grados) y POMO (duraciones y ciclos). Se crea al abrirla y se destruye al
 * cerrarla: no tiene sentido tener esto ocupando RAM las 24 horas.
 */

static lv_obj_t *s_scr;
static lv_obj_t *s_roll_h, *s_roll_m, *s_roll_d, *s_roll_mo, *s_roll_y;
static lv_obj_t *s_ta_ssid, *s_ta_pass, *s_kb;
static lv_obj_t *s_slider, *s_lbl_bright, *s_sw_flip;
static lv_obj_t *s_night_roller;
static lv_obj_t *s_pomo_work, *s_pomo_short, *s_pomo_long, *s_pomo_cycles;
static lv_obj_t *s_sw_resume;
static lv_obj_t *s_lbl_battery;
static lv_obj_t *s_lbl_battery_trend;
static lv_obj_t *s_lbl_battery_runtime;
static lv_timer_t *s_battery_timer;
static lv_timer_t *s_sleep_timer;
static settings_origin_t s_origin = SETTINGS_FROM_CLOCK;

static const char POWER_CONFIRM_TOKEN = 0;

#define BATTERY_DISPLAY_AVERAGE_SIZE 15
static uint32_t s_battery_voltage_samples[BATTERY_DISPLAY_AVERAGE_SIZE];
static uint32_t s_battery_percent_samples[BATTERY_DISPLAY_AVERAGE_SIZE];
static uint32_t s_battery_voltage_sum;
static uint32_t s_battery_percent_sum;
static uint8_t s_battery_average_count;
static uint8_t s_battery_average_next;

static const char *OPT_HORA  = "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n"
                               "12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23";
static const char *OPT_MES   = "ENE\nFEB\nMAR\nABR\nMAY\nJUN\n"
                               "JUL\nAGO\nSEP\nOCT\nNOV\nDIC";

static char s_opt_pomo_minutes[99 * 3 + 1];
static const char *OPT_POMO_CYCLES = "1\n2\n3\n4\n5\n6\n7\n8\n9";
static const char *OPT_NIGHT_BRIGHTNESS =
    "1%\n2%\n3%\n4%\n5%\n6%\n7%\n8%\n9%\n10%\n"
    "11%\n12%\n13%\n14%\n15%\n16%\n17%\n18%\n19%\n20%";

/* En este panel, 1 mm son aproximadamente 6,5 px. */
#define POMO_MM_PX 7

static void close_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (s_battery_timer) {
        lv_timer_delete(s_battery_timer);
        s_battery_timer = NULL;
    }
    s_lbl_battery = NULL;
    s_lbl_battery_trend = NULL;
    s_lbl_battery_runtime = NULL;
    bsp_display_brightness_set(prefs_get_brightness());   /* ya sin el minimo de UI */
    if (s_origin == SETTINGS_FROM_POMODORO) pomodoro_ui_show();
    else                                    clock_ui_show();
    lv_obj_delete_async(s_scr);
    s_scr = NULL;
}

static void enter_sleep_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    s_sleep_timer = NULL;
    power_manager_enter_deep_sleep();
}

static void power_dialog_cb(lv_event_t *e)
{
    lv_obj_t *button = lv_event_get_current_target_obj(e);
    lv_obj_t *mbox = lv_obj_get_parent(lv_obj_get_parent(button));

    if (lv_event_get_user_data(e) == &POWER_CONFIRM_TOKEN) {
        lv_msgbox_close(mbox);
        if (!s_sleep_timer) {
            s_sleep_timer = lv_timer_create(enter_sleep_cb, 250, NULL);
            if (s_sleep_timer) lv_timer_set_repeat_count(s_sleep_timer, 1);
        }
    } else {
        lv_msgbox_close(mbox);
    }
}

static void power_button_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (s_sleep_timer) return;

    lv_obj_t *mbox = lv_msgbox_create(NULL);
    if (!mbox) return;

    lv_obj_set_size(mbox, 300, 150);
    lv_msgbox_add_title(mbox, "Apagar reloj");
    lv_msgbox_add_text(mbox, "Entrar en deep sleep?");

    lv_obj_t *confirm = lv_msgbox_add_footer_button(mbox, "Apagar");
    lv_obj_add_event_cb(confirm, power_dialog_cb, LV_EVENT_CLICKED,
                        (void *)&POWER_CONFIRM_TOKEN);

    lv_obj_t *cancel = lv_msgbox_add_footer_button(mbox, "Cancelar");
    lv_obj_add_event_cb(cancel, power_dialog_cb, LV_EVENT_CLICKED, NULL);
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

static void battery_average_reset(void)
{
    s_battery_voltage_sum = 0;
    s_battery_percent_sum = 0;
    s_battery_average_count = 0;
    s_battery_average_next = 0;
}

static void battery_average_apply(battery_data_t *data)
{
    if (!data || !data->valid) return;

    if (s_battery_average_count == BATTERY_DISPLAY_AVERAGE_SIZE) {
        s_battery_voltage_sum -= s_battery_voltage_samples[s_battery_average_next];
        s_battery_percent_sum -= s_battery_percent_samples[s_battery_average_next];
    } else {
        s_battery_average_count++;
    }

    s_battery_voltage_samples[s_battery_average_next] = data->millivolts;
    s_battery_percent_samples[s_battery_average_next] = data->percent;
    s_battery_voltage_sum += data->millivolts;
    s_battery_percent_sum += data->percent;

    s_battery_average_next++;
    if (s_battery_average_next == BATTERY_DISPLAY_AVERAGE_SIZE) {
        s_battery_average_next = 0;
    }

    data->millivolts = s_battery_voltage_sum / s_battery_average_count;
    data->percent = (uint8_t)
        ((s_battery_percent_sum + s_battery_average_count / 2) /
         s_battery_average_count);
}

static void battery_trend_update(const battery_data_t *data)
{
    if (!s_lbl_battery_trend || !data) return;

    const char *symbol = LV_SYMBOL_BULLET;
    const char *state = "Esperando";
    lv_color_t color = lv_color_hex(0x9A9A9E);

    switch (data->trend) {
    case BATTERY_TREND_UP:
        symbol = LV_SYMBOL_UP;
        state = "Cargando";
        color = lv_color_hex(0x22C55E);
        break;
    case BATTERY_TREND_DOWN:
        symbol = LV_SYMBOL_DOWN;
        state = "Descargando";
        color = lv_color_hex(0xEF4444);
        break;
    case BATTERY_TREND_STABLE:
        symbol = LV_SYMBOL_BULLET;
        state = "Estable";
        color = lv_color_hex(0x3B82F6);
        break;
    default:
        break;
    }

    char text[48];
    snprintf(text, sizeof(text), "Tendencia: %s %s", symbol, state);
    lv_label_set_text(s_lbl_battery_trend, text);
    lv_obj_set_style_text_color(s_lbl_battery_trend, color, 0);
}

static void battery_runtime_update(const battery_data_t *data)
{
    if (!s_lbl_battery_runtime || !data) return;

    char text[64];
    if (data->autonomy_valid) {
        const unsigned hours = (unsigned)(data->autonomy_minutes / 60);
        const unsigned minutes = (unsigned)(data->autonomy_minutes % 60);
        if (hours > 0) {
            snprintf(text, sizeof(text), "Autonomia: ~%uh %02um  (-%u%%/h)",
                     hours, minutes,
                     (unsigned)data->discharge_percent_per_hour);
        } else {
            snprintf(text, sizeof(text), "Autonomia: ~%um  (-%u%%/h)",
                     minutes,
                     (unsigned)data->discharge_percent_per_hour);
        }
    } else if (data->trend == BATTERY_TREND_DOWN) {
        snprintf(text, sizeof(text), "Autonomia: calculando...");
    } else if (data->trend == BATTERY_TREND_UP) {
        snprintf(text, sizeof(text), "Autonomia: no aplica mientras carga");
    } else if (data->trend == BATTERY_TREND_STABLE) {
        snprintf(text, sizeof(text), "Autonomia: esperando descarga");
    } else {
        snprintf(text, sizeof(text), "Autonomia: esperando mediciones");
    }
    lv_label_set_text(s_lbl_battery_runtime, text);
}

static void battery_update(void)
{
    if (!s_lbl_battery) return;

    battery_data_t data;
    if (!battery_src_read(&data)) {
        lv_label_set_text(s_lbl_battery, "Bateria: sin lectura");
        battery_trend_update(&data);
        battery_runtime_update(&data);
        return;
    }

    battery_average_apply(&data);
    char text[48];
    snprintf(text, sizeof(text), "Bateria: %d%%  (%u,%02u V)",
             (int)data.percent,
             (unsigned)(data.millivolts / 1000),
             (unsigned)((data.millivolts % 1000) / 10));
    lv_label_set_text(s_lbl_battery, text);
    battery_trend_update(&data);
    battery_runtime_update(&data);
}

static void battery_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    battery_update();
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

static lv_obj_t *make_button_sized(lv_obj_t *parent, const char *txt,
                                    lv_event_cb_t cb, int width, int height)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_size(b, width, height);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, txt);
    lv_obj_center(l);
    return b;
}

static lv_obj_t *make_button(lv_obj_t *parent, const char *txt, lv_event_cb_t cb)
{
    return make_button_sized(parent, txt, cb, 110, 44);
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
    make_button(bar, "Apagar", power_button_cb);
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

    s_lbl_battery = lv_label_create(tab);
    lv_label_set_text(s_lbl_battery, "Bateria: leyendo...");
    lv_obj_align(s_lbl_battery, LV_ALIGN_TOP_MID, 0, 158);

    s_lbl_battery_trend = lv_label_create(tab);
    lv_label_set_text(s_lbl_battery_trend, "Tendencia: esperando");
    lv_obj_align(s_lbl_battery_trend, LV_ALIGN_TOP_MID, 0, 184);

    s_lbl_battery_runtime = lv_label_create(tab);
    lv_label_set_text(s_lbl_battery_runtime, "Autonomia: esperando mediciones");
    lv_obj_align(s_lbl_battery_runtime, LV_ALIGN_TOP_MID, 0, 210);

    battery_average_reset();
    battery_update();
    s_battery_timer = lv_timer_create(battery_timer_cb, 1000, NULL);
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

static void night_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    lv_obj_delete_async(s_scr);
    s_scr = NULL;
    night_ui_show();
}

static void night_brightness_cb(lv_event_t *e)
{
    int brightness = (int)lv_roller_get_selected(lv_event_get_target(e))
                     + PREFS_NIGHT_BRIGHTNESS_MIN;
    prefs_set_night_brightness(brightness);
}

static void preset_pomo_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    lv_roller_set_selected(s_pomo_work, 24, LV_ANIM_OFF);
    lv_roller_set_selected(s_pomo_short, 4, LV_ANIM_OFF);
    lv_roller_set_selected(s_pomo_long, 14, LV_ANIM_OFF);
    lv_roller_set_selected(s_pomo_cycles, 3, LV_ANIM_OFF);
}

static void save_pomo_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    prefs_pomodoro_t p;
    p.work_minutes = (uint16_t)lv_roller_get_selected(s_pomo_work) + 1;
    p.short_break_minutes = (uint16_t)lv_roller_get_selected(s_pomo_short) + 1;
    p.long_break_minutes = (uint16_t)lv_roller_get_selected(s_pomo_long) + 1;
    p.pomodoros_per_cycle = (uint8_t)lv_roller_get_selected(s_pomo_cycles) + 1;
    p.resume_session = lv_obj_has_state(s_sw_resume, LV_STATE_CHECKED);
    prefs_set_pomodoro(&p);
    pomodoro_reload_config();
    close_cb(e);
}

static void build_tab_pomodoro(lv_obj_t *tab)
{
    prefs_pomodoro_t p;
    prefs_get_pomodoro(&p);

    char *out = s_opt_pomo_minutes;
    for (int i = 1; i <= 99; i++) {
        out += sprintf(out, i == 1 ? "%d" : "\n%d", i);
    }

    const int xs[] = {10, 128, 246, 364};
    const int widths[] = {96, 96, 96, 106};
    const char *titles[] = {"Trabajo", "Corto", "Largo", "Bloques"};
    for (int i = 0; i < 4; i++) {
        lv_obj_t *title = lv_label_create(tab);
        lv_label_set_text(title, titles[i]);
        lv_obj_set_width(title, widths[i]);
        lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(title, xs[i], -POMO_MM_PX);
    }

    s_pomo_work = make_roller(tab, s_opt_pomo_minutes, p.work_minutes - 1, widths[0]);
    s_pomo_short = make_roller(tab, s_opt_pomo_minutes, p.short_break_minutes - 1, widths[1]);
    s_pomo_long = make_roller(tab, s_opt_pomo_minutes, p.long_break_minutes - 1, widths[2]);
    s_pomo_cycles = make_roller(tab, OPT_POMO_CYCLES, p.pomodoros_per_cycle - 1, widths[3]);
    lv_obj_set_pos(s_pomo_work, xs[0], 20 - POMO_MM_PX);
    lv_obj_set_pos(s_pomo_short, xs[1], 20 - POMO_MM_PX);
    lv_obj_set_pos(s_pomo_long, xs[2], 20 - POMO_MM_PX);
    lv_obj_set_pos(s_pomo_cycles, xs[3], 20 - POMO_MM_PX);

    lv_obj_t *preset = make_button_sized(tab, "Usar preset clasico",
                                          preset_pomo_cb, 220, 36);
    lv_obj_set_pos(preset, 130, 128);

    lv_obj_t *resume_label = lv_label_create(tab);
    lv_label_set_text(resume_label, "Reanudar sesion tras reinicio");
    lv_obj_set_pos(resume_label, 18, 178);

    s_sw_resume = lv_switch_create(tab);
    lv_obj_set_size(s_sw_resume, 70, 36);
    lv_obj_align(s_sw_resume, LV_ALIGN_TOP_RIGHT, -18, 168);
    if (p.resume_session) lv_obj_add_state(s_sw_resume, LV_STATE_CHECKED);

    lv_obj_t *bar = lv_obj_create(tab);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, lv_pct(100), 50);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    make_button(bar, "Guardar", save_pomo_cb);
    make_button(bar, "Cancelar", close_cb);
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

    lv_obj_t *night = make_button(tab, "Noche", night_cb);
    lv_obj_set_pos(night, 20, 106);

    s_night_roller = make_roller(
        tab, OPT_NIGHT_BRIGHTNESS,
        prefs_get_night_brightness() - PREFS_NIGHT_BRIGHTNESS_MIN, 72);
    lv_obj_set_pos(s_night_roller, 39, 154);
    lv_obj_add_event_cb(s_night_roller, night_brightness_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *back = make_button(tab, "Volver", close_cb);
    lv_obj_align(back, LV_ALIGN_BOTTOM_RIGHT, -12, -4);
}

void settings_ui_open(settings_origin_t origin)
{
    if (s_scr) return;
    s_origin = origin;

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
    build_tab_pomodoro(lv_tabview_add_tab(tabs, "POMO"));

    if (s_origin == SETTINGS_FROM_POMODORO) {
        lv_tabview_set_active(tabs, 3, LV_ANIM_OFF);
    }

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
