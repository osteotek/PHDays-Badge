#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "adc_utils.h"
#include "buttons.h"
#include "buzzer.h"
#include "clock_time.h"
#include "display.h"
#include "dns_server.h"
#include "ota.h"
#include "rest_server.h"
#include "settings.h"
#include "timer.h"
#include "weather.h"
#include "wifi.h"

static const char *TAG = "badge main";

static void start_task(TaskFunction_t task, const char *name, uint32_t stack, UBaseType_t priority) {
    if (xTaskCreate(task, name, stack, NULL, priority, NULL) != pdPASS)
        ESP_LOGE(TAG, "Failed to create task %s", name);
}

void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Erasing NVS (%s)", esp_err_to_name(ret));
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    const char *base_path = "/data"; // saved melodies
    ESP_ERROR_CHECK(mount_storage(base_path));

    settings_init();
    init_buzzer();
    timer_init();
    display_init();
    // Core 1, away from Wi-Fi and the web server, keeps the LEDs smooth.
    if (xTaskCreatePinnedToCore(display_task, "display", 4096, NULL, 2, NULL, 1) != pdPASS)
        ESP_LOGE(TAG, "Failed to create display task");

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    initWiFi();
    clock_time_start();

    ESP_ERROR_CHECK(start_file_server(base_path));
    initButtons();

    // Captive portal for the fallback hotspot: every name resolves to the badge.
    dns_server_config_t config = {.num_of_entries = 1, .item = {{.name = "*", .ip = {.addr = ESP_IP4TOADDR(192, 168, 4, 1)}}}};
    start_dns_server(&config);

    start_task(ota_verify_after_update, "ota_verify", 4096, 5);
    init_adc();
    start_task(sample_adc, "sample_adc", 4096, 6);
    start_task(weather_task, "weather", 6144, 4);
}
