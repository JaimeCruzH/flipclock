#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** Abre la pantalla de ajustes y selecciona la pestaña segun el origen. */
typedef enum {
    SETTINGS_FROM_CLOCK = 0,
    SETTINGS_FROM_POMODORO,
} settings_origin_t;

void settings_ui_open(settings_origin_t origin);

#ifdef __cplusplus
}
#endif
