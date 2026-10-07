#pragma once

#include "esp_http_server.h"

// POST /api/v1/ota: write a firmware image to the inactive OTA slot and reboot.
// Requires "Authorization: Bearer <token>" with the [ota] token from wifi_secrets.ini.
esp_err_t registerOtaUpload(httpd_handle_t server);
