#pragma once
#include <stdbool.h>
#include <stdint.h>

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

/** Configuracion del temporizador Pomodoro, persistida en NVS. */
typedef struct {
    uint16_t work_minutes;
    uint16_t short_break_minutes;
    uint16_t long_break_minutes;
    uint8_t  pomodoros_per_cycle;
    bool     resume_session;
} prefs_pomodoro_t;

typedef struct {
    uint8_t  phase;
    uint8_t  pomodoro;
    uint32_t remaining_seconds;
    bool     completed;
} prefs_pomodoro_session_t;

void prefs_get_pomodoro(prefs_pomodoro_t *out);
void prefs_set_pomodoro(const prefs_pomodoro_t *config);

/** Instantanea de sesion. Devuelve false si no hay ninguna guardada. */
bool prefs_get_pomodoro_session(prefs_pomodoro_session_t *out);
void prefs_set_pomodoro_session(const prefs_pomodoro_session_t *session);
void prefs_clear_pomodoro_session(void);

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
