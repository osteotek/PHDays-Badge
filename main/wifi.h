#pragma once

#include <stdbool.h>
#include <stdint.h>

// Router DHCP name and mDNS name (phdays-badge.local).
#define BADGE_HOSTNAME "phdays-badge"

void initWiFi(void);
bool wifi_home_configured(void);
bool wifi_home_connected(void);
bool wifi_hotspot_active(void);
