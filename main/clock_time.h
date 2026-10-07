#pragma once

// Wall-clock time from NTP, in the time zone from settings.
#include "cJSON.h"
#include "screens.h"
#include <stdbool.h>

void clock_time_start(void); // after esp_netif_init(); syncs once Wi-Fi has internet
bool clock_time_get(screen_time_t *out);
cJSON *clock_time_ntp_json(void); // per-server reachability, for diagnostics
