#pragma once
#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Crea la pantalla del tiempo (no la muestra). Con el mutex de LVGL cogido. */
void weather_ui_create(void);

/** La muestra y refresca su contenido. */
void weather_ui_show(void);

/** Diagnostico: pulsaciones y gestos recibidos por esta pantalla. */

#ifdef __cplusplus
}
#endif
