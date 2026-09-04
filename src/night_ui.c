#include "night_ui.h"

#include "clock_ui.h"
#include "esp_bsp.h"
#include "prefs.h"
#include "time_src.h"

#include "esp_log.h"

#include <lvgl.h>
#include <stdio.h>

#if defined(NIGHT_TTF_USE) && NIGHT_TTF_USE
#include "assets/night_font.h"
#endif

#if defined(NIGHT_TTF_BENCHMARK)
#include "lv_port.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include <inttypes.h>
#endif

#define NIGHT_EXIT_HOLD_MS       2000
#define NIGHT_TICK_MS            500
#if defined(NIGHT_TTF_BENCHMARK)
#define NIGHT_BENCH_DURATION_MS  10000
#endif
#if defined(NIGHT_TTF_USE) && NIGHT_TTF_USE
#define NIGHT_TTF_MAX_FONT_SIZE  512
#define NIGHT_TTF_CACHE_COUNT    16
#define NIGHT_SCREEN_WIDTH       480
#define NIGHT_SCREEN_HEIGHT      320
#endif
static lv_obj_t  *s_screen;
static lv_obj_t  *s_time;
static lv_timer_t *s_exit_timer;
static int        s_last_minute = -1;
static int        s_restore_brightness;

#if defined(NIGHT_TTF_USE) && NIGHT_TTF_USE
static lv_font_t *s_ttf_font;
static int32_t   s_ttf_font_size;
#endif

#if defined(NIGHT_TTF_BENCHMARK)
static bool        s_benchmark_active;
static lv_timer_t *s_benchmark_timer;
static int64_t     s_benchmark_start_us;
static uint32_t    s_benchmark_heap_before;
static uint32_t    s_benchmark_heap_after_show;
static int64_t     s_benchmark_show_us;
#endif

static void cancel_exit_timer(void)
{
    if (s_exit_timer) {
        lv_timer_delete(s_exit_timer);
        s_exit_timer = NULL;
    }
}

static void update_time(void)
{
    struct tm t;
    char buf[6];

    time_src_now(&t);
    s_last_minute = t.tm_hour * 60 + t.tm_min;
    snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
    lv_label_set_text(s_time, buf);
}

#if defined(NIGHT_TTF_USE) && NIGHT_TTF_USE
static bool night_ttf_fits(const lv_font_t *font)
{
    static const char worst_case[] = "88:88";
    uint32_t width = 0;

    for (size_t i = 0; i < sizeof(worst_case) - 1; i++) {
        width += lv_font_get_glyph_width(font,
                                         (uint8_t)worst_case[i],
                                         (uint8_t)worst_case[i + 1]);
    }

    return width <= NIGHT_SCREEN_WIDTH &&
           lv_font_get_line_height(font) <= NIGHT_SCREEN_HEIGHT;
}

static int32_t night_ttf_find_max_size(lv_font_t *font)
{
    int32_t low = 1;
    int32_t high = NIGHT_TTF_MAX_FONT_SIZE;
    int32_t best = 1;

    while (low <= high) {
        const int32_t candidate = low + (high - low) / 2;
        lv_tiny_ttf_set_size(font, candidate);
        if (night_ttf_fits(font)) {
            best = candidate;
            low = candidate + 1;
        } else {
            high = candidate - 1;
        }
    }

    lv_tiny_ttf_set_size(font, best);
    return best;
}
#endif

static void tick_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);

    if (lv_screen_active() != s_screen) return;
#if defined(NIGHT_TTF_BENCHMARK)
    if (s_benchmark_active) return;
#endif

    struct tm t;
    time_src_now(&t);
    int minute = t.tm_hour * 60 + t.tm_min;
    if (minute == s_last_minute) return;

    update_time();
}

static void exit_to_clock_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    s_exit_timer = NULL;

    if (lv_screen_active() != s_screen) return;

    lv_indev_t *indev = bsp_display_get_input_dev();
    if (indev) lv_indev_wait_release(indev);

    bsp_display_brightness_set(s_restore_brightness);
    clock_ui_show();
}

static void touch_cb(lv_event_t *e)
{
    if (lv_screen_active() != s_screen) return;

    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        cancel_exit_timer();
        s_exit_timer = lv_timer_create(exit_to_clock_cb, NIGHT_EXIT_HOLD_MS, NULL);
        lv_timer_set_repeat_count(s_exit_timer, 1);
    } else if (code == LV_EVENT_RELEASED) {
        cancel_exit_timer();
    }
}

static void create_screen(void)
{
    s_screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_screen);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_screen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);

    s_time = lv_label_create(s_screen);
#if defined(NIGHT_TTF_USE) && NIGHT_TTF_USE
    s_ttf_font = lv_tiny_ttf_create_data_ex(
        night_font_ttf,
        (size_t)(night_font_ttf_end - night_font_ttf),
        NIGHT_TTF_MAX_FONT_SIZE,
        LV_FONT_KERNING_NONE,
        NIGHT_TTF_CACHE_COUNT);
    if (s_ttf_font) {
        s_ttf_font_size = night_ttf_find_max_size(s_ttf_font);
        lv_obj_set_width(s_time, NIGHT_SCREEN_WIDTH);
        lv_obj_set_height(s_time, lv_font_get_line_height(s_ttf_font));
        lv_obj_set_style_text_font(s_time, s_ttf_font, 0);
    } else {
        ESP_LOGE("NIGHT", "Tiny TTF no pudo inicializarse; usando respaldo bitmap sin escalado");
        lv_obj_set_size(s_time, 160, 52);
        lv_obj_set_style_text_font(s_time, &lv_font_montserrat_48, 0);
    }
#else
    ESP_LOGE("NIGHT", "Tiny TTF esta deshabilitado; usando respaldo bitmap sin escalado");
    lv_obj_set_size(s_time, 160, 52);
    lv_obj_set_style_text_font(s_time, &lv_font_montserrat_48, 0);
#endif
    lv_obj_set_style_text_color(s_time, lv_color_white(), 0);
    lv_obj_set_style_text_align(s_time, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_time, "00:00");
    lv_obj_align(s_time, LV_ALIGN_CENTER, 0, 0);

    lv_timer_create(tick_cb, NIGHT_TICK_MS, NULL);

    lv_indev_t *indev = bsp_display_get_input_dev();
    if (indev) {
        lv_indev_add_event_cb(indev, touch_cb, LV_EVENT_PRESSED, NULL);
        lv_indev_add_event_cb(indev, touch_cb, LV_EVENT_RELEASED, NULL);
    }
}

void night_ui_show(void)
{
    bool entering = s_screen == NULL || lv_screen_active() != s_screen;
    if (!s_screen) create_screen();

    if (entering) s_restore_brightness = prefs_get_brightness();
    s_last_minute = -1;
    update_time();
    lv_screen_load(s_screen);
    bsp_display_brightness_set(prefs_get_night_brightness());
}

#if defined(NIGHT_TTF_BENCHMARK)
static void benchmark_finish_cb(lv_timer_t *timer)
{
    if (lv_screen_active() != s_screen) {
        s_benchmark_active = false;
        s_benchmark_timer = NULL;
        lv_timer_delete(timer);
        return;
    }

    s_benchmark_active = false;
    s_benchmark_timer = NULL;
    lv_timer_delete(timer);
    update_time();

    const int64_t elapsed_us = esp_timer_get_time() - s_benchmark_start_us;
    ESP_LOGI("NIGHT_BENCH",
             "font_mode=%s font_size=%d ttf_bytes=%" PRIu32
             " show_us=%" PRId64 " heap_show_delta=%" PRIi32,
#if defined(NIGHT_TTF_USE) && NIGHT_TTF_USE
             s_ttf_font ? "tiny_ttf" : "fallback",
             s_ttf_font_size,
             (uint32_t)(night_font_ttf_end - night_font_ttf),
#else
             "bitmap",
             48,
             0,
#endif
             s_benchmark_show_us,
             (int32_t)s_benchmark_heap_before - (int32_t)s_benchmark_heap_after_show);
    ESP_LOGI("NIGHT_BENCH",
             "window_us=%" PRId64 " mode=idle_single_render",
             elapsed_us);
    lvgl_port_benchmark_print();
}

void night_ui_run_benchmark(void)
{
    if (s_benchmark_timer) {
        lv_timer_delete(s_benchmark_timer);
        s_benchmark_timer = NULL;
    }

    s_benchmark_active = true;
    s_benchmark_heap_before = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    const int64_t show_start_us = esp_timer_get_time();

    night_ui_show();

    s_benchmark_show_us = esp_timer_get_time() - show_start_us;
    s_benchmark_heap_after_show = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    s_benchmark_start_us = esp_timer_get_time();
    lvgl_port_benchmark_reset();
    s_benchmark_timer = lv_timer_create(benchmark_finish_cb, NIGHT_BENCH_DURATION_MS, NULL);
    if (!s_benchmark_timer) {
        s_benchmark_active = false;
        ESP_LOGE("NIGHT_BENCH", "no se pudo crear el temporizador de prueba");
        return;
    }

    ESP_LOGI("NIGHT_BENCH", "prueba iniciada: 10 s, un render inicial y reposo");
}

#endif
