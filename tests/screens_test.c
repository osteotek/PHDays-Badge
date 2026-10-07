// Host test and preview for main/screens.c.
//   cc -std=c11 -Wall -Wextra -Imain tests/screens_test.c main/screens.c -lm -o /tmp/screens_test && /tmp/screens_test
// Pass --preview to print every screen as ASCII art.
#include "screens.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond)                                                                                                                                  \
    do {                                                                                                                                             \
        if (!(cond)) {                                                                                                                               \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                                                   \
            failures++;                                                                                                                              \
        }                                                                                                                                            \
    } while (0)

static bool lit(const Pixel *out, int x, int y) {
    Pixel p = out[y * DISPLAY_WIDTH + x];
    return p.r || p.g || p.b;
}

static int count_lit(const Pixel *out, int y0, int y1) {
    int n = 0;
    for (int y = y0; y <= y1; y++)
        for (int x = 0; x < DISPLAY_WIDTH; x++)
            n += lit(out, x, y);
    return n;
}

// One character per pixel by dominant colour, so layouts can be eyeballed.
static char shade(Pixel p) {
    if (!p.r && !p.g && !p.b)
        return '.';
    if (p.r > 200 && p.g > 200 && p.b > 200)
        return 'W';
    if (p.r > 200 && p.g > 180 && p.b < 120)
        return 'Y';
    if (p.r >= p.g && p.r >= p.b)
        return p.g > 90 ? 'O' : 'R';
    if (p.g >= p.b)
        return 'G';
    return p.r > 100 ? 'L' : 'B';
}

static void print_screen(const char *title, const Pixel *out) {
    printf("%s\n", title);
    for (int y = 0; y < DISPLAY_HEIGHT; y++) {
        printf("  ");
        for (int x = 0; x < DISPLAY_WIDTH; x++)
            putchar(shade(out[y * DISPLAY_WIDTH + x]));
        putchar('\n');
    }
}

int main(int argc, char **argv) {
    bool preview = argc > 1 && strcmp(argv[1], "--preview") == 0;
    Pixel out[DISPLAY_PIXELS];

    // Clock: "23:47:30" -> hours top, minutes bottom, 5 seconds pixels.
    screen_time_t time = {.valid = true, .hour = 23, .minute = 47, .second = 30};
    screen_draw_clock(&time, out);
    CHECK(lit(out, 1, 0) && lit(out, 3, 0) && !lit(out, 1, 1)); // "2" top row full, then right side only
    CHECK(lit(out, 1, 5) && !lit(out, 2, 5) && lit(out, 3, 5)); // "4" top row: X.X
    int seconds = 0;
    for (int y = 0; y < DISPLAY_HEIGHT; y++)
        seconds += lit(out, 9, y);
    CHECK(seconds == 5 && lit(out, 9, 9) && !lit(out, 9, 4));
    if (preview)
        print_screen("clock 23:47:30", out);

    screen_time_t unknown = {.valid = false};
    screen_draw_clock(&unknown, out);
    CHECK(count_lit(out, 0, 9) == 12 && lit(out, 1, 2) && !lit(out, 4, 2));
    if (preview)
        print_screen("clock, no time yet", out);

    // Weather: icon at the top left (rows 0-4), temperature right-aligned in rows 5-9.
    struct {
        float celsius;
        int min_x;
        bool degree;
    } temps[] = {{7.4f, 4, true}, {-0.4f, 4, true}, {23.6f, 0, true}, {-5.0f, 1, true}, {-12.0f, 0, false}, {150.0f, 0, true}};
    for (size_t i = 0; i < sizeof(temps) / sizeof(temps[0]); i++) {
        screen_weather_t w = {.valid = true, .temperature = temps[i].celsius, .code = 0, .is_day = true};
        screen_draw_weather(&w, 0, 10000, 0, out);
        int min_x = DISPLAY_WIDTH, max_x = -1;
        for (int y = 5; y < DISPLAY_HEIGHT; y++)
            for (int x = 0; x < DISPLAY_WIDTH; x++)
                if (lit(out, x, y)) {
                    min_x = x < min_x ? x : min_x;
                    max_x = x > max_x ? x : max_x;
                }
        CHECK(min_x == temps[i].min_x && max_x == 9);
        // The 2x2 degree sign: lit at rows 5-6 of columns 8-9, empty below.
        CHECK((lit(out, 8, 5) && lit(out, 9, 6) && !lit(out, 8, 7) && !lit(out, 9, 7)) == temps[i].degree);
        CHECK(lit(out, 1, 1) && lit(out, 5, 3)); // the sun's disc at the top left
        for (int y = 0; y < 5; y++)
            for (int x = 7; x < DISPLAY_WIDTH; x++)
                CHECK(!lit(out, x, y)); // nothing right of the icon
        if (preview) {
            char title[40];
            snprintf(title, sizeof(title), "weather: sun, %.1f C", temps[i].celsius);
            print_screen(title, out);
        }
    }
    screen_weather_t missing = {.valid = false};
    screen_draw_weather(&missing, 0, 10000, 0, out);
    CHECK(count_lit(out, 0, 9) == 6);

    // Icons for each WMO group stay in the top-left 7x5.
    struct {
        int code;
        bool is_day;
        const char *name;
    } icons[] = {{0, true, "clear day"}, {0, false, "clear night"}, {2, true, "partly cloudy day"}, {1, false, "partly cloudy night"}, {3, true, "overcast"},
                 {45, true, "fog"},         {63, true, "rain"},             {73, true, "snow"},                 {95, true, "thunderstorm"}};
    for (size_t i = 0; i < sizeof(icons) / sizeof(icons[0]); i++) {
        screen_weather_t w = {.valid = true, .temperature = -12, .code = icons[i].code, .is_day = icons[i].is_day};
        screen_draw_weather(&w, 0, 10000, 0, out);
        int icon = 0;
        for (int y = 0; y < 5; y++)
            for (int x = 0; x < 7; x++)
                icon += lit(out, x, y);
        CHECK(icon > 6 && count_lit(out, 5, 9) == 21); // "-12": minus 2 + "1" 8 + "2" 11 pixels
        // Every icon moves slightly within a few seconds and stays above the temperature.
        Pixel moved[DISPLAY_PIXELS];
        bool animates = false;
        for (uint32_t ms = 100; ms < 3000 && !animates; ms += 100) {
            screen_draw_weather(&w, 0, 10000, ms, moved);
            animates = memcmp(out, moved, sizeof(out)) != 0;
            CHECK(count_lit(moved, 5, 9) == 21);
        }
        CHECK(animates);
        if (preview) {
            char title[40];
            snprintf(title, sizeof(title), "weather: %s, -12 C", icons[i].name);
            print_screen(title, out);
        }
    }
    screen_weather_t rain = {.valid = true, .temperature = 4, .code = 63, .is_day = true};
    Pixel later[DISPLAY_PIXELS];
    screen_draw_weather(&rain, 0, 10000, 0, out);
    screen_draw_weather(&rain, 0, 10000, 200, later);
    CHECK(memcmp(out, later, sizeof(out)) != 0); // drops move
    screen_weather_t sunny = {.valid = true, .temperature = 18, .code = 0, .is_day = true};

    // Timer: 24:10 left of 25:00 focus -> "25", nearly full bar.
    screen_timer_t focus = {.phase = TIMER_FOCUS, .remaining_s = 24 * 60 + 10, .total_s = 25 * 60};
    screen_draw_timer(&focus, 0, out);
    CHECK(count_lit(out, 6, 9) == 39 && count_lit(out, 5, 5) == 0);
    CHECK(out[0 * DISPLAY_WIDTH + 1].r == 255 && out[0 * DISPLAY_WIDTH + 1].g == 50);
    if (preview)
        print_screen("timer focus 24:10 of 25:00", out);
    screen_timer_t pause = {.phase = TIMER_BREAK, .remaining_s = 45, .total_s = 300};
    screen_draw_timer(&pause, 0, out);
    CHECK(count_lit(out, 6, 9) == 6 && out[0 * DISPLAY_WIDTH + 1].g == 230);
    if (preview)
        print_screen("timer break 0:45 of 5:00", out);
    screen_timer_t ending = {.phase = TIMER_FOCUS, .remaining_s = 5, .total_s = 1500};
    screen_draw_timer(&ending, 600, out);
    CHECK(count_lit(out, 0, 4) == 0 && count_lit(out, 6, 9) == 1); // blink off phase
    screen_draw_timer(&ending, 0, out);
    CHECK(count_lit(out, 0, 4) > 0);

    // Today's range: second half of the slot, high (13.5 -> 14) above low (-6).
    screen_weather_t today = {.valid = true, .temperature = 7, .code = 0, .is_day = true, .has_range = true, .high = 13.5f, .low = -6.1f};
    screen_draw_weather(&today, 6000, 10000, 0, out);
    CHECK(!lit(out, 0, 0) && count_lit(out, 0, 4) > 0 && count_lit(out, 5, 9) > 0); // no icon, two lines
    CHECK(lit(out, 8, 0) && lit(out, 9, 1) && !lit(out, 8, 2));                        // degree sign on the high
    Pixel high_color = out[0 * DISPLAY_WIDTH + 1], low_color = out[7 * DISPLAY_WIDTH + 2];
    CHECK(high_color.r == 255 && high_color.g == 230); // 14: yellow
    CHECK(low_color.b == 255 && low_color.r == 150);   // -6: light blue
    if (preview)
        print_screen("weather today: high 14, low -6", out);
    screen_draw_weather(&today, 4000, 10000, 0, out);
    CHECK(lit(out, 1, 1)); // first half: the sun again
    today.has_range = false;
    screen_draw_weather(&today, 6000, 10000, 0, out);
    CHECK(lit(out, 1, 1)); // without a forecast the "now" page stays

    // Matrix rain transition from the clock to the weather icon.
    Pixel from[DISPLAY_PIXELS], to[DISPLAY_PIXELS];
    screen_draw_clock(&time, from);
    screen_draw_weather(&sunny, 0, 10000, 0, to);
    screen_draw_transition(from, to, 0, 7, out);
    int changed = 0;
    for (int i = 0; i < DISPLAY_PIXELS; i++)
        changed += memcmp(&out[i], &from[i], sizeof(Pixel)) != 0;
    CHECK(changed <= DISPLAY_WIDTH); // at most the first row of drops has started
    screen_draw_transition(from, to, screen_transition_ms(), 7, out);
    CHECK(memcmp(out, to, sizeof(out)) == 0); // done: the new screen
    screen_draw_transition(from, to, screen_transition_ms() / 2, 7, out);
    int green = 0;
    for (int i = 0; i < DISPLAY_PIXELS; i++)
        green += out[i].g >= 70 && out[i].r == 0 && out[i].b == 0;
    CHECK(green >= 10); // drops are falling mid-way
    Pixel other[DISPLAY_PIXELS];
    screen_draw_transition(from, to, screen_transition_ms() / 2, 8, other);
    CHECK(memcmp(out, other, sizeof(out)) != 0); // the seed varies the rain
    if (preview)
        for (uint32_t t = 0; t <= screen_transition_ms(); t += 250) {
            char title[40];
            snprintf(title, sizeof(title), "matrix rain clock -> sun, %u ms", (unsigned)t);
            screen_draw_transition(from, to, t, 7, out);
            print_screen(title, out);
        }

    // Text: widths, glyphs, UTF-8 and scrolling.
    CHECK(screen_text_width("HI") == 7);       // 3 + 1 + 3
    CHECK(screen_text_width("Hi!") == 9);      // lowercase is drawn as capitals; '!' is 1 wide
    CHECK(screen_text_width("") == 0);
    CHECK(screen_text_width("7\xc2\xb0") == 6); // "7°": the degree sign is 2 wide
    CHECK(screen_text_width("\xd0\x9f") == 3);  // Cyrillic "П" becomes one '?'
    Pixel text_color = {0, 200, 255};
    memset(out, 0, sizeof(out));
    screen_draw_text("HI", 0, TEXT_ROW, text_color, out);
    CHECK(lit(out, 0, 2) && !lit(out, 1, 2) && lit(out, 2, 2) && lit(out, 1, 4)); // H
    CHECK(lit(out, 4, 2) && lit(out, 5, 3) && !lit(out, 4, 3));                   // I
    CHECK(count_lit(out, 0, 1) == 0 && count_lit(out, 7, 9) == 0);
    screen_draw_scroll("HI", 0, text_color, out);
    CHECK(count_lit(out, 0, 9) == 0); // starts just off the right edge
    screen_draw_scroll("HI", 10 * TEXT_STEP_MS, text_color, out);
    CHECK(lit(out, 0, 2) && lit(out, 2, 4)); // after 10 steps: at the left edge
    CHECK(screen_scroll_pass_ms("HI") == 17 * TEXT_STEP_MS);
    screen_draw_scroll("HI", screen_scroll_pass_ms("HI") - TEXT_STEP_MS, text_color, out);
    CHECK(count_lit(out, 0, 9) <= 4); // last step: only the tail of the I
    if (preview)
        for (uint32_t s = 2; s <= 14; s += 4) {
            char title[32];
            snprintf(title, sizeof(title), "scroll \"HI\", step %u", (unsigned)s);
            screen_draw_scroll("HI", s * TEXT_STEP_MS, text_color, out);
            print_screen(title, out);
        }

    // Night window, including one that crosses midnight.
    CHECK(screens_in_window(23 * 60, 7 * 60, 23 * 60) && screens_in_window(23 * 60, 7 * 60, 3 * 60));
    CHECK(!screens_in_window(23 * 60, 7 * 60, 7 * 60) && !screens_in_window(23 * 60, 7 * 60, 12 * 60));
    CHECK(screens_in_window(13 * 60, 14 * 60, 13 * 60 + 30) && !screens_in_window(13 * 60, 14 * 60, 14 * 60));
    CHECK(!screens_in_window(600, 600, 600));

    // Rotation.
    uint32_t elapsed;
    CHECK(screens_pick(false, true, true, true, 10000, 3000, &elapsed) == SCREEN_CLOCK && elapsed == 3000);
    CHECK(screens_pick(false, true, true, true, 10000, 13000, &elapsed) == SCREEN_WEATHER && elapsed == 3000);
    CHECK(screens_pick(false, true, true, true, 10000, 23000, &elapsed) == SCREEN_CLOCK);
    CHECK(screens_pick(false, true, true, false, 10000, 13000, &elapsed) == SCREEN_CLOCK); // no weather data
    CHECK(screens_pick(false, false, true, true, 10000, 3000, &elapsed) == SCREEN_WEATHER);
    CHECK(screens_pick(false, false, false, true, 10000, 3000, &elapsed) == SCREEN_CLOCK); // nothing enabled
    CHECK(screens_pick(true, true, true, true, 10000, 13000, &elapsed) == SCREEN_TIMER);

    printf(failures ? "%d check(s) failed\n" : "all screen checks passed\n", failures);
    return failures != 0;
}
