#include "ui_color.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3_ttf/SDL_ttf.h>

#include "color.h"

enum { ZONE_SQUARE, ZONE_HUE, ZONE_PALETTE, ZONE_HEX, ZONE_COUNT };
enum { DRAG_NONE, DRAG_SQUARE, DRAG_HUE };
enum { MAX_RECENT = 5, TEX_D = 64, MAX_SLOTS = COLOR_PALETTE_COUNT + MAX_RECENT };
enum { LAB_HEX, LAB_RGB, LAB_HSV, LAB_RECENT, LAB_FOOTER, LAB_COUNT };

typedef struct {
    SDL_Texture *tex;
    int  w, h;
    char text[40];
} Label;

struct ColorUI {
    SDL_Renderer *ren;
    char         *font_path;
    TTF_Font     *font;
    bool          open, dirty, laid_out;
    int           win_w, win_h;

    ColorHSV      hsv;       /* fonte di verità */
    uint32_t      rgb, original;
    uint32_t      recent[MAX_RECENT];
    int           nrecent;

    int           zone, drag;
    int           pal_cur;                 /* cursore nella palette (indice slot) */
    char          hexbuf[7];
    int           hexlen;

    /* layout (pixel) */
    float fs, pad, px, py, pw, ph, radius;
    SDL_FRect sq, hue, swatch, field, grid_box;
    float text_x, rgb_y, hsv_y, recent_y, footer_y;
    float cell, rcell, grid_x, grid_y, rec_x, rec_y;
    float hash_w;

    SDL_Texture *panel_tex, *disc, *ring;
    Label        lab[LAB_COUNT];
};

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* ---- texture ---- */

static void label_free(Label *l)
{
    if (l->tex) SDL_DestroyTexture(l->tex);
    l->tex = NULL;
    l->w = l->h = 0;
    l->text[0] = '\0';
}

static void labels_free(ColorUI *ui)
{
    for (int i = 0; i < LAB_COUNT; i++) label_free(&ui->lab[i]);
}

/* Testo in bianco, tinto al disegno: si ricostruisce solo se cambia la stringa. */
static void label_set(ColorUI *ui, Label *l, const char *text)
{
    if (l->tex && strcmp(l->text, text) == 0) return;
    if (l->tex) SDL_DestroyTexture(l->tex);
    l->tex = NULL;
    l->w = l->h = 0;
    snprintf(l->text, sizeof l->text, "%s", text);
    if (!ui->font) return;
    SDL_Surface *s = TTF_RenderText_Blended(ui->font, text, 0, (SDL_Color){255, 255, 255, 255});
    if (!s) return;
    l->tex = SDL_CreateTextureFromSurface(ui->ren, s);
    l->w = s->w;
    l->h = s->h;
    SDL_DestroySurface(s);
    if (l->tex) SDL_SetTextureScaleMode(l->tex, SDL_SCALEMODE_LINEAR);
}

static SDL_Texture *make_disc(ColorUI *ui, bool ring)
{
    SDL_Surface *s = SDL_CreateSurface(TEX_D, TEX_D, SDL_PIXELFORMAT_RGBA32);
    if (!s) return NULL;
    const float R = TEX_D * 0.5f, rin = R - 6.0f;
    for (int y = 0; y < TEX_D; y++) {
        Uint8 *row = (Uint8 *)s->pixels + (size_t)y * (size_t)s->pitch;
        for (int x = 0; x < TEX_D; x++) {
            float d = hypotf(x + 0.5f - R, y + 0.5f - R);
            float c = clampf(R - d + 0.5f, 0.0f, 1.0f);
            if (ring) c *= clampf(d - rin + 0.5f, 0.0f, 1.0f);
            row[x * 4 + 0] = row[x * 4 + 1] = row[x * 4 + 2] = 255;
            row[x * 4 + 3] = (Uint8)(c * 255.0f + 0.5f);
        }
    }
    SDL_Texture *t = SDL_CreateTextureFromSurface(ui->ren, s);
    SDL_DestroySurface(s);
    if (t) {
        SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    }
    return t;
}

static SDL_Texture *make_panel(ColorUI *ui, int w, int h, float radius)
{
    SDL_Surface *s = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32);
    if (!s) return NULL;
    for (int y = 0; y < h; y++) {
        Uint8 *row = (Uint8 *)s->pixels + (size_t)y * (size_t)s->pitch;
        for (int x = 0; x < w; x++) {
            float px = x + 0.5f, py = y + 0.5f;
            float cx = clampf(px, radius, w - radius), cy = clampf(py, radius, h - radius);
            float cov = 1.0f;
            if (cx != px || cy != py)
                cov = clampf(radius - hypotf(px - cx, py - cy) + 0.5f, 0.0f, 1.0f);
            row[x * 4 + 0] = row[x * 4 + 1] = row[x * 4 + 2] = 0x1a;
            row[x * 4 + 3] = (Uint8)(cov * 255.0f + 0.5f);
        }
    }
    SDL_Texture *t = SDL_CreateTextureFromSurface(ui->ren, s);
    SDL_DestroySurface(s);
    if (t) SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
    return t;
}

/* ---- slot della palette: 0..44 palette+neutri, 45.. recenti ---- */

static int slots(const ColorUI *ui) { return COLOR_PALETTE_COUNT + ui->nrecent; }

static uint32_t slot_color(const ColorUI *ui, int i)
{
    return i < COLOR_PALETTE_COUNT ? color_palette[i] : ui->recent[i - COLOR_PALETTE_COUNT];
}

static void slot_rc(int i, int *r, int *c)
{
    int grid = COLOR_PALETTE_COLS * COLOR_PALETTE_ROWS;
    if (i < grid) { *r = i / COLOR_PALETTE_COLS; *c = i % COLOR_PALETTE_COLS; }
    else if (i < COLOR_PALETTE_COUNT) { *r = COLOR_PALETTE_ROWS; *c = i - grid; }
    else { *r = COLOR_PALETTE_ROWS + 1; *c = i - COLOR_PALETTE_COUNT; }
}

static int row_len(const ColorUI *ui, int r)
{
    if (r < COLOR_PALETTE_ROWS) return COLOR_PALETTE_COLS;
    if (r == COLOR_PALETTE_ROWS) return COLOR_PALETTE_NEUTRALS;
    return ui->nrecent;
}

static int slot_from_rc(int r, int c)
{
    if (r < COLOR_PALETTE_ROWS) return r * COLOR_PALETTE_COLS + c;
    if (r == COLOR_PALETTE_ROWS) return COLOR_PALETTE_COLS * COLOR_PALETTE_ROWS + c;
    return COLOR_PALETTE_COUNT + c;
}

static void slot_geom(const ColorUI *ui, int i, float *cx, float *cy, float *d)
{
    int r, c;
    slot_rc(i, &r, &c);
    if (r <= COLOR_PALETTE_ROWS) {
        *cx = ui->grid_x + ((float)c + 0.5f) * ui->cell;
        *cy = ui->grid_y + ((float)r + 0.5f) * ui->cell;
        *d = ui->cell * 0.78f;
    } else {
        *cx = ui->rec_x + ((float)c + 0.5f) * ui->rcell;
        *cy = ui->rec_y + 0.5f * ui->rcell;
        *d = ui->rcell * 0.78f;
    }
}

static int slot_hit(const ColorUI *ui, float x, float y)
{
    for (int i = 0; i < slots(ui); i++) {
        float cx, cy, d;
        slot_geom(ui, i, &cx, &cy, &d);
        float half = (i < COLOR_PALETTE_COUNT ? ui->cell : ui->rcell) * 0.5f;
        if (fabsf(x - cx) <= half && fabsf(y - cy) <= half) return i;
    }
    return -1;
}

static int match_slot(const ColorUI *ui)
{
    for (int i = 0; i < slots(ui); i++)
        if (slot_color(ui, i) == ui->rgb) return i;
    return -1;
}

/* ---- layout ---- */

static void layout(ColorUI *ui)
{
    labels_free(ui);
    if (ui->panel_tex) SDL_DestroyTexture(ui->panel_tex);
    ui->panel_tex = NULL;
    if (!ui->font) return;

    const float W = (float)ui->win_w, H = (float)ui->win_h;
    float density = H / 1080.0f;
    if (density < 1.0f) density = 1.0f;
    float margin = H * 0.025f;
    float pw = fminf(W * 0.42f, 560.0f * density);
    float f = clampf(H / 52.0f, 11.0f, 40.0f);
    float pad = pw * 0.045f;
    float inner = pw - 2.0f * pad;

    /* il piè di pagina deve entrare in larghezza */
    TTF_SetFontSize(ui->font, f);
    int fw = 0, fh = 0;
    if (TTF_GetStringSize(ui->font, "Tab zona · frecce · Invio applica · Esc annulla", 0, &fw, &fh) &&
        (float)fw > inner) {
        f = fmaxf(8.0f, f * inner / (float)fw);
        TTF_SetFontSize(ui->font, f);
    }
    int hw = 0;
    TTF_GetStringSize(ui->font, "#", 0, &hw, &fh);
    ui->hash_w = (float)hw;

    float cell = inner / (float)COLOR_PALETTE_COLS;
    float rcell = cell * 1.35f;
    float g1 = 0.9f * f, hue_h = 1.1f * f, g2 = 1.0f * f, row_h = 2.8f * f, g3 = 1.0f * f;
    float grid_h = (float)(COLOR_PALETTE_ROWS + 1) * cell, g4 = 0.7f * f, rl_h = 1.4f * f;
    float g5 = 0.6f * f, foot_h = 1.4f * f;
    float fixed = g1 + hue_h + g2 + row_h + g3 + grid_h + g4 + rl_h + rcell + g5 + foot_h;
    float avail = H - 2.0f * margin - 2.0f * pad - fixed;
    float sq_h = clampf(avail, inner * 0.3f, inner);
    float ph = 2.0f * pad + fixed + sq_h;
    if (ph > H) ph = H;

    ui->fs = f; ui->pad = pad; ui->pw = pw; ui->ph = ph;
    ui->px = floorf(W - margin - pw);
    ui->py = floorf((H - ph) * 0.5f);
    ui->radius = fminf(pw * 0.03f, 18.0f * density);
    ui->panel_tex = make_panel(ui, (int)pw, (int)ph, ui->radius);

    float x0 = ui->px + pad, y = ui->py + pad;
    ui->sq = (SDL_FRect){x0, y, inner, sq_h};
    y += sq_h + g1;
    ui->hue = (SDL_FRect){x0, y, inner, hue_h};
    y += hue_h + g2;
    float sw = row_h;
    ui->swatch = (SDL_FRect){x0, y, sw, sw};
    float fw_ = fminf(8.5f * f, inner * 0.45f);
    ui->field = (SDL_FRect){x0 + sw + 0.8f * f, y + (row_h - 2.2f * f) * 0.5f, fw_, 2.2f * f};
    ui->text_x = ui->field.x + fw_ + 1.0f * f;
    ui->rgb_y = y + row_h * 0.5f - 1.05f * f;
    ui->hsv_y = y + row_h * 0.5f + 0.05f * f;
    y += row_h + g3;
    ui->cell = cell; ui->rcell = rcell;
    ui->grid_x = x0; ui->grid_y = y;
    ui->grid_box = (SDL_FRect){x0, y, inner, grid_h};
    y += grid_h + g4;
    ui->recent_y = y;
    y += rl_h;
    ui->rec_x = x0; ui->rec_y = y;
    y += rcell + g5;
    ui->footer_y = y;
    ui->laid_out = true;
}

/* ---- stato ---- */

static void sync_rgb(ColorUI *ui) { ui->rgb = color_from_hsv(ui->hsv); }

/* Imposta un colore RGB esatto, conservando la tonalità se il nuovo è grigio. */
static UiColorResult set_rgb(ColorUI *ui, uint32_t rgb)
{
    rgb &= 0xffffff;
    ColorHSV n = color_to_hsv(rgb);
    if (n.s <= 0.0f || n.v <= 0.0f) n.h = ui->hsv.h;
    bool changed = rgb != ui->rgb;
    ui->hsv = n;
    ui->rgb = rgb;
    ui->dirty = true;
    return changed ? UICOLOR_CHANGED : UICOLOR_NONE;
}

static UiColorResult set_hsv(ColorUI *ui, ColorHSV h)
{
    uint32_t old = ui->rgb;
    h.s = clampf(h.s, 0.0f, 1.0f);
    h.v = clampf(h.v, 0.0f, 1.0f);
    h.h = fmodf(h.h, 360.0f);
    if (h.h < 0.0f) h.h += 360.0f;
    ui->hsv = h;
    sync_rgb(ui);
    ui->dirty = true;
    return ui->rgb != old ? UICOLOR_CHANGED : UICOLOR_NONE;
}

static void set_zone(ColorUI *ui, int z)
{
    if (z == ui->zone) return;
    ui->zone = z;
    ui->hexlen = 0;
    ui->hexbuf[0] = '\0';
    if (z == ZONE_PALETTE) {
        int m = match_slot(ui);
        if (m >= 0) ui->pal_cur = m;
    }
    ui->dirty = true;
}

/* ---- API ---- */

ColorUI *ui_color_create(SDL_Renderer *ren, const char *font_path)
{
    ColorUI *ui = calloc(1, sizeof *ui);
    if (!ui) return NULL;
    ui->ren = ren;
    ui->font_path = font_path ? SDL_strdup(font_path) : NULL;
    return ui;
}

void ui_color_destroy(ColorUI *ui)
{
    if (!ui) return;
    ui_color_close(ui);
    SDL_free(ui->font_path);
    free(ui);
}

void ui_color_open(ColorUI *ui, int pixel_w, int pixel_h, uint32_t current,
                   const uint32_t *recent, int recent_count)
{
    if (!ui) return;
    if (!ui->font && ui->font_path) ui->font = TTF_OpenFont(ui->font_path, 24.0f);
    if (!ui->disc) ui->disc = make_disc(ui, false);
    if (!ui->ring) ui->ring = make_disc(ui, true);
    current &= 0xffffff;
    ui->open = true;
    ui->win_w = pixel_w;
    ui->win_h = pixel_h;
    ui->original = current;
    ui->rgb = current;
    ui->hsv = color_to_hsv(current);
    ui->nrecent = 0;
    for (int i = 0; recent && i < recent_count && ui->nrecent < MAX_RECENT; i++)
        ui->recent[ui->nrecent++] = recent[i] & 0xffffff;
    ui->zone = ZONE_SQUARE;
    ui->drag = DRAG_NONE;
    ui->pal_cur = 0;
    ui->hexlen = 0;
    ui->hexbuf[0] = '\0';
    layout(ui);
    ui->dirty = true;
}

void ui_color_close(ColorUI *ui)
{
    if (!ui) return;
    labels_free(ui);
    if (ui->panel_tex) SDL_DestroyTexture(ui->panel_tex);
    if (ui->disc) SDL_DestroyTexture(ui->disc);
    if (ui->ring) SDL_DestroyTexture(ui->ring);
    ui->panel_tex = ui->disc = ui->ring = NULL;
    if (ui->font) TTF_CloseFont(ui->font);
    ui->font = NULL;
    ui->open = false;
    ui->dirty = false;
    ui->laid_out = false;
    ui->drag = DRAG_NONE;
}

bool ui_color_is_open(const ColorUI *ui) { return ui && ui->open; }

void ui_color_resize(ColorUI *ui, int pixel_w, int pixel_h)
{
    if (!ui || !ui->open) return;
    if (pixel_w == ui->win_w && pixel_h == ui->win_h) return;
    ui->win_w = pixel_w;
    ui->win_h = pixel_h;
    layout(ui);
    ui->dirty = true;
}

uint32_t ui_color_value(const ColorUI *ui) { return ui ? ui->rgb : 0; }
uint32_t ui_color_original(const ColorUI *ui) { return ui ? ui->original : 0; }
bool ui_color_dirty(const ColorUI *ui) { return ui && ui->open && ui->dirty; }

static int hex_digit(SDL_Keycode k)
{
    if (k >= SDLK_0 && k <= SDLK_9) return (int)(k - SDLK_0);
    if (k >= SDLK_KP_1 && k <= SDLK_KP_9) return (int)(k - SDLK_KP_1) + 1;
    if (k == SDLK_KP_0) return 0;
    if (k >= SDLK_A && k <= SDLK_F) return (int)(k - SDLK_A) + 10;
    return -1;
}

static UiColorResult key_palette(ColorUI *ui, SDL_Keycode key)
{
    int r, c;
    slot_rc(ui->pal_cur, &r, &c);
    int nr = COLOR_PALETTE_ROWS + 1 + (ui->nrecent > 0 ? 1 : 0);
    switch (key) {
    case SDLK_LEFT:  if (c > 0) c--; break;
    case SDLK_RIGHT: if (c < row_len(ui, r) - 1) c++; break;
    case SDLK_UP:    if (r > 0) r--; break;
    case SDLK_DOWN:  if (r < nr - 1) r++; break;
    default: return UICOLOR_NONE;
    }
    int len = row_len(ui, r);
    if (c > len - 1) c = len - 1;
    ui->pal_cur = slot_from_rc(r, c);
    ui->dirty = true;
    return set_rgb(ui, slot_color(ui, ui->pal_cur));
}

UiColorResult ui_color_key(ColorUI *ui, SDL_Keycode key, SDL_Keymod mod)
{
    if (!ui || !ui->open) return UICOLOR_NONE;
    bool shift = (mod & SDL_KMOD_SHIFT) != 0;
    switch (key) {
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        return UICOLOR_APPLY;
    case SDLK_ESCAPE:
        ui->rgb = ui->original;
        ui->hsv = color_to_hsv(ui->original);
        return UICOLOR_CANCEL;
    case SDLK_TAB:
        set_zone(ui, (ui->zone + (shift ? ZONE_COUNT - 1 : 1)) % ZONE_COUNT);
        return UICOLOR_NONE;
    default: break;
    }

    ColorHSV h = ui->hsv;
    switch (ui->zone) {
    case ZONE_SQUARE: {
        float st = shift ? 0.1f : 0.01f;
        switch (key) {
        case SDLK_LEFT:  h.s -= st; break;
        case SDLK_RIGHT: h.s += st; break;
        case SDLK_UP:    h.v += st; break;
        case SDLK_DOWN:  h.v -= st; break;
        default: return UICOLOR_NONE;
        }
        return set_hsv(ui, h);
    }
    case ZONE_HUE: {
        float st = shift ? 10.0f : 1.0f;
        if (key == SDLK_LEFT) h.h -= st;
        else if (key == SDLK_RIGHT) h.h += st;
        else return UICOLOR_NONE;
        return set_hsv(ui, h);
    }
    case ZONE_PALETTE:
        return key_palette(ui, key);
    default: {
        if (key == SDLK_BACKSPACE) {
            if (ui->hexlen > 0) { ui->hexbuf[--ui->hexlen] = '\0'; ui->dirty = true; }
            return UICOLOR_NONE;
        }
        int d = hex_digit(key);
        if (d < 0 || ui->hexlen >= 6) return UICOLOR_NONE;
        ui->hexbuf[ui->hexlen++] = "0123456789abcdef"[d];
        ui->hexbuf[ui->hexlen] = '\0';
        ui->dirty = true;
        uint32_t v;
        if (ui->hexlen == 6 && color_parse_hex(ui->hexbuf, &v)) return set_rgb(ui, v);
        return UICOLOR_NONE;
    }
    }
}

static UiColorResult drag_to(ColorUI *ui, float x, float y)
{
    ColorHSV h = ui->hsv;
    if (ui->drag == DRAG_SQUARE) {
        h.s = clampf((x - ui->sq.x) / ui->sq.w, 0.0f, 1.0f);
        h.v = 1.0f - clampf((y - ui->sq.y) / ui->sq.h, 0.0f, 1.0f);
    } else {
        h.h = clampf((x - ui->hue.x) / ui->hue.w, 0.0f, 1.0f) * 359.99f;
    }
    return set_hsv(ui, h);
}

static bool in_rect(const SDL_FRect *r, float x, float y)
{
    return x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h;
}

UiColorResult ui_color_mouse(ColorUI *ui, const SDL_Event *ev)
{
    if (!ui || !ui->open || !ev || !ui->laid_out) return UICOLOR_NONE;
    switch (ev->type) {
    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        if (ev->button.button != SDL_BUTTON_LEFT) return UICOLOR_NONE;
        float x = ev->button.x, y = ev->button.y;
        if (in_rect(&ui->sq, x, y)) {
            set_zone(ui, ZONE_SQUARE);
            ui->drag = DRAG_SQUARE;
            return drag_to(ui, x, y);
        }
        SDL_FRect hb = ui->hue;
        hb.y -= ui->fs * 0.4f; hb.h += ui->fs * 0.8f;   /* bersaglio più comodo */
        if (in_rect(&hb, x, y)) {
            set_zone(ui, ZONE_HUE);
            ui->drag = DRAG_HUE;
            return drag_to(ui, x, y);
        }
        int s = slot_hit(ui, x, y);
        if (s >= 0) {
            set_zone(ui, ZONE_PALETTE);
            ui->pal_cur = s;
            return set_rgb(ui, slot_color(ui, s));
        }
        if (in_rect(&ui->field, x, y)) set_zone(ui, ZONE_HEX);
        return UICOLOR_NONE;
    }
    case SDL_EVENT_MOUSE_MOTION:
        if (ui->drag != DRAG_NONE) return drag_to(ui, ev->motion.x, ev->motion.y);
        return UICOLOR_NONE;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (ev->button.button == SDL_BUTTON_LEFT) ui->drag = DRAG_NONE;
        return UICOLOR_NONE;
    default:
        return UICOLOR_NONE;
    }
}

/* ---- disegno ---- */

static SDL_FColor fcol(uint32_t rgb, float a)
{
    return (SDL_FColor){(float)((rgb >> 16) & 0xff) / 255.0f, (float)((rgb >> 8) & 0xff) / 255.0f,
                        (float)(rgb & 0xff) / 255.0f, a};
}

static void quad(ColorUI *ui, const SDL_FRect *r, SDL_FColor tl, SDL_FColor tr, SDL_FColor bl,
                 SDL_FColor br)
{
    SDL_Vertex v[4] = {
        {{r->x, r->y}, tl, {0, 0}},
        {{r->x + r->w, r->y}, tr, {0, 0}},
        {{r->x, r->y + r->h}, bl, {0, 0}},
        {{r->x + r->w, r->y + r->h}, br, {0, 0}},
    };
    static const int idx[6] = {0, 1, 2, 1, 3, 2};
    SDL_RenderGeometry(ui->ren, NULL, v, 4, idx, 6);
}

static void disc(ColorUI *ui, SDL_Texture *t, float cx, float cy, float d, uint32_t rgb, Uint8 a)
{
    if (!t) return;
    SDL_SetTextureColorMod(t, (Uint8)(rgb >> 16), (Uint8)(rgb >> 8), (Uint8)rgb);
    SDL_SetTextureAlphaMod(t, a);
    SDL_FRect r = {cx - d * 0.5f, cy - d * 0.5f, d, d};
    SDL_RenderTexture(ui->ren, t, NULL, &r);
}

static void tint_blit(ColorUI *ui, const Label *l, float x, float y, uint32_t rgb, Uint8 a)
{
    if (!l->tex) return;
    SDL_SetTextureColorMod(l->tex, (Uint8)(rgb >> 16), (Uint8)(rgb >> 8), (Uint8)rgb);
    SDL_SetTextureAlphaMod(l->tex, a);
    SDL_FRect d = {floorf(x), floorf(y), (float)l->w, (float)l->h};
    SDL_RenderTexture(ui->ren, l->tex, NULL, &d);
}

static void outline(ColorUI *ui, SDL_FRect r, float t, uint32_t rgb, Uint8 a)
{
    SDL_SetRenderDrawColor(ui->ren, (Uint8)(rgb >> 16), (Uint8)(rgb >> 8), (Uint8)rgb, a);
    SDL_FRect e[4] = {{r.x, r.y, r.w, t}, {r.x, r.y + r.h - t, r.w, t},
                      {r.x, r.y + t, t, r.h - 2 * t}, {r.x + r.w - t, r.y + t, t, r.h - 2 * t}};
    SDL_RenderFillRects(ui->ren, e, 4);
}

static SDL_FRect inflate(SDL_FRect r, float d)
{
    return (SDL_FRect){r.x - d, r.y - d, r.w + 2 * d, r.h + 2 * d};
}

void ui_color_draw(ColorUI *ui)
{
    if (!ui || !ui->open || !ui->font || !ui->laid_out) return;
    SDL_Renderer *ren = ui->ren;

    SDL_BlendMode prev = SDL_BLENDMODE_NONE;
    Uint8 pr, pg, pb, pa;
    SDL_GetRenderDrawBlendMode(ren, &prev);
    SDL_GetRenderDrawColor(ren, &pr, &pg, &pb, &pa);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);

    const uint32_t TXT = 0xb7b7b7, WHITE = 0xffffff, ACC = 0x6fa8dc;
    const float f = ui->fs;
    float th = fmaxf(1.0f, roundf(f * 0.1f));

    /* testi (ricostruiti solo se cambia la stringa) */
    char buf[48], hex[8];
    color_format_hex(ui->rgb, hex);
    if (ui->zone == ZONE_HEX && ui->hexlen > 0) snprintf(buf, sizeof buf, "#%s", ui->hexbuf);
    else snprintf(buf, sizeof buf, "%s", hex);
    label_set(ui, &ui->lab[LAB_HEX], buf);
    snprintf(buf, sizeof buf, "RGB  %d, %d, %d", (int)((ui->rgb >> 16) & 0xff),
             (int)((ui->rgb >> 8) & 0xff), (int)(ui->rgb & 0xff));
    label_set(ui, &ui->lab[LAB_RGB], buf);
    snprintf(buf, sizeof buf, "HSV  %d, %d, %d", (int)lroundf(ui->hsv.h) % 360,
             (int)lroundf(ui->hsv.s * 100.0f), (int)lroundf(ui->hsv.v * 100.0f));
    label_set(ui, &ui->lab[LAB_HSV], buf);
    label_set(ui, &ui->lab[LAB_RECENT], "Recenti");
    label_set(ui, &ui->lab[LAB_FOOTER], "Tab zona · frecce · Invio applica · Esc annulla");

    /* overlay leggero + pannello */
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 89);
    SDL_FRect all = {0, 0, (float)ui->win_w, (float)ui->win_h};
    SDL_RenderFillRect(ren, &all);
    SDL_FRect pr_ = {ui->px, ui->py, ui->pw, ui->ph};
    if (ui->panel_tex) SDL_RenderTexture(ren, ui->panel_tex, NULL, &pr_);
    else {
        SDL_SetRenderDrawColor(ren, 0x1a, 0x1a, 0x1a, 255);
        SDL_RenderFillRect(ren, &pr_);
    }

    /* quadrato S/V */
    ColorHSV pure = {ui->hsv.h, 1.0f, 1.0f};
    SDL_FColor w = fcol(0xffffff, 1.0f), hc = fcol(color_from_hsv(pure), 1.0f);
    quad(ui, &ui->sq, w, hc, w, hc);
    quad(ui, &ui->sq, fcol(0, 0.0f), fcol(0, 0.0f), fcol(0, 1.0f), fcol(0, 1.0f));

    /* barra tonalità */
    static const uint32_t hues[7] = {0xff0000, 0xffff00, 0x00ff00, 0x00ffff, 0x0000ff, 0xff00ff,
                                     0xff0000};
    for (int i = 0; i < 6; i++) {
        SDL_FRect seg = {ui->hue.x + ui->hue.w * (float)i / 6.0f, ui->hue.y, ui->hue.w / 6.0f,
                         ui->hue.h};
        quad(ui, &seg, fcol(hues[i], 1), fcol(hues[i + 1], 1), fcol(hues[i], 1), fcol(hues[i + 1], 1));
    }
    float hx = ui->hue.x + ui->hue.w * clampf(ui->hsv.h / 360.0f, 0.0f, 1.0f);
    SDL_FRect mk = {hx - th * 1.5f, ui->hue.y - f * 0.25f, th * 3.0f, ui->hue.h + f * 0.5f};
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 160);
    SDL_FRect mko = inflate(mk, 1.0f);
    SDL_RenderFillRect(ren, &mko);
    SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
    SDL_RenderFillRect(ren, &mk);

    /* cursore sul quadrato */
    float cx = ui->sq.x + ui->hsv.s * ui->sq.w, cy = ui->sq.y + (1.0f - ui->hsv.v) * ui->sq.h;
    disc(ui, ui->ring, cx, cy, f * 1.5f + 3.0f, 0x000000, 150);
    disc(ui, ui->ring, cx, cy, f * 1.5f, WHITE, 255);

    /* focus: contorno sulla zona attiva */
    if (ui->zone == ZONE_SQUARE) outline(ui, inflate(ui->sq, th + 1.0f), th, ACC, 255);
    else if (ui->zone == ZONE_HUE) outline(ui, inflate(ui->hue, th + 3.0f + f * 0.25f), th, ACC, 255);

    /* campione + campo HEX */
    float sd = ui->swatch.w;
    float scx = ui->swatch.x + sd * 0.5f, scy = ui->swatch.y + sd * 0.5f;
    disc(ui, ui->ring, scx, scy, sd, 0x555555, 255);
    disc(ui, ui->disc, scx, scy, sd - th * 2.0f - 2.0f, ui->rgb, 255);

    SDL_SetRenderDrawColor(ren, 0x11, 0x11, 0x11, 255);
    SDL_RenderFillRect(ren, &ui->field);
    bool hexf = ui->zone == ZONE_HEX;
    outline(ui, ui->field, hexf ? th : 1.0f, hexf ? ACC : 0x444444, 255);
    const Label *hl = &ui->lab[LAB_HEX];
    float tx = ui->field.x + f * 0.7f, ty = ui->field.y + (ui->field.h - (float)hl->h) * 0.5f;
    bool placeholder = hexf && ui->hexlen == 0;
    tint_blit(ui, hl, tx, ty, hexf ? WHITE : TXT, placeholder ? 110 : 255);
    if (hexf) {
        float caret = tx + (ui->hexlen > 0 ? (float)hl->w : ui->hash_w);
        SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
        SDL_FRect cr = {floorf(caret) + 1.0f, ui->field.y + ui->field.h * 0.2f, th, ui->field.h * 0.6f};
        SDL_RenderFillRect(ren, &cr);
    }
    tint_blit(ui, &ui->lab[LAB_RGB], ui->text_x, ui->rgb_y, TXT, 255);
    tint_blit(ui, &ui->lab[LAB_HSV], ui->text_x, ui->hsv_y, TXT, 255);

    /* palette, neutri, recenti */
    int sel = match_slot(ui);
    for (int i = 0; i < slots(ui); i++) {
        float sx, sy, d;
        slot_geom(ui, i, &sx, &sy, &d);
        disc(ui, ui->disc, sx, sy, d, slot_color(ui, i), 255);
        if (i == sel) disc(ui, ui->ring, sx, sy, d * 1.3f, WHITE, 255);
    }
    if (ui->zone == ZONE_PALETTE) {
        float sx, sy, d;
        slot_geom(ui, ui->pal_cur, &sx, &sy, &d);
        disc(ui, ui->ring, sx, sy, d * 1.3f + th * 1.5f, ACC, 255);
    }
    tint_blit(ui, &ui->lab[LAB_RECENT], ui->grid_x, ui->recent_y, TXT, 255);
    tint_blit(ui, &ui->lab[LAB_FOOTER], ui->grid_x, ui->footer_y, 0x808080, 255);

    ui->dirty = false;
    SDL_SetRenderDrawBlendMode(ren, prev);
    SDL_SetRenderDrawColor(ren, pr, pg, pb, pa);
}
