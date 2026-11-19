#include "core/color.h"
#include "core/settings.h"
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main(void) {
    uint32_t v = 0;
    CHECK(color_parse_hex("#b7b7b7", &v) && v == 0xb7b7b7);
    CHECK(color_parse_hex("FFB347", &v) && v == 0xffb347);
    CHECK(color_parse_hex("  #6fa8dc ", &v) && v == 0x6fa8dc);
    CHECK(!color_parse_hex("", &v));
    CHECK(!color_parse_hex("#12345", &v));
    CHECK(!color_parse_hex("#1234567", &v));
    CHECK(!color_parse_hex("#gg0000", &v));
    CHECK(!color_parse_hex("hello!", &v));

    char buf[8];
    color_format_hex(0xFFB347, buf);
    CHECK(strcmp(buf, "#ffb347") == 0);
    color_format_hex(0x000a0b, buf);
    CHECK(strcmp(buf, "#000a0b") == 0);

    /* HSV round-trip (tolleranza 1 per canale) e palette */
    for (int i = 0; i < COLOR_PALETTE_COUNT; i++) {
        uint32_t c = color_palette[i];
        uint32_t r = color_from_hsv(color_to_hsv(c));
        for (int sh = 0; sh <= 16; sh += 8) {
            int a = (int)((c >> sh) & 0xff), b = (int)((r >> sh) & 0xff);
            CHECK(a - b <= 1 && b - a <= 1);
        }
        CHECK(color_palette_index(c) == i);
        int n = color_palette_step(c, 1) == color_palette[(i + 1) % COLOR_PALETTE_COUNT];
        int p = color_palette_step(c, -1) == color_palette[(i + COLOR_PALETTE_COUNT - 1) % COLOR_PALETTE_COUNT];
        CHECK(n);
        CHECK(p);
    }
    CHECK(color_palette_step(color_palette[COLOR_PALETTE_COUNT - 1], 1) == color_palette[0]);
    CHECK(color_palette_step(color_palette[0], -1) == color_palette[COLOR_PALETTE_COUNT - 1]);
    CHECK(color_palette_index(0x123457) == -1 || color_palette_index(0x123457) >= 0);
    uint32_t odd = color_palette_step(0x123457, 1);
    CHECK(color_palette_index(odd) >= 0);

    CHECK(color_is_bluish(0x6fa8dc));
    CHECK(color_is_bluish(0x0000ff));
    CHECK(!color_is_bluish(0xffb347));
    CHECK(!color_is_bluish(0xb7b7b7));   /* grigio: saturazione bassa */
    CHECK(!color_is_bluish(0xff0000));

    /* color_break */
    CHECK(color_break(BREAK_COLOR_DIGITS, 0x112233, 0xb7b7b7) == 0xb7b7b7);
    CHECK(color_break(BREAK_COLOR_AUTO, 0x112233, 0xb7b7b7) == COLOR_BREAK);
    CHECK(color_break(BREAK_COLOR_AUTO, 0x112233, 0x0000ff) == COLOR_BREAK_ALT);
    CHECK(color_break(BREAK_COLOR_CUSTOM, 0x112233, 0xb7b7b7) == 0x112233);
    CHECK(color_break(99, 0x112233, 0xb7b7b7) == 0xb7b7b7);
    CHECK(color_break(-1, 0x112233, 0x0000ff) == 0x0000ff);

    if (fails) return 1;
    puts("ok");
    return 0;
}
