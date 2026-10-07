#include "led_plate.h"
#include "esp_log.h"
#include "led_plate.inc"

#define LED_STRIP_GPIO_PIN CONFIG_LEDS_STRIP
#define LED_STRIP_LED_COUNT 100
#define LED_STRIP_RMT_RES_HZ (10 * 1000 * 1000)

static const char *TAG = "led plate";

uint8_t currentImage = 0;
// uint8_t currentFrame = 0;

// Pixel frameToShow[100] = {0};
// Pixel frameDisplayed[100] = {0};

Pixel frameBuffer[LED_STRIP_LED_COUNT];
Pixel frameBufferDisplayed[LED_STRIP_LED_COUNT];

Image imageToShowBase;
Image imageToShow;
Image imageToTmp;

bool showCustom = false;
bool settedCustom = false;
bool ledsOn = true;

uint8_t currentBrightness = 10;
uint8_t lastBrightness = 0;
uint8_t beforeFadeBrightness = 0;
const uint8_t minBrightness = 1;
const uint8_t maxBrightness = 15;
const uint8_t turboBrightness = 15;
const uint8_t fadeBrightness = 4;
const uint8_t brightnessStep = 1;

unsigned long lastActivity = 0;
bool autoFadeLeds = true;
bool autoFaded = false;

static uint8_t step = 0;
const uint8_t steps = 20;

// Todo: maybe add global shifter state for galery cicle?
//  uint8_t ledShifterState = 0;

int lastActivitySecondsDelta = 600;
// Todo: move to images array!
// uint32_t oneImageShowMilles = 6000;

static SemaphoreHandle_t xSemaphore = NULL;

// Status images (update progress/result, battery level, hotspot name/password)
// temporarily replace the display. The user's picture, mode and screen state are
// stashed once and restored by end_status_image(); saveData() skips the display
// state meanwhile, and user actions during a status image apply to the stash.
static bool status_active, saved_show_custom, saved_setted_custom, saved_leds_on;

// Brightness offset of the breathing effect. It never changes currentBrightness,
// so interrupting the effect cannot shift the user's brightness setting.
static int8_t breathOffset = 0;

static bool lock_display(void) { return xSemaphore != NULL && xSemaphoreTake(xSemaphore, portMAX_DELAY) == pdTRUE; }

static void unlock_display(void) { xSemaphoreGive(xSemaphore); }

static void show_status_image(const Image *image, uint8_t shift_mode);

// Waits at least one tick: shorter frame durations would otherwise busy-loop the
// display task and starve lower-priority tasks on its core.
static void frameDelay(uint32_t ms) {
    TickType_t ticks = pdMS_TO_TICKS(ms);
    vTaskDelay(ticks ? ticks : 1);
}

static led_strip_handle_t led_strip;

led_strip_handle_t configure_led(void) {

    led_strip_config_t strip_config = {.strip_gpio_num = LED_STRIP_GPIO_PIN,
                                       .max_leds = LED_STRIP_LED_COUNT,
                                       .led_model = LED_MODEL_WS2812,
                                       .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
                                       .flags = {
                                           .invert_out = false,
                                       }};

    led_strip_rmt_config_t rmt_config = {.clk_src = RMT_CLK_SRC_DEFAULT,
                                         .resolution_hz = LED_STRIP_RMT_RES_HZ,
                                         .mem_block_symbols = 64,
                                         .flags = {
                                             .with_dma = false, // DMA feature is available on chips like ESP32-S3/P4
                                         }};

    led_strip_handle_t led_strip_;
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip_));
    ESP_LOGI(TAG, "Created LED strip object with RMT backend");
    ESP_ERROR_CHECK(led_strip_clear(led_strip_));
    return led_strip_;
}

void showFrame(const Pixel pixels[]) {
    int brightness = currentBrightness + breathOffset;
    if (brightness < minBrightness)
        brightness = minBrightness;
    if (xSemaphore != NULL && xSemaphoreTake(xSemaphore, portMAX_DELAY) == pdTRUE) {
        for (size_t leds = 0; leds < LED_STRIP_LED_COUNT; leds++) {
            ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, LED_STRIP_LED_COUNT - 1 - leds, pixels[leds].r * brightness / 100, pixels[leds].g * brightness / 100,
                                                pixels[leds].b * brightness / 100));
        }
        ESP_ERROR_CHECK(led_strip_refresh(led_strip));
        xSemaphoreGive(xSemaphore);
    }
}

void showBlackFrame() {
    if (xSemaphore != NULL && xSemaphoreTake(xSemaphore, portMAX_DELAY) == pdTRUE) {
        for (size_t leds = 0; leds < LED_STRIP_LED_COUNT; leds++) {
            ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, leds, 0, 0, 0));
        }
        ESP_ERROR_CHECK(led_strip_refresh(led_strip));
        xSemaphoreGive(xSemaphore);
    }
}

void setAutoFade() {
    if (autoFadeLeds && lastActivity > 0 && ledsOn) {
        ESP_LOGI(TAG, "Fade last activity check");
        if (lastActivity + lastActivitySecondsDelta < pdTICKS_TO_MS(xTaskGetTickCount()) / 1000) {
            ESP_LOGW(TAG, "Fade last activity in past %lu for %lu", lastActivity, pdTICKS_TO_MS(xTaskGetTickCount()) / 1000);
            if (!autoFaded) {
                beforeFadeBrightness = currentBrightness;
                currentBrightness = fadeBrightness;
                autoFaded = true;
            }
        } else {
            ESP_LOGW(TAG, "Fade last activity not in past %lu for %lu", lastActivity, pdTICKS_TO_MS(xTaskGetTickCount()) / 1000);
            if (autoFaded) {
                currentBrightness = beforeFadeBrightness;
                autoFaded = false;
            }
        }
    }
}

void updateLastActivity() {
    // ESP_LOGI(TAG, "Update last activity %lu", lastActivity);
    lastActivity = pdTICKS_TO_MS(xTaskGetTickCount()) / 1000;
    // ESP_LOGI(TAG, "beforefade bight  `%u`  curr bigth `%u`", beforeFadeBrightness, currentBrightness);
}

void setframeBufferDisplayed(const Pixel pixels[]) {
    for (size_t i = 0; i < LED_STRIP_LED_COUNT; i++) {
        frameBufferDisplayed[i] = pixels[i];
    }
}

void copyBuffers() {
    for (size_t i = 0; i < LED_STRIP_LED_COUNT; i++) {
        frameBufferDisplayed[i] = frameBuffer[i];
    }
}

void shiftLedsDown() {
    for (size_t i = 0; i < LED_STRIP_LED_COUNT; i++) {
        if (i < 90) {
            frameBuffer[i + 10] = frameBufferDisplayed[i];
        } else {
            frameBuffer[i - 90] = frameBufferDisplayed[i];
        }
    }
}

void shiftLedsUp() {
    for (size_t i = 0; i < LED_STRIP_LED_COUNT; i++) {
        if (i < 10) {
            frameBuffer[i + 90] = frameBufferDisplayed[i];
        } else {
            frameBuffer[i - 10] = frameBufferDisplayed[i];
        }
    }
}

void shiftLedsRight() {
    for (size_t i = 0; i < LED_STRIP_LED_COUNT; i++) {
        if (i % 10 == 9) {
            frameBuffer[i - 9] = frameBufferDisplayed[i];
        } else {
            frameBuffer[i + 1] = frameBufferDisplayed[i];
        }
    }
}

void shiftLedsLeft() {
    for (size_t i = 0; i < LED_STRIP_LED_COUNT; i++) {
        if (i % 10 == 0) {
            frameBuffer[i + 9] = frameBufferDisplayed[i];
        } else {
            frameBuffer[i - 1] = frameBufferDisplayed[i];
        }
    }
}

void breathEffect() {
    // Dim by one step per step for steps 1-9, then brighten back to 0 by step 18.
    breathOffset = step < 10 ? -(int8_t)step : (step < 19 ? (int8_t)step - 18 : 0);

    for (size_t i = 0; i < LED_STRIP_LED_COUNT; i++) {
        frameBuffer[i] = frameBufferDisplayed[i];
    }
}

Image *getImage() {
    if (showCustom)
        return &imageToShow;
    else
        return &imageToShowBase;
}

void shiftBaseLoopStep(void shift()) {
    if (step == 0) {
        setframeBufferDisplayed((*getImage()).frames[0].pixels);
    } else {
        shift();
        copyBuffers();
    }
    showFrame(frameBufferDisplayed);
}

void initPlate() {
    ESP_LOGI(TAG, "Init Plate");
    led_strip = configure_led();
    imageToShowBase = baseImages[currentImage];
    ESP_LOGI(TAG, "Finish Init Plate");
}

void tickNextBaseImage() {
    if (showCustom)
        return;

    ESP_LOGI(TAG, "Tick next base image");
    if (currentImage < baseImagesSize - 1) {
        currentImage++;
    } else {
        currentImage = 0;
    }

    imageToShowBase = baseImages[currentImage];
}

void processImage() {
    ESP_LOGI(TAG, "processImage step: %i", step);
    step = 0;
    if ((*getImage()).framesCount == 0) {
        frameDelay(100); // nothing valid to draw; never spin
    } else if ((*getImage()).framesCount == 1) {
        while (step < steps) {
            breathOffset = 0;
            if (!ledsOn) {
                step++;
                frameDelay((*getImage()).frames[0].duration / steps);
                continue;
            }

            switch ((*getImage()).shiftMode) {
            case 0:
                showFrame((*getImage()).frames[0].pixels);
                break;
            case 1:
                shiftBaseLoopStep(shiftLedsRight);
                break;
            case 2:
                shiftBaseLoopStep(shiftLedsLeft);
                break;
            case 3:
                shiftBaseLoopStep(shiftLedsUp);
                break;
            case 4:
                shiftBaseLoopStep(shiftLedsDown);
                break;
            case 5:
                shiftBaseLoopStep(breathEffect);
                break;
            case 6:
                // Todo: wave
                break;
            case 7:
                // Todo: rotation right
                break;
            case 8:
                // Todo: rotation left
                break;
            default:
                showFrame((*getImage()).frames[0].pixels);
                break;
            }

            step++;
            frameDelay((*getImage()).frames[0].duration / steps);
        }

    } else if ((*getImage()).framesCount > 1) {
        for (size_t i = 0; i < (*getImage()).framesCount; i++) {
            if (ledsOn) {
                showFrame((*getImage()).frames[i].pixels);
            }
            frameDelay((*getImage()).frames[i].duration);
        }
    }
}

void plateUpdateTask(void *pvParameters) {
    ESP_ERROR_CHECK(gpio_set_direction(26, GPIO_MODE_OUTPUT));
    ESP_ERROR_CHECK(gpio_set_level(26, 1));
    initPlate();
    xSemaphore = xSemaphoreCreateBinary();
    xSemaphoreGive(xSemaphore); // binary semaphores start taken
    ESP_LOGI(TAG, "Show plate");
    while (1) {
        tickNextBaseImage();
        processImage();
    }
}

// Installs an uploaded picture and switches to it. During a status image it
// goes to the stash, so ending the status image shows it instead of losing it.
void set_custom_image(const Image *image) {
    bool locked = lock_display();
    if (status_active) {
        imageToTmp = *image;
        saved_show_custom = saved_setted_custom = true;
    } else {
        imageToShow = *image;
        showCustom = settedCustom = true;
        step = 0;
    }
    if (locked)
        unlock_display();
}

Image *getImageToShowCustom() { return &imageToShow; }

SemaphoreHandle_t getShowFrameSemaphore() { return xSemaphore; }

void shiftBrightness() {
    updateLastActivity();
    if (ledsOn) {
        ESP_LOGI(TAG, "Shifting led Brightness");
        if (autoFaded) {
            currentBrightness = beforeFadeBrightness;
            autoFaded = false;
        }

        if (currentBrightness >= maxBrightness) {
            currentBrightness = minBrightness;
        } else {
            currentBrightness += brightnessStep;
        }
    }
}

void setTurboBrightness() {
    updateLastActivity();
    if (ledsOn) {
        ESP_LOGI(TAG, "Shifting led Brightness to Turbo");
        currentBrightness = turboBrightness;
    }
}

void switchLeds() {
    updateLastActivity();
    if (status_active) {
        saved_leds_on = !saved_leds_on;
        return;
    }
    if (ledsOn) {
        ledsOn = false;
        showBlackFrame();
    } else {
        ledsOn = true;
    }
}

void switchCustom() {
    updateLastActivity();
    if (status_active) {
        if (saved_setted_custom)
            saved_show_custom = !saved_show_custom;
        return;
    }
    if (settedCustom) {
        showCustom = !showCustom;
    }
}

// Shows a number as bars: row k holds the k-th digit (most significant first),
// with as many lit pixels as the digit's value. Up to 10 digits.
void show_number_image(uint64_t number, uint8_t r, uint8_t g, uint8_t b) {
    static Image digits;
    uint8_t value[10];
    int count = 0;
    updateLastActivity();
    for (; number != 0 && count < 10; number /= 10)
        value[count++] = number % 10;
    memset(&digits, 0, sizeof(digits));
    digits.framesCount = 1;
    digits.frames[0].duration = 1000;
    for (int row = 0; row < count; row++)
        for (int column = 0; column < value[count - 1 - row]; column++)
            digits.frames[0].pixels[row * 10 + column] = (Pixel){r, g, b};
    show_status_image(&digits, 0);
}

// Callers of the *_locked helpers hold the display lock (when it exists yet).
static void begin_status_locked(void) {
    if (status_active)
        return;
    imageToTmp = imageToShow;
    saved_show_custom = showCustom;
    saved_setted_custom = settedCustom;
    saved_leds_on = ledsOn;
    status_active = true;
}

static void show_status_image(const Image *image, uint8_t shift_mode) {
    bool locked = lock_display();
    begin_status_locked();
    imageToShow = *image;
    imageToShow.shiftMode = shift_mode;
    showCustom = true;
    ledsOn = true;
    step = 0;
    if (locked)
        unlock_display();
}

bool status_image_active(void) { return status_active; }

void end_status_image(void) {
    bool locked = lock_display();
    bool screen_off = false;
    if (status_active) {
        imageToShow = imageToTmp;
        showCustom = saved_show_custom;
        settedCustom = saved_setted_custom;
        ledsOn = saved_leds_on;
        screen_off = !ledsOn;
        step = 0;
        status_active = false;
    }
    if (locked)
        unlock_display();
    if (screen_off)
        showBlackFrame();
}

void set_ota_display_image(uint8_t state) {
    switch (state) {
    case 0:
        show_status_image(&loadImage, 4); // scroll the download arrow down
        break;
    case 1:
        show_status_image(&successImage, successImage.shiftMode);
        break;
    case 2:
        show_status_image(&errorImage, errorImage.shiftMode);
        break;
    }
}

// Fills the 10x10 screen one pixel per percent, with a blinking leading pixel.
void show_update_progress(uint8_t percent) {
    static Image progress;
    uint8_t lit = percent > LED_STRIP_LED_COUNT ? LED_STRIP_LED_COUNT : percent;
    progress.framesCount = 2;
    progress.shiftMode = 0;
    for (size_t f = 0; f < progress.framesCount; f++) {
        progress.frames[f].duration = 300;
        for (size_t i = 0; i < LED_STRIP_LED_COUNT; i++)
            progress.frames[f].pixels[i] = i < lit ? (Pixel){0, 255, 0} : (Pixel){0, 0, 0};
    }
    if (lit < LED_STRIP_LED_COUNT)
        progress.frames[0].pixels[lit] = (Pixel){255, 255, 255};
    show_status_image(&progress, 0);
}

uint8_t getSettedCustom() {
    if (settedCustom)
        return 1;
    else
        return 0;
}

void restoreSettedCustom(uint8_t state) {
    if (state == 0)
        settedCustom = false;
    else
        settedCustom = true;
}

uint8_t getShowCustom() {
    if (showCustom)
        return 1;
    else
        return 0;
}

/* !!! Call after restoreImageToShow !!!*/
void restoreShowCustom(uint8_t state) {
    if (state == 0)
        showCustom = false;
    else
        showCustom = true;

    step = 0;
}

void switchAutoFade() { autoFadeLeds = !autoFadeLeds; }

void switchLedsShiter() {
    // Cycle through the implemented modes only (0 still, 1-4 scroll, 5 breathe).
    Image *image = status_active ? &imageToTmp : &imageToShow;
    image->shiftMode = image->shiftMode >= SHIFT_MODE_MAX ? 0 : image->shiftMode + 1;
    step = 0;
    ESP_LOGI(TAG, "shiftMode: %i", image->shiftMode);
}

void set_power_display_image(uint8_t percent) {
    const Image *image = percent <= 30 ? &lowBattery : percent <= 60 ? &mediumBattery : percent <= 90 ? &greenBattery : &fullBattery;
    show_status_image(image, image->shiftMode);
}
