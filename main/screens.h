#pragma once

// Info screens for the 10x10 matrix. Pure functions without ESP-IDF
// dependencies, so tests/screens_test.c can build and preview them on a host.
#include "frame.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum { SCREEN_CLOCK, SCREEN_WEATHER, SCREEN_TIMER } screen_id_t;

typedef struct {
    bool valid;
    int hour;
    int minute;
    int second;
} screen_time_t;

typedef struct {
    bool valid;
    float temperature; // degrees Celsius
    int code;          // WMO weather interpretation code
    bool is_day;
    bool has_range; // today's high and low are known
    float high;
    float low;
} screen_weather_t;

typedef enum { TIMER_IDLE, TIMER_FOCUS, TIMER_BREAK } timer_phase_t;

typedef struct {
    timer_phase_t phase;
    uint32_t remaining_s;
    uint32_t total_s;
} screen_timer_t;

// Hours above minutes, plus a seconds bar in the right column.
void screen_draw_clock(const screen_time_t *time, Pixel out[DISPLAY_PIXELS]);

// First half of the slot: icon at the top left, temperature at the bottom
// right. Second half (when known): today's high above today's low.
void screen_draw_weather(const screen_weather_t *weather, uint32_t slot_elapsed_ms, uint32_t slot_ms, uint32_t now_ms, Pixel out[DISPLAY_PIXELS]);

// Minutes left (seconds in the last minute) over a shrinking 4-row bar.
void screen_draw_timer(const screen_timer_t *timer, uint32_t now_ms, Pixel out[DISPLAY_PIXELS]);

// Matrix-style digital rain from one screen to the next: green drops fall
// down each column, the new screen appears above them and the old one stays
// below. seed varies the column timing. Done after screen_transition_ms().
uint32_t screen_transition_ms(void);
void screen_draw_transition(const Pixel from[DISPLAY_PIXELS], const Pixel to[DISPLAY_PIXELS], uint32_t elapsed_ms, uint32_t seed, Pixel out[DISPLAY_PIXELS]);

// Scrolling text in a 5-row font: Latin and Russian letters (lowercase shown
// as capitals), digits, common punctuation and the degree sign; anything else
// shows as '?'.
#define TEXT_ROW 2         // glyph rows 2-6, vertically centred
#define TEXT_STEP_MS 70    // one column per step
int screen_text_width(const char *text);
void screen_draw_text(const char *text, int x, int y, Pixel color, Pixel out[DISPLAY_PIXELS]);
// One pass enters from the right edge and leaves on the left.
uint32_t screen_scroll_pass_ms(const char *text);
void screen_draw_scroll(const char *text, uint32_t elapsed_ms, Pixel color, Pixel out[DISPLAY_PIXELS]);

// Whether minute-of-day now lies in [start, end), a window that may cross
// midnight (23:00-07:00). An empty window (start == end) is never active.
bool screens_in_window(uint16_t start, uint16_t end, uint16_t now);

// Which screen to show t_ms after the rotation started, each for slot_ms. A
// running timer takes over; with nothing else to show the clock is used.
screen_id_t screens_pick(bool timer_active, bool show_clock, bool show_weather, bool weather_valid, uint32_t slot_ms, uint32_t t_ms, uint32_t *slot_elapsed_ms);
