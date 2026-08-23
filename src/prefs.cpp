#include "prefs.h"

#include <Preferences.h>

#define NVS_NS  "flipclk"       /* el mismo que usa time_src para las credenciales */

/* Vitacura, por defecto */
#define DEF_LAT    -33.3898f
#define DEF_LON    -70.5741f
#define DEF_PLACE  "VITACURA"

static Preferences nvs;
static int  s_brightness = 100;
static bool s_flipped = false;
static float s_lat = DEF_LAT;
static float s_lon = DEF_LON;
static char  s_place[24] = DEF_PLACE;

void prefs_init(void)
{
    nvs.begin(NVS_NS, true);
    s_brightness = nvs.getInt("bright", 100);
    s_flipped = nvs.getBool("flip180", false);
    s_lat = nvs.getFloat("lat", DEF_LAT);
    s_lon = nvs.getFloat("lon", DEF_LON);
    String p = nvs.getString("place", DEF_PLACE);
    nvs.end();
    snprintf(s_place, sizeof(s_place), "%s", p.c_str());

    if (s_brightness < 0)   s_brightness = 0;
    if (s_brightness > 100) s_brightness = 100;
}

int prefs_get_brightness(void)
{
    return s_brightness;
}

void prefs_set_brightness(int percent)
{
    if (percent < 0)   percent = 0;
    if (percent > 100) percent = 100;
    if (percent == s_brightness) return;      /* no gastar escrituras de flash en vano */
    s_brightness = percent;
    nvs.begin(NVS_NS, false);
    nvs.putInt("bright", percent);
    nvs.end();
}

bool prefs_get_flipped(void)
{
    return s_flipped;
}

void prefs_set_flipped(bool flipped)
{
    if (flipped == s_flipped) return;
    s_flipped = flipped;
    nvs.begin(NVS_NS, false);
    nvs.putBool("flip180", flipped);
    nvs.end();
}

float prefs_get_lat(void) { return s_lat; }
float prefs_get_lon(void) { return s_lon; }

const char *prefs_get_place(void) { return s_place; }

void prefs_set_location(float lat, float lon, const char *place)
{
    s_lat = lat;
    s_lon = lon;
    if (place && place[0]) snprintf(s_place, sizeof(s_place), "%s", place);
    nvs.begin(NVS_NS, false);
    nvs.putFloat("lat", lat);
    nvs.putFloat("lon", lon);
    nvs.putString("place", s_place);
    nvs.end();
}
