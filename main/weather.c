#include "weather.h"

#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "home_wifi_secrets.h"
#include "wifi.h"
#include <stdlib.h>

#define REFRESH_MS (15 * 60 * 1000)
// Connections right after Wi-Fi comes up sometimes time out; retry soon, then back off.
#define RETRY_FIRST_MS (15 * 1000)
#define RETRY_MAX_MS (5 * 60 * 1000)
#define MAX_AGE_US (2 * 60 * 60 * 1000000LL)
#define BODY_MAX 2048

static const char *TAG = "weather";
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static bool have_data;
static float temperature;
static int code;
static bool is_day;
static bool has_range;
static float high, low;
static int64_t updated_us;

static bool configured(void) { return sizeof(HOME_WEATHER_LATITUDE) > 1 && sizeof(HOME_WEATHER_LONGITUDE) > 1; }

static esp_err_t fetch(void) {
    // Plain HTTP: the data is public and the firmware has no TLS client.
    const char *url = "http://api.open-meteo.com/v1/forecast?latitude=" HOME_WEATHER_LATITUDE "&longitude=" HOME_WEATHER_LONGITUDE
                      "&current=temperature_2m,weather_code,is_day&daily=temperature_2m_max,temperature_2m_min&forecast_days=1&timezone=auto";
    esp_http_client_config_t config = {.url = url, .timeout_ms = 10000, .addr_type = HTTP_ADDR_TYPE_INET};
    esp_http_client_handle_t client = esp_http_client_init(&config);
    char *body = malloc(BODY_MAX);
    esp_err_t err = client && body ? esp_http_client_open(client, 0) : ESP_ERR_NO_MEM;
    int length = 0;
    if (err == ESP_OK) {
        esp_http_client_fetch_headers(client);
        int status = esp_http_client_get_status_code(client);
        for (int count; length < BODY_MAX - 1 && (count = esp_http_client_read(client, body + length, BODY_MAX - 1 - length)) > 0;)
            length += count;
        if (status != 200) {
            ESP_LOGW(TAG, "Open-Meteo answered HTTP %d", status);
            err = ESP_FAIL;
        }
    }
    if (err != ESP_OK && err != ESP_FAIL)
        ESP_LOGW(TAG, "Open-Meteo request failed: %s", esp_err_to_name(err));
    if (client) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
    }
    if (err == ESP_OK) {
        body[length] = '\0';
        cJSON *root = cJSON_Parse(body);
        const cJSON *current = cJSON_GetObjectItemCaseSensitive(root, "current");
        const cJSON *t = cJSON_GetObjectItemCaseSensitive(current, "temperature_2m");
        const cJSON *c = cJSON_GetObjectItemCaseSensitive(current, "weather_code");
        const cJSON *d = cJSON_GetObjectItemCaseSensitive(current, "is_day");
        const cJSON *daily = cJSON_GetObjectItemCaseSensitive(root, "daily");
        const cJSON *max = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(daily, "temperature_2m_max"), 0);
        const cJSON *min = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(daily, "temperature_2m_min"), 0);
        if (cJSON_IsNumber(t) && cJSON_IsNumber(c) && cJSON_IsNumber(d)) {
            portENTER_CRITICAL(&lock);
            temperature = (float)t->valuedouble;
            code = c->valueint;
            is_day = d->valueint != 0;
            has_range = cJSON_IsNumber(max) && cJSON_IsNumber(min);
            high = has_range ? (float)max->valuedouble : 0;
            low = has_range ? (float)min->valuedouble : 0;
            updated_us = esp_timer_get_time();
            have_data = true;
            portEXIT_CRITICAL(&lock);
            ESP_LOGI(TAG, "%.1f C, code %d, %s", t->valuedouble, c->valueint, d->valueint ? "day" : "night");
        } else {
            ESP_LOGW(TAG, "Unexpected Open-Meteo response");
            err = ESP_FAIL;
        }
        cJSON_Delete(root);
    }
    free(body);
    return err;
}

void weather_task(void *arg) {
    if (!configured()) {
        ESP_LOGI(TAG, "No [weather] location in wifi_secrets.ini; weather disabled");
        vTaskDelete(NULL);
    }
    uint32_t retry_ms = RETRY_FIRST_MS;
    for (;;) {
        if (!wifi_home_connected()) {
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }
        if (fetch() == ESP_OK) {
            retry_ms = RETRY_FIRST_MS;
            vTaskDelay(pdMS_TO_TICKS(REFRESH_MS));
        } else {
            vTaskDelay(pdMS_TO_TICKS(retry_ms));
            retry_ms = retry_ms * 2 > RETRY_MAX_MS ? RETRY_MAX_MS : retry_ms * 2;
        }
    }
}

void weather_get(weather_data_t *out) {
    portENTER_CRITICAL(&lock);
    int64_t age_us = esp_timer_get_time() - updated_us;
    out->configured = configured();
    out->valid = have_data && age_us < MAX_AGE_US;
    out->temperature = temperature;
    out->code = code;
    out->is_day = is_day;
    out->has_range = has_range;
    out->high = high;
    out->low = low;
    out->age_s = have_data ? (uint32_t)(age_us / 1000000) : 0;
    portEXIT_CRITICAL(&lock);
}
