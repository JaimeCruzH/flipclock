#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Preferencias de pantalla, guardadas en NVS (namespace "flipclk").
 * Se aplican al arrancar, antes de montar la UI.
 */
void prefs_init(void);

/** Brillo del backlight, 0..100 %. */
int  prefs_get_brightness(void);
void prefs_set_brightness(int percent);

/** true = imagen girada 180 grados (placa montada del otro lado). */
bool prefs_get_flipped(void);
void prefs_set_flipped(bool flipped);

/** Ubicacion para el tiempo. Por defecto, Vitacura. */
float       prefs_get_lat(void);
float       prefs_get_lon(void);
const char *prefs_get_place(void);
void        prefs_set_location(float lat, float lon, const char *place);

/**
 * Brillo minimo mientras la pantalla de ajustes esta abierta.
 * Con el backlight a 0 la pantalla se apaga del todo y no se veria el propio
 * deslizador: dentro de ajustes nunca se baja de aqui, y el valor real elegido
 * se aplica al salir.
 */
#define PREFS_BRIGHTNESS_UI_MIN 10

#ifdef __cplusplus
}
#endif
