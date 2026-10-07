#pragma once

// Pixel types shared by the display driver and the (host-testable) screens.
#include <stdint.h>

#define DISPLAY_WIDTH 10
#define DISPLAY_HEIGHT 10
#define DISPLAY_PIXELS (DISPLAY_WIDTH * DISPLAY_HEIGHT)

// Index y * DISPLAY_WIDTH + x, with (0, 0) at the top left.
typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} Pixel;

// Status images (update progress/result, battery) have at most two frames.
#define IMAGE_MAX_FRAMES 2
#define SHIFT_MODE_STILL 0
#define SHIFT_MODE_SCROLL_DOWN 4

typedef struct {
    Pixel pixels[DISPLAY_PIXELS];
    uint16_t duration;
} Frame;

typedef struct {
    Frame frames[IMAGE_MAX_FRAMES];
    uint8_t shiftMode;
    uint8_t framesCount;
} Image;
