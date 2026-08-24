#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    POMODORO_WORK = 0,
    POMODORO_SHORT_BREAK,
    POMODORO_LONG_BREAK,
} pomodoro_phase_t;

typedef struct {
    pomodoro_phase_t phase;
    uint8_t  pomodoro;
    uint8_t  pomodoros_per_cycle;
    uint32_t remaining_seconds;
    bool     running;
    bool     completed;
} pomodoro_state_t;

/** Inicializa la sesion desde la configuracion y, si procede, desde NVS. */
void pomodoro_init(void);

/** Recarga la configuracion tras guardarla desde los ajustes. */
void pomodoro_reload_config(void);

/** Actualiza el reloj monotono. Devuelve true cuando acaba una fase. */
bool pomodoro_tick(void);

void pomodoro_get_state(pomodoro_state_t *out);
void pomodoro_start_pause(void);
void pomodoro_reset(void);
void pomodoro_advance(void);

const char *pomodoro_phase_name(pomodoro_phase_t phase);

#ifdef __cplusplus
}
#endif
