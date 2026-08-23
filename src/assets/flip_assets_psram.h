#pragma once
#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Copia el blob de sprites a PSRAM y reapunta todos los descriptores.
 *
 * Los sprites se embeben con .incbin, o sea que viven en la flash, que en esta
 * placa es QUAD. La PSRAM es OCTAL: el doble de ancho de bus. El dibujado de
 * LVGL esta limitado por la lectura de esos pixeles, no por el calculo.
 *
 * Hay que llamarla ANTES de crear ninguna pantalla, para que LVGL no llegue a
 * cachear una imagen con el puntero viejo. Si no hay PSRAM libre no hace nada y
 * todo sigue funcionando desde flash.
 *
 * Este archivo NO lo genera tools/gen_assets.py. Lo que si depende del
 * generador es que los descriptores no sean const, para poder rebasarlos.
 */
void flip_assets_use_psram(void);

#ifdef __cplusplus
}
#endif
