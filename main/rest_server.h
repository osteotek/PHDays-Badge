#pragma once

#include "cJSON.h"
#include "esp_err.h"
#include "esp_http_server.h"
#include <stddef.h>

esp_err_t start_file_server(const char *base_path);
esp_err_t mount_storage(const char *base_path);

// Reads the request body as a NUL-terminated string (must fit in size);
// sends the error response itself on failure.
esp_err_t http_recv_body(httpd_req_t *req, char *buf, size_t size);
// Sends json as the response and frees it.
esp_err_t http_send_json(httpd_req_t *req, cJSON *json);
