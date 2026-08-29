#include "prefs.h"

#include <Preferences.h>

#define NVS_NS  "flipclk"       /* el mismo que usa time_src para las credenciales */

/* Vitacura, por defecto */
#define DEF_LAT    -33.3898f
#define DEF_LON    -70.5741f
#define DEF_PLACE  "VITACURA"

#define DEF_POMO_WORK       25
#define DEF_POMO_SHORT       5
#define DEF_POMO_LONG       15
#define DEF_POMO_CYCLES      4
#define DEF_POMO_RESUME  false
#define POMO_SESSION_BLOB_VERSION 1

typedef struct {
    uint8_t version;
    prefs_pomodoro_session_t session;
} prefs_pomodoro_blob_t;

static Preferences nvs;
static int  s_brightness = 100;
static int  s_night_brightness = PREFS_NIGHT_BRIGHTNESS_DEFAULT;
static bool s_flipped = false;
static float s_lat = DEF_LAT;
static float s_lon = DEF_LON;
static char  s_place[24] = DEF_PLACE;
static prefs_pomodoro_t s_pomodoro = {
    DEF_POMO_WORK,
    DEF_POMO_SHORT,
    DEF_POMO_LONG,
    DEF_POMO_CYCLES,
    DEF_POMO_RESUME,
};

static uint16_t clamp_minutes(uint16_t value)
{
    if (value < 1)  return 1;
    if (value > 99) return 99;
    return value;
}

static uint8_t clamp_cycles(uint8_t value)
{
    if (value < 1) return 1;
    if (value > 9) return 9;
    return value;
}

void prefs_init(void)
{
    nvs.begin(NVS_NS, true);
    s_brightness = nvs.getInt("bright", 100);
    s_night_brightness = nvs.getInt("night_bright", PREFS_NIGHT_BRIGHTNESS_DEFAULT);
    s_flipped = nvs.getBool("flip180", false);
    s_lat = nvs.getFloat("lat", DEF_LAT);
    s_lon = nvs.getFloat("lon", DEF_LON);
    String p = nvs.getString("place", DEF_PLACE);
    s_pomodoro.work_minutes = clamp_minutes(nvs.getUShort("pomo_work", DEF_POMO_WORK));
    s_pomodoro.short_break_minutes = clamp_minutes(nvs.getUShort("pomo_short", DEF_POMO_SHORT));
    s_pomodoro.long_break_minutes = clamp_minutes(nvs.getUShort("pomo_long", DEF_POMO_LONG));
    s_pomodoro.pomodoros_per_cycle = clamp_cycles(nvs.getUChar("pomo_cycles", DEF_POMO_CYCLES));
    s_pomodoro.resume_session = nvs.getBool("pomo_resume", DEF_POMO_RESUME);
    nvs.end();
    snprintf(s_place, sizeof(s_place), "%s", p.c_str());

    if (s_brightness < 0)   s_brightness = 0;
    if (s_brightness > 100) s_brightness = 100;
    if (s_night_brightness < PREFS_NIGHT_BRIGHTNESS_MIN) {
        s_night_brightness = PREFS_NIGHT_BRIGHTNESS_MIN;
    }
    if (s_night_brightness > PREFS_NIGHT_BRIGHTNESS_MAX) {
        s_night_brightness = PREFS_NIGHT_BRIGHTNESS_MAX;
    }
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

int prefs_get_night_brightness(void)
{
    return s_night_brightness;
}

void prefs_set_night_brightness(int percent)
{
    if (percent < PREFS_NIGHT_BRIGHTNESS_MIN) percent = PREFS_NIGHT_BRIGHTNESS_MIN;
    if (percent > PREFS_NIGHT_BRIGHTNESS_MAX) percent = PREFS_NIGHT_BRIGHTNESS_MAX;
    if (percent == s_night_brightness) return;

    s_night_brightness = percent;
    nvs.begin(NVS_NS, false);
    nvs.putInt("night_bright", percent);
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

void prefs_get_pomodoro(prefs_pomodoro_t *out)
{
    if (out) *out = s_pomodoro;
}

void prefs_set_pomodoro(const prefs_pomodoro_t *config)
{
    if (!config) return;

    prefs_pomodoro_t next = *config;
    next.work_minutes = clamp_minutes(next.work_minutes);
    next.short_break_minutes = clamp_minutes(next.short_break_minutes);
    next.long_break_minutes = clamp_minutes(next.long_break_minutes);
    next.pomodoros_per_cycle = clamp_cycles(next.pomodoros_per_cycle);

    if (next.work_minutes == s_pomodoro.work_minutes &&
        next.short_break_minutes == s_pomodoro.short_break_minutes &&
        next.long_break_minutes == s_pomodoro.long_break_minutes &&
        next.pomodoros_per_cycle == s_pomodoro.pomodoros_per_cycle &&
        next.resume_session == s_pomodoro.resume_session) return;
    s_pomodoro = next;

    nvs.begin(NVS_NS, false);
    nvs.putUShort("pomo_work", s_pomodoro.work_minutes);
    nvs.putUShort("pomo_short", s_pomodoro.short_break_minutes);
    nvs.putUShort("pomo_long", s_pomodoro.long_break_minutes);
    nvs.putUChar("pomo_cycles", s_pomodoro.pomodoros_per_cycle);
    nvs.putBool("pomo_resume", s_pomodoro.resume_session);
    nvs.end();
}

bool prefs_get_pomodoro_session(prefs_pomodoro_session_t *out)
{
    if (!out) return false;

    nvs.begin(NVS_NS, true);
    prefs_pomodoro_blob_t blob;
    bool valid = nvs.getBytesLength("pomo_session") == sizeof(blob) &&
                 nvs.getBytes("pomo_session", &blob, sizeof(blob)) == sizeof(blob) &&
                 blob.version == POMO_SESSION_BLOB_VERSION;
    if (valid) {
        *out = blob.session;
    } else if (nvs.getBool("pomo_s_valid", false)) {
        /* Compatibilidad con sesiones guardadas por el formato anterior. */
        out->phase = nvs.getUChar("pomo_s_phase", 0);
        out->pomodoro = nvs.getUChar("pomo_s_block", 1);
        out->remaining_seconds = nvs.getUInt("pomo_s_rem", 0);
        out->completed = nvs.getBool("pomo_s_done", false);
        valid = true;
    }
    nvs.end();
    return valid;
}

void prefs_set_pomodoro_session(const prefs_pomodoro_session_t *session)
{
    if (!session) return;
    prefs_pomodoro_blob_t blob = {
        POMO_SESSION_BLOB_VERSION,
        *session,
    };
    nvs.begin(NVS_NS, false);
    nvs.putBytes("pomo_session", &blob, sizeof(blob));
    nvs.end();
}

void prefs_clear_pomodoro_session(void)
{
    nvs.begin(NVS_NS, false);
    nvs.remove("pomo_session");
    nvs.putBool("pomo_s_valid", false);
    nvs.end();
}
