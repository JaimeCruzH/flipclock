#include "weather_src.h"
#include "prefs.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <cJSON.h>
#include <math.h>
#include <esp_heap_caps.h>

#define REFRESH_MS      (15UL * 60UL * 1000UL)
#define RETRY_MS        (60UL * 1000UL)
#define MAX_STATION_KM  40        /* mas lejos, la observacion ya no representa */
/*
 * El METAR se emite cada hora, asi que una observacion al dia no pasa de ~65
 * min. Si tiene mas de 75, esa estacion se ha saltado un ciclo y hay que
 * descartarla: con el limite en 90 min, Tobalaba colaba con 88 minutos y ganaba
 * por cercania, dejando la temperatura de las 18:00 congelada al anochecer.
 */
#define MAX_OBS_AGE_MIN 75
#define HTTP_TIMEOUT_MS 12000

/* Estaciones chilenas con METAR activo, verificadas contra la API el 22/08/2026.
 * Cubren de Arica a Porvenir; para cualquier comuna del Gran Santiago la buena
 * es SCTB (Tobalaba), que esta dentro de la ciudad. */
typedef struct { const char *id; float lat; float lon; const char *name; } station_t;

static const station_t STATIONS[] = {
    {"SCAR", -18.3510f,  -70.3360f, "ARICA"},
    {"SCDA", -20.5350f,  -70.1810f, "IQUIQUE"},
    {"SCFA", -23.4500f,  -70.4410f, "ANTOFAGASTA"},
    {"SCAT", -27.2620f,  -70.7740f, "COPIAPO"},
    {"SCIP", -27.1610f, -109.4270f, "ISLA DE PASCUA"},
    {"SCSE", -29.9180f,  -71.2010f, "LA SERENA"},
    {"SCVM", -32.9470f,  -71.4790f, "VINA DEL MAR"},
    {"SCTB", -33.4560f,  -70.5470f, "TOBALABA"},
    {"SCEL", -33.3930f,  -70.7860f, "PUDAHUEL"},
    {"SCRG", -34.1740f,  -70.7760f, "RANCAGUA"},
    {"SCCH", -36.5830f,  -72.0310f, "CHILLAN"},
    {"SCIE", -36.7730f,  -73.0630f, "CONCEPCION"},
    {"SCGE", -37.4030f,  -72.4220f, "LOS ANGELES"},
    {"SCQP", -38.9250f,  -72.6480f, "TEMUCO"},
    {"SCVD", -39.6500f,  -73.0860f, "VALDIVIA"},
    {"SCJO", -40.6050f,  -73.0610f, "OSORNO"},
    {"SCTE", -41.4390f,  -73.0940f, "PUERTO MONTT"},
    {"SCBA", -45.9130f,  -71.6940f, "BALMACEDA"},
    {"SCNT", -51.6720f,  -72.5280f, "PUERTO NATALES"},
    {"SCCI", -53.0030f,  -70.8550f, "PUNTA ARENAS"},
    {"SCFM", -53.2540f,  -70.3190f, "PORVENIR"},
};
#define N_STATIONS (sizeof(STATIONS) / sizeof(STATIONS[0]))

static wx_data_t        s_data;
static SemaphoreHandle_t s_mutex;
static volatile bool     s_refresh_now = false;

/* ------------------------------------------------------------------ utiles */

static float haversine_km(float lat1, float lon1, float lat2, float lon2)
{
    const float R = 6371.0f;
    float dlat = radians(lat2 - lat1);
    float dlon = radians(lon2 - lon1);
    float a = sinf(dlat / 2) * sinf(dlat / 2) +
              cosf(radians(lat1)) * cosf(radians(lat2)) * sinf(dlon / 2) * sinf(dlon / 2);
    return R * 2 * atan2f(sqrtf(a), sqrtf(1 - a));
}

/*
 * Devuelve en `out` hasta `max` estaciones dentro del radio, de mas cercana a
 * mas lejana. No basta con la mas cercana: los aerodromos pequeños solo
 * reportan mientras estan operativos. Tobalaba, que es el que mejor representa
 * a Santiago, deja de emitir al anochecer, y quedarse con el se traducia en
 * mostrar la temperatura de las 18:00 durante toda la noche. Pudahuel, algo mas
 * lejos, reporta las 24 horas y sirve de relevo.
 */
static const station_t *station_by_id(const char *id)
{
    if (!id) return NULL;
    for (unsigned i = 0; i < N_STATIONS; i++) {
        if (strcmp(STATIONS[i].id, id) == 0) return &STATIONS[i];
    }
    return NULL;
}

static int nearest_stations(float lat, float lon, const station_t **out, int max)
{
    typedef struct { const station_t *st; float km; } cand_t;
    cand_t c[N_STATIONS];
    int n = 0;

    for (unsigned i = 0; i < N_STATIONS; i++) {
        float km = haversine_km(lat, lon, STATIONS[i].lat, STATIONS[i].lon);
        if (km <= MAX_STATION_KM) {
            c[n].st = &STATIONS[i];
            c[n].km = km;
            n++;
        }
    }
    /* insercion: son pocas y ya estan casi ordenadas */
    for (int i = 1; i < n; i++) {
        cand_t k = c[i];
        int j = i - 1;
        while (j >= 0 && c[j].km > k.km) { c[j + 1] = c[j]; j--; }
        c[j + 1] = k;
    }
    if (n > max) n = max;
    for (int i = 0; i < n; i++) out[i] = c[i].st;
    return n;
}

/*
 * Sensacion termica, con la convencion que usan las apps del movil (y por
 * tanto la que el usuario va a comparar):
 *
 *   - con calor (>= 27 C) manda la humedad  -> indice de calor
 *   - con frio (<= 10 C) y viento manda el viento -> sensacion por viento
 *   - en el medio, la sensacion ES la temperatura
 *
 * Se probo primero la temperatura aparente australiana y daba 21,5 C para una
 * tarde de 25 C: correcto segun esa formula (lleva un -4 constante), pero 3,5
 * grados por debajo de lo que muestra el movil. Aqui interesa coincidir con lo
 * que el usuario ve, no con una formula concreta.
 */
static float apparent_temp(float t_c, float dew_c, float wind_ms)
{
    float rh = 100.0f * expf(17.625f * dew_c / (243.04f + dew_c)) /
                        expf(17.625f * t_c  / (243.04f + t_c));
    if (rh > 100.0f) rh = 100.0f;
    if (rh < 0.0f)   rh = 0.0f;

    float wind_kmh = wind_ms * 3.6f;

    if (t_c >= 27.0f) {
        /* indice de calor de Rothfusz, que se define en Fahrenheit */
        float tf = t_c * 9.0f / 5.0f + 32.0f;
        float hi = -42.379f + 2.04901523f * tf + 10.14333127f * rh
                   - 0.22475541f * tf * rh - 0.00683783f * tf * tf
                   - 0.05481717f * rh * rh + 0.00122874f * tf * tf * rh
                   + 0.00085282f * tf * rh * rh - 0.00000199f * tf * tf * rh * rh;
        return (hi - 32.0f) * 5.0f / 9.0f;
    }

    if (t_c <= 10.0f && wind_kmh > 4.8f) {
        float v = powf(wind_kmh, 0.16f);
        return 13.12f + 0.6215f * t_c - 11.37f * v + 0.3965f * t_c * v;
    }

    return t_c;
}

static wx_cond_t wmo_to_cond(int wmo)
{
    if (wmo == 0)                      return WX_CLEAR;
    if (wmo == 1 || wmo == 2)          return WX_PARTLY;
    if (wmo == 3)                      return WX_CLOUDY;
    if (wmo == 45 || wmo == 48)        return WX_FOG;
    if (wmo >= 51 && wmo <= 57)        return WX_DRIZZLE;
    if (wmo >= 61 && wmo <= 67)        return WX_RAIN;
    if (wmo >= 71 && wmo <= 77)        return WX_SNOW;
    if (wmo >= 80 && wmo <= 82)        return WX_SHOWERS;
    if (wmo == 85 || wmo == 86)        return WX_SNOW;
    if (wmo >= 95)                     return WX_THUNDER;
    return WX_PARTLY;
}

const char *wx_cond_name(wx_cond_t c)
{
    switch (c) {
    case WX_CLEAR:   return "Despejado";
    case WX_PARTLY:  return "Parcialmente nublado";
    case WX_CLOUDY:  return "Nublado";
    case WX_FOG:     return "Niebla";
    case WX_DRIZZLE: return "Llovizna";
    case WX_RAIN:    return "Lluvia";
    case WX_SHOWERS: return "Chubascos";
    case WX_SNOW:    return "Nieve";
    case WX_THUNDER: return "Tormenta";
    default:         return "";
    }
}

/*
 * Condicion a partir del METAR crudo. Los fenomenos (lluvia, tormenta...) van
 * en los grupos de tiempo presente, y la nubosidad en CAVOK/FEW/SCT/BKN/OVC.
 * La busqueda arranca DESPUES del grupo de hora (el que acaba en 'Z') para no
 * confundir el codigo de la estacion con un fenomeno.
 */
static wx_cond_t metar_to_cond(const char *raw)
{
    if (!raw) return WX_PARTLY;

    const char *p = strchr(raw, 'Z');
    if (p == NULL) p = raw; else p++;

    bool ts = strstr(p, "TS") != NULL;
    bool sn = strstr(p, "SN") != NULL;
    bool sh = strstr(p, "SH") != NULL;
    bool ra = strstr(p, "RA") != NULL;
    bool dz = strstr(p, "DZ") != NULL;
    bool fg = (strstr(p, " FG") != NULL) || (strstr(p, "BCFG") != NULL) ||
              (strstr(p, " BR") != NULL);

    if (ts) return WX_THUNDER;
    if (sn) return WX_SNOW;
    if (sh) return WX_SHOWERS;
    if (ra) return WX_RAIN;
    if (dz) return WX_DRIZZLE;
    if (fg) return WX_FOG;

    if (strstr(p, "OVC") || strstr(p, "BKN")) return WX_CLOUDY;
    if (strstr(p, "SCT"))                     return WX_PARTLY;
    if (strstr(p, "CAVOK") || strstr(p, "CLR") || strstr(p, "SKC") ||
        strstr(p, "NSC")   || strstr(p, "FEW")) return WX_CLEAR;

    return WX_PARTLY;
}

/* --------------------------------------------------------------------- red */

static bool http_get(const char *url, String &out)
{
    WiFiClientSecure cli;
    cli.setInsecure();            /* patron ya verificado en test_suite */
    cli.setTimeout(HTTP_TIMEOUT_MS);

    HTTPClient http;
    http.setTimeout(HTTP_TIMEOUT_MS);
    http.setConnectTimeout(HTTP_TIMEOUT_MS);
    http.setUserAgent("flipclock-esp32/1.0");
    if (!http.begin(cli, url)) return false;

    int code = http.GET();
    bool ok = (code == 200);
    if (ok) out = http.getString();
    else Serial.printf("[wx] HTTP %d en %.60s\n", code, url);
    http.end();
    return ok;
}

/*
 * Pide varias estaciones de una vez y se queda con la MEJOR: la mas cercana de
 * entre las que tengan una observacion reciente. Una observacion vieja no vale
 * aunque venga de la estacion de al lado — es preferible el modelo.
 */
static bool fetch_metar(float lat, float lon, const station_t **sts, int n_sts,
                        wx_data_t *d)
{
    char ids[64] = {0};
    for (int i = 0; i < n_sts; i++) {
        strncat(ids, sts[i]->id, sizeof(ids) - strlen(ids) - 2);
        if (i + 1 < n_sts) strncat(ids, ",", sizeof(ids) - strlen(ids) - 1);
    }

    char url[200];
    snprintf(url, sizeof(url),
             "https://aviationweather.gov/api/data/metar?ids=%s&format=json", ids);

    String body;
    if (!http_get(url, body)) return false;

    cJSON *root = cJSON_Parse(body.c_str());
    if (!root || !cJSON_IsArray(root)) { cJSON_Delete(root); return false; }

    time_t now = time(NULL);
    bool   ok = false;
    float  best_km = 1e9f;

    int n = cJSON_GetArraySize(root);
    for (int i = 0; i < n; i++) {
        cJSON *item = cJSON_GetArrayItem(root, i);
        cJSON *temp = cJSON_GetObjectItem(item, "temp");
        cJSON *obs  = cJSON_GetObjectItem(item, "obsTime");
        cJSON *slat = cJSON_GetObjectItem(item, "lat");
        cJSON *slon = cJSON_GetObjectItem(item, "lon");
        cJSON *icao = cJSON_GetObjectItem(item, "icaoId");
        if (!cJSON_IsNumber(temp) || !cJSON_IsNumber(obs)) continue;

        time_t t_obs = (time_t)obs->valuedouble;
        long age_min = (long)((now - t_obs) / 60);
        Serial.printf("[wx]   %s: %.1fC, %ld min de antiguedad\n",
                      cJSON_IsString(icao) ? icao->valuestring : "?",
                      temp->valuedouble, age_min);
        if (age_min < 0) age_min = 0;
        if (age_min > MAX_OBS_AGE_MIN) continue;      /* rancia: no sirve */

        float km = (cJSON_IsNumber(slat) && cJSON_IsNumber(slon))
                   ? haversine_km(lat, lon, (float)slat->valuedouble,
                                  (float)slon->valuedouble)
                   : 999.0f;
        if (km >= best_km) continue;                  /* ya teniamos una mejor */

        cJSON *dewp = cJSON_GetObjectItem(item, "dewp");
        cJSON *wspd = cJSON_GetObjectItem(item, "wspd");
        cJSON *raw  = cJSON_GetObjectItem(item, "rawOb");

        d->temp = (float)temp->valuedouble;
        float dew  = cJSON_IsNumber(dewp) ? (float)dewp->valuedouble : d->temp - 5.0f;
        float wind = cJSON_IsNumber(wspd) ? (float)wspd->valuedouble * 0.5144f : 0.0f;
        d->feels = apparent_temp(d->temp, dew, wind);
        d->cond  = metar_to_cond(cJSON_IsString(raw) ? raw->valuestring : NULL);
        d->obs_time = t_obs;
        d->station_km = (int)(km + 0.5f);
        const char *id = cJSON_IsString(icao) ? icao->valuestring : "?";
        snprintf(d->station, sizeof(d->station), "%s", id);
        /* El rotulo dice de donde sale el dato de verdad, no de donde nos
         * gustaria que saliese: si de noche el relevo pasa a Pudahuel, en
         * pantalla se lee PUDAHUEL. */
        const station_t *known = station_by_id(id);
        snprintf(d->place, sizeof(d->place), "%s",
                 known ? known->name : id);
        d->from_metar = true;
        best_km = km;
        ok = true;
    }

    cJSON_Delete(root);
    return ok;
}

static bool fetch_forecast(float lat, float lon, wx_data_t *d)
{
    char url[420];
    snprintf(url, sizeof(url),
             "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
             "&current=temperature_2m,apparent_temperature,weather_code,is_day"
             "&daily=temperature_2m_max,temperature_2m_min"
             "&hourly=temperature_2m,weather_code,precipitation_probability,is_day"
             "&forecast_days=2&forecast_hours=10&timezone=America%%2FSantiago",
             lat, lon);

    String body;
    if (!http_get(url, body)) return false;

    cJSON *root = cJSON_Parse(body.c_str());
    if (!root) return false;

    bool ok = false;

    /* maxima y minima de HOY */
    cJSON *daily = cJSON_GetObjectItem(root, "daily");
    if (daily) {
        cJSON *mx = cJSON_GetObjectItem(daily, "temperature_2m_max");
        cJSON *mn = cJSON_GetObjectItem(daily, "temperature_2m_min");
        if (cJSON_IsArray(mx) && cJSON_IsArray(mn)) {
            cJSON *a = cJSON_GetArrayItem(mx, 0), *b = cJSON_GetArrayItem(mn, 0);
            if (cJSON_IsNumber(a)) d->tmax = (float)a->valuedouble;
            if (cJSON_IsNumber(b)) d->tmin = (float)b->valuedouble;
        }
    }

    /* respaldo del dato actual, por si no hubo METAR */
    cJSON *cur = cJSON_GetObjectItem(root, "current");
    if (cur) {
        cJSON *isd = cJSON_GetObjectItem(cur, "is_day");
        if (cJSON_IsNumber(isd)) d->is_day = (isd->valuedouble != 0);
        if (!d->from_metar) {
            cJSON *t  = cJSON_GetObjectItem(cur, "temperature_2m");
            cJSON *ap = cJSON_GetObjectItem(cur, "apparent_temperature");
            cJSON *wc = cJSON_GetObjectItem(cur, "weather_code");
            if (cJSON_IsNumber(t))  d->temp  = (float)t->valuedouble;
            if (cJSON_IsNumber(ap)) d->feels = (float)ap->valuedouble;
            if (cJSON_IsNumber(wc)) d->cond  = wmo_to_cond((int)wc->valuedouble);
            d->obs_time = time(NULL);
            snprintf(d->station, sizeof(d->station), "%s", "modelo");
        }
        ok = true;
    }

    /* las proximas 6 horas: se saltan las que ya han pasado */
    cJSON *hourly = cJSON_GetObjectItem(root, "hourly");
    if (hourly) {
        cJSON *times = cJSON_GetObjectItem(hourly, "time");
        cJSON *temps = cJSON_GetObjectItem(hourly, "temperature_2m");
        cJSON *codes = cJSON_GetObjectItem(hourly, "weather_code");
        cJSON *pops  = cJSON_GetObjectItem(hourly, "precipitation_probability");
        cJSON *days  = cJSON_GetObjectItem(hourly, "is_day");

        /* Las marcas vienen en ISO 8601 y en hora local, y ese formato ordena
         * igual como texto que como fecha: basta quedarse con las cadenas
         * mayores que la hora actual para tener las horas futuras, sin liarse
         * con cambios de dia ni de mes. */
        time_t now = time(NULL);
        struct tm lt;
        localtime_r(&now, &lt);
        char now_iso[20];
        strftime(now_iso, sizeof(now_iso), "%Y-%m-%dT%H:%M", &lt);

        d->n_hours = 0;
        int n = cJSON_IsArray(times) ? cJSON_GetArraySize(times) : 0;
        for (int i = 0; i < n && d->n_hours < 6; i++) {
            cJSON *ts = cJSON_GetArrayItem(times, i);
            if (!cJSON_IsString(ts)) continue;

            const char *s = ts->valuestring;          /* "2026-08-22T19:00" */
            if (strlen(s) < 16) continue;
            if (strcmp(s, now_iso) <= 0) continue;    /* ya pasada */

            int hh = (s[11] - '0') * 10 + (s[12] - '0');

            wx_hour_t *h = &d->hours[d->n_hours];
            h->hour = hh;
            cJSON *e;
            e = cJSON_GetArrayItem(temps, i); h->temp = cJSON_IsNumber(e) ? (float)e->valuedouble : 0;
            e = cJSON_GetArrayItem(codes, i); h->cond = wmo_to_cond(cJSON_IsNumber(e) ? (int)e->valuedouble : 1);
            e = cJSON_GetArrayItem(pops,  i); h->pop  = cJSON_IsNumber(e) ? (int)e->valuedouble : 0;
            e = cJSON_GetArrayItem(days,  i); h->is_day = cJSON_IsNumber(e) ? (e->valuedouble != 0) : true;
            d->n_hours++;
        }
    }

    cJSON_Delete(root);
    return ok;
}

static void refresh(void)
{
    if (WiFi.status() != WL_CONNECTED) return;

    float lat = prefs_get_lat();
    float lon = prefs_get_lon();

    wx_data_t d;
    memset(&d, 0, sizeof(d));
    d.from_metar = false;

    const station_t *sts[3];
    int n_sts = nearest_stations(lat, lon, sts, 3);
    if (n_sts > 0) {
        fetch_metar(lat, lon, sts, n_sts, &d);
    }
    bool fc = fetch_forecast(lat, lon, &d);

    if (d.from_metar || fc) {
        /* La maxima y la minima salen del modelo, que hoy se quedaba corto: daba
         * 24,7 de maxima cuando la estacion ya habia observado 25,0. Un extremo
         * ya alcanzado es un hecho, asi que la observacion los ensancha. */
        if (d.from_metar) {
            if (d.temp > d.tmax) d.tmax = d.temp;
            if (d.temp < d.tmin) d.tmin = d.temp;
        }

        /* Sin observacion, el dato es del modelo para la comuna configurada:
         * ahi lo honesto es mostrar el nombre de la comuna. */
        if (!d.from_metar) {
            snprintf(d.place, sizeof(d.place), "%s", prefs_get_place());
        }

        d.valid = true;
        d.fetch_time = time(NULL);
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        s_data = d;
        xSemaphoreGive(s_mutex);
        Serial.printf("[wx] %s %.1fC (%s, %d km)\n",
                      d.from_metar ? "observado" : "modelo",
                      d.temp, d.station, d.station_km);
    }
}

static void wx_task(void *arg)
{
    (void)arg;
    uint32_t next = 0;
    for (;;) {
        uint32_t now = millis();
        if (s_refresh_now || (int32_t)(now - next) >= 0) {
            s_refresh_now = false;
            refresh();
            wx_data_t tmp;
            bool ok = weather_src_get(&tmp);
            next = millis() + (ok ? REFRESH_MS : RETRY_MS);
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/* ------------------------------------------------------------------- API */

void weather_src_init(void)
{
    memset(&s_data, 0, sizeof(s_data));
    s_mutex = xSemaphoreCreateMutex();
    xTaskCreatePinnedToCore(wx_task, "flip_wx", 8192, NULL, 1, NULL, 0);
}

bool weather_src_get(wx_data_t *out)
{
    if (!out || !s_mutex) return false;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    *out = s_data;
    xSemaphoreGive(s_mutex);
    return out->valid;
}

void weather_src_request_refresh(void)
{
    s_refresh_now = true;
}

void weather_src_dump(void)
{
    wx_data_t d;
    if (!weather_src_get(&d)) {
        Serial.println("[wx] aun no hay datos");
        return;
    }
    struct tm obs;
    localtime_r(&d.obs_time, &obs);

    Serial.printf("[wx] %s  %.1f C  sensacion %.1f C  %s\n",
                  d.from_metar ? "OBSERVADO" : "MODELO (sin METAR)",
                  d.temp, d.feels, wx_cond_name(d.cond));
    Serial.printf("     estacion %s a %d km, observado %02d:%02d\n",
                  d.station, d.station_km, obs.tm_hour, obs.tm_min);
    Serial.printf("     maxima %.1f  minima %.1f  %s\n",
                  d.tmax, d.tmin, d.is_day ? "de dia" : "de noche");
    for (int i = 0; i < d.n_hours; i++) {
        Serial.printf("     %02dh  %5.1f C  %3d%%  %s\n",
                      d.hours[i].hour, d.hours[i].temp, d.hours[i].pop,
                      wx_cond_name(d.hours[i].cond));
    }
    /* Interno, no el total: los 8 MB de PSRAM esconderian cualquier apuro, y es
     * el heap interno el que consume mbedtls durante el handshake TLS. */
    Serial.printf("     heap interno libre %u B (minimo historico %u B)\n",
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
}
