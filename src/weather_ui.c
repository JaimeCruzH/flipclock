#include "weather_ui.h"
#include "clock_ui.h"
#include "weather_src.h"
#include "prefs.h"
#include "assets/wx_assets.h"
#include "swipe.h"

#include <stdio.h>
#include <math.h>

/*
 * Pantalla del tiempo, en 480x320 apaisado:
 *
 *   ┌──────────────────────────────────────────┐
 *   │ VITACURA                            ☀    │
 *   │   25°   Despejado                        │
 *   │   ↑27° ↓12°   Se siente 24°    obs. 18:00 │
 *   ├──────────────────────────────────────────┤
 *   │ 19h  20h  21h  22h  23h  00h             │
 *   │  ☾    ☾    ☁    ☾    ☾    ☾              │
 *   │ 22°  21°  20°  18°  17°  16°             │
 *   │  0%   0%   0%   0%   0%   0%             │
 *   └──────────────────────────────────────────┘
 *
 * El fondo es un degradado que cambia con la hora. No hay animacion al entrar
 * ni al salir: a ~10 fps un deslizamiento se ve peor que un cambio seco.
 */

#define AUTO_BACK_MS   30000
#define STRIP_Y        188
#define STRIP_H        (320 - STRIP_Y)

static lv_obj_t *s_screen, *s_bg;
static lv_obj_t *s_place, *s_temp, *s_cond, *s_minmax, *s_feels, *s_obs;
static lv_obj_t *s_icon_big;
static lv_obj_t *s_strip;
static lv_obj_t *s_h_hour[6], *s_h_icon[6], *s_h_temp[6], *s_h_pop[6];
static lv_timer_t *s_back_timer;

/* Paleta del fondo segun el momento del dia. El degradado va de arriba (mas
 * saturado) a abajo (mas claro), como en la app del movil. */
static void background_colors(const wx_data_t *d, lv_color_t *top, lv_color_t *bot)
{
    struct tm t;
    time_t now = time(NULL);
    localtime_r(&now, &t);
    int h = t.tm_hour;

    if (!d->is_day) {
        if (h >= 5 && h < 8) {              /* amanecer */
            *top = lv_color_hex(0x3B3A6B);
            *bot = lv_color_hex(0xC2748A);
        } else {                            /* noche cerrada */
            *top = lv_color_hex(0x20224E);
            *bot = lv_color_hex(0x5C4A84);
        }
        return;
    }

    if (h >= 18) {                          /* atardecer */
        *top = lv_color_hex(0x3E5C93);
        *bot = lv_color_hex(0xE0925E);
    } else {                                /* pleno dia */
        *top = lv_color_hex(0x2F79C4);
        *bot = lv_color_hex(0xB0CEE8);
    }
}

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, "");
    return l;
}

/* El temporizador de vuelta hay que matarlo al salir a mano: si no, seguiria
 * vivo y 30 s despues sacaria al usuario de donde estuviese (por ejemplo, de
 * los ajustes). */
static void cancel_auto_back(void)
{
    if (s_back_timer) {
        lv_timer_delete(s_back_timer);
        s_back_timer = NULL;
    }
}

static void back_to_clock(lv_event_t *e)
{
    LV_UNUSED(e);
    cancel_auto_back();
    clock_ui_show();
}

static void auto_back_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    s_back_timer = NULL;          /* se autodestruye: repeat_count = 1 */
    clock_ui_show();
}

static void on_swipe(lv_dir_t dir)
{
    if (dir == LV_DIR_RIGHT) {
        cancel_auto_back();
        clock_ui_show();
    }
}

void weather_ui_create(void)
{
    const lv_color_t WHITE = lv_color_white();
    const lv_color_t SOFT  = lv_color_hex(0xD8DEEA);

    s_screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_screen);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    swipe_attach(s_screen, on_swipe);

    /* Un objeto de fondo que cubre la pantalla, en vez de pintar el degradado
     * sobre la pantalla misma: el degradado vive aqui y aqui se escucha la
     * pulsacion larga. */
    s_bg = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_bg);
    lv_obj_set_pos(s_bg, 0, 0);
    lv_obj_set_size(s_bg, 480, 320);
    lv_obj_remove_flag(s_bg, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_bg, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(s_bg, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_grad_dir(s_bg, LV_GRAD_DIR_VER, 0);
    lv_obj_add_event_cb(s_bg, back_to_clock, LV_EVENT_LONG_PRESSED, NULL);

    s_place = label(s_screen, &lv_font_montserrat_22, WHITE);
    lv_obj_set_pos(s_place, 22, 16);

    s_temp = label(s_screen, &lv_font_montserrat_48, WHITE);
    lv_obj_set_pos(s_temp, 18, 54);

    s_cond = label(s_screen, &lv_font_montserrat_20, WHITE);
    lv_obj_set_pos(s_cond, 22, 116);

    s_minmax = label(s_screen, &lv_font_montserrat_18, SOFT);
    lv_obj_set_pos(s_minmax, 22, 146);

    s_feels = label(s_screen, &lv_font_montserrat_16, SOFT);
    lv_obj_set_pos(s_feels, 170, 148);

    s_obs = label(s_screen, &lv_font_montserrat_14, SOFT);
    lv_obj_set_pos(s_obs, 300, 150);

    s_icon_big = lv_image_create(s_screen);
    lv_obj_set_pos(s_icon_big, 480 - WX_ICON_BIG - 26, 26);

    /* Franja de las proximas horas, sobre un panel translucido como en la
     * referencia: separa sin romper el degradado. */
    s_strip = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_strip);
    lv_obj_set_pos(s_strip, 12, STRIP_Y);
    lv_obj_set_size(s_strip, 480 - 24, STRIP_H - 12);
    lv_obj_remove_flag(s_strip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_strip, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(s_strip, LV_OPA_20, 0);
    lv_obj_set_style_radius(s_strip, 14, 0);

    const int col_w = (480 - 24) / 6;
    for (int i = 0; i < 6; i++) {
        int cx = i * col_w;

        s_h_hour[i] = label(s_strip, &lv_font_montserrat_16, SOFT);
        lv_obj_set_width(s_h_hour[i], col_w);
        lv_obj_set_style_text_align(s_h_hour[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(s_h_hour[i], cx, 6);

        s_h_icon[i] = lv_image_create(s_strip);
        lv_obj_set_pos(s_h_icon[i], cx + (col_w - WX_ICON_SMALL) / 2, 26);

        s_h_temp[i] = label(s_strip, &lv_font_montserrat_18, WHITE);
        lv_obj_set_width(s_h_temp[i], col_w);
        lv_obj_set_style_text_align(s_h_temp[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(s_h_temp[i], cx, 72);

        s_h_pop[i] = label(s_strip, &lv_font_montserrat_14, lv_color_hex(0x8FC4F0));
        lv_obj_set_width(s_h_pop[i], col_w);
        lv_obj_set_style_text_align(s_h_pop[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(s_h_pop[i], cx, 96);
    }
}

static void refresh(void)
{
    wx_data_t d;
    char buf[48];

    if (!weather_src_get(&d)) {
        lv_label_set_text(s_place, prefs_get_place());
        lv_label_set_text(s_temp, "--");
        lv_label_set_text(s_cond, "Sin datos");
        lv_label_set_text(s_minmax, "");
        lv_label_set_text(s_feels, "");
        lv_label_set_text(s_obs, "");
        lv_obj_add_flag(s_icon_big, LV_OBJ_FLAG_HIDDEN);
        for (int i = 0; i < 6; i++) {
            lv_label_set_text(s_h_hour[i], "");
            lv_label_set_text(s_h_temp[i], "");
            lv_label_set_text(s_h_pop[i], "");
            lv_obj_add_flag(s_h_icon[i], LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_set_style_bg_color(s_bg, lv_color_hex(0x20224E), 0);
        lv_obj_set_style_bg_grad_color(s_bg, lv_color_hex(0x5C4A84), 0);
        return;
    }

    lv_label_set_text(s_place, d.place[0] ? d.place : prefs_get_place());

    lv_color_t top, bot;
    background_colors(&d, &top, &bot);
    lv_obj_set_style_bg_color(s_bg, top, 0);
    lv_obj_set_style_bg_grad_color(s_bg, bot, 0);

    snprintf(buf, sizeof(buf), "%d°", (int)lroundf(d.temp));
    lv_label_set_text(s_temp, buf);

    lv_label_set_text(s_cond, wx_cond_name(d.cond));

    snprintf(buf, sizeof(buf), LV_SYMBOL_UP " %d°   " LV_SYMBOL_DOWN " %d°",
             (int)lroundf(d.tmax), (int)lroundf(d.tmin));
    lv_label_set_text(s_minmax, buf);

    snprintf(buf, sizeof(buf), "Se siente %d°", (int)lroundf(d.feels));
    lv_label_set_text(s_feels, buf);

    /* De cuando es el dato, no la hora actual: el METAR es horario y conviene
     * que se vea. Si no hubo observacion, se dice que es del modelo. */
    struct tm obs;
    localtime_r(&d.obs_time, &obs);
    snprintf(buf, sizeof(buf), "%s %02d:%02d",
             d.from_metar ? "obs." : "modelo", obs.tm_hour, obs.tm_min);
    lv_label_set_text(s_obs, buf);

    lv_obj_remove_flag(s_icon_big, LV_OBJ_FLAG_HIDDEN);
    lv_image_set_src(s_icon_big, wx_icon_big(d.cond, d.is_day));

    for (int i = 0; i < 6; i++) {
        if (i < d.n_hours) {
            snprintf(buf, sizeof(buf), "%02d h", d.hours[i].hour);
            lv_label_set_text(s_h_hour[i], buf);
            snprintf(buf, sizeof(buf), "%d°", (int)lroundf(d.hours[i].temp));
            lv_label_set_text(s_h_temp[i], buf);
            snprintf(buf, sizeof(buf), "%d%%", d.hours[i].pop);
            lv_label_set_text(s_h_pop[i], buf);
            lv_obj_remove_flag(s_h_icon[i], LV_OBJ_FLAG_HIDDEN);
            lv_image_set_src(s_h_icon[i],
                             wx_icon_small(d.hours[i].cond, d.hours[i].is_day));
        } else {
            lv_label_set_text(s_h_hour[i], "");
            lv_label_set_text(s_h_temp[i], "");
            lv_label_set_text(s_h_pop[i], "");
            lv_obj_add_flag(s_h_icon[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void weather_ui_show(void)
{
    if (!s_screen) weather_ui_create();
    refresh();
    lv_screen_load(s_screen);

    cancel_auto_back();
    s_back_timer = lv_timer_create(auto_back_cb, AUTO_BACK_MS, NULL);
    lv_timer_set_repeat_count(s_back_timer, 1);
}

