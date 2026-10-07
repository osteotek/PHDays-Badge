#include "ota_upload.h"

#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "home_wifi_secrets.h"
#include "display.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define OTA_CHUNK_SIZE 4096
// httpd waits 5 s per receive; allow about a minute without data.
#define OTA_RECV_RETRIES 12
// The app description follows the image header and the first segment header.
#define OTA_APP_DESC_OFFSET (sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t))

static const char *TAG = "ota_upload";
static esp_timer_handle_t restart_timer;
static esp_timer_handle_t status_timer;
static bool update_in_progress;

static void restart_badge(void *arg) { esp_restart(); }

static void restore_display(void *arg) { end_status_image(); }

// Compare without an early exit so response timing does not reveal the token.
static bool token_matches(const char *authorization) {
    static const char prefix[] = "Bearer ";
    const char *expected = HOME_OTA_TOKEN;
    size_t prefix_len = sizeof(prefix) - 1, expected_len = sizeof(HOME_OTA_TOKEN) - 1;
    if (strncmp(authorization, prefix, prefix_len) != 0)
        return false;
    const char *given = authorization + prefix_len;
    size_t given_len = strlen(given);
    unsigned char diff = given_len != expected_len;
    for (size_t i = 0; i < expected_len; i++)
        diff |= (unsigned char)expected[i] ^ (unsigned char)given[i < given_len ? i : 0];
    return diff == 0;
}

static esp_err_t send_status(httpd_req_t *req, const char *status, const char *message) {
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_sendstr(req, message);
}

// Fills buf with exactly len bytes of the request body.
static esp_err_t recv_exact(httpd_req_t *req, char *buf, size_t len) {
    size_t received = 0;
    int retries = 0;
    while (received < len) {
        int count = httpd_req_recv(req, buf + received, len - received);
        if (count == HTTPD_SOCK_ERR_TIMEOUT && ++retries <= OTA_RECV_RETRIES)
            continue;
        if (count <= 0)
            return ESP_FAIL;
        received += count;
        retries = 0;
    }
    return ESP_OK;
}

static const char *check_image_header(const char *buf, size_t len) {
    if (len < OTA_APP_DESC_OFFSET + sizeof(esp_app_desc_t))
        return "Firmware image too small";
    const esp_image_header_t *header = (const esp_image_header_t *)buf;
    if (header->magic != ESP_IMAGE_HEADER_MAGIC)
        return "Not an ESP firmware image (use firmware.bin)";
    if (header->chip_id != CONFIG_IDF_FIRMWARE_CHIP_ID)
        return "Firmware is built for a different chip";
    const esp_app_desc_t *incoming = (const esp_app_desc_t *)(buf + OTA_APP_DESC_OFFSET);
    if (incoming->magic_word != ESP_APP_DESC_MAGIC_WORD)
        return "Firmware image has no app description";
    if (strncmp(incoming->project_name, esp_app_get_description()->project_name, sizeof(incoming->project_name)) != 0)
        return "Firmware is for a different project";
    return NULL;
}

static esp_err_t ota_post(httpd_req_t *req) {
    if (sizeof(HOME_OTA_TOKEN) <= 1)
        return send_status(req, "403 Forbidden", "Wi-Fi updates are disabled: set [ota] token in wifi_secrets.ini and flash over USB");
    char authorization[128] = "";
    if (httpd_req_get_hdr_value_str(req, "Authorization", authorization, sizeof(authorization)) != ESP_OK ||
        !token_matches(authorization)) {
        memset(authorization, 0, sizeof(authorization));
        return send_status(req, "401 Unauthorized", "Wrong or missing update token");
    }
    memset(authorization, 0, sizeof(authorization));
    if (update_in_progress || esp_timer_is_active(restart_timer))
        return send_status(req, "409 Conflict", "An update is already in progress");

    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
    if (!target)
        return send_status(req, "500 Internal Server Error", "No OTA partition available");
    if (req->content_len == 0 || req->content_len > target->size)
        return send_status(req, "400 Bad Request", "Firmware size does not fit the OTA partition");

    char *buf = malloc(OTA_CHUNK_SIZE);
    if (!buf)
        return send_status(req, "500 Internal Server Error", "Out of memory");

    update_in_progress = true;
    esp_timer_stop(status_timer);
    set_ota_display_image(0); // scrolling download arrow while flash is erased
    ESP_LOGI(TAG, "Receiving %u-byte firmware into %s", (unsigned)req->content_len, target->label);
    // Power saving adds latency to every received packet; disable it for the upload.
    wifi_ps_type_t power_save = WIFI_PS_MIN_MODEM;
    esp_wifi_get_ps(&power_save);
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_ota_handle_t handle = 0;
    const char *failure = NULL;
    // Erase the image's space before reading the body. Erasing while receiving
    // stalls Wi-Fi, drops packets and pushes the sender into long TCP backoff;
    // up front, the sender just waits on a closed TCP window.
    esp_err_t err = esp_ota_begin(target, req->content_len, &handle);
    if (err == ESP_ERR_OTA_ROLLBACK_INVALID_STATE)
        failure = "The previous update is still being confirmed; try again in a few minutes";
    else if (err != ESP_OK)
        failure = "Could not start the update";

    size_t done = 0;
    uint8_t shown_percent = 255;
    for (int next_report = 10; !failure && done < req->content_len;) {
        size_t len = req->content_len - done < OTA_CHUNK_SIZE ? req->content_len - done : OTA_CHUNK_SIZE;
        if (recv_exact(req, buf, len) != ESP_OK) {
            failure = "Upload interrupted";
            break;
        }
        if (done == 0 && (failure = check_image_header(buf, len)) != NULL)
            break;
        if ((err = esp_ota_write(handle, buf, len)) != ESP_OK) {
            failure = "Writing firmware failed";
            break;
        }
        done += len;
        uint8_t percent = done * 100 / req->content_len;
        if (percent != shown_percent) {
            show_update_progress(percent);
            shown_percent = percent;
        }
        if (percent >= next_report) {
            ESP_LOGI(TAG, "Received %d%%", next_report);
            next_report += 10;
        }
    }
    free(buf);
    esp_wifi_set_ps(power_save);

    if (!failure) {
        // esp_ota_end verifies the image checksum and SHA-256 before it can boot.
        err = esp_ota_end(handle);
        handle = 0;
        if (err != ESP_OK)
            failure = err == ESP_ERR_OTA_VALIDATE_FAILED ? "Firmware image failed verification" : "Finishing the update failed";
        else if ((err = esp_ota_set_boot_partition(target)) != ESP_OK)
            failure = "Could not select the new firmware";
    }
    if (handle)
        esp_ota_abort(handle);
    update_in_progress = false;

    if (failure) {
        set_ota_display_image(2);
        esp_timer_start_once(status_timer, 3000000);
        ESP_LOGE(TAG, "Update failed after %u of %u bytes: %s (%s)", (unsigned)done, (unsigned)req->content_len, failure, esp_err_to_name(err));
        return send_status(req, "400 Bad Request", failure);
    }
    ESP_LOGI(TAG, "Update written to %s; restarting", target->label);
    esp_timer_start_once(restart_timer, 1000000);
    return send_status(req, "200 OK", "Update installed; the badge is restarting");
}

esp_err_t registerOtaUpload(httpd_handle_t server) {
    const esp_timer_create_args_t timer_args = {.callback = restart_badge, .name = "ota_restart"};
    esp_err_t err = esp_timer_create(&timer_args, &restart_timer);
    if (err != ESP_OK)
        return err;
    const esp_timer_create_args_t status_args = {.callback = restore_display, .name = "ota_status"};
    if ((err = esp_timer_create(&status_args, &status_timer)) != ESP_OK)
        return err;
    const httpd_uri_t route = {.uri = "/api/v1/ota", .method = HTTP_POST, .handler = ota_post};
    return httpd_register_uri_handler(server, &route);
}
