#pragma once
#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Fuente de hora: RTC interno como base, puesto en hora por NTP cuando hay WiFi.
 * Nada de esto bloquea la UI: la conexion y la sincronizacion viven en su
 * propia tarea, y el reloj funciona desde el primer segundo aunque no haya red.
 */
void time_src_init(void);

/** Hora local actual. Siempre devuelve algo valido. */
void time_src_now(struct tm *out);

/** true si el RTC ha sido puesto en hora alguna vez (por NTP o a mano). */
bool time_src_is_valid(void);

/** true si hay WiFi conectado ahora mismo. */
bool time_src_wifi_connected(void);

/** Pone el RTC a mano y lo marca como valido. */
void time_src_set_manual(int year, int mon, int mday, int hour, int min);

/** Guarda credenciales en NVS y relanza la conexion. */
void time_src_set_wifi(const char *ssid, const char *pass);

/** Copia el SSID guardado (cadena vacia si no hay). */
void time_src_get_ssid(char *buf, size_t len);

#ifdef __cplusplus
}
#endif
