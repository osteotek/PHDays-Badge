#include "screens.h"

#include <math.h>
#include <string.h>

// 3x5 digits, one row per byte, bit 2 = leftmost column.
static const uint8_t DIGITS[10][5] = {
    {7, 5, 5, 5, 7}, {2, 6, 2, 2, 7}, {7, 1, 7, 4, 7}, {7, 1, 7, 1, 7}, {5, 5, 7, 1, 1},
    {7, 4, 7, 1, 7}, {7, 4, 7, 5, 7}, {7, 1, 2, 2, 2}, {7, 5, 7, 5, 7}, {7, 5, 7, 1, 7},
};

static const Pixel OFF = {0, 0, 0};
static const Pixel HOUR_COLOR = {255, 150, 40};
static const Pixel MINUTE_COLOR = {60, 180, 255};
static const Pixel SECONDS_COLOR = {70, 70, 70};
static const Pixel MISSING_COLOR = {90, 90, 90};
static const Pixel FOCUS_COLOR = {255, 50, 30};
static const Pixel BREAK_COLOR = {40, 230, 80};

static void clear(Pixel *out) { memset(out, 0, sizeof(Pixel) * DISPLAY_PIXELS); }

static void put(Pixel *out, int x, int y, Pixel color) {
    if (x >= 0 && x < DISPLAY_WIDTH && y >= 0 && y < DISPLAY_HEIGHT)
        out[y * DISPLAY_WIDTH + x] = color;
}

static void draw_digit(Pixel *out, int x, int y, int digit, Pixel color) {
    for (int row = 0; row < 5; row++)
        for (int column = 0; column < 3; column++)
            if (DIGITS[digit][row] >> (2 - column) & 1)
                put(out, x + column, y + row, color);
}

// Two 3-wide dashes per row: "no data yet".
static void draw_missing(Pixel *out, int y) {
    for (int x = 1; x <= 7; x++)
        if (x != 4)
            put(out, x, y, MISSING_COLOR);
}

void screen_draw_clock(const screen_time_t *time, Pixel out[DISPLAY_PIXELS]) {
    clear(out);
    if (!time->valid) {
        draw_missing(out, 2);
        draw_missing(out, 7);
        return;
    }
    draw_digit(out, 1, 0, time->hour / 10, HOUR_COLOR);
    draw_digit(out, 5, 0, time->hour % 10, HOUR_COLOR);
    draw_digit(out, 1, 5, time->minute / 10, MINUTE_COLOR);
    draw_digit(out, 5, 5, time->minute % 10, MINUTE_COLOR);
    // The right column fills bottom-up over the minute, one pixel per 6 s.
    for (int i = 0; i < time->second / 6; i++)
        put(out, 9, 9 - i, SECONDS_COLOR);
}

// --- weather -------------------------------------------------------------

typedef enum { ICON_CLEAR, ICON_PARTLY, ICON_CLOUDY, ICON_FOG, ICON_RAIN, ICON_SNOW, ICON_THUNDER } icon_t;

static icon_t icon_for(int code) {
    if (code == 0)
        return ICON_CLEAR;
    if (code == 1 || code == 2)
        return ICON_PARTLY;
    if (code == 45 || code == 48)
        return ICON_FOG;
    if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82))
        return ICON_RAIN;
    if ((code >= 71 && code <= 77) || code == 85 || code == 86)
        return ICON_SNOW;
    if (code >= 95 && code <= 99)
        return ICON_THUNDER;
    return ICON_CLOUDY;
}

static Pixel palette(char key) {
    switch (key) {
    case 'Y':
        return (Pixel){255, 200, 0}; // sun
    case 'O':
        return (Pixel){255, 100, 0}; // sun rays
    case 'M':
        return (Pixel){200, 200, 150}; // moon
    case 'W':
        return (Pixel){230, 230, 230}; // cloud
    case 'G':
        return (Pixel){120, 120, 140}; // dark cloud
    case 'L':
        return (Pixel){150, 150, 170}; // fog
    default:
        return OFF;
    }
}

// Diagonal layout: a 7x5 icon at the top left, the temperature at the bottom
// right (rows 5-9), so the two never touch.
#define ICON_WIDTH 7
#define ICON_HEIGHT 5
#define TEMPERATURE_ROW 5

static bool is_cloud(char key) { return key == 'W' || key == 'G' || key == 'L'; }

static Pixel scaled(Pixel p, int percent) { return (Pixel){(uint8_t)(p.r * percent / 100), (uint8_t)(p.g * percent / 100), (uint8_t)(p.b * percent / 100)}; }

// Draws the sun/moon part at glow percent, then the cloud part shifted by
// drift columns on top.
static void draw_bitmap(Pixel *out, const char *const rows[ICON_HEIGHT], int drift, int glow) {
    for (int pass = 0; pass < 2; pass++)
        for (int y = 0; y < ICON_HEIGHT; y++)
            for (int x = 0; x < ICON_WIDTH && rows[y][x]; x++) {
                char key = rows[y][x];
                if (key == '.' || is_cloud(key) != (pass == 1))
                    continue;
                put(out, pass ? x + drift : x, y, pass ? palette(key) : scaled(palette(key), glow));
            }
}

// Slow, slight motion: clouds drift a column left and back, glows breathe 70-100%.
static int cloud_drift(uint32_t now_ms) { return -(int)((now_ms / 1500) % 2); }

static int glow(uint32_t now_ms) {
    uint32_t phase = now_ms % 3000;
    return 70 + (int)(30 * (phase < 1500 ? 1500 - phase : phase - 1500) / 1500);
}

// The sun's rays alternate between the corners and the middle of each side.
static const char *const SUN_CORNERS[] = {"O.....O", ".YYYYY.", ".YYYYY.", ".YYYYY.", "O.....O"};
static const char *const SUN_SIDES[] = {"...O...", ".YYYYY.", "OYYYYYO", ".YYYYY.", "...O..."};
static const char *const MOON[] = {"..MMM..", ".MM....", ".M.....", ".MM....", "..MMM.."};
static const char *const PARTLY_DAY[] = {"....YY.", "..WWYYY", ".WWWWYY", "WWWWWWW", ".WWWWW."};
static const char *const PARTLY_NIGHT[] = {"....MM.", "..WWM..", ".WWWWM.", "WWWWWWW", ".WWWWW."};
static const char *const CLOUDY[] = {"..WWW..", ".WWWWW.", "WWWWWWW", "WWWWWWW", ".WWWWW."};
// Three rows of cloud; rain, snow and lightning are animated in the two below.
static const char *const DARK_CLOUD[] = {"..GGG..", ".GGGGG.", "GGGGGGG", ".......", "......."};

// Drops alternate between rows 3 and 4; neighbouring columns are out of phase.
static void draw_falling(Pixel *out, const int columns[3], uint32_t now_ms, uint32_t step_ms, Pixel color) {
    for (int i = 0; i < 3; i++)
        put(out, columns[i], 3 + (int)((now_ms / step_ms + (uint32_t)i) % 2), color);
}

// Fog bands on rows 0, 2 and 4 slide one column, neighbours in opposite directions.
static void draw_fog(Pixel *out, uint32_t now_ms) {
    int shift = (int)((now_ms / 1200) % 2);
    for (int band = 0; band < 3; band++) {
        int start = band % 2 ? 1 - shift : shift;
        for (int x = start; x < start + 6; x++)
            put(out, x, band * 2, palette('L'));
    }
}

// Two stars beside the moon twinkle out of phase.
static void draw_stars(Pixel *out, uint32_t now_ms) {
    Pixel star = {255, 255, 220};
    put(out, 6, 0, scaled(star, glow(now_ms)));
    put(out, 5, 3, scaled(star, glow(now_ms + 1500)));
}

static void draw_icon(const screen_weather_t *weather, uint32_t now_ms, Pixel *out) {
    static const int RAIN_COLUMNS[3] = {1, 3, 5};
    static const int SNOW_COLUMNS[3] = {1, 3, 5};
    int drift = cloud_drift(now_ms);
    switch (icon_for(weather->code)) {
    case ICON_CLEAR:
        if (weather->is_day) {
            draw_bitmap(out, (now_ms / 800) % 2 ? SUN_SIDES : SUN_CORNERS, 0, 100);
        } else {
            draw_bitmap(out, MOON, 0, 100);
            draw_stars(out, now_ms);
        }
        break;
    case ICON_PARTLY:
        draw_bitmap(out, weather->is_day ? PARTLY_DAY : PARTLY_NIGHT, drift, glow(now_ms));
        break;
    case ICON_CLOUDY:
        draw_bitmap(out, CLOUDY, drift, 100);
        break;
    case ICON_FOG:
        draw_fog(out, now_ms);
        break;
    case ICON_RAIN:
        draw_bitmap(out, DARK_CLOUD, drift, 100);
        draw_falling(out, RAIN_COLUMNS, now_ms, 200, (Pixel){0, 90, 255});
        break;
    case ICON_SNOW:
        draw_bitmap(out, DARK_CLOUD, drift, 100);
        draw_falling(out, SNOW_COLUMNS, now_ms, 500, (Pixel){230, 230, 255});
        break;
    case ICON_THUNDER:
        draw_bitmap(out, DARK_CLOUD, drift, 100);
        if ((now_ms / 300) % 4 != 3) { // flickering bolt
            Pixel bolt = {255, 220, 0};
            put(out, 4, 3, bolt);
            put(out, 3, 3, bolt);
            put(out, 3, 4, bolt);
            put(out, 2, 4, bolt);
        }
        break;
    }
}

static Pixel temperature_color(long celsius) {
    if (celsius <= -10)
        return (Pixel){80, 120, 255};
    if (celsius < 0)
        return (Pixel){150, 200, 255};
    if (celsius < 10)
        return (Pixel){100, 255, 200};
    if (celsius < 20)
        return (Pixel){255, 230, 80};
    if (celsius < 30)
        return (Pixel){255, 150, 40};
    return (Pixel){255, 60, 40};
}

// One right-aligned line in rows 5-9: minus (2 wide), digits (3 wide) and a
// 2x2 degree sign at the top right, one column apart. "23°" and "-5°" fit;
// "-12" alone fills all ten columns, so two-digit frosts drop the degree sign.
static void draw_temperature(float temperature, int y, Pixel *out) {
    long celsius = lroundf(temperature);
    if (celsius > 99)
        celsius = 99;
    if (celsius < -99)
        celsius = -99;
    bool negative = celsius < 0;
    long value = negative ? -celsius : celsius;
    int digits = value >= 10 ? 2 : 1;
    bool degree = !(negative && digits == 2);
    int width = (negative ? 3 : 0) + digits * 4 - 1 + (degree ? 3 : 0);
    int x = DISPLAY_WIDTH - width;
    Pixel color = temperature_color(celsius);
    if (negative) {
        put(out, x, y + 2, color);
        put(out, x + 1, y + 2, color);
        x += 3;
    }
    if (digits == 2) {
        draw_digit(out, x, y, (int)(value / 10), color);
        x += 4;
    }
    draw_digit(out, x, y, (int)(value % 10), color);
    if (degree)
        for (int dy = 0; dy < 2; dy++)
            for (int dx = 4; dx < 6; dx++)
                put(out, x + dx, y + dy, color);
}

void screen_draw_weather(const screen_weather_t *weather, uint32_t slot_elapsed_ms, uint32_t slot_ms, uint32_t now_ms, Pixel out[DISPLAY_PIXELS]) {
    clear(out);
    if (!weather->valid) {
        draw_missing(out, 7);
        return;
    }
    if (weather->has_range && slot_elapsed_ms >= slot_ms / 2) {
        draw_temperature(weather->high, 0, out);
        draw_temperature(weather->low, TEMPERATURE_ROW, out);
        return;
    }
    draw_icon(weather, now_ms, out);
    draw_temperature(weather->temperature, TEMPERATURE_ROW, out);
}

// --- timer -----------------------------------------------------------------

void screen_draw_timer(const screen_timer_t *timer, uint32_t now_ms, Pixel out[DISPLAY_PIXELS]) {
    clear(out);
    if (timer->phase == TIMER_IDLE)
        return;
    Pixel color = timer->phase == TIMER_FOCUS ? FOCUS_COLOR : BREAK_COLOR;
    bool in_seconds = timer->remaining_s < 60;
    uint32_t value = in_seconds ? timer->remaining_s : (timer->remaining_s + 59) / 60;
    if (value > 99)
        value = 99;
    // The last 10 seconds blink.
    if (!(in_seconds && timer->remaining_s <= 10 && (now_ms / 500) % 2)) {
        if (value >= 10) {
            draw_digit(out, 1, 0, (int)(value / 10), color);
            draw_digit(out, 5, 0, (int)(value % 10), color);
        } else {
            draw_digit(out, 3, 0, (int)value, color);
        }
    }
    // Rows 6-9 hold the time left as a bar that shrinks from the end.
    uint32_t cells = 4 * DISPLAY_WIDTH;
    uint32_t lit = timer->total_s ? (timer->remaining_s * cells + timer->total_s - 1) / timer->total_s : 0;
    for (uint32_t i = 0; i < lit && i < cells; i++)
        put(out, (int)(i % DISPLAY_WIDTH), 6 + (int)(i / DISPLAY_WIDTH), color);
}

// --- transition ------------------------------------------------------------

#define RAIN_TAIL 3          // trail pixels behind each drop's head
#define RAIN_STEP_MS 55      // slowest drop: one row per 55 ms
#define RAIN_MAX_DELAY_MS 500 // columns start at different times

static uint32_t mix(uint32_t x) { // integer hash for per-column randomness
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

uint32_t screen_transition_ms(void) { return RAIN_MAX_DELAY_MS + (DISPLAY_HEIGHT + RAIN_TAIL + 1) * RAIN_STEP_MS; }

void screen_draw_transition(const Pixel from[DISPLAY_PIXELS], const Pixel to[DISPLAY_PIXELS], uint32_t elapsed_ms, uint32_t seed, Pixel out[DISPLAY_PIXELS]) {
    static const Pixel TRAIL[RAIN_TAIL + 1] = {{190, 255, 190}, {0, 230, 0}, {0, 140, 0}, {0, 70, 0}};
    for (int x = 0; x < DISPLAY_WIDTH; x++) {
        uint32_t r = mix(seed * 31U + (uint32_t)x);
        uint32_t delay = r % RAIN_MAX_DELAY_MS;
        uint32_t step = RAIN_STEP_MS - (r >> 12) % 20; // 36-55 ms per row
        int head = elapsed_ms < delay ? -1 : (int)((elapsed_ms - delay) / step);
        for (int y = 0; y < DISPLAY_HEIGHT; y++) {
            int i = y * DISPLAY_WIDTH + x;
            if (y > head)
                out[i] = from[i];
            else if (head - y <= RAIN_TAIL)
                out[i] = TRAIL[head - y];
            else
                out[i] = to[i];
        }
    }
}

// --- text ----------------------------------------------------------------------

// Glyph rows are left-aligned in `width` bits (bit width-1 = leftmost column).
typedef struct {
    char c;
    uint8_t width;
    uint8_t rows[5];
} glyph_t;

static const glyph_t GLYPHS[] = {
    {'A', 3, {2, 5, 7, 5, 5}}, {'B', 3, {6, 5, 6, 5, 6}}, {'C', 3, {3, 4, 4, 4, 3}}, {'D', 3, {6, 5, 5, 5, 6}},
    {'E', 3, {7, 4, 6, 4, 7}}, {'F', 3, {7, 4, 6, 4, 4}}, {'G', 3, {3, 4, 5, 5, 3}}, {'H', 3, {5, 5, 7, 5, 5}},
    {'I', 3, {7, 2, 2, 2, 7}}, {'J', 3, {1, 1, 1, 5, 2}}, {'K', 3, {5, 5, 6, 5, 5}}, {'L', 3, {4, 4, 4, 4, 7}},
    {'M', 3, {5, 7, 7, 5, 5}}, {'N', 3, {6, 5, 5, 5, 5}}, {'O', 3, {2, 5, 5, 5, 2}}, {'P', 3, {6, 5, 6, 4, 4}},
    {'Q', 3, {2, 5, 5, 6, 3}}, {'R', 3, {6, 5, 6, 5, 5}}, {'S', 3, {3, 4, 2, 1, 6}}, {'T', 3, {7, 2, 2, 2, 2}},
    {'U', 3, {5, 5, 5, 5, 7}}, {'V', 3, {5, 5, 5, 5, 2}}, {'W', 3, {5, 5, 7, 7, 5}}, {'X', 3, {5, 5, 2, 5, 5}},
    {'Y', 3, {5, 5, 2, 2, 2}}, {'Z', 3, {7, 1, 2, 4, 7}}, {' ', 2, {0, 0, 0, 0, 0}}, {'.', 1, {0, 0, 0, 0, 1}},
    {',', 2, {0, 0, 0, 1, 2}}, {'!', 1, {1, 1, 1, 0, 1}}, {':', 1, {0, 1, 0, 1, 0}}, {';', 2, {0, 1, 0, 1, 2}},
    {'?', 3, {7, 1, 2, 0, 2}}, {'-', 3, {0, 0, 7, 0, 0}}, {'+', 3, {0, 2, 7, 2, 0}}, {'/', 3, {1, 1, 2, 4, 4}},
    {'\'', 1, {1, 1, 0, 0, 0}}, {'"', 3, {5, 5, 0, 0, 0}}, {'(', 2, {1, 2, 2, 2, 1}}, {')', 2, {2, 1, 1, 1, 2}},
    {'%', 3, {5, 1, 2, 4, 5}}, {'#', 3, {5, 7, 5, 7, 5}}, {'=', 3, {0, 7, 0, 7, 0}}, {'_', 3, {0, 0, 0, 0, 7}},
    {'*', 3, {5, 2, 7, 2, 5}}, {'<', 3, {1, 2, 4, 2, 1}}, {'>', 3, {4, 2, 1, 2, 4}}, {'@', 3, {7, 5, 7, 4, 7}},
    {'&', 3, {2, 5, 2, 5, 3}}, {'$', 3, {3, 6, 2, 3, 6}},
};
static const glyph_t DEGREE = {0, 2, {3, 3, 0, 0, 0}};

static const glyph_t *glyph_for(char c) {
    static glyph_t digit;
    if (c >= 'a' && c <= 'z')
        c = (char)(c - 'a' + 'A');
    if (c >= '0' && c <= '9') {
        digit.c = c;
        digit.width = 3;
        memcpy(digit.rows, DIGITS[c - '0'], sizeof(digit.rows));
        return &digit;
    }
    for (size_t i = 0; i < sizeof(GLYPHS) / sizeof(GLYPHS[0]); i++)
        if (GLYPHS[i].c == c)
            return &GLYPHS[i];
    return glyph_for('?');
}

// Next glyph from UTF-8 text: "°" is supported, other multibyte characters
// become '?'. Returns NULL at the end.
static const glyph_t *next_glyph(const char **text) {
    const unsigned char *s = (const unsigned char *)*text;
    if (!*s)
        return NULL;
    if (s[0] < 0x80) {
        *text += 1;
        return glyph_for((char)s[0]);
    }
    size_t length = 1;
    while (s[length] && (s[length] & 0xC0) == 0x80)
        length++;
    *text += length;
    return length == 2 && s[0] == 0xC2 && s[1] == 0xB0 ? &DEGREE : glyph_for('?');
}

int screen_text_width(const char *text) {
    int width = 0, count = 0;
    for (const glyph_t *g; (g = next_glyph(&text)) != NULL; count++)
        width += g->width;
    return count ? width + count - 1 : 0;
}

void screen_draw_text(const char *text, int x, int y, Pixel color, Pixel out[DISPLAY_PIXELS]) {
    for (const glyph_t *g; (g = next_glyph(&text)) != NULL; x += g->width + 1)
        for (int row = 0; row < 5; row++)
            for (int column = 0; column < g->width; column++)
                if (g->rows[row] >> (g->width - 1 - column) & 1)
                    put(out, x + column, y + row, color);
}

uint32_t screen_scroll_pass_ms(const char *text) { return (uint32_t)(screen_text_width(text) + DISPLAY_WIDTH) * TEXT_STEP_MS; }

void screen_draw_scroll(const char *text, uint32_t elapsed_ms, Pixel color, Pixel out[DISPLAY_PIXELS]) {
    clear(out);
    uint32_t pass = screen_scroll_pass_ms(text);
    int step = (int)((pass ? elapsed_ms % pass : 0) / TEXT_STEP_MS);
    screen_draw_text(text, DISPLAY_WIDTH - step, TEXT_ROW, color, out);
}

bool screens_in_window(uint16_t start, uint16_t end, uint16_t now) {
    if (start == end)
        return false;
    return start < end ? now >= start && now < end : now >= start || now < end;
}

// --- rotation --------------------------------------------------------------

screen_id_t screens_pick(bool timer_active, bool show_clock, bool show_weather, bool weather_valid, uint32_t slot_ms, uint32_t t_ms, uint32_t *slot_elapsed_ms) {
    if (slot_ms == 0)
        slot_ms = 1;
    *slot_elapsed_ms = t_ms % slot_ms;
    if (timer_active)
        return SCREEN_TIMER;
    screen_id_t available[2];
    uint32_t count = 0;
    if (show_clock)
        available[count++] = SCREEN_CLOCK;
    if (show_weather && weather_valid)
        available[count++] = SCREEN_WEATHER;
    if (count == 0)
        return SCREEN_CLOCK;
    return available[(t_ms / slot_ms) % count];
}
