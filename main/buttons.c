#include "buttons.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "button_gpio.h"
#include "iot_button.h"

#include "adc_utils.h"
#include "display.h"
#include "settings.h"
#include "timer.h"

#define BUTTON_ACTIVE_LEVEL 0

static const char *TAG = "buttons";

// The battery level is shown as a status image for 5 s by a short-lived task:
// button callbacks run in the shared esp_timer task and must not wait
// themselves. A press while it is showing is ignored.
static volatile bool battery_busy; // only touched from button callbacks and the battery task

static void battery_task(void *arg) {
    uint8_t percent = get_battery_level_percent();
    ESP_LOGI(TAG, "Battery: %d%%", percent);
    set_power_display_image(percent);
    vTaskDelay(pdMS_TO_TICKS(5000));
    end_status_image();
    battery_busy = false;
    vTaskDelete(NULL);
}

static void show_battery(void) {
    if (battery_busy)
        return;
    battery_busy = true;
    if (xTaskCreate(battery_task, "battery_info", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create battery display task");
        battery_busy = false;
    }
}

static void brightness_click_cb(void *arg, void *usr_data) { settings_cycle_brightness(); }

static void timer_click_cb(void *arg, void *usr_data) { timer_toggle(); }

static void timer_long_press_cb(void *arg, void *usr_data) { timer_skip(); }

static void timer_double_click_cb(void *arg, void *usr_data) { show_battery(); }

static void screen_click_cb(void *arg, void *usr_data) { display_toggle_screen(); }

static void screen_double_click_cb(void *arg, void *usr_data) { display_next_screen(); }

static button_handle_t new_button(int gpio) {
    const button_config_t config = {0};
    // Power save: GPIO wake-up instead of polling, so the chip can light-sleep.
    const button_gpio_config_t gpio_config = {.gpio_num = gpio, .active_level = BUTTON_ACTIVE_LEVEL, .enable_power_save = true};
    button_handle_t button = NULL;
    ESP_ERROR_CHECK(iot_button_new_gpio_device(&config, &gpio_config, &button));
    return button;
}

void initButtons(void) {
    button_event_args_t double_click = {.multiple_clicks.clicks = 2};

    button_handle_t brightness = new_button(CONFIG_BRIGHT_BUTTON);
    iot_button_register_cb(brightness, BUTTON_SINGLE_CLICK, NULL, brightness_click_cb, NULL);

    button_handle_t timer = new_button(CONFIG_PASS_BUTTON);
    iot_button_register_cb(timer, BUTTON_SINGLE_CLICK, NULL, timer_click_cb, NULL);
    iot_button_register_cb(timer, BUTTON_LONG_PRESS_START, NULL, timer_long_press_cb, NULL);
    iot_button_register_cb(timer, BUTTON_MULTIPLE_CLICK, &double_click, timer_double_click_cb, NULL);

    button_handle_t screen = new_button(CONFIG_LEDS_BUTTON);
    iot_button_register_cb(screen, BUTTON_SINGLE_CLICK, NULL, screen_click_cb, NULL);
    iot_button_register_cb(screen, BUTTON_MULTIPLE_CLICK, &double_click, screen_double_click_cb, NULL);
}
