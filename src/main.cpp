/*
 * Reloj flip fotorrealista - Elecrow 3.5" ESP32-S3 Display (ST77922, 320x480).
 *
 * La pantalla es 320x480 en vertical; el BSP la rota 90 grados por software,
 * asi que la UI trabaja en 480x320 apaisado, que es como estan generados los
 * sprites de src/assets/.
 */

#include <Arduino.h>
#include <lvgl.h>

#include "display.h"
#include "esp_bsp.h"
#include "lv_port.h"

#include "assets/flip_assets_psram.h"
#include "clock_ui.h"
#include "time_src.h"
#include "prefs.h"
#include "weather_src.h"
#include "weather_ui.h"

#define LVGL_PORT_ROTATION_DEGREE (90)

void setup()
{
    Serial.begin(115200);
    Serial.println("flipclock: arrancando");

    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = EXAMPLE_LCD_QSPI_H_RES * EXAMPLE_LCD_QSPI_V_RES,
        .rotate = LV_DISPLAY_ROTATION_90,
    };
    bsp_display_start_with_config(&cfg);

    prefs_init();
    bsp_display_brightness_set(prefs_get_brightness());

    time_src_init();
    weather_src_init();

    /* Las APIs de LVGL no son thread-safe: la UI se monta con el mutex cogido */
    bsp_display_lock(0);
    /* ANTES de crear pantallas, para que LVGL no cachee ninguna imagen con el
     * puntero viejo: los sprites pasan de la flash (quad) a la PSRAM (octal,
     * doble ancho de bus). El dibujado de LVGL esta limitado por la lectura de
     * esos pixeles. */
    flip_assets_use_psram();

    clock_ui_create();
    weather_ui_create();
    if (prefs_get_flipped()) bsp_display_set_flipped(true);
    bsp_display_unlock();

    Serial.println("flipclock: listo. Comandos: f = volteo, t = hora, w = tiempo, W = refrescar tiempo");
}

void loop()
{
    /* Comandos por Serial para no tener que esperar al minuto mientras se
     * ajusta la animacion. */
    if (Serial.available()) {
        int c = Serial.read();
        if (c == 'f') {
            bsp_display_lock(0);
            clock_ui_test_flip();
            bsp_display_unlock();
        } else if (c == 'w') {
            weather_src_dump();
        } else if (c == 'W') {
            Serial.println("[wx] pidiendo actualizacion...");
            weather_src_request_refresh();
        } else if (c == 't') {
            struct tm t;
            time_src_now(&t);
            Serial.printf("[time] %04d-%02d-%02d %02d:%02d:%02d  valida=%d wifi=%d\n",
                          t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                          t.tm_hour, t.tm_min, t.tm_sec,
                          time_src_is_valid(), time_src_wifi_connected());
        }
    }
    delay(20);
}
