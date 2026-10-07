#include "nvs_utils.h"

#define STORAGE_NAMESPACE "storage"

static const char *TAG = "nvs";

static esp_err_t save_display_flags(nvs_handle_t my_handle) {
    esp_err_t err;

    uint8_t settedCustom = 0;
    err = nvs_get_u8(my_handle, "setted_custom", &settedCustom);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
        return err;

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = nvs_set_u8(my_handle, "setted_custom", settedCustom);
        if (err != ESP_OK)
            return err;
    } else {
        if (settedCustom != getSettedCustom()) {
            settedCustom = getSettedCustom();
            err = nvs_set_u8(my_handle, "setted_custom", settedCustom);
            if (err != ESP_OK)
                return err;
        }
    }

    uint8_t showCustom = 0;
    err = nvs_get_u8(my_handle, "show_custom", &showCustom);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
        return err;

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = nvs_set_u8(my_handle, "show_custom", showCustom);
        if (err != ESP_OK)
            return err;
    } else {
        if (showCustom != getShowCustom()) {
            showCustom = getShowCustom();
            err = nvs_set_u8(my_handle, "show_custom", showCustom);
            if (err != ESP_OK)
                return err;
        }
    }
    return ESP_OK;
}

// The *_with_handle helpers may return early on any error; their callers own
// the NVS handle and always close it.
static esp_err_t save_with_handle(nvs_handle_t my_handle) {
    esp_err_t err;
    // While a status image (update progress, battery) is shown, the display
    // state is temporary; keep the user's saved picture and mode untouched.
    bool save_display = !status_image_active();

    if (save_display && (err = save_display_flags(my_handle)) != ESP_OK)
        return err;

    uint8_t swebOpened = 0;
    err = nvs_get_u8(my_handle, "web_openned", &swebOpened);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
        return err;
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = nvs_set_u8(my_handle, "web_openned", webOpenedCounter);
        if (err != ESP_OK)
            return err;
    } else {
        if (swebOpened != webOpenedCounter) {
            err = nvs_set_u8(my_handle, "web_openned", webOpenedCounter);
            if (err != ESP_OK)
                return err;
        }
    }

    uint8_t simageSetted = 0;
    err = nvs_get_u8(my_handle, "image_setted", &simageSetted);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
        return err;
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = nvs_set_u8(my_handle, "image_setted", imageSetCounter);
        if (err != ESP_OK)
            return err;
    } else {
        if (simageSetted != imageSetCounter) {
            err = nvs_set_u8(my_handle, "image_setted", imageSetCounter);
            if (err != ESP_OK)
                return err;
        }
    }

    uint8_t sprojectSaved = 0;
    err = nvs_get_u8(my_handle, "project_saved", &sprojectSaved);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
        return err;
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = nvs_set_u8(my_handle, "project_saved", projectSaveCounter);
        if (err != ESP_OK)
            return err;
    } else {
        if (sprojectSaved != projectSaveCounter) {
            err = nvs_set_u8(my_handle, "project_saved", projectSaveCounter);
            if (err != ESP_OK)
                return err;
        }
    }

    uint8_t sbrightnessSwitched = 0;
    err = nvs_get_u8(my_handle, "brightness_sw", &sbrightnessSwitched);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
        return err;
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = nvs_set_u8(my_handle, "brightness_sw", brightnessSwitchCounter);
        if (err != ESP_OK)
            return err;
    } else {
        if (sbrightnessSwitched != brightnessSwitchCounter) {
            err = nvs_set_u8(my_handle, "brightness_sw", brightnessSwitchCounter);
            if (err != ESP_OK)
                return err;
        }
    }

    uint8_t sscreenOff = 0;
    err = nvs_get_u8(my_handle, "screen_off", &sscreenOff);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
        return err;
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = nvs_set_u8(my_handle, "screen_off", screenOffCounter);
        if (err != ESP_OK)
            return err;
    } else {
        if (sscreenOff != screenOffCounter) {
            err = nvs_set_u8(my_handle, "screen_off", screenOffCounter);
            if (err != ESP_OK)
                return err;
        }
    }

    uint8_t smodeSwitched = 0;
    err = nvs_get_u8(my_handle, "mode_switched", &smodeSwitched);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
        return err;
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = nvs_set_u8(my_handle, "mode_switched", modeSwitchCounter);
        if (err != ESP_OK)
            return err;
    } else {
        if (smodeSwitched != modeSwitchCounter) {
            err = nvs_set_u8(my_handle, "mode_switched", modeSwitchCounter);
            if (err != ESP_OK)
                return err;
        }
    }

    uint8_t swifiClientConnected = 0;
    err = nvs_get_u8(my_handle, "wifi_client_con", &swifiClientConnected);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
        return err;
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = nvs_set_u8(my_handle, "wifi_client_con", wifiClientConnectCounter);
        if (err != ESP_OK)
            return err;
    } else {
        if (swifiClientConnected != wifiClientConnectCounter) {
            err = nvs_set_u8(my_handle, "wifi_client_con", wifiClientConnectCounter);
            if (err != ESP_OK)
                return err;
        }
    }

    size_t required_size = sizeof(Image);
    SemaphoreHandle_t showFrameSemaphore = getShowFrameSemaphore();
    ESP_LOGI(TAG, "saveToNVS image_to_show required_size: %i", required_size);
    // Never persist an empty picture; the loader would discard it anyway.
    uint8_t frames = getImageToShowCustom()->framesCount;
    if (save_display && frames >= 1 && frames <= IMAGE_MAX_FRAMES && showFrameSemaphore != NULL && xSemaphoreTake(showFrameSemaphore, portMAX_DELAY) == pdTRUE) {
        err = nvs_set_blob(my_handle, "image_to_show", getImageToShowCustom(), required_size);
        // Release before checking: the display task blocks forever on this lock.
        xSemaphoreGive(showFrameSemaphore);
        if (err != ESP_OK)
            return err;
    }

    ESP_LOGI(TAG, "end saveSave to nvs");
    return nvs_commit(my_handle);
}

esp_err_t saveData() {
    nvs_handle_t my_handle;
    ESP_LOGI(TAG, "start saveSave to nvs");
    esp_err_t err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK)
        return err;
    err = save_with_handle(my_handle);
    nvs_close(my_handle);
    return err;
}

void saveToNVS(void *pvParameters) {
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(15000));
        // A failed periodic save is retried on the next pass instead of rebooting.
        esp_err_t err = saveData();
        if (err != ESP_OK)
            ESP_LOGE(TAG, "Saving state to NVS failed: %s", esp_err_to_name(err));
    }
}

static esp_err_t load_with_handle(nvs_handle_t my_handle) {
    esp_err_t err;

    uint8_t swebOpened = 0;
    err = nvs_get_u8(my_handle, "web_openned", &swebOpened);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
        return err;
    webOpenedCounter = swebOpened;

    uint8_t simageSetted = 0;
    err = nvs_get_u8(my_handle, "image_setted", &simageSetted);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
        return err;
    imageSetCounter = simageSetted;

    uint8_t sprojectSaved = 0;
    err = nvs_get_u8(my_handle, "project_saved", &sprojectSaved);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
        return err;
    projectSaveCounter = sprojectSaved;

    uint8_t sbrightnessSwitched = 0;
    err = nvs_get_u8(my_handle, "brightness_sw", &sbrightnessSwitched);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
        return err;
    brightnessSwitchCounter = sbrightnessSwitched;

    uint8_t sscreenOff = 0;
    err = nvs_get_u8(my_handle, "screen_off", &sscreenOff);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
        return err;
    screenOffCounter = sscreenOff;

    uint8_t smodeSwitched = 0;
    err = nvs_get_u8(my_handle, "mode_switched", &smodeSwitched);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
        return err;
    modeSwitchCounter = smodeSwitched;

    uint8_t swifiClientConnected = 0;
    err = nvs_get_u8(my_handle, "wifi_client_con", &swifiClientConnected);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
        return err;
    wifiClientConnectCounter = swifiClientConnected;

    size_t required_size = 0; // value will default to 0, if not set yet in NVS
    err = nvs_get_blob(my_handle, "image_to_show", NULL, &required_size);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
        return err;

    bool image_valid = false;
    if (required_size > 0) {
        Image *image = getImageToShowCustom();
        if (required_size == sizeof(Image)) {
            err = nvs_get_blob(my_handle, "image_to_show", image, &required_size);
        } else {
            ESP_LOGW(TAG, "Unsupported saved image size: %u", (unsigned)required_size);
            err = ESP_ERR_INVALID_SIZE;
        }
        if (err == ESP_OK && image->framesCount >= 1 && image->framesCount <= IMAGE_MAX_FRAMES) {
            if (image->shiftMode > SHIFT_MODE_MAX)
                image->shiftMode = 0;
            image_valid = true;
            ESP_LOGI(TAG, "loadFromNVS image_to_show loaded framesCount: %i", image->framesCount);
        } else {
            // An empty, corrupt or unknown saved picture means "no picture"; it must
            // never stop the badge from booting.
            ESP_LOGW(TAG, "Ignoring saved picture (%s, %u frames)", esp_err_to_name(err), err == ESP_OK ? image->framesCount : 0);
            memset(image, 0, sizeof(*image));
            if (err == ESP_OK || err == ESP_ERR_INVALID_SIZE) {
                nvs_erase_key(my_handle, "image_to_show");
                nvs_commit(my_handle);
            }
        }
    }

    uint8_t settedCustom = 0;
    err = nvs_get_u8(my_handle, "setted_custom", &settedCustom);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
        return err;

    restoreSettedCustom(image_valid ? settedCustom : 0);

    uint8_t showCustom = 0;
    err = nvs_get_u8(my_handle, "show_custom", &showCustom);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
        return err;

    restoreShowCustom(image_valid ? showCustom : 0);

    ESP_LOGI(TAG, "end  loadFromNVS to nvs");
    return ESP_OK;
}

esp_err_t loadFromNVS() {
    nvs_handle_t my_handle;
    ESP_LOGI(TAG, "start loadFromNVS to nvs");
    esp_err_t err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK)
        return err;
    err = load_with_handle(my_handle);
    nvs_close(my_handle);
    return err;
}
