#include "settings.h"

#include "esp_log.h"
#include "power.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define NVS_NAMESPACE "badge"
#define NVS_KEY "settings"

static const char *TAG = "settings";
static SemaphoreHandle_t lock;
static badge_settings_t current;

static const badge_settings_t DEFAULTS = {
    .show_clock = true,
    .show_weather = true,
    .transitions = true,
    .power_save = true,
    .screen_seconds = 10,
    .brightness = 10,
    .auto_off_minutes = 5,
    .timezone = "MSK-3",
    .focus_minutes = 25,
    .break_minutes = 5,
    .alert_melody = "Alert:d=8,o=6,b=180:c,e,g,c7",
    .night_mode = false,
    .night_start = 23 * 60,
    .night_end = 7 * 60,
    .night_brightness = 1,
};

static const char *read_bool(const cJSON *json, const char *key, bool *out) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(json, key);
    if (!item)
        return NULL;
    if (!cJSON_IsBool(item))
        return "Expected true or false";
    *out = cJSON_IsTrue(item);
    return NULL;
}

static const char *read_int(const cJSON *json, const char *key, int min, int max, uint8_t *out) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(json, key);
    if (!item)
        return NULL;
    if (!cJSON_IsNumber(item) || item->valuedouble != (int)item->valuedouble || item->valueint < min || item->valueint > max)
        return "Number out of range";
    *out = (uint8_t)item->valueint;
    return NULL;
}

// POSIX TZ strings: "MSK-3", "CET-1CEST,M3.5.0,M10.5.0/3", "<+04>-4".
static bool valid_timezone(const char *tz) {
    if (!tz[0])
        return false;
    for (const char *c = tz; *c; c++)
        if (!((*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || strchr("+-,.:/<>", *c)))
            return false;
    return true;
}

// RTTTL is "name:defaults:notes"; the buzzer validates the notes when playing.
static bool valid_melody(const char *melody) {
    int colons = 0;
    for (const char *c = melody; *c; c++)
        colons += *c == ':';
    return colons == 2;
}

// "HH:MM" -> minutes after midnight.
static const char *read_time(const cJSON *json, const char *key, uint16_t *out) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(json, key);
    if (!item)
        return NULL;
    unsigned hours, minutes;
    char end;
    if (!cJSON_IsString(item) || sscanf(item->valuestring, "%2u:%2u%c", &hours, &minutes, &end) != 2 || hours > 23 || minutes > 59)
        return "Expected a time like \"23:00\"";
    *out = (uint16_t)(hours * 60 + minutes);
    return NULL;
}

static const char *read_string(const cJSON *json, const char *key, char *out, size_t size, bool (*valid)(const char *)) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(json, key);
    if (!item)
        return NULL;
    if (!cJSON_IsString(item) || strlen(item->valuestring) >= size || !valid(item->valuestring))
        return "Invalid text";
    strcpy(out, item->valuestring);
    return NULL;
}

// Merges known keys from json into settings; unknown keys are ignored.
static const char *merge(badge_settings_t *s, const cJSON *json) {
    if (!cJSON_IsObject(json))
        return "Expected a JSON object";
    struct {
        const char *key;
        const char *error;
    } fields[] = {
        {"show_clock", read_bool(json, "show_clock", &s->show_clock)},
        {"show_weather", read_bool(json, "show_weather", &s->show_weather)},
        {"transitions", read_bool(json, "transitions", &s->transitions)},
        {"power_save", read_bool(json, "power_save", &s->power_save)},
        {"screen_seconds", read_int(json, "screen_seconds", 3, 120, &s->screen_seconds)},
        {"brightness", read_int(json, "brightness", 1, 15, &s->brightness)},
        {"auto_off_minutes", read_int(json, "auto_off_minutes", 0, 120, &s->auto_off_minutes)},
        {"timezone", read_string(json, "timezone", s->timezone, sizeof(s->timezone), valid_timezone)},
        {"focus_minutes", read_int(json, "focus_minutes", 1, 180, &s->focus_minutes)},
        {"break_minutes", read_int(json, "break_minutes", 1, 60, &s->break_minutes)},
        {"alert_melody", read_string(json, "alert_melody", s->alert_melody, sizeof(s->alert_melody), valid_melody)},
        {"night_mode", read_bool(json, "night_mode", &s->night_mode)},
        {"night_start", read_time(json, "night_start", &s->night_start)},
        {"night_end", read_time(json, "night_end", &s->night_end)},
        {"night_brightness", read_int(json, "night_brightness", 0, 15, &s->night_brightness)},
    };
    static char message[64];
    for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) {
        if (fields[i].error) {
            snprintf(message, sizeof(message), "%s: %s", fields[i].key, fields[i].error);
            return message;
        }
    }
    return NULL;
}

cJSON *settings_to_json(const badge_settings_t *s) {
    cJSON *json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "show_clock", s->show_clock);
    cJSON_AddBoolToObject(json, "show_weather", s->show_weather);
    cJSON_AddBoolToObject(json, "transitions", s->transitions);
    cJSON_AddBoolToObject(json, "power_save", s->power_save);
    cJSON_AddNumberToObject(json, "screen_seconds", s->screen_seconds);
    cJSON_AddNumberToObject(json, "brightness", s->brightness);
    cJSON_AddNumberToObject(json, "auto_off_minutes", s->auto_off_minutes);
    cJSON_AddStringToObject(json, "timezone", s->timezone);
    cJSON_AddNumberToObject(json, "focus_minutes", s->focus_minutes);
    cJSON_AddNumberToObject(json, "break_minutes", s->break_minutes);
    cJSON_AddStringToObject(json, "alert_melody", s->alert_melody);
    cJSON_AddBoolToObject(json, "night_mode", s->night_mode);
    char time[12]; // room for any uint16_t, though values stay below 24:00
    snprintf(time, sizeof(time), "%02u:%02u", s->night_start / 60, s->night_start % 60);
    cJSON_AddStringToObject(json, "night_start", time);
    snprintf(time, sizeof(time), "%02u:%02u", s->night_end / 60, s->night_end % 60);
    cJSON_AddStringToObject(json, "night_end", time);
    cJSON_AddNumberToObject(json, "night_brightness", s->night_brightness);
    return json;
}

static void apply_timezone(const char *tz) {
    setenv("TZ", tz, 1);
    tzset();
}

static void save_locked(void) {
    cJSON *json = settings_to_json(&current);
    char *text = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    nvs_handle_t handle;
    esp_err_t err = text ? nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) : ESP_ERR_NO_MEM;
    if (err == ESP_OK) {
        err = nvs_set_str(handle, NVS_KEY, text);
        if (err == ESP_OK)
            err = nvs_commit(handle);
        nvs_close(handle);
    }
    cJSON_free(text);
    if (err != ESP_OK)
        ESP_LOGE(TAG, "Saving settings failed: %s", esp_err_to_name(err));
}

// Picture mode, festival statistics and the hotspot password are gone; drop
// what they left in NVS.
static void erase_obsolete_keys(void) {
    static const char *const keys[] = {"image_to_show", "setted_custom", "show_custom", "web_openned", "image_setted", "project_saved", "brightness_sw", "screen_off", "mode_switched", "wifi_client_con", "ap_password"};
    nvs_handle_t handle;
    if (nvs_open("storage", NVS_READWRITE, &handle) != ESP_OK)
        return;
    bool erased = false;
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
        erased |= nvs_erase_key(handle, keys[i]) == ESP_OK;
    if (erased)
        nvs_commit(handle);
    nvs_close(handle);
}

void settings_init(void) {
    lock = xSemaphoreCreateMutex();
    current = DEFAULTS;
    erase_obsolete_keys();
    nvs_handle_t handle;
    size_t length = 0;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
        if (nvs_get_str(handle, NVS_KEY, NULL, &length) == ESP_OK && length > 0) {
            char *text = malloc(length);
            if (text && nvs_get_str(handle, NVS_KEY, text, &length) == ESP_OK) {
                cJSON *json = cJSON_Parse(text);
                badge_settings_t loaded = DEFAULTS;
                const char *error = merge(&loaded, json);
                if (error)
                    ESP_LOGW(TAG, "Ignoring saved settings (%s)", error);
                else
                    current = loaded;
                cJSON_Delete(json);
            }
            free(text);
        }
        nvs_close(handle);
    }
    apply_timezone(current.timezone);
    power_apply(current.power_save);
    ESP_LOGI(TAG, "Time zone %s, %u s per screen", current.timezone, current.screen_seconds);
}

void settings_get(badge_settings_t *out) {
    xSemaphoreTake(lock, portMAX_DELAY);
    *out = current;
    xSemaphoreGive(lock);
}

void settings_cycle_brightness(void) {
    xSemaphoreTake(lock, portMAX_DELAY);
    current.brightness = current.brightness >= 15 ? 1 : current.brightness + 1;
    save_locked();
    xSemaphoreGive(lock);
}

const char *settings_update_json(const cJSON *json) {
    xSemaphoreTake(lock, portMAX_DELAY);
    badge_settings_t updated = current;
    const char *error = merge(&updated, json);
    if (!error) {
        bool power_changed = updated.power_save != current.power_save;
        current = updated;
        save_locked();
        apply_timezone(current.timezone);
        if (power_changed)
            power_apply(current.power_save);
    }
    xSemaphoreGive(lock);
    return error;
}
