/* Rendering: tessere, cifre pre-renderizzate, flip simulato con scala verticale. */
#include "render.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3_ttf/SDL_ttf.h>

#define MAX_TILES 3
#define ANIM_NS   400000000ULL
#define PI_F      3.14159265358979f

#define DIGIT_H_RATIO     0.72f  /* altezza cifre / altezza tessera */
#define DIGIT_MAX_STRETCH 1.25f  /* allungamento verticale massimo */

typedef struct {
    SDL_Texture *face[2]; /* target: face[cur] = valore mostrato/nuovo, face[cur^1] = precedente */
    int cur;
    int value;
    Uint64 start;
    bool anim;
} Tile;

struct Render {
    SDL_Renderer *ren;
    char *font_path;
    int pw, ph;
    int scale, bright;

    /* layout in pixel */
    int count, tw, th, gap, x0, y0, radius, cut;

    SDL_Texture *bg;         /* tessera vuota con angoli arrotondati */
    SDL_Texture *dig[10];
    float dig_w[10], dig_h[10];
    float cell_w;            /* larghezza cella tabulare */
    float ink_cy;            /* centro verticale dell'inchiostro nella superficie cifra */
    SDL_Texture *lbl[2];     /* AM, PM */
    float lbl_w[2], lbl_h[2];

    Tile tile[MAX_TILES];
    int vals[MAX_TILES];
    bool have_vals, hide_lz;
    int ampm;
};

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

static void free_textures(Render *r)
{
    if (r->bg) SDL_DestroyTexture(r->bg);
    r->bg = NULL;
    for (int i = 0; i < 10; i++) {
        if (r->dig[i]) SDL_DestroyTexture(r->dig[i]);
        r->dig[i] = NULL;
    }
    for (int i = 0; i < 2; i++) {
        if (r->lbl[i]) SDL_DestroyTexture(r->lbl[i]);
        r->lbl[i] = NULL;
    }
    for (int i = 0; i < MAX_TILES; i++)
        for (int k = 0; k < 2; k++) {
            if (r->tile[i].face[k]) SDL_DestroyTexture(r->tile[i].face[k]);
            r->tile[i].face[k] = NULL;
        }
}

Render *render_create(SDL_Renderer *ren, const char *font_path)
{
    if (!ren || !font_path) return NULL;
    Render *r = SDL_calloc(1, sizeof *r);
    if (!r) return NULL;
    r->ren = ren;
    r->font_path = SDL_strdup(font_path);
    if (!r->font_path) { SDL_free(r); return NULL; }
    r->scale = 100;
    r->bright = 100;
    return r;
}

void render_destroy(Render *r)
{
    if (!r) return;
    free_textures(r);
    SDL_free(r->font_path);
    SDL_free(r);
}

/* Tessera rettangolare con angoli arrotondati anti-aliased. */
static SDL_Texture *make_tile_bg(Render *r)
{
    SDL_Surface *s = SDL_CreateSurface(r->tw, r->th, SDL_PIXELFORMAT_RGBA32);
    if (!s) return NULL;
    const float R = (float)r->radius;
    for (int y = 0; y < r->th; y++) {
        Uint8 *row = (Uint8 *)s->pixels + (size_t)y * s->pitch;
        for (int x = 0; x < r->tw; x++) {
            float px = x + 0.5f, py = y + 0.5f, cov = 1.0f;
            float cx = px < R ? R : (px > r->tw - R ? r->tw - R : px);
            float cy = py < R ? R : (py > r->th - R ? r->th - R : py);
            if (cx != px || cy != py) {
                float d = sqrtf((px - cx) * (px - cx) + (py - cy) * (py - cy));
                cov = clampf(R - d + 0.5f, 0.0f, 1.0f);
            }
            row[x * 4 + 0] = 0x1a; row[x * 4 + 1] = 0x1a; row[x * 4 + 2] = 0x1a;
            row[x * 4 + 3] = (Uint8)(cov * 255.0f + 0.5f);
        }
    }
    SDL_Texture *t = SDL_CreateTextureFromSurface(r->ren, s);
    SDL_DestroySurface(s);
    if (t) SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
    return t;
}

/* Apre il font in modo che l'altezza d'inchiostro di `ref` sia ~target px. */
static TTF_Font *open_font_for(const char *path, Uint32 ref, float target, int *ink_top_off)
{
    TTF_Font *f = TTF_OpenFont(path, 100.0f);
    if (!f) return NULL;
    int minx, maxx, miny, maxy, adv;
    if (!TTF_GetGlyphMetrics(f, ref, &minx, &maxx, &miny, &maxy, &adv) || maxy <= miny) return f;
    float size = 100.0f * target / (float)(maxy - miny);
    if (size < 4.0f) size = 4.0f;
    TTF_SetFontSize(f, size);
    if (TTF_GetGlyphMetrics(f, ref, &minx, &maxx, &miny, &maxy, &adv) && ink_top_off)
        *ink_top_off = TTF_GetFontAscent(f) - (maxy + miny) / 2; /* centro inchiostro dall'alto */
    return f;
}

static bool build_glyphs(Render *r)
{
    const float b = (float)r->bright / 100.0f;
    const Uint8 g = (Uint8)(0xb7 * b + 0.5f);
    const SDL_Color col = { g, g, g, 255 };

    /* cifre */
    int top_off = 0;
    float target = r->th * DIGIT_H_RATIO, maxw = 0;
    TTF_Font *f = NULL;
    for (int pass = 0; pass < 2; pass++) {
        f = open_font_for(r->font_path, '0', target, &top_off);
        if (!f) { SDL_Log("render: font open failed: %s", SDL_GetError()); return false; }
        /* larghezza d'inchiostro (senza spazi laterali) della cifra più larga */
        maxw = 0;
        for (int d = 0; d < 10; d++) {
            int minx, maxx, miny, maxy, adv;
            if (TTF_GetGlyphMetrics(f, (Uint32)('0' + d), &minx, &maxx, &miny, &maxy, &adv)
                && (float)(maxx - minx) > maxw)
                maxw = (float)(maxx - minx);
        }
        /* cella = inchiostro + 10% di spaziatura; due celle entro l'86% della tessera */
        float cell = maxw * 1.10f, lim = 0.86f * (float)r->tw;
        if (pass == 0 && cell * 2.0f > lim) { target *= lim / (cell * 2.0f); TTF_CloseFont(f); f = NULL; }
        else { r->cell_w = cell; break; }
    }
    float cell_w = r->cell_w;
    /* Cifre più lunghe: se la larghezza ha ridotto l'altezza, renderizza alla piena altezza
     * (fino a 1.25x) e comprimi in orizzontale in fase di disegno -> glifi alti e nitidi. */
    const float full = r->th * DIGIT_H_RATIO;
    float sx = 1.0f;
    if (target < full) {
        float sy = SDL_min(full / target, DIGIT_MAX_STRETCH);
        TTF_CloseFont(f);
        f = open_font_for(r->font_path, '0', target * sy, &top_off);
        if (!f) { SDL_Log("render: font open failed: %s", SDL_GetError()); return false; }
        sx = 1.0f / sy;
    }
    maxw = 0;
    for (int d = 0; d < 10; d++) {
        SDL_Surface *s = TTF_RenderGlyph_Blended(f, (Uint32)('0' + d), col);
        if (!s) continue;
        r->dig_w[d] = (float)s->w * sx;
        r->dig_h[d] = (float)s->h;
        if (s->w > maxw) maxw = (float)s->w;
        r->dig[d] = SDL_CreateTextureFromSurface(r->ren, s);
        SDL_DestroySurface(s);
        if (r->dig[d]) SDL_SetTextureScaleMode(r->dig[d], SDL_SCALEMODE_LINEAR);
    }
    (void)maxw;
    r->cell_w = cell_w;
    r->ink_cy = (float)top_off;
    TTF_CloseFont(f);

    /* etichette AM/PM */
    f = open_font_for(r->font_path, 'A', r->th * 0.08f, NULL);
    if (f) {
        static const char *txt[2] = { "AM", "PM" };
        for (int i = 0; i < 2; i++) {
            SDL_Surface *s = TTF_RenderText_Blended(f, txt[i], 0, col);
            if (!s) continue;
            r->lbl_w[i] = (float)s->w;
            r->lbl_h[i] = (float)s->h;
            r->lbl[i] = SDL_CreateTextureFromSurface(r->ren, s);
            SDL_DestroySurface(s);
            if (r->lbl[i]) {
                SDL_SetTextureBlendMode(r->lbl[i], SDL_BLENDMODE_BLEND);
                SDL_SetTextureAlphaMod(r->lbl[i], 178); /* 70% */
            }
        }
        TTF_CloseFont(f);
    }
    return true;
}

/* Renderizza il valore v nella faccia (tessera + cifre) della tessera i. */
static void render_face(Render *r, int i, int k, int v)
{
    SDL_Texture *face = r->tile[i].face[k];
    if (!face) return;
    SDL_Texture *prev = SDL_GetRenderTarget(r->ren);
    SDL_SetRenderTarget(r->ren, face);
    SDL_SetRenderDrawColor(r->ren, 0, 0, 0, 0);
    SDL_SetRenderDrawBlendMode(r->ren, SDL_BLENDMODE_NONE);
    SDL_RenderClear(r->ren);
    if (r->bg) {
        SDL_SetTextureBlendMode(r->bg, SDL_BLENDMODE_NONE);
        SDL_RenderTexture(r->ren, r->bg, NULL, NULL);
    }
    int d[2], n = 2;
    d[0] = v / 10; d[1] = v % 10;
    if (i == 0 && r->hide_lz && v < 10) { d[0] = d[1]; n = 1; }
    float x = ((float)r->tw - r->cell_w * (float)n) * 0.5f;
    for (int j = 0; j < n; j++) {
        int dd = d[j];
        SDL_Texture *t = r->dig[dd];
        if (t) {
            float w = r->dig_w[dd], h = r->dig_h[dd];
            SDL_FRect dst = { SDL_roundf(x + (r->cell_w - w) * 0.5f),
                              SDL_roundf((float)r->th * 0.5f - r->ink_cy), w, h };
            SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
            SDL_RenderTexture(r->ren, t, NULL, &dst);
        }
        x += r->cell_w;
    }
    SDL_SetRenderTarget(r->ren, prev);
}

static void layout(Render *r)
{
    int n = r->count;
    float W = (float)r->pw * (float)r->scale / 100.0f;
    float tw = W * (n == 3 ? 0.31f : 0.46f);
    float gap = W * (n == 3 ? 0.035f : 0.04f);
    float th = tw * 0.95f;
    float maxh = (float)r->ph * 0.85f;
    if (th > maxh) {
        float k = maxh / th;
        tw *= k; gap *= k; th = maxh;
    }
    r->tw = (int)(tw + 0.5f); if (r->tw < 8) r->tw = 8;
    r->th = (int)(th + 0.5f) & ~1; if (r->th < 8) r->th = 8;
    r->gap = (int)(gap + 0.5f);
    r->radius = (int)((float)r->tw * 0.05f + 0.5f);
    int cut = (int)(r->th * 0.012f + 0.5f);
    r->cut = cut < 2 ? 2 : cut;
    int total = n * r->tw + (n - 1) * r->gap;
    r->x0 = (r->pw - total) / 2;
    r->y0 = (r->ph - r->th) / 2;
}

/* Ricostruisce texture e facce al layout corrente. */
static void rebuild(Render *r)
{
    free_textures(r);
    if (r->pw <= 0 || r->ph <= 0) return;
    layout(r);
    r->bg = make_tile_bg(r);
    if (!r->bg) SDL_Log("render: tile texture failed: %s", SDL_GetError());
    if (!build_glyphs(r)) return;
    for (int i = 0; i < r->count; i++) {
        Tile *t = &r->tile[i];
        for (int k = 0; k < 2; k++) {
            t->face[k] = SDL_CreateTexture(r->ren, SDL_PIXELFORMAT_RGBA32,
                                           SDL_TEXTUREACCESS_TARGET, r->tw, r->th);
            if (!t->face[k]) { SDL_Log("render: face failed: %s", SDL_GetError()); continue; }
            SDL_SetTextureBlendMode(t->face[k], SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(t->face[k], SDL_SCALEMODE_LINEAR);
        }
        t->anim = false;
        if (r->have_vals) {
            t->value = r->vals[i];
            render_face(r, i, t->cur, t->value);
        }
    }
}

void render_resize(Render *r, int pixel_w, int pixel_h)
{
    if (!r) return;
    r->pw = pixel_w;
    r->ph = pixel_h;
    rebuild(r);
}

void render_set_style(Render *r, int scale_pct, int brightness_pct)
{
    if (!r) return;
    scale_pct = scale_pct < 50 ? 50 : (scale_pct > 100 ? 100 : scale_pct);
    brightness_pct = brightness_pct < 20 ? 20 : (brightness_pct > 100 ? 100 : brightness_pct);
    if (scale_pct == r->scale && brightness_pct == r->bright) return;
    r->scale = scale_pct;
    r->bright = brightness_pct;
    rebuild(r);
}

void render_set_tiles(Render *r, const int *values, int count, bool hide_leading_zero,
                      bool animate, Uint64 now_ns)
{
    if (!r || !values || count < 2 || count > MAX_TILES) return;
    bool relayout = !r->have_vals || count != r->count;
    bool hz_changed = hide_leading_zero != r->hide_lz;
    r->hide_lz = hide_leading_zero;
    if (relayout) {
        r->count = count;
        for (int i = 0; i < count; i++) r->vals[i] = values[i];
        r->have_vals = true;
        rebuild(r);
        return;
    }
    for (int i = 0; i < count; i++) {
        Tile *t = &r->tile[i];
        int v = values[i];
        if (v < 0) v = 0;
        if (v > 99) v = 99;
        r->vals[i] = v;
        if (v == t->value && !hz_changed) continue;
        if (!t->face[0]) { t->value = v; continue; }
        if (animate && v != t->value) {
            t->cur ^= 1;            /* la faccia precedente resta come "old" */
            t->anim = true;
            t->start = now_ns;
        } else {
            t->anim = false;
        }
        t->value = v;
        render_face(r, i, t->cur, v);
    }
}

void render_set_ampm(Render *r, int ampm)
{
    if (r) r->ampm = ampm < 0 || ampm > 2 ? 0 : ampm;
}

static bool tile_animating(const Tile *t, Uint64 now)
{
    return t->anim && now - t->start < ANIM_NS;
}

bool render_is_animating(const Render *r, Uint64 now_ns)
{
    if (!r) return false;
    for (int i = 0; i < r->count; i++)
        if (tile_animating(&r->tile[i], now_ns)) return true;
    return false;
}

static float ease_in(float u) { return u * u; }

/* ease-out con piccolo overshoot (max ~2%) */
static float ease_out_back(float u)
{
    const float c1 = 0.35f, c3 = c1 + 1.0f;
    float p = u - 1.0f;
    return 1.0f + c3 * p * p * p + c1 * p * p;
}

static void draw_flip(Render *r, const Tile *t, float x, float y, Uint64 now)
{
    SDL_Texture *nw = t->face[t->cur], *old = t->face[t->cur ^ 1];
    const float tw = (float)r->tw, hh = (float)r->th * 0.5f;
    float p = (float)(now - t->start) / (float)ANIM_NS;
    SDL_FRect top_src = { 0, 0, tw, hh }, bot_src = { 0, hh, tw, hh };
    SDL_FRect top_dst = { x, y, tw, hh }, bot_dst = { x, y + hh, tw, hh };

    SDL_RenderTexture(r->ren, nw, &top_src, &top_dst);            /* top_new */
    if (p < 0.5f) {
        SDL_RenderTexture(r->ren, old, &bot_src, &bot_dst);       /* bottom_old */
        float u = ease_in(p / 0.5f);
        float h = hh * cosf(u * PI_F * 0.5f);
        SDL_FRect src = top_src, dst = { x, y + hh - h, tw, h };
        Uint8 m = (Uint8)(255.0f * (1.0f - 0.6f * u) + 0.5f);
        if (h >= 0.5f) {
            SDL_SetTextureColorMod(old, m, m, m);
            SDL_RenderTexture(r->ren, old, &src, &dst);
            SDL_SetTextureColorMod(old, 255, 255, 255);
        }
    } else {
        float q = (p - 0.5f) / 0.5f;
        float u = ease_out_back(q);
        float h = hh * sinf(u * PI_F * 0.5f);
        if (h > hh) h = hh;
        Uint8 mo = (Uint8)(255.0f * (1.0f - 0.3f * q) + 0.5f);
        SDL_SetTextureColorMod(old, mo, mo, mo);
        SDL_RenderTexture(r->ren, old, &bot_src, &bot_dst);
        SDL_SetTextureColorMod(old, 255, 255, 255);
        Uint8 m = (Uint8)(255.0f * (1.0f - 0.6f * (1.0f - q)) + 0.5f);
        SDL_FRect dst = { x, y + hh, tw, h };
        SDL_FRect src = bot_src; /* la metà intera viene schiacciata */
        if (h >= 0.5f) {
            SDL_SetTextureColorMod(nw, m, m, m);
            SDL_RenderTexture(r->ren, nw, &src, &dst);
            SDL_SetTextureColorMod(nw, 255, 255, 255);
        }
    }
}

void render_draw(Render *r, Uint64 now_ns)
{
    if (!r) return;
    SDL_SetRenderDrawBlendMode(r->ren, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(r->ren, 0, 0, 0, 255);
    SDL_RenderClear(r->ren);
    if (!r->have_vals) return;

    SDL_SetRenderDrawBlendMode(r->ren, SDL_BLENDMODE_NONE);
    for (int i = 0; i < r->count; i++) {
        Tile *t = &r->tile[i];
        if (!t->face[0] || !t->face[1]) continue;
        float x = (float)(r->x0 + i * (r->tw + r->gap)), y = (float)r->y0;
        if (tile_animating(t, now_ns)) {
            draw_flip(r, t, x, y, now_ns);
        } else {
            SDL_FRect dst = { x, y, (float)r->tw, (float)r->th };
            SDL_RenderTexture(r->ren, t->face[t->cur], NULL, &dst);
        }
        /* linea di taglio */
        SDL_SetRenderDrawColor(r->ren, 0, 0, 0, 255);
        SDL_FRect cut = { x, y + (float)r->th * 0.5f - (float)r->cut * 0.5f, (float)r->tw, (float)r->cut };
        SDL_RenderFillRect(r->ren, &cut);
    }
    if (r->ampm >= 1 && r->ampm <= 2) {
        int k = r->ampm - 1;
        if (r->lbl[k]) {
            float m = (float)r->tw * 0.06f;
            SDL_FRect dst = { (float)r->x0 + m, (float)r->y0 + m, r->lbl_w[k], r->lbl_h[k] };
            SDL_RenderTexture(r->ren, r->lbl[k], NULL, &dst);
        }
    }
}
