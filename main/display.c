#include "display.h"

#include "clock_time.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "screens.h"
#include "settings.h"
#include "timer.h"
#include "weather.h"
#include <string.h>

#include "status_images.inc"

#define LED_STRIP_GPIO_PIN CONFIG_LEDS_STRIP
#define LED_POWER_GPIO 26
#define LED_STRIP_RMT_RES_HZ (10 * 1000 * 1000)
#define FRAME_MS 100
#define TRANSITION_FRAME_MS 40 // smoother frames while the rain falls

static const char *TAG = "display";
static led_strip_handle_t led_strip;
static SemaphoreHandle_t lock; // guards the status image state below

static Image status_image;
static bool status_active;
static int64_t status_started_us;

// Notification text, guarded by lock.
static char notify_text[NOTIFY_TEXT_MAX];
static Pixel notify_color;
static int64_t notify_started_us;
static uint32_t notify_duration_ms;
static bool notify_active;

// Touched only by the display task, except for the flags set by buttons.
static volatile bool screen_on = true;
static volatile bool skip_screen;
static volatile screen_id_t current_screen = SCREEN_CLOCK;
static int64_t rotation_started_us;

// Auto-off state: milliseconds since boot (wrapping) of the last activity.
static volatile uint32_t last_activity_ms;
static volatile bool asleep;

// Screen shown in the last frame, and the Matrix rain transition (display task only).
static bool have_previous;
static screen_id_t previous_screen;
static uint32_t previous_slot;
static bool transition_active;
static screen_id_t transition_from;
static uint32_t transition_seed;
static int64_t transition_started_us;

static uint32_t ms_since(int64_t start_us, int64_t now_us) { return (uint32_t)((now_us - start_us) / 1000); }

static void draw_status_locked(int64_t now_us, Pixel *out) {
    uint32_t elapsed = ms_since(status_started_us, now_us);
    const Frame *frame = &status_image.frames[0];
    if (status_image.framesCount > 1) {
        uint32_t duration = status_image.frames[0].duration ? status_image.frames[0].duration : 300;
        frame = &status_image.frames[(elapsed / duration) % status_image.framesCount];
    }
    if (status_image.framesCount == 1 && status_image.shiftMode == SHIFT_MODE_SCROLL_DOWN) {
        int offset = (int)((elapsed / FRAME_MS) % DISPLAY_HEIGHT);
        for (int y = 0; y < DISPLAY_HEIGHT; y++)
            memcpy(&out[((y + offset) % DISPLAY_HEIGHT) * DISPLAY_WIDTH], &frame->pixels[y * DISPLAY_WIDTH], sizeof(Pixel) * DISPLAY_WIDTH);
    } else {
        memcpy(out, frame->pixels, sizeof(Pixel) * DISPLAY_PIXELS);
    }
}

static void draw_screen(screen_id_t id, const screen_timer_t *timer, const screen_weather_t *weather, uint32_t elapsed, uint32_t slot_ms, uint32_t now_ms, Pixel *out) {
    switch (id) {
    case SCREEN_TIMER:
        screen_draw_timer(timer, now_ms, out);
        break;
    case SCREEN_WEATHER:
        screen_draw_weather(weather, elapsed, slot_ms, now_ms, out);
        break;
    case SCREEN_CLOCK: {
        screen_time_t time;
        clock_time_get(&time);
        screen_draw_clock(&time, out);
        break;
    }
    }
}

// Returns true while a transition is running (the caller renders faster).
static bool draw_screens(int64_t now_us, Pixel *out) {
    badge_settings_t settings;
    settings_get(&settings);
    screen_timer_t timer;
    timer_get(&timer);
    weather_data_t weather;
    weather_get(&weather);
    screen_weather_t screen_weather = {
        .valid = weather.valid,
        .temperature = weather.temperature,
        .code = weather.code,
        .is_day = weather.is_day,
        .has_range = weather.has_range,
        .high = weather.high,
        .low = weather.low,
    };

    uint32_t slot_ms = settings.screen_seconds * 1000U;
    uint32_t t = ms_since(rotation_started_us, now_us);
    if (skip_screen) {
        skip_screen = false;
        rotation_started_us -= (int64_t)(slot_ms - t % slot_ms) * 1000;
        t = ms_since(rotation_started_us, now_us);
    }
    uint32_t elapsed;
    screen_id_t id = screens_pick(timer.phase != TIMER_IDLE, settings.show_clock, settings.show_weather, screen_weather.valid, slot_ms, t, &elapsed);
    current_screen = id;
    uint32_t now_ms = (uint32_t)(now_us / 1000);
    uint32_t slot = t / slot_ms;

    // Rain at every screen change and every rotation step (so a single enabled
    // screen still gets it), but not while a running timer counts down.
    bool changed = have_previous && (id != previous_screen || (slot != previous_slot && id != SCREEN_TIMER));
    if (changed && settings.transitions) {
        transition_active = true;
        transition_from = previous_screen;
        transition_seed = esp_random();
        transition_started_us = now_us;
    }
    have_previous = true;
    previous_screen = id;
    previous_slot = slot;

    draw_screen(id, &timer, &screen_weather, elapsed, slot_ms, now_ms, out);
    uint32_t transition_elapsed = ms_since(transition_started_us, now_us);
    if (transition_active && transition_elapsed < screen_transition_ms()) {
        static Pixel from[DISPLAY_PIXELS], to[DISPLAY_PIXELS];
        memcpy(to, out, sizeof(to));
        // The outgoing screen as it ended its slot.
        draw_screen(transition_from, &timer, &screen_weather, slot_ms - 1, slot_ms, now_ms, from);
        screen_draw_transition(from, to, transition_elapsed, transition_seed, out);
        return true;
    }
    transition_active = false;
    return false;
}

static volatile bool night_active;

// Night mode brightness inside its window (once the clock is set), else the
// normal brightness.
static int display_brightness(void) {
    badge_settings_t settings;
    settings_get(&settings);
    screen_time_t time;
    night_active = settings.night_mode && clock_time_get(&time) &&
                   screens_in_window(settings.night_start, settings.night_end, (uint16_t)(time.hour * 60 + time.minute));
    return night_active ? settings.night_brightness : settings.brightness;
}

bool display_night_active(void) { return night_active; }

static uint32_t uptime_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

bool display_wake(void) {
    bool was_asleep = asleep;
    last_activity_ms = uptime_ms();
    asleep = false;
    return was_asleep;
}

bool display_asleep(void) { return asleep; }

static void update_auto_off(void) {
    badge_settings_t settings;
    settings_get(&settings);
    screen_timer_t timer;
    timer_get(&timer);
    uint32_t now = uptime_ms();
    if (timer.phase != TIMER_IDLE || !screen_on) // the idle time starts when the timer ends or the screen comes on
        last_activity_ms = now;
    asleep = settings.auto_off_minutes && now - last_activity_ms >= settings.auto_off_minutes * 60000u;
}

// The 100 WS2812s draw current even when black, so their supply (GPIO 26) is
// switched off while the screen is dark and the frame resent once powered.
static bool strip_powered = true;

static void set_strip_power(bool on) {
    if (on == strip_powered)
        return;
    gpio_set_level(LED_POWER_GPIO, on);
    strip_powered = on;
    if (on)
        vTaskDelay(pdMS_TO_TICKS(2)); // let the LEDs start before sending data
}

// Sends the frame only when it changed: every refresh is a chance for Wi-Fi
// interrupts to corrupt the WS2812 bit stream, and most frames repeat.
static void push(const Pixel *pixels, bool on, int brightness) {
    static Pixel shown[DISPLAY_PIXELS];
    static bool have_shown;
    if (!on) {
        set_strip_power(false);
        have_shown = false; // the LEDs lose their colours without power
        return;
    }
    set_strip_power(true);
    Pixel next[DISPLAY_PIXELS];
    for (int i = 0; i < DISPLAY_PIXELS; i++) {
        Pixel p = pixels[i];
        next[i] = (Pixel){(uint8_t)(p.r * brightness / 100), (uint8_t)(p.g * brightness / 100), (uint8_t)(p.b * brightness / 100)};
    }
    if (have_shown && memcmp(next, shown, sizeof(next)) == 0)
        return;
    for (int i = 0; i < DISPLAY_PIXELS; i++) // the strip is wired from the last pixel
        led_strip_set_pixel(led_strip, DISPLAY_PIXELS - 1 - i, next[i].r, next[i].g, next[i].b);
    led_strip_refresh(led_strip);
    memcpy(shown, next, sizeof(next));
    have_shown = true;
}

// Called from the display task so the RMT interrupt runs on its core (1), not
// on core 0 next to Wi-Fi, which delays refills and garbles pixels.
static void init_strip(void) {
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_STRIP_GPIO_PIN,
        .max_leds = DISPLAY_PIXELS,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
    };
    led_strip_rmt_config_t rmt_config = {.clk_src = RMT_CLK_SRC_DEFAULT, .resolution_hz = LED_STRIP_RMT_RES_HZ, .mem_block_symbols = 64};
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    ESP_ERROR_CHECK(led_strip_clear(led_strip));
}

void display_init(void) {
    lock = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(gpio_set_direction(LED_POWER_GPIO, GPIO_MODE_OUTPUT));
    ESP_ERROR_CHECK(gpio_set_level(LED_POWER_GPIO, 1));
    rotation_started_us = esp_timer_get_time();
}

void display_task(void *arg) {
    static Pixel frame[DISPLAY_PIXELS];
    init_strip();
    ESP_LOGI(TAG, "Display running on core %d", xPortGetCoreID());
    for (;;) {
        int64_t now_us = esp_timer_get_time();
        xSemaphoreTake(lock, portMAX_DELAY);
        bool status = status_active;
        if (status)
            draw_status_locked(now_us, frame);
        if (notify_active && ms_since(notify_started_us, now_us) >= notify_duration_ms)
            notify_active = false;
        bool notifying = !status && notify_active;
        if (notifying)
            screen_draw_scroll(notify_text, ms_since(notify_started_us, now_us), notify_color, frame);
        xSemaphoreGive(lock);
        // Screens keep rotating underneath, so the rain resumes cleanly afterwards.
        static Pixel hidden[DISPLAY_PIXELS];
        bool animating = !status && draw_screens(now_us, notifying ? hidden : frame);
        animating = animating || notifying;
        int brightness = display_brightness();
        update_auto_off();
        // Status images show even with the screen off, asleep or dark for the night.
        push(frame, status || (screen_on && !asleep && brightness > 0), status && brightness < 1 ? 1 : brightness);
        vTaskDelay(pdMS_TO_TICKS(animating ? TRANSITION_FRAME_MS : FRAME_MS));
    }
}

void display_toggle_screen(void) {
    if (!display_wake())
        screen_on = !screen_on;
}

void display_set_screen(bool on) {
    display_wake();
    screen_on = on;
}

void display_next_screen(void) { skip_screen = true; }

bool display_screen_on(void) { return screen_on; }

const char *display_current_screen(void) {
    switch (current_screen) {
    case SCREEN_WEATHER:
        return "weather";
    case SCREEN_TIMER:
        return "timer";
    default:
        return "clock";
    }
}

static void show_status_image(const Image *image, uint8_t shift_mode) {
    xSemaphoreTake(lock, portMAX_DELAY);
    status_image = *image;
    status_image.shiftMode = shift_mode;
    if (!status_active)
        status_started_us = esp_timer_get_time();
    status_active = true;
    xSemaphoreGive(lock);
}

bool status_image_active(void) { return status_active; }

uint32_t display_notify(const char *text, Pixel color, int repeat) {
    xSemaphoreTake(lock, portMAX_DELAY);
    strlcpy(notify_text, text, sizeof(notify_text));
    notify_color = color;
    notify_started_us = esp_timer_get_time();
    notify_duration_ms = screen_scroll_pass_ms(notify_text) * (uint32_t)repeat;
    notify_active = true;
    uint32_t duration = notify_duration_ms;
    xSemaphoreGive(lock);
    display_wake();
    return duration;
}

bool display_notifying(void) { return notify_active; }

void end_status_image(void) {
    xSemaphoreTake(lock, portMAX_DELAY);
    status_active = false;
    xSemaphoreGive(lock);
}

void set_ota_display_image(uint8_t state) {
    switch (state) {
    case 0:
        show_status_image(&loadImage, SHIFT_MODE_SCROLL_DOWN);
        break;
    case 1:
        show_status_image(&successImage, SHIFT_MODE_STILL);
        break;
    case 2:
        show_status_image(&errorImage, SHIFT_MODE_STILL);
        break;
    }
}

// Fills the screen one pixel per percent, with a blinking leading pixel.
void show_update_progress(uint8_t percent) {
    static Image progress;
    uint8_t lit = percent > DISPLAY_PIXELS ? DISPLAY_PIXELS : percent;
    progress.framesCount = 2;
    for (int f = 0; f < 2; f++) {
        progress.frames[f].duration = 300;
        for (int i = 0; i < DISPLAY_PIXELS; i++)
            progress.frames[f].pixels[i] = i < lit ? (Pixel){0, 255, 0} : (Pixel){0, 0, 0};
    }
    if (lit < DISPLAY_PIXELS)
        progress.frames[0].pixels[lit] = (Pixel){255, 255, 255};
    show_status_image(&progress, SHIFT_MODE_STILL);
}

void set_power_display_image(uint8_t percent) {
    const Image *image = percent <= 30 ? &lowBattery : percent <= 60 ? &mediumBattery : percent <= 90 ? &greenBattery : &fullBattery;
    show_status_image(image, SHIFT_MODE_STILL);
}
