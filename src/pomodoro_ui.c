#include "pomodoro_ui.h"

#include "clock_ui.h"
#include "flip_card.h"
#include "pomodoro_src.h"
#include "settings_ui.h"
#include "weather_ui.h"
#include "assets/flip_assets.h"
#include "swipe.h"

#include <stdio.h>
#include <stdint.h>

static lv_obj_t *s_screen;
static lv_obj_t *s_phase;
static lv_obj_t *s_count;
static lv_obj_t *s_state;
static lv_obj_t *s_start_label;
static lv_obj_t *s_advance_label;
static flip_card_t *s_minutes;
static flip_card_t *s_seconds;
static lv_timer_t *s_timer;

static uint32_t s_last_remaining = UINT32_MAX;
static pomodoro_phase_t s_last_phase = (pomodoro_phase_t)-1;
static uint8_t s_last_pomodoro = 0;
static bool s_last_running = false;
static bool s_last_completed = false;

static lv_obj_t *make_button(lv_obj_t *parent, const char *txt, int x,
                             lv_event_cb_t cb)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_pos(button, x, 272);
    lv_obj_set_size(button, 144, 38);
    lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, txt);
    lv_obj_center(label);
    return label;
}

static void update_labels(const pomodoro_state_t *state)
{
    char buf[48];
    snprintf(buf, sizeof(buf), "%s", pomodoro_phase_name(state->phase));
    lv_label_set_text(s_phase, buf);

    snprintf(buf, sizeof(buf), "POMODORO %u/%u",
             (unsigned)state->pomodoro,
             (unsigned)state->pomodoros_per_cycle);
    lv_label_set_text(s_count, buf);

    if (state->completed) {
        lv_label_set_text(s_state, "TERMINADO - Siguiente");
        lv_label_set_text(s_advance_label, "Siguiente");
    } else if (state->running) {
        lv_label_set_text(s_state, "EN CURSO");
        lv_label_set_text(s_advance_label, "Saltar");
    } else {
        lv_label_set_text(s_state, "PAUSADO");
        lv_label_set_text(s_advance_label, "Saltar");
    }
    lv_label_set_text(s_start_label, state->running ? "Pausar" : "Iniciar");
}

static void refresh(const pomodoro_state_t *state, bool animate)
{
    bool changed = state->remaining_seconds != s_last_remaining ||
                   state->phase != s_last_phase ||
                   state->pomodoro != s_last_pomodoro ||
                   state->running != s_last_running ||
                   state->completed != s_last_completed;
    if (!changed) return;

    int minutes = (int)(state->remaining_seconds / 60UL);
    int seconds = (int)(state->remaining_seconds % 60UL);
    flip_card_set_value(s_minutes, minutes, animate);
    flip_card_set_value(s_seconds, seconds, animate);
    update_labels(state);

    s_last_remaining = state->remaining_seconds;
    s_last_phase = state->phase;
    s_last_pomodoro = state->pomodoro;
    s_last_running = state->running;
    s_last_completed = state->completed;
}

static void sync_screen(bool animate)
{
    pomodoro_state_t state;
    pomodoro_get_state(&state);
    refresh(&state, animate);
}

static void start_pause_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    pomodoro_start_pause();
    sync_screen(true);
}

static void reset_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    pomodoro_reset();
    sync_screen(false);
}

static void advance_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    pomodoro_advance();
    sync_screen(false);
}

static void long_press_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    settings_ui_open(SETTINGS_FROM_POMODORO);
}

static void on_swipe(lv_dir_t dir)
{
    if (dir == LV_DIR_RIGHT) {
        weather_ui_show();
    } else if (dir == LV_DIR_LEFT) {
        clock_ui_show();
    }
}

static void timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    bool finished = pomodoro_tick();
    if (finished) {
        /* El usuario debe ver la fase terminada y pulsar Siguiente. */
        pomodoro_ui_show();
        return;
    }

    sync_screen(lv_screen_active() == s_screen);
}

static void full_refresh_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    lv_obj_invalidate(s_screen);
}

void pomodoro_ui_create(void)
{
    s_screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_screen);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    swipe_attach(s_screen, on_swipe);

    lv_obj_t *bg = lv_image_create(s_screen);
    lv_image_set_src(bg, &img_case_bg);
    lv_obj_set_pos(bg, 0, 0);
    lv_obj_add_flag(bg, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(bg, long_press_cb, LV_EVENT_LONG_PRESSED, NULL);

    s_minutes = flip_card_create(s_screen, 0, FLIP_WIN_X, FLIP_WIN_Y);
    s_seconds = flip_card_create(s_screen, 1,
                                 FLIP_WIN_X + FLIP_CARD_W + FLIP_GAP,
                                 FLIP_WIN_Y);

    s_phase = lv_label_create(s_screen);
    lv_obj_set_style_text_color(s_phase, lv_color_hex(0xD8D8DC), 0);
    lv_obj_set_style_text_font(s_phase, &lv_font_montserrat_18, 0);
    lv_obj_set_pos(s_phase, 20, 16);

    s_count = lv_label_create(s_screen);
    lv_obj_set_style_text_color(s_count, lv_color_hex(0x9A9A9E), 0);
    lv_obj_set_style_text_font(s_count, &lv_font_montserrat_16, 0);
    lv_obj_set_width(s_count, 140);
    lv_obj_set_style_text_align(s_count, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(s_count, LV_ALIGN_TOP_RIGHT, -20, 18);

    s_state = lv_label_create(s_screen);
    lv_obj_set_style_text_color(s_state, lv_color_hex(0x9A9A9E), 0);
    lv_obj_set_style_text_font(s_state, &lv_font_montserrat_14, 0);
    lv_obj_set_width(s_state, 480);
    lv_obj_set_style_text_align(s_state, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_state, 0, 248);

    s_start_label = make_button(s_screen, "Iniciar", 12, start_pause_cb);
    make_button(s_screen, "Reiniciar", 168, reset_cb);
    s_advance_label = make_button(s_screen, "Saltar", 324, advance_cb);

    sync_screen(false);
    s_timer = lv_timer_create(timer_cb, 200, NULL);
    lv_timer_create(full_refresh_cb, 10000, NULL);
}

void pomodoro_ui_show(void)
{
    if (!s_screen) return;
    sync_screen(false);
    lv_screen_load(s_screen);
}
