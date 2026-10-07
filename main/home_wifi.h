#pragma once

#include "esp_http_server.h"
#include "esp_wifi.h"
#include <stdbool.h>

bool loadHomeWiFi(wifi_config_t *config);
esp_err_t registerHomeWiFiSettings(httpd_handle_t server);
