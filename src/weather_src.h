#pragma once
#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Datos del tiempo. Toda la red vive aqui dentro, en su propia tarea, para que
 * cambiar de fuente mañana sea barato y para que la UI nunca se bloquee.
 *
 * Fuente hibrida, y no por capricho: los modelos globales fallan 4-5 grados en
 * el valle de Santiago (suavizan el efecto urbano), mientras que la observacion
 * real del aerodromo de Tobalaba acerto a 0,2 grados del termometro de casa.
 * Asi que:
 *   - condiciones AHORA  -> METAR observado (aviationweather.gov, sin clave)
 *   - pronostico         -> Open-Meteo (sin clave), que es lo que el METAR no da
 * Si el METAR falla o no hay estacion cerca, se usa Open-Meteo tambien para el
 * dato actual y se marca con `from_metar = false`.
 */

typedef enum {
    WX_CLEAR = 0,
    WX_PARTLY,
    WX_CLOUDY,
    WX_FOG,
    WX_DRIZZLE,
    WX_RAIN,
    WX_SHOWERS,
    WX_SNOW,
    WX_THUNDER,
    WX_COND_COUNT
} wx_cond_t;

typedef struct {
    int       hour;      /* hora local, 0..23 */
    float     temp;
    wx_cond_t cond;
    bool      is_day;
    int       pop;       /* probabilidad de precipitacion, % */
} wx_hour_t;

typedef struct {
    bool      valid;         /* false hasta la primera lectura buena */
    bool      from_metar;    /* false = el dato actual es del modelo, no observado */
    char      station[8];    /* codigo OACI de la estacion usada */
    char      place[24];     /* nombre a mostrar: la ESTACION de la que sale el
                                dato, o la comuna configurada si es del modelo */
    int       station_km;    /* a que distancia esta de la ubicacion */
    time_t    obs_time;      /* momento de la OBSERVACION, no de la consulta */
    time_t    fetch_time;    /* cuando se trajo (para saber si esta rancio) */

    float     temp;
    float     feels;
    wx_cond_t cond;
    bool      is_day;

    float     tmax;
    float     tmin;

    wx_hour_t hours[6];
    int       n_hours;
} wx_data_t;

void weather_src_init(void);

/** Copia seguro el ultimo dato bueno. Devuelve false si aun no hay ninguno. */
bool weather_src_get(wx_data_t *out);

/** Pide una actualizacion inmediata (no bloquea). */
void weather_src_request_refresh(void);

/** Vuelca por Serial lo ultimo leido, para comparar con el movil. */
void weather_src_dump(void);

/** Nombre en español de la condicion, para la pantalla. */
const char *wx_cond_name(wx_cond_t c);

#ifdef __cplusplus
}
#endif
