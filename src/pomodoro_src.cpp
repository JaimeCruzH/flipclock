#include "pomodoro_src.h"
#include "prefs.h"

#include <Arduino.h>
#include <string.h>

#define POMODORO_CHECKPOINT_MS (15UL * 1000UL)

static prefs_pomodoro_t s_config;
static pomodoro_state_t s_state;
static uint32_t s_deadline_ms;
static uint32_t s_last_checkpoint_ms;

static uint32_t duration_seconds(pomodoro_phase_t phase)
{
    switch (phase) {
    case POMODORO_SHORT_BREAK:
        return (uint32_t)s_config.short_break_minutes * 60UL;
    case POMODORO_LONG_BREAK:
        return (uint32_t)s_config.long_break_minutes * 60UL;
    case POMODORO_WORK:
    default:
        return (uint32_t)s_config.work_minutes * 60UL;
    }
}

static bool valid_phase(uint8_t phase)
{
    return phase <= (uint8_t)POMODORO_LONG_BREAK;
}

static void save_session(void)
{
    prefs_pomodoro_session_t session = {
        (uint8_t)s_state.phase,
        s_state.pomodoro,
        s_state.remaining_seconds,
        s_state.completed,
    };
    prefs_set_pomodoro_session(&session);
}

static void set_ready_duration(void)
{
    s_state.remaining_seconds = duration_seconds(s_state.phase);
    s_state.running = false;
    s_state.completed = false;
}

static void update_remaining(uint32_t now)
{
    if (!s_state.running) return;

    int32_t delta = (int32_t)(s_deadline_ms - now);
    if (delta <= 0) {
        s_state.remaining_seconds = 0;
        s_state.running = false;
        s_state.completed = true;
        save_session();
        return;
    }

    s_state.remaining_seconds = ((uint32_t)delta + 999UL) / 1000UL;
}

void pomodoro_init(void)
{
    prefs_get_pomodoro(&s_config);

    memset(&s_state, 0, sizeof(s_state));
    s_state.phase = POMODORO_WORK;
    s_state.pomodoro = 1;
    s_state.pomodoros_per_cycle = s_config.pomodoros_per_cycle;
    set_ready_duration();

    prefs_pomodoro_session_t saved;
    if (s_config.resume_session && prefs_get_pomodoro_session(&saved) &&
        valid_phase(saved.phase) && saved.pomodoro >= 1 &&
        saved.pomodoro <= s_config.pomodoros_per_cycle) {
        s_state.phase = (pomodoro_phase_t)saved.phase;
        s_state.pomodoro = saved.pomodoro;
        s_state.remaining_seconds = saved.remaining_seconds;
        s_state.completed = saved.completed;
        s_state.running = false;

        uint32_t max_seconds = duration_seconds(s_state.phase);
        if (s_state.remaining_seconds > max_seconds) {
            s_state.remaining_seconds = max_seconds;
            s_state.completed = false;
        }
    }

    s_deadline_ms = 0;
    s_last_checkpoint_ms = millis();
}

void pomodoro_reload_config(void)
{
    prefs_pomodoro_t old = s_config;
    prefs_get_pomodoro(&s_config);
    s_state.pomodoros_per_cycle = s_config.pomodoros_per_cycle;
    if (s_state.pomodoro > s_config.pomodoros_per_cycle) {
        s_state.pomodoro = s_config.pomodoros_per_cycle;
    }

    /* Una sesion lista para arrancar adopta inmediatamente la nueva duracion.
     * Una sesion pausada a mitad de fase conserva lo ya trabajado y usara la
     * nueva configuracion al pasar a la siguiente fase. */
    uint32_t old_duration;
    switch (s_state.phase) {
    case POMODORO_SHORT_BREAK:
        old_duration = (uint32_t)old.short_break_minutes * 60UL;
        break;
    case POMODORO_LONG_BREAK:
        old_duration = (uint32_t)old.long_break_minutes * 60UL;
        break;
    case POMODORO_WORK:
    default:
        old_duration = (uint32_t)old.work_minutes * 60UL;
        break;
    }
    if (!s_state.running && !s_state.completed &&
        s_state.remaining_seconds == old_duration) {
        set_ready_duration();
    }
    save_session();
}

bool pomodoro_tick(void)
{
    if (!s_state.running) return false;

    uint32_t now = millis();
    bool was_running = s_state.running;
    update_remaining(now);

    if (was_running && !s_state.running) return true;

    if ((uint32_t)(now - s_last_checkpoint_ms) >= POMODORO_CHECKPOINT_MS) {
        s_last_checkpoint_ms = now;
        save_session();
    }
    return false;
}

void pomodoro_get_state(pomodoro_state_t *out)
{
    if (!out) return;
    *out = s_state;
    out->pomodoros_per_cycle = s_config.pomodoros_per_cycle;
}

void pomodoro_start_pause(void)
{
    uint32_t now = millis();

    if (s_state.running) {
        update_remaining(now);
        if (s_state.completed) return;
        s_state.running = false;
        save_session();
        return;
    }

    if (s_state.completed || s_state.remaining_seconds == 0) return;

    s_deadline_ms = now + s_state.remaining_seconds * 1000UL;
    s_last_checkpoint_ms = now;
    s_state.running = true;
    save_session();
}

void pomodoro_reset(void)
{
    prefs_get_pomodoro(&s_config);
    s_state.pomodoros_per_cycle = s_config.pomodoros_per_cycle;
    s_state.phase = POMODORO_WORK;
    s_state.pomodoro = 1;
    set_ready_duration();
    s_deadline_ms = 0;
    s_last_checkpoint_ms = millis();
    save_session();
}

void pomodoro_advance(void)
{
    if (s_state.phase == POMODORO_WORK) {
        if (s_state.pomodoro < s_config.pomodoros_per_cycle) {
            s_state.phase = POMODORO_SHORT_BREAK;
        } else {
            s_state.phase = POMODORO_LONG_BREAK;
        }
    } else if (s_state.phase == POMODORO_SHORT_BREAK) {
        s_state.phase = POMODORO_WORK;
        s_state.pomodoro++;
    } else {
        s_state.phase = POMODORO_WORK;
        s_state.pomodoro = 1;
    }

    s_state.pomodoros_per_cycle = s_config.pomodoros_per_cycle;
    set_ready_duration();
    s_deadline_ms = 0;
    s_last_checkpoint_ms = millis();
    save_session();
}

const char *pomodoro_phase_name(pomodoro_phase_t phase)
{
    switch (phase) {
    case POMODORO_SHORT_BREAK: return "DESCANSO CORTO";
    case POMODORO_LONG_BREAK:  return "DESCANSO LARGO";
    case POMODORO_WORK:
    default:                   return "TRABAJO";
    }
}
