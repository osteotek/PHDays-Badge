#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "button_gpio.h"
#include "iot_button.h"

#include "adc_utils.h"
#include "buttons.h"
#include "led_plate.h"
#include "wifi_utilss.h"

#define BUTTON_ACTIVE_LEVEL 0

uint8_t brightnessSwitchCounter = 0;
uint8_t screenOffCounter = 0;
uint8_t modeSwitchCounter = 0;

static const char *TAG = "buttons";

static void button_click_bright_cb(void *arg, void *usr_data) {
    button_event_t event = iot_button_get_event(arg);
    if ((event == BUTTON_SINGLE_CLICK || event == BUTTON_MULTIPLE_CLICK) && brightnessSwitchCounter < 255)
        brightnessSwitchCounter++;

    if (event == BUTTON_SINGLE_CLICK)
        shiftBrightness();

    if (event == BUTTON_MULTIPLE_CLICK && (int)usr_data == 3)
        switchAutoFade();

    if (event == BUTTON_MULTIPLE_CLICK && (int)usr_data == 4)
        setTurboBrightness();
}

static void button_single_click_leds_cb(void *arg, void *usr_data) {
    if (screenOffCounter < 255)
        screenOffCounter++;
    switchLeds();
}

static void button_press_leds_cb(void *arg, void *usr_data) {
    if (modeSwitchCounter < 255)
        modeSwitchCounter++;
    switchCustom();
}
static void button_triple_click_leds_cb(void *arg, void *usr_data) { switchLedsShiter(); }

// Hotspot name, hotspot password and battery level are shown as status images by
// one short-lived task. Button callbacks run in the shared esp_timer task, so
// they must not wait themselves; a press while a display is running is ignored.
typedef enum { INFO_SSID, INFO_PASSWORD, INFO_BATTERY } info_kind_t;
static volatile bool info_busy; // only touched from button callbacks and the info task

static void info_task(void *arg) {
    switch ((info_kind_t)(intptr_t)arg) {
    case INFO_SSID:
        show_number_image(ESP_WIFI_SSID_INT, 255, 255, 255);
        vTaskDelay(pdMS_TO_TICKS(15000));
        break;
    case INFO_PASSWORD:
        show_number_image(ESP_WIFI_PASS_LONG, 255, 0, 0);
        vTaskDelay(pdMS_TO_TICKS(15000));
        break;
    case INFO_BATTERY: {
        uint8_t percent = get_battery_level_percent();
        ESP_LOGI(TAG, "Battery: %d%%", percent);
        set_power_display_image(percent);
        vTaskDelay(pdMS_TO_TICKS(5000));
        break;
    }
    }
    end_status_image();
    info_busy = false;
    vTaskDelete(NULL);
}

static void show_info(info_kind_t kind) {
    if (info_busy)
        return;
    info_busy = true;
    if (xTaskCreate(info_task, "badge_info", 4096, (void *)(intptr_t)kind, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create info display task");
        info_busy = false;
    }
}

static void button_single_click_ssid_cb(void *arg, void *usr_data) { show_info(INFO_SSID); }

static void button_press_pass_cb(void *arg, void *usr_data) { show_info(INFO_PASSWORD); }

static void button_double_click_pass_cb(void *arg, void *usr_data) { show_info(INFO_BATTERY); }

void initButtons() {
    const button_config_t btn_cfg = {0};
    const button_gpio_config_t gpio_btn_bright_cfg = {
        .gpio_num = CONFIG_BRIGHT_BUTTON,
        .active_level = BUTTON_ACTIVE_LEVEL,
    };

    button_handle_t gpio_btn_bright = NULL;
    esp_err_t ret = iot_button_new_gpio_device(&btn_cfg, &gpio_btn_bright_cfg, &gpio_btn_bright);
    ESP_ERROR_CHECK(ret);

    button_event_args_t args = {
        .multiple_clicks.clicks = 3,
    };
    iot_button_register_cb(gpio_btn_bright, BUTTON_SINGLE_CLICK, NULL, button_click_bright_cb, NULL);
    iot_button_register_cb(gpio_btn_bright, BUTTON_MULTIPLE_CLICK, &args, button_click_bright_cb, (void *)3);
    args.multiple_clicks.clicks = 4;
    iot_button_register_cb(gpio_btn_bright, BUTTON_MULTIPLE_CLICK, &args, button_click_bright_cb, (void *)4);

    const button_gpio_config_t gpio_btn_pass_cfg = {
        .gpio_num = CONFIG_PASS_BUTTON,
        .active_level = BUTTON_ACTIVE_LEVEL,
    };

    button_handle_t gpio_btn_pass = NULL;
    ret = iot_button_new_gpio_device(&btn_cfg, &gpio_btn_pass_cfg, &gpio_btn_pass);
    ESP_ERROR_CHECK(ret);

    button_event_args_t args_pass = {
        .multiple_clicks.clicks = 2,
    };

    iot_button_register_cb(gpio_btn_pass, BUTTON_SINGLE_CLICK, NULL, button_single_click_ssid_cb, NULL);
    iot_button_register_cb(gpio_btn_pass, BUTTON_LONG_PRESS_START, NULL, button_press_pass_cb, NULL);
    iot_button_register_cb(gpio_btn_pass, BUTTON_MULTIPLE_CLICK, &args_pass, button_double_click_pass_cb, NULL);

    const button_gpio_config_t gpio_btn_leds_cfg = {
        .gpio_num = CONFIG_LEDS_BUTTON,
        .active_level = BUTTON_ACTIVE_LEVEL,
    };

    button_handle_t gpio_btn_leds = NULL;
    ret = iot_button_new_gpio_device(&btn_cfg, &gpio_btn_leds_cfg, &gpio_btn_leds);
    ESP_ERROR_CHECK(ret);

    button_event_args_t args_leds = {
        .multiple_clicks.clicks = 3,
    };
    iot_button_register_cb(gpio_btn_leds, BUTTON_SINGLE_CLICK, NULL, button_single_click_leds_cb, NULL);
    iot_button_register_cb(gpio_btn_leds, BUTTON_LONG_PRESS_START, NULL, button_press_leds_cb, NULL);
    iot_button_register_cb(gpio_btn_leds, BUTTON_MULTIPLE_CLICK, &args_leds, button_triple_click_leds_cb, (void *)3);
}
