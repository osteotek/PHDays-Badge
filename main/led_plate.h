#pragma once

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"
#include <string.h>

#define IMAGE_MAX_FRAMES 8
// Highest implemented shift mode: 0 still, 1-4 scroll, 5 breathe.
#define SHIFT_MODE_MAX 5

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} Pixel;

typedef struct {
    Pixel pixels[100];
    uint16_t duration;
} Frame;

typedef struct {
    Frame frames[IMAGE_MAX_FRAMES];
    uint8_t shiftMode;
    uint8_t framesCount;
} Image;


void plateUpdateTask(void *pvParameters);

// void updateImageToShow(const Image image);
void set_custom_image(const Image *image);

void shiftBrightness();
void setTurboBrightness();

void switchLeds();

void switchCustom();

void show_number_image(uint64_t number, uint8_t r, uint8_t g, uint8_t b);

uint8_t getSettedCustom();
void restoreSettedCustom(uint8_t state);

uint8_t getShowCustom();
void restoreShowCustom(uint8_t state);

// uint8_t *getImageToShow();
// void restoreImageToShow(const uint8_t pixels[]);

void updateLastActivity();
void switchAutoFade();

void switchLedsShiter();

Image *getImageToShowCustom();
SemaphoreHandle_t getShowFrameSemaphore();
void set_ota_display_image(uint8_t state);
void set_power_display_image(uint8_t percent);
void show_update_progress(uint8_t percent);
void end_status_image(void);
bool status_image_active(void);
