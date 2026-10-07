#pragma once

#include "esp_http_server.h"

// GET /api/v1/status, GET/POST /api/v1/settings, POST /api/v1/timer,
// POST /api/v1/screen and POST /api/v1/notify.
esp_err_t registerInfoApi(httpd_handle_t server);
