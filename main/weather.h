#pragma once

// Current weather from Open-Meteo (no API key), for the [weather] location in
// wifi_secrets.ini. Refreshed every 15 minutes while home Wi-Fi is up.
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool configured; // a location was compiled in
    bool valid;      // data fetched within the last two hours
    float temperature;
    int code; // WMO weather interpretation code
    bool is_day;
    bool has_range; // today's forecast high and low
    float high;
    float low;
    uint32_t age_s;
} weather_data_t;

void weather_task(void *arg);
void weather_get(weather_data_t *out);
