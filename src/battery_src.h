#ifndef BATTERY_SRC_H
#define BATTERY_SRC_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BATTERY_TREND_UNKNOWN = 0,
    BATTERY_TREND_UP,
    BATTERY_TREND_DOWN,
    BATTERY_TREND_STABLE,
} battery_trend_t;

typedef struct {
    bool valid;
    uint32_t millivolts;
    uint8_t percent;
    battery_trend_t trend;
    bool autonomy_valid;
    uint32_t autonomy_minutes;
    uint32_t discharge_percent_per_hour;
} battery_data_t;

void battery_src_init(void);
bool battery_src_read(battery_data_t *out);

#ifdef __cplusplus
}
#endif

#endif
