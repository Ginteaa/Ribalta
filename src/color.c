#include "color.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* Material 400 / 600 / 800 per tonalità (colonna), poi i neutri. Indice = riga * COLS + colonna. */
const uint32_t color_palette[COLOR_PALETTE_COUNT] = {
    /* 400 */
    0xef5350, 0xec407a, 0xab47bc, 0x7e57c2, 0x5c6bc0, 0x42a5f5, 0x29b6f6,
    0x26c6da, 0x26a69a, 0x66bb6a, 0x9ccc65, 0xd4e157, 0xffca28, 0xffa726,
    /* 600 */
    0xe53935, 0xd81b60, 0x8e24aa, 0x5e35b1, 0x3949ab, 0x1e88e5, 0x039be5,
    0x00acc1, 0x00897b, 0x43a047, 0x7cb342, 0xc0ca33, 0xffb300, 0xfb8c00,
    /* 800 */
    0xc62828, 0xad1457, 0x6a1b9a, 0x4527a0, 0x283593, 0x1565c0, 0x0277bd,
    0x00838f, 0x00695c, 0x2e7d32, 0x558b2f, 0x9e9d24, 0xff8f00, 0xef6c00,
    /* neutri */
    0x3a3a3a, 0xffffff, 0xb7b7b7  /* grigio di default per ultimo: → passa al primo colore, ← al bianco */
};

static uint32_t to_byte(float x)
{
    float r = floorf(x * 255.0f + 0.5f);
    if (r < 0.0f) r = 0.0f;
    if (r > 255.0f) r = 255.0f;
    return (uint32_t)r;
}

uint32_t color_from_hsv(ColorHSV c)
{
    float h = fmodf(c.h, 360.0f);
    if (h < 0.0f) h += 360.0f;
    float s = c.s < 0.0f ? 0.0f : (c.s > 1.0f ? 1.0f : c.s);
    float v = c.v < 0.0f ? 0.0f : (c.v > 1.0f ? 1.0f : c.v);
    float hh = h / 60.0f;
    int i = (int)hh;
    float f = hh - (float)i;
    float p = v * (1.0f - s), q = v * (1.0f - s * f), t = v * (1.0f - s * (1.0f - f));
    float r, g, b;
    switch (i % 6) {
    case 0:  r = v; g = t; b = p; break;
    case 1:  r = q; g = v; b = p; break;
    case 2:  r = p; g = v; b = t; break;
    case 3:  r = p; g = q; b = v; break;
    case 4:  r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
    }
    return (to_byte(r) << 16) | (to_byte(g) << 8) | to_byte(b);
}

ColorHSV color_to_hsv(uint32_t rgb)
{
    float r = (float)((rgb >> 16) & 0xff) / 255.0f;
    float g = (float)((rgb >> 8) & 0xff) / 255.0f;
    float b = (float)(rgb & 0xff) / 255.0f;
    float mx = fmaxf(r, fmaxf(g, b)), mn = fminf(r, fminf(g, b));
    float d = mx - mn;
    ColorHSV o = {0.0f, 0.0f, mx};
    if (mx > 0.0f) o.s = d / mx;
    if (d > 0.0f) {
        float h;
        if (mx == r)      h = (g - b) / d;
        else if (mx == g) h = 2.0f + (b - r) / d;
        else              h = 4.0f + (r - g) / d;
        h *= 60.0f;
        if (h < 0.0f) h += 360.0f;
        o.h = h;
    }
    return o;
}

bool color_parse_hex(const char *s, uint32_t *out)
{
    if (!s) return false;
    while (isspace((unsigned char)*s)) s++;
    if (*s == '#') s++;
    uint32_t v = 0;
    int n = 0;
    while (n < 6 && isxdigit((unsigned char)s[n])) {
        char c = s[n];
        uint32_t d = (c >= '0' && c <= '9') ? (uint32_t)(c - '0') : (uint32_t)((c | 0x20) - 'a' + 10);
        v = (v << 4) | d;
        n++;
    }
    if (n != 6) return false;
    const char *e = s + 6;
    while (isspace((unsigned char)*e)) e++;
    if (*e != '\0') return false;
    if (out) *out = v;
    return true;
}

void color_format_hex(uint32_t rgb, char out[8])
{
    snprintf(out, 8, "#%06x", (unsigned)(rgb & 0xffffff));
}

int color_palette_index(uint32_t rgb)
{
    rgb &= 0xffffff;
    for (int i = 0; i < COLOR_PALETTE_COUNT; i++)
        if (color_palette[i] == rgb) return i;
    return -1;
}

static long dist2(uint32_t a, uint32_t b)
{
    long dr = (long)((a >> 16) & 0xff) - (long)((b >> 16) & 0xff);
    long dg = (long)((a >> 8) & 0xff) - (long)((b >> 8) & 0xff);
    long db = (long)(a & 0xff) - (long)(b & 0xff);
    return dr * dr + dg * dg + db * db;
}

uint32_t color_palette_step(uint32_t rgb, int dir)
{
    int i = color_palette_index(rgb);
    if (i < 0) {
        long best = -1;
        for (int k = 0; k < COLOR_PALETTE_COUNT; k++) {
            long d = dist2(rgb & 0xffffff, color_palette[k]);
            if (best < 0 || d < best) { best = d; i = k; }
        }
    }
    i += dir >= 0 ? 1 : -1;
    i = (i % COLOR_PALETTE_COUNT + COLOR_PALETTE_COUNT) % COLOR_PALETTE_COUNT;
    return color_palette[i];
}

bool color_is_bluish(uint32_t rgb)
{
    ColorHSV c = color_to_hsv(rgb);
    return c.s >= 0.25f && c.h >= 180.0f && c.h <= 260.0f;
}
