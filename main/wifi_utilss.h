#pragma once

#include <stdbool.h>
#include <stdint.h>

extern uint8_t wifiClientConnectCounter;

extern uint8_t ESP_WIFI_SSID_bytes[4];
extern uint32_t ESP_WIFI_SSID_INT;
extern uint64_t ESP_WIFI_PASS_LONG;

void initWiFi();
bool wifi_home_configured(void);
bool wifi_home_connected(void);
bool wifi_hotspot_active(void);
