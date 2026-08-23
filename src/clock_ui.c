#include "clock_ui.h"
#include "flip_card.h"
#include "settings_ui.h"
#include "weather_ui.h"
#include "swipe.h"

#include "time_src.h"
#include "assets/flip_assets.h"

#include <stdio.h>

static lv_obj_t    *s_screen;
static lv_obj_t    *s_date;
static lv_obj_t    *s_wifi_dot;
static flip_card_t *s_hours;
static flip_card_t *s_mins;
static lv_timer_t  *s_tick;

/* Sin depender del locale de la libc, que en el core Arduino solo trae "C". */
static const char *DIA[7]  = {"DOM", "LUN", "MAR", "MIE", "JUE", "VIE", "SAB"};
static const char *MES[12] = {"ENE", "FEB", "MAR", "ABR", "MAY", "JUN",
                              "JUL", "AGO", "SEP", "OCT", "NOV", "DIC"};

static void update_date(const struct tm *t)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%s %d %s",
             DIA[t->tm_wday % 7], t->tm_mday, MES[t->tm_mon % 12]);
    lv_label_set_text(s_date, buf);
}

static void tick_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    struct tm t;
    time_src_now(&t);

    /* Solo se voltea cuando el valor cambia de verdad; el resto de segundos no
     * se toca nada y la pantalla no se invalida. Mientras una tarjeta gira se
     * la deja en paz: get_value aun devuelve el valor viejo y volveriamos a
     * lanzar el mismo volteo en cada tick. */
    if (!flip_card_is_animating(s_hours) && t.tm_hour != flip_card_get_value(s_hours)) {
        flip_card_set_value(s_hours, t.tm_hour, true);
    }
    if (!flip_card_is_animating(s_mins) && t.tm_min != flip_card_get_value(s_mins)) {
        flip_card_set_value(s_mins, t.tm_min, true);
        update_date(&t);
    }

    lv_obj_set_style_bg_opa(s_wifi_dot,
                            time_src_wifi_connected() ? LV_OPA_60 : LV_OPA_TRANSP, 0);
}

static void full_refresh_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    lv_obj_invalidate(s_screen);
}

static void long_press_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    settings_ui_open();
}

/* Deslizar a la izquierda saca el tiempo. */
static void on_swipe(lv_dir_t dir)
{
    if (dir == LV_DIR_LEFT) weather_ui_show();
}

void clock_ui_create(void)
{
    s_screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_screen);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *bg = lv_image_create(s_screen);
    lv_image_set_src(bg, &img_case_bg);
    lv_obj_set_pos(bg, 0, 0);

    /* La pulsacion larga abre los ajustes; se escucha en el fondo, que ocupa
     * toda la pantalla salvo donde estan las tarjetas. */
    lv_obj_add_flag(bg, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(bg, long_press_cb, LV_EVENT_LONG_PRESSED, NULL);
    /* El detector va en el indev, asi que se registra la PANTALLA: da igual
     * que el dedo empiece sobre el fondo o sobre una tarjeta. */
    swipe_attach(s_screen, on_swipe);

    s_hours = flip_card_create(s_screen, 0, FLIP_WIN_X, FLIP_WIN_Y);
    s_mins  = flip_card_create(s_screen, 1, FLIP_WIN_X + FLIP_CARD_W + FLIP_GAP,
                               FLIP_WIN_Y);

    s_date = lv_label_create(s_screen);
    lv_obj_set_style_text_color(s_date, lv_color_hex(0x9A9A9E), 0);
    lv_obj_set_style_text_font(s_date, &lv_font_montserrat_16, 0);
    lv_label_set_text(s_date, "");
    lv_obj_align(s_date, LV_ALIGN_TOP_MID, 0, FLIP_DATE_Y - 4);

    /* Testigo discreto de WiFi: un punto tenue en la esquina, nada mas */
    s_wifi_dot = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_wifi_dot);
    lv_obj_set_size(s_wifi_dot, 6, 6);
    lv_obj_set_style_radius(s_wifi_dot, 3, 0);
    lv_obj_set_style_bg_color(s_wifi_dot, lv_color_hex(0x7FC77F), 0);
    lv_obj_set_style_bg_opa(s_wifi_dot, LV_OPA_TRANSP, 0);
    lv_obj_align(s_wifi_dot, LV_ALIGN_BOTTOM_RIGHT, -10, -10);

    struct tm t;
    time_src_now(&t);
    flip_card_set_value(s_hours, t.tm_hour, false);   /* al arrancar, sin volteo */
    flip_card_set_value(s_mins, t.tm_min, false);
    update_date(&t);

    lv_screen_load(s_screen);
    s_tick = lv_timer_create(tick_cb, 500, NULL);

    /* Refresco completo periodico.
     *
     * El driver del fabricante no limpia la memoria grafica del panel al
     * inicializar, y en modo FULL LVGL solo refresca cuando algo se invalida:
     * el reloj apenas invalida nada entre minuto y minuto, asi que una zona que
     * no cambia puede conservar basura indefinidamente. Repintar la pantalla
     * entera cada 10 s cuesta muy poco y garantiza que nada se quede pegado. */
    lv_timer_create(full_refresh_cb, 10000, NULL);
}

void clock_ui_show(void)
{
    lv_screen_load(s_screen);
}

void clock_ui_test_flip(void)
{
    flip_card_set_value(s_mins, (flip_card_get_value(s_mins) + 1) % 60, true);
}
