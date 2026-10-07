#pragma once

// LED matrix driver: renders the info screens (clock, weather, timer) about
// ten times a second, with status images (update progress/result, battery)
// temporarily drawn on top.
#include "frame.h"
#include <stdbool.h>
#include <stdint.h>

void display_init(void);   // before any other display call
void display_task(void *); // render loop; pin to its own core

void display_toggle_screen(void); // LEDs off/on; status images still show
void display_set_screen(bool on);
void display_next_screen(void);   // skip to the next screen in the rotation
bool display_screen_on(void);
bool display_night_active(void); // inside the night mode window
const char *display_current_screen(void); // "clock", "weather" or "timer"

// Scrolls text `repeat` times between the screens (below status images).
// Follows the screen switch and night mode like the screens. Returns the duration.
#define NOTIFY_TEXT_MAX 128
uint32_t display_notify(const char *text, Pixel color, int repeat);
bool display_notifying(void);

// Status images stay until end_status_image() or the next status image.
void set_ota_display_image(uint8_t state); // 0 downloading, 1 success, 2 error
void set_power_display_image(uint8_t percent);
void show_update_progress(uint8_t percent);
void end_status_image(void);
bool status_image_active(void);
