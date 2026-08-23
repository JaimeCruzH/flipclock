#pragma once
#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Monta la escena del reloj. Llamar con el mutex de LVGL cogido. */
void clock_ui_create(void);

/** Vuelve a mostrar el reloj (lo usa la pantalla de ajustes al cerrarse). */
void clock_ui_show(void);

/** Fuerza un volteo de prueba sin esperar al minuto (comando por Serial). */
void clock_ui_test_flip(void);


#ifdef __cplusplus
}
#endif
