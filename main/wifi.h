#pragma once

#include <stdbool.h>
#include <stdint.h>

// Router DHCP name and mDNS name (pixeldesk.local).
#define BADGE_HOSTNAME "pixeldesk"

void initWiFi(void);
bool wifi_home_configured(void);
bool wifi_home_connected(void);
bool wifi_hotspot_active(void);
