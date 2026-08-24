#pragma once

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Crea la pantalla Pomodoro, pero no la muestra. */
void pomodoro_ui_create(void);

/** Muestra la pantalla Pomodoro y sincroniza el contador. */
void pomodoro_ui_show(void);

#ifdef __cplusplus
}
#endif
