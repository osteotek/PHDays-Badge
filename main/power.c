#include "power.h"

#include "esp_log.h"
#include "esp_pm.h"

static const char *TAG = "power";

void power_apply(bool light_sleep) {
    esp_pm_config_t config = {.max_freq_mhz = 160, .min_freq_mhz = 80, .light_sleep_enable = light_sleep};
    esp_err_t err = esp_pm_configure(&config);
    if (err != ESP_OK)
        ESP_LOGE(TAG, "Power management failed: %s", esp_err_to_name(err));
    else
        ESP_LOGI(TAG, "CPU 80-160 MHz, light sleep %s", light_sleep ? "on" : "off");
}
