#include "power_manager.h"

#include <Arduino.h>
#include <WiFi.h>

#include "esp_bsp.h"
#include "esp_sleep.h"
#include "pomodoro_src.h"

void power_manager_enter_deep_sleep(void)
{
    static bool already_sleeping = false;
    if (already_sleeping) return;
    already_sleeping = true;

    pomodoro_prepare_sleep();
    bsp_display_prepare_sleep();

    Serial.println("flipclock: deep sleep; pulse RESET to wake");
    Serial.flush();

    /* Apagar la radio antes de que el ESP32 corte los perifericos digitales. */
    WiFi.disconnect(true, false, 100);
    WiFi.mode(WIFI_OFF);
    delay(50);

    /* Sin wakeup por GPIO: la salida queda reservada al reset fisico EN. */
    esp_deep_sleep_start();
}
