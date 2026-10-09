#include "info_api.h"

#include "adc_utils.h"
#include "buzzer.h"
#include "cJSON.h"
#include "clock_time.h"
#include "display.h"
#include "esp_app_desc.h"
#include "rest_server.h"
#include "settings.h"
#include "timer.h"
#include "weather.h"
#include <stdio.h>
#include <string.h>

#define BODY_MAX 2048

static cJSON *timer_json(void) {
    screen_timer_t timer;
    timer_get(&timer);
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "phase", timer_phase_name(timer.phase));
    cJSON_AddNumberToObject(json, "remaining_s", timer.remaining_s);
    cJSON_AddNumberToObject(json, "total_s", timer.total_s);
    return json;
}

static esp_err_t status_get(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();
    screen_time_t time;
    if (clock_time_get(&time)) {
        char text[9];
        snprintf(text, sizeof(text), "%02d:%02d:%02d", time.hour, time.minute, time.second);
        cJSON_AddStringToObject(root, "time", text);
    } else {
        cJSON_AddNullToObject(root, "time");
    }
    cJSON_AddItemToObject(root, "ntp", clock_time_ntp_json());
    weather_data_t weather;
    weather_get(&weather);
    cJSON *w = cJSON_AddObjectToObject(root, "weather");
    cJSON_AddBoolToObject(w, "configured", weather.configured);
    cJSON_AddBoolToObject(w, "valid", weather.valid);
    if (weather.valid) {
        cJSON_AddNumberToObject(w, "temperature", weather.temperature);
        cJSON_AddNumberToObject(w, "code", weather.code);
        cJSON_AddBoolToObject(w, "is_day", weather.is_day);
        if (weather.has_range) {
            cJSON_AddNumberToObject(w, "high", weather.high);
            cJSON_AddNumberToObject(w, "low", weather.low);
        }
        cJSON_AddNumberToObject(w, "age_s", weather.age_s);
    }
    cJSON_AddItemToObject(root, "timer", timer_json());
    cJSON_AddStringToObject(root, "screen", display_current_screen());
    cJSON_AddBoolToObject(root, "screen_on", display_screen_on());
    cJSON_AddBoolToObject(root, "asleep", display_asleep());
    cJSON_AddBoolToObject(root, "night", display_night_active());
    cJSON_AddBoolToObject(root, "notifying", display_notifying());
    cJSON_AddNumberToObject(root, "battery", get_battery_level_percent());
    cJSON_AddNumberToObject(root, "battery_mv", get_battery_voltage_mv());
    cJSON_AddStringToObject(root, "app_version", esp_app_get_description()->version);
    return http_send_json(req, root);
}

static esp_err_t settings_get_handler(httpd_req_t *req) {
    badge_settings_t settings;
    settings_get(&settings);
    return http_send_json(req, settings_to_json(&settings));
}

static esp_err_t settings_post_handler(httpd_req_t *req) {
    static char body[BODY_MAX]; // httpd runs one handler at a time
    if (http_recv_body(req, body, sizeof(body)) != ESP_OK)
        return ESP_FAIL;
    cJSON *json = cJSON_Parse(body);
    const char *error = json ? settings_update_json(json) : "Invalid JSON";
    cJSON_Delete(json);
    if (error)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, error);
    display_wake(); // show the changed settings
    return settings_get_handler(req);
}

static esp_err_t timer_post_handler(httpd_req_t *req) {
    char body[128];
    if (http_recv_body(req, body, sizeof(body)) != ESP_OK)
        return ESP_FAIL;
    cJSON *json = cJSON_Parse(body);
    const char *action = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(json, "action"));
    bool known = true;
    if (action && strcmp(action, "start") == 0)
        timer_start();
    else if (action && strcmp(action, "stop") == 0)
        timer_stop();
    else if (action && strcmp(action, "skip") == 0)
        timer_skip();
    else
        known = false;
    cJSON_Delete(json);
    if (!known)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Expected {\"action\": \"start\" | \"stop\" | \"skip\"}");
    return http_send_json(req, timer_json());
}

static cJSON *screen_json(void) {
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "screen", display_current_screen());
    cJSON_AddBoolToObject(json, "screen_on", display_screen_on());
    cJSON_AddBoolToObject(json, "asleep", display_asleep());
    return json;
}

static esp_err_t screen_post_handler(httpd_req_t *req) {
    char body[128];
    if (http_recv_body(req, body, sizeof(body)) != ESP_OK)
        return ESP_FAIL;
    cJSON *json = cJSON_Parse(body);
    const char *action = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(json, "action"));
    bool known = true;
    if (action && strcmp(action, "on") == 0)
        display_set_screen(true);
    else if (action && strcmp(action, "off") == 0)
        display_set_screen(false);
    else if (action && strcmp(action, "toggle") == 0)
        display_toggle_screen();
    else if (action && strcmp(action, "next") == 0) {
        display_wake();
        display_next_screen();
    }
    else
        known = false;
    cJSON_Delete(json);
    if (!known)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Expected {\"action\": \"on\" | \"off\" | \"toggle\" | \"next\"}");
    return http_send_json(req, screen_json());
}

// "#rrggbb" or "rrggbb".
static bool parse_color(const char *text, Pixel *out) {
    unsigned r, g, b;
    char end;
    if (text[0] == '#')
        text++;
    if (strlen(text) != 6 || sscanf(text, "%2x%2x%2x%c", &r, &g, &b, &end) != 3)
        return false;
    *out = (Pixel){(uint8_t)r, (uint8_t)g, (uint8_t)b};
    return true;
}

static esp_err_t notify_post_handler(httpd_req_t *req) {
    char body[512];
    if (http_recv_body(req, body, sizeof(body)) != ESP_OK)
        return ESP_FAIL;
    cJSON *json = cJSON_Parse(body);
    const char *text = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(json, "text"));
    const cJSON *color = cJSON_GetObjectItemCaseSensitive(json, "color");
    const cJSON *repeat = cJSON_GetObjectItemCaseSensitive(json, "repeat");
    const cJSON *sound = cJSON_GetObjectItemCaseSensitive(json, "sound");
    Pixel pixel = {255, 255, 255};
    const char *error = NULL;
    if (!text || !text[0] || strlen(text) >= NOTIFY_TEXT_MAX)
        error = "text: 1-127 bytes required";
    else if (color && !(cJSON_IsString(color) && parse_color(color->valuestring, &pixel)))
        error = "color: expected \"#rrggbb\"";
    else if (repeat && !(cJSON_IsNumber(repeat) && repeat->valueint >= 1 && repeat->valueint <= 10 && repeat->valuedouble == repeat->valueint))
        error = "repeat: expected 1-10";
    else if (sound && !cJSON_IsBool(sound))
        error = "sound: expected true or false";
    if (error) {
        cJSON_Delete(json);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, error);
    }
    uint32_t duration = display_notify(text, pixel, repeat ? repeat->valueint : 2);
    if (cJSON_IsTrue(sound)) {
        badge_settings_t settings;
        settings_get(&settings);
        parse_rtttl(settings.alert_melody, strlen(settings.alert_melody));
    }
    cJSON_Delete(json);
    cJSON *response = cJSON_CreateObject();
    cJSON_AddNumberToObject(response, "duration_ms", duration);
    return http_send_json(req, response);
}

esp_err_t registerInfoApi(httpd_handle_t server) {
    const httpd_uri_t routes[] = {
        {.uri = "/api/v1/status", .method = HTTP_GET, .handler = status_get},
        {.uri = "/api/v1/settings", .method = HTTP_GET, .handler = settings_get_handler},
        {.uri = "/api/v1/settings", .method = HTTP_POST, .handler = settings_post_handler},
        {.uri = "/api/v1/timer", .method = HTTP_POST, .handler = timer_post_handler},
        {.uri = "/api/v1/screen", .method = HTTP_POST, .handler = screen_post_handler},
        {.uri = "/api/v1/notify", .method = HTTP_POST, .handler = notify_post_handler},
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        esp_err_t err = httpd_register_uri_handler(server, &routes[i]);
        if (err != ESP_OK)
            return err;
    }
    return ESP_OK;
}
