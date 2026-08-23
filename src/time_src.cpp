#include "time_src.h"
#include "weather_src.h"

#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>

/*
 * America/Santiago (Chile continental), con cambio de horario automatico:
 *   estandar UTC-4, verano UTC-3.
 *   Empieza el primer sabado de septiembre a las 24:00 -> domingo 00:00.
 *   Termina el primer sabado de abril a las 24:00      -> domingo 00:00.
 * Los nombres de zona en Chile son numericos (<-04>/<-03>), no siglas.
 * OJO: la Region de Magallanes NO cambia de hora, va en UTC-3 todo el ano;
 * si el reloj fuera a parar alli, la cadena correcta seria "<-03>3".
 */
#define TZ_SANTIAGO   "<-04>4<-03>,M9.1.6/24,M4.1.6/24"
#define NVS_NS     "flipclk"        /* propio: no pisa el "net" de test_suite */
#define RESYNC_MS  (15UL * 60UL * 1000UL)

static Preferences nvs;
static volatile bool s_valid = false;
static char s_ssid[33] = {0};
static char s_pass[65] = {0};

static void load_creds(void)
{
    nvs.begin(NVS_NS, true);
    String ssid = nvs.getString("ssid", "");
    String pass = nvs.getString("pass", "");
    nvs.end();
    snprintf(s_ssid, sizeof(s_ssid), "%s", ssid.c_str());
    snprintf(s_pass, sizeof(s_pass), "%s", pass.c_str());
}

static void net_task(void *arg)
{
    (void)arg;
    uint32_t fail_ms = 0;        /* 0 = la ultima vez hubo red */
    for (;;) {
        if (s_ssid[0] == '\0') {
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        if (WiFi.status() != WL_CONNECTED) {
            WiFi.mode(WIFI_STA);
            WiFi.begin(s_ssid, s_pass);
            uint32_t t0 = millis();
            while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) {
                vTaskDelay(pdMS_TO_TICKS(250));
            }
        }

        if (WiFi.status() == WL_CONNECTED) {
            /* configTzTime aplica la zona horaria y lanza el cliente SNTP */
            configTzTime(TZ_SANTIAGO, "pool.ntp.org", "time.nist.gov");
            uint32_t t0 = millis();
            while (millis() - t0 < 15000) {
                time_t now = time(nullptr);
                if (now > 1700000000) {          /* ya no es la epoca de arranque */
                    s_valid = true;
                    Serial.println("[time] NTP sincronizado");
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(250));
            }
            weather_src_request_refresh();   /* ya hay red: que el tiempo se traiga */
            fail_ms = 0;
        } else {
            Serial.println("[time] sin WiFi, sigo con el RTC interno");
            /* Reintento progresivo. Antes se esperaban los 15 minutos completos
             * tambien cuando la conexion habia fallado, asi que un rechazo del
             * router al arrancar dejaba la placa sin red un cuarto de hora. */
            if (fail_ms == 0)  fail_ms = 15000;
            else               fail_ms *= 2;
            if (fail_ms > 300000) fail_ms = 300000;   /* tope de 5 min */
        }

        vTaskDelay(pdMS_TO_TICKS(fail_ms ? fail_ms : RESYNC_MS));
    }
}

void time_src_init(void)
{
    setenv("TZ", TZ_SANTIAGO, 1);
    tzset();
    load_creds();

    /* Si el RTC ya trae una hora creible (deep sleep / reset por software) la
     * damos por buena en vez de esperar a la red. */
    if (time(nullptr) > 1700000000) s_valid = true;

    xTaskCreatePinnedToCore(net_task, "flip_net", 4096, nullptr, 1, nullptr, 0);
}

void time_src_now(struct tm *out)
{
    time_t now = time(nullptr);
    localtime_r(&now, out);
}

bool time_src_is_valid(void)      { return s_valid; }
bool time_src_wifi_connected(void) { return WiFi.status() == WL_CONNECTED; }

void time_src_set_manual(int year, int mon, int mday, int hour, int min)
{
    struct tm t = {0};
    t.tm_year = year - 1900;
    t.tm_mon  = mon - 1;
    t.tm_mday = mday;
    t.tm_hour = hour;
    t.tm_min  = min;
    t.tm_sec  = 0;
    t.tm_isdst = -1;                 /* que libc decida si toca horario de verano */

    time_t epoch = mktime(&t);
    struct timeval tv = { .tv_sec = epoch, .tv_usec = 0 };
    settimeofday(&tv, nullptr);
    s_valid = true;
}

void time_src_set_wifi(const char *ssid, const char *pass)
{
    nvs.begin(NVS_NS, false);
    nvs.putString("ssid", ssid);
    nvs.putString("pass", pass);
    nvs.end();
    snprintf(s_ssid, sizeof(s_ssid), "%s", ssid);
    snprintf(s_pass, sizeof(s_pass), "%s", pass);
    WiFi.disconnect();               /* net_task reintentara con las nuevas */
}

void time_src_get_ssid(char *buf, size_t len)
{
    snprintf(buf, len, "%s", s_ssid);
}
