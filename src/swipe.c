#include "swipe.h"

#include <stdio.h>
#include <stdlib.h>

/* Recorrido horizontal minimo para que cuente como deslizamiento. Por debajo
 * de esto es un toque con temblor de dedo, no una intencion de cambiar de
 * pantalla. */
#define SWIPE_MIN_PX      55

/* Reloj y tiempo. De sobra, pero evita una lista enlazada para dos entradas. */
#define SWIPE_MAX_SCREENS 4

static struct {
    lv_obj_t   *screen;
    swipe_cb_t  cb;
} s_reg[SWIPE_MAX_SCREENS];

static uint8_t     s_n = 0;
static lv_point_t  s_start;
static bool        s_tracking = false;
static bool        s_hooked = false;

static void swipe_indev_cb(lv_event_t *e)
{
    lv_indev_t *indev = (lv_indev_t *)lv_event_get_target(e);
    lv_event_code_t code = lv_event_get_code(e);
    if (!indev) return;

    if (code == LV_EVENT_PRESSED) {
        lv_indev_get_point(indev, &s_start);
        s_tracking = true;
        return;
    }

    if (code != LV_EVENT_RELEASED || !s_tracking) return;
    s_tracking = false;

    lv_point_t end;
    lv_indev_get_point(indev, &end);
    int32_t dx = end.x - s_start.x;
    int32_t dy = end.y - s_start.y;

    if (LV_ABS(dx) < SWIPE_MIN_PX || LV_ABS(dx) <= LV_ABS(dy)) return;


    lv_obj_t *activa = lv_screen_active();
    for (uint8_t i = 0; i < s_n; i++) {
        if (s_reg[i].screen == activa) {
            s_reg[i].cb(dx > 0 ? LV_DIR_RIGHT : LV_DIR_LEFT);
            return;
        }
    }
}

void swipe_attach(lv_obj_t *screen, swipe_cb_t cb)
{
    if (!screen || !cb || s_n >= SWIPE_MAX_SCREENS) return;

    s_reg[s_n].screen = screen;
    s_reg[s_n].cb = cb;
    s_n++;

    /* Los ganchos del indev se ponen una sola vez, no una por pantalla. */
    if (s_hooked) return;

    lv_indev_t *indev = NULL;
    while ((indev = lv_indev_get_next(indev)) != NULL) {
        if (lv_indev_get_type(indev) != LV_INDEV_TYPE_POINTER) continue;
        lv_indev_add_event_cb(indev, swipe_indev_cb, LV_EVENT_PRESSED, NULL);
        lv_indev_add_event_cb(indev, swipe_indev_cb, LV_EVENT_RELEASED, NULL);
        s_hooked = true;
    }
}
