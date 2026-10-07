#include "timer.h"

#include "buzzer.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "settings.h"
#include <string.h>

static const char *TAG = "timer";
static SemaphoreHandle_t lock;
static esp_timer_handle_t phase_timer;
static timer_phase_t phase = TIMER_IDLE;
static int64_t phase_end_us;
static uint32_t phase_total_s;

static void begin_phase_locked(timer_phase_t next) {
    esp_timer_stop(phase_timer);
    phase = next;
    if (next == TIMER_IDLE)
        return;
    badge_settings_t settings;
    settings_get(&settings);
    phase_total_s = (next == TIMER_FOCUS ? settings.focus_minutes : settings.break_minutes) * 60U;
    phase_end_us = esp_timer_get_time() + (int64_t)phase_total_s * 1000000;
    esp_timer_start_once(phase_timer, (uint64_t)phase_total_s * 1000000);
    ESP_LOGI(TAG, "%s for %lu min", timer_phase_name(next), (unsigned long)(phase_total_s / 60));
}

static void play_alert(void) {
    badge_settings_t settings;
    settings_get(&settings);
    if (parse_rtttl(settings.alert_melody, strlen(settings.alert_melody)) < 0)
        ESP_LOGW(TAG, "Alert melody could not be played");
}

static void phase_finished(void *arg) {
    xSemaphoreTake(lock, portMAX_DELAY);
    begin_phase_locked(phase == TIMER_FOCUS ? TIMER_BREAK : TIMER_IDLE);
    xSemaphoreGive(lock);
    play_alert();
}

void timer_init(void) {
    lock = xSemaphoreCreateMutex();
    const esp_timer_create_args_t args = {.callback = phase_finished, .name = "pomodoro"};
    ESP_ERROR_CHECK(esp_timer_create(&args, &phase_timer));
}

void timer_start(void) {
    xSemaphoreTake(lock, portMAX_DELAY);
    begin_phase_locked(TIMER_FOCUS);
    xSemaphoreGive(lock);
}

void timer_stop(void) {
    xSemaphoreTake(lock, portMAX_DELAY);
    begin_phase_locked(TIMER_IDLE);
    xSemaphoreGive(lock);
}

void timer_skip(void) {
    xSemaphoreTake(lock, portMAX_DELAY);
    if (phase != TIMER_IDLE)
        begin_phase_locked(phase == TIMER_FOCUS ? TIMER_BREAK : TIMER_IDLE);
    xSemaphoreGive(lock);
}

void timer_toggle(void) {
    screen_timer_t state;
    timer_get(&state);
    if (state.phase == TIMER_IDLE)
        timer_start();
    else
        timer_stop();
}

void timer_get(screen_timer_t *out) {
    xSemaphoreTake(lock, portMAX_DELAY);
    out->phase = phase;
    out->total_s = phase == TIMER_IDLE ? 0 : phase_total_s;
    int64_t left_us = phase == TIMER_IDLE ? 0 : phase_end_us - esp_timer_get_time();
    out->remaining_s = left_us > 0 ? (uint32_t)((left_us + 999999) / 1000000) : 0;
    xSemaphoreGive(lock);
}

const char *timer_phase_name(timer_phase_t p) {
    switch (p) {
    case TIMER_FOCUS:
        return "focus";
    case TIMER_BREAK:
        return "break";
    default:
        return "idle";
    }
}
