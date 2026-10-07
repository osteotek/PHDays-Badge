#include "ota.h"

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "display.h"
#include "wifi.h"

static const char *TAG = "ota";

// A freshly installed image boots in PENDING_VERIFY. Keep it only once it is
// reachable for the next Wi-Fi update again, via home Wi-Fi or, when home Wi-Fi is
// out of range, via the fallback hotspot (which starts after 120 s). A build that
// breaks networking rolls back instead of locking out Wi-Fi updates.
#define OTA_VERIFY_WIFI_TIMEOUT_MS (180 * 1000)

void ota_verify_after_update(void *pvParameter) {
    vTaskDelay(pdMS_TO_TICKS(5000));
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK && ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
        if (wifi_home_configured()) {
            for (int waited = 0; !wifi_home_connected() && !wifi_hotspot_active() && waited < OTA_VERIFY_WIFI_TIMEOUT_MS; waited += 1000)
                vTaskDelay(pdMS_TO_TICKS(1000));
            if (!wifi_home_connected() && !wifi_hotspot_active()) {
                ESP_LOGE(TAG, "New firmware is unreachable over Wi-Fi; rolling back");
                set_ota_display_image(2);
                vTaskDelay(pdMS_TO_TICKS(3000));
                esp_ota_mark_app_invalid_rollback_and_reboot();
            }
        }
        esp_ota_mark_app_valid_cancel_rollback();
        ESP_LOGI(TAG, "New firmware confirmed on %s", running->label);
        set_ota_display_image(1);
        vTaskDelay(pdMS_TO_TICKS(5000));
        end_status_image();
    }
    (void)vTaskDelete(NULL);
}
