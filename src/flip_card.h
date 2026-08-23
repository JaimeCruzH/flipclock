#pragma once
#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Una tarjeta abatible de dos digitos (00..99). Las dos cifras voltean juntas,
 * como en un flip clock real: al pasar de 09 a 10 se mueve la tarjeta entera.
 *
 * card_idx selecciona el par de columnas de sprites (0 = horas, 1 = minutos).
 * Importa porque los sprites llevan la iluminacion horneada segun su posicion
 * en la escena: usar los de la columna equivocada rompe la continuidad del brillo.
 */
typedef struct flip_card_t flip_card_t;

flip_card_t *flip_card_create(lv_obj_t *parent, int card_idx, int x, int y);

/** Cambia el valor. Con anim=false salta al valor sin volteo (arranque). */
void flip_card_set_value(flip_card_t *card, int value, bool anim);

int  flip_card_get_value(const flip_card_t *card);
bool flip_card_is_animating(const flip_card_t *card);

#ifdef __cplusplus
}
#endif
