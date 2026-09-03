#include "battery_src.h"

#include <string.h>

#include "esp32-hal-adc.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define BATTERY_GPIO 8
#define BATTERY_SAMPLE_INTERVAL_MS (10UL * 60UL * 1000UL)
#define BATTERY_HISTORY_SIZE 6
#define BATTERY_STABLE_BAND_MV 12
#define BATTERY_MIN_DROP_MV 10

typedef struct {
    uint64_t timestamp_us;
    uint32_t millivolts;
} battery_sample_t;

static SemaphoreHandle_t s_mutex;
static battery_data_t s_data;
static battery_sample_t s_last_sample;
static battery_sample_t s_discharge_history[BATTERY_HISTORY_SIZE];
static uint8_t s_discharge_count;
static bool s_have_last_sample;

static uint8_t battery_percent(uint32_t millivolts)
{
    if (millivolts <= 2500) return 0;
    if (millivolts >= 4200) return 100;
    return (uint8_t)((millivolts - 2500) / 17);
}

static void reset_discharge_history_locked(void)
{
    s_discharge_count = 0;
    s_data.autonomy_valid = false;
    s_data.autonomy_minutes = 0;
    s_data.discharge_percent_per_hour = 0;
}

static void add_discharge_sample_locked(battery_sample_t sample)
{
    if (s_discharge_count == BATTERY_HISTORY_SIZE) {
        memmove(&s_discharge_history[0], &s_discharge_history[1],
                (BATTERY_HISTORY_SIZE - 1) * sizeof(s_discharge_history[0]));
        s_discharge_count--;
    }
    s_discharge_history[s_discharge_count++] = sample;
}

static void calculate_autonomy_locked(void)
{
    s_data.autonomy_valid = false;
    s_data.autonomy_minutes = 0;
    s_data.discharge_percent_per_hour = 0;

    if (s_discharge_count < 2) return;

    const battery_sample_t *first = &s_discharge_history[0];
    const battery_sample_t *last = &s_discharge_history[s_discharge_count - 1];
    if (last->timestamp_us <= first->timestamp_us || first->millivolts <= last->millivolts) return;

    const uint32_t drop_millivolts = first->millivolts - last->millivolts;
    if (drop_millivolts < BATTERY_MIN_DROP_MV) return;

    const uint64_t elapsed_us = last->timestamp_us - first->timestamp_us;
    const uint64_t drop_per_hour =
        ((uint64_t)drop_millivolts * 3600000000ULL) / elapsed_us;
    if (drop_per_hour == 0) return;

    uint32_t current_millivolts = last->millivolts;
    if (current_millivolts < 2500) current_millivolts = 2500;
    if (current_millivolts > 4200) current_millivolts = 4200;

    const uint64_t remaining_millivolts = current_millivolts - 2500;
    s_data.autonomy_minutes = (uint32_t)
        ((remaining_millivolts * 60ULL + drop_per_hour / 2) / drop_per_hour);
    s_data.discharge_percent_per_hour = (uint32_t)((drop_per_hour + 8) / 17);
    s_data.autonomy_valid = true;
}

static void record_sample_locked(battery_sample_t sample)
{
    if (!s_have_last_sample) {
        s_last_sample = sample;
        s_have_last_sample = true;
        return;
    }

    const int32_t delta_millivolts =
        (int32_t)sample.millivolts - (int32_t)s_last_sample.millivolts;

    if (delta_millivolts > BATTERY_STABLE_BAND_MV) {
        s_data.trend = BATTERY_TREND_UP;
        reset_discharge_history_locked();
    } else if (delta_millivolts < -BATTERY_STABLE_BAND_MV) {
        s_data.trend = BATTERY_TREND_DOWN;
        if (s_discharge_count == 0) add_discharge_sample_locked(s_last_sample);
        add_discharge_sample_locked(sample);
        calculate_autonomy_locked();
    } else {
        s_data.trend = BATTERY_TREND_STABLE;
        reset_discharge_history_locked();
    }

    s_last_sample = sample;
}

static bool read_voltage_locked(uint32_t *millivolts)
{
    const uint32_t adc_millivolts = analogReadMilliVolts(BATTERY_GPIO);
    if (adc_millivolts == 0) return false;

    *millivolts = adc_millivolts * 2;
    return true;
}

static void battery_sample_and_record(void)
{
    if (!s_mutex) return;

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    uint32_t millivolts;
    if (!read_voltage_locked(&millivolts)) {
        s_data.valid = false;
        xSemaphoreGive(s_mutex);
        return;
    }

    s_data.valid = true;
    s_data.millivolts = millivolts;
    s_data.percent = battery_percent(millivolts);
    battery_sample_t sample = {
        .timestamp_us = (uint64_t)esp_timer_get_time(),
        .millivolts = millivolts,
    };
    record_sample_locked(sample);

    xSemaphoreGive(s_mutex);
}

static void battery_task(void *arg)
{
    (void)arg;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(BATTERY_SAMPLE_INTERVAL_MS));
        battery_sample_and_record();
    }
}

void battery_src_init(void)
{
    if (s_mutex) return;

    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return;

    analogSetPinAttenuation(BATTERY_GPIO, ADC_11db);
    battery_sample_and_record();
    xTaskCreatePinnedToCore(battery_task, "flip_bat", 4096, 0, 1, 0, 0);
}

bool battery_src_read(battery_data_t *out)
{
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!s_mutex) return false;

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    uint32_t millivolts;
    if (!read_voltage_locked(&millivolts)) {
        s_data.valid = false;
        *out = s_data;
        xSemaphoreGive(s_mutex);
        return false;
    }

    s_data.valid = true;
    s_data.millivolts = millivolts;
    s_data.percent = battery_percent(millivolts);
    *out = s_data;

    xSemaphoreGive(s_mutex);
    return true;
}
