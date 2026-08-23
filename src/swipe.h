#pragma once
#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Deteccion propia de deslizamiento horizontal.
 *
 * Se engancha al DISPOSITIVO DE ENTRADA, no a un widget. LVGL entrega
 * PRESSED y RELEASED primero al indev y despues al objeto pulsado
 * (send_event() en lv_indev.c), asi que asi el gesto se ve caiga donde caiga
 * el dedo. Enganchado a un widget no funcionaba: si el toque empezaba sobre
 * una tarjeta del reloj o sobre la franja del tiempo, el detector ni se
 * enteraba. LV_OBJ_FLAG_GESTURE_BUBBLE no ayuda, porque sube el GESTURE pero
 * no el PRESSED.
 *
 * Se registra una PANTALLA (la que devuelve lv_screen_active()) con su
 * callback; el gesto se entrega solo si esa pantalla es la activa.
 */
typedef void (*swipe_cb_t)(lv_dir_t dir);

void swipe_attach(lv_obj_t *screen, swipe_cb_t cb);

#ifdef __cplusplus
}
#endif
