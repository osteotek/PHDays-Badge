#pragma once

// Badge settings, stored as JSON in NVS and edited through /api/v1/settings.
#include "cJSON.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SETTINGS_TIMEZONE_MAX 48
#define SETTINGS_MELODY_MAX 512

typedef struct {
    bool show_clock;
    bool show_weather;
    bool transitions; // Matrix rain between screens
    uint8_t screen_seconds; // time per screen in the rotation
    uint8_t brightness;     // 1-15
    char timezone[SETTINGS_TIMEZONE_MAX]; // POSIX TZ, e.g. "MSK-3"
    uint8_t focus_minutes;
    uint8_t break_minutes;
    char alert_melody[SETTINGS_MELODY_MAX]; // RTTTL played when a timer phase ends
    bool night_mode;          // dim or blank the LEDs between night_start and night_end
    uint16_t night_start;     // minutes after midnight; the window may cross midnight
    uint16_t night_end;
    uint8_t night_brightness; // 0 = LEDs off
} badge_settings_t;

void settings_init(void); // loads saved settings and applies the time zone
void settings_get(badge_settings_t *out);
void settings_cycle_brightness(void); // brightness button

cJSON *settings_to_json(const badge_settings_t *settings);
// Validates a partial JSON object, merges, saves and applies it. Returns NULL on
// success, otherwise a message for the user.
const char *settings_update_json(const cJSON *json);
