#include "ui/ui_settings.h"
#include "core/color.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3_ttf/SDL_ttf.h>

enum { ROW_COUNT = 15, ROW_COLOR = 7 };

typedef struct {
    SDL_Texture *tex;
    int w, h;
} Label;

struct SettingsUI {
    SDL_Renderer *ren;
    char         *font_path;
    TTF_Font     *font;
    bool          open;
    bool          dirty;
    int           win_w, win_h;
    int           sel;
    Settings      shown;       /* valori con cui sono state costruite le texture */
    bool          have_shown;
    bool          arrows;      /* il font ha le frecce */
    bool          color_req;   /* Invio sulla riga colore */

    float         pw, ph, px, py, pitch, pad;
    SDL_Texture  *panel_tex;
    SDL_Texture  *swatch_tex;    /* quadrato arrotondato bianco, colorato con color mod */
    float         swatch;        /* lato in px */
    Label         title, footer;
    Label         label[ROW_COUNT][2];   /* [riga][0=normale, 1=selezionata] */
    Label         value[ROW_COUNT][2];
};

static const char *const k_labels[ROW_COUNT] = {
    "Mostra secondi", "Formato 24 ore", "Timer predefinito", "Suono fine timer",
    "Notifica fine timer", "Dimensione", "Luminosità", "Colore cifre", "Schermo intero all'avvio", "Animazioni",
    "Pomodoro: studio", "Pomodoro: riposo", "Pomodoro: riposo lungo", "Pomodoro: cicli",
    "Pomodoro: avvio automatico"
};

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* ---- testo ---- */

static void label_free(Label *l)
{
    if (l->tex) SDL_DestroyTexture(l->tex);
    l->tex = NULL;
    l->w = l->h = 0;
}

static void label_make(SettingsUI *ui, Label *l, const char *text, SDL_Color c)
{
    label_free(l);
    SDL_Surface *s = TTF_RenderText_Blended(ui->font, text, 0, c);
    if (!s) return;
    l->tex = SDL_CreateTextureFromSurface(ui->ren, s);
    l->w = s->w;
    l->h = s->h;
    SDL_DestroySurface(s);
    if (l->tex) SDL_SetTextureScaleMode(l->tex, SDL_SCALEMODE_LINEAR);
}

static void free_textures(SettingsUI *ui)
{
    if (ui->panel_tex) SDL_DestroyTexture(ui->panel_tex);
    ui->panel_tex = NULL;
    if (ui->swatch_tex) SDL_DestroyTexture(ui->swatch_tex);
    ui->swatch_tex = NULL;
    label_free(&ui->title);
    label_free(&ui->footer);
    for (int i = 0; i < ROW_COUNT; i++)
        for (int k = 0; k < 2; k++) { label_free(&ui->label[i][k]); label_free(&ui->value[i][k]); }
}

static void fmt_value(int row, const Settings *s, char *buf, size_t n)
{
    switch (row) {
    case 0: snprintf(buf, n, "%s", s->show_seconds ? "sì" : "no"); break;
    case 1: snprintf(buf, n, "%s", s->clock_24h ? "sì" : "no"); break;
    case 2: {
        int m = s->timer_default_minutes;
        if (m < 60) snprintf(buf, n, "%d min", m);
        else if (m % 60 == 0) snprintf(buf, n, "%d h", m / 60);
        else snprintf(buf, n, "%d h %d min", m / 60, m % 60);
        break;
    }
    case 3: snprintf(buf, n, "%s", s->timer_sound ? "sì" : "no"); break;
    case 4: snprintf(buf, n, "%s", s->timer_notify ? "sì" : "no"); break;
    case 5: snprintf(buf, n, "%d%%", s->scale); break;
    case 6: snprintf(buf, n, "%d%%", s->brightness); break;
    case 7: { char hx[8]; color_format_hex(s->digit_color, hx); snprintf(buf, n, "%s", hx); break; }
    case 8: snprintf(buf, n, "%s", s->start_fullscreen ? "sì" : "no"); break;
    case 9: snprintf(buf, n, "%s", s->animations ? "sì" : "no"); break;
    case 10: snprintf(buf, n, "%d min", s->pomodoro_work); break;
    case 11: snprintf(buf, n, "%d min", s->pomodoro_break); break;
    case 12: snprintf(buf, n, "%d min", s->pomodoro_long_break); break;
    case 13: snprintf(buf, n, "%d", s->pomodoro_cycles); break;
    default: snprintf(buf, n, "%s", s->pomodoro_auto_continue ? "sì" : "no"); break;
    }
}

/* Pannello con angoli arrotondati, antialiasing per copertura. */
static SDL_Texture *make_panel(SettingsUI *ui, int w, int h, float radius, Uint8 gray)
{
    SDL_Surface *s = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32);
    if (!s) return NULL;
    for (int y = 0; y < h; y++) {
        Uint8 *row = (Uint8 *)s->pixels + (size_t)y * (size_t)s->pitch;
        for (int x = 0; x < w; x++) {
            float px = x + 0.5f, py = y + 0.5f;
            float cx = px < radius ? radius : (px > w - radius ? w - radius : px);
            float cy = py < radius ? radius : (py > h - radius ? h - radius : py);
            float cov = 1.0f;
            if (cx != px || cy != py) {
                float d = sqrtf((px - cx) * (px - cx) + (py - cy) * (py - cy));
                cov = radius - d + 0.5f;
                cov = cov < 0.0f ? 0.0f : (cov > 1.0f ? 1.0f : cov);
            }
            row[x * 4 + 0] = gray; row[x * 4 + 1] = gray; row[x * 4 + 2] = gray;
            row[x * 4 + 3] = (Uint8)(cov * 255.0f + 0.5f);
        }
    }
    SDL_Texture *t = SDL_CreateTextureFromSurface(ui->ren, s);
    SDL_DestroySurface(s);
    if (t) SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
    return t;
}

static void rebuild(SettingsUI *ui, const Settings *s)
{
    free_textures(ui);
    if (!ui->font) return;

    const float W = (float)ui->win_w, H = (float)ui->win_h;
    float density = H / 1080.0f;
    if (density < 1.0f) density = 1.0f;
    float pw = fminf(W * 0.60f, 900.0f * density);

    /* altezza testo: ~ panel_h/16, con minimo 14 px; si riduce se non entra */
    float fs = fmaxf(14.0f, H / 24.0f);
    float ph;
    for (;;) {
        ph = fs * (1.55f * ROW_COUNT + 6.0f);
        if (ph <= H * 0.94f || fs <= 8.0f) break;
        fs -= 1.0f;
    }
    TTF_SetFontSize(ui->font, fs);
    ui->pitch = fs * 1.55f;
    ui->pad = fs * 1.1f;
    ui->pw = pw;
    ui->ph = ph;
    ui->px = floorf((W - pw) * 0.5f);
    ui->py = floorf((H - ph) * 0.5f);
    ui->panel_tex = make_panel(ui, (int)pw, (int)ph, pw * 0.03f, 0x1a);
    ui->swatch = floorf(ui->pitch * 0.6f);
    ui->swatch_tex = make_panel(ui, (int)ui->swatch, (int)ui->swatch, ui->swatch * 0.28f, 0xff);

    const SDL_Color norm = {0xb7, 0xb7, 0xb7, 255}, sel = {0xff, 0xff, 0xff, 255};
    const SDL_Color dim = {0x80, 0x80, 0x80, 255};
    label_make(ui, &ui->title, "IMPOSTAZIONI", dim);
    label_make(ui, &ui->footer,
               ui->arrows ? "↑↓ scegli · ←→ cambia · Esc chiudi"
                          : "su/giù scegli · sx/dx cambia · Esc chiudi", dim);
    /* se il testo non entra in larghezza, riduci il font e rifai (una volta) */
    float maxw = pw - 2.0f * ui->pad;
    if (ui->footer.w > maxw && ui->footer.w > 0) {
        fs = fmaxf(8.0f, fs * maxw / (float)ui->footer.w);
        TTF_SetFontSize(ui->font, fs);
        label_make(ui, &ui->title, "IMPOSTAZIONI", dim);
        label_make(ui, &ui->footer,
                   ui->arrows ? "↑↓ scegli · ←→ cambia · Esc chiudi"
                              : "su/giù scegli · sx/dx cambia · Esc chiudi", dim);
    }
    char buf[64];
    for (int i = 0; i < ROW_COUNT; i++) {
        fmt_value(i, s, buf, sizeof buf);
        label_make(ui, &ui->label[i][0], k_labels[i], norm);
        label_make(ui, &ui->label[i][1], k_labels[i], sel);
        label_make(ui, &ui->value[i][0], buf, norm);
        label_make(ui, &ui->value[i][1], buf, sel);
    }
    ui->shown = *s;
    ui->have_shown = true;
    ui->dirty = false;
}

/* ---- API ---- */

SettingsUI *ui_settings_create(SDL_Renderer *ren, const char *font_path)
{
    SettingsUI *ui = calloc(1, sizeof *ui);
    if (!ui) return NULL;
    ui->ren = ren;
    ui->font_path = font_path ? SDL_strdup(font_path) : NULL;
    return ui;
}

void ui_settings_destroy(SettingsUI *ui)
{
    if (!ui) return;
    ui_settings_close(ui);
    SDL_free(ui->font_path);
    free(ui);
}

void ui_settings_open(SettingsUI *ui, int pixel_w, int pixel_h)
{
    if (!ui) return;
    if (!ui->font && ui->font_path) {
        ui->font = TTF_OpenFont(ui->font_path, 24.0f);
        if (ui->font) {
            ui->arrows = TTF_FontHasGlyph(ui->font, 0x2191) && TTF_FontHasGlyph(ui->font, 0x2193) &&
                         TTF_FontHasGlyph(ui->font, 0x2190) && TTF_FontHasGlyph(ui->font, 0x2192);
        }
    }
    ui->open = true;
    ui->win_w = pixel_w;
    ui->win_h = pixel_h;
    ui->sel = 0;
    ui->have_shown = false;
    ui->dirty = true;
}

void ui_settings_close(SettingsUI *ui)
{
    if (!ui) return;
    free_textures(ui);
    if (ui->font) TTF_CloseFont(ui->font);
    ui->font = NULL;
    ui->open = false;
    ui->dirty = false;
    ui->have_shown = false;
}

bool ui_settings_is_open(const SettingsUI *ui) { return ui && ui->open; }

void ui_settings_resize(SettingsUI *ui, int pixel_w, int pixel_h)
{
    if (!ui || !ui->open) return;
    if (pixel_w != ui->win_w || pixel_h != ui->win_h) {
        ui->win_w = pixel_w;
        ui->win_h = pixel_h;
        ui->dirty = true;
    }
}

bool ui_settings_key(SettingsUI *ui, SDL_Keycode key, SDL_Keymod mod, Settings *s)
{
    if (!ui || !ui->open || !s) return false;
    bool shift = (mod & SDL_KMOD_SHIFT) != 0;
    int dir = 0;
    switch (key) {
    case SDLK_UP:    ui->sel = (ui->sel + ROW_COUNT - 1) % ROW_COUNT; ui->dirty = true; return false;
    case SDLK_DOWN:  ui->sel = (ui->sel + 1) % ROW_COUNT;             ui->dirty = true; return false;
    case SDLK_LEFT:  dir = -1; break;
    case SDLK_RIGHT: dir = 1; break;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
    case SDLK_SPACE:
        if (ui->sel == ROW_COLOR) { ui->color_req = true; return false; }
        dir = 1;
        break;
    default: return false;
    }

    Settings old = *s;
    switch (ui->sel) {
    case 0: s->show_seconds = !s->show_seconds; break;
    case 1: s->clock_24h = !s->clock_24h; break;
    case 2: s->timer_default_minutes = clampi(s->timer_default_minutes + dir * (shift ? 60 : 5), 1, 1439); break;
    case 3: s->timer_sound = !s->timer_sound; break;
    case 4: s->timer_notify = !s->timer_notify; break;
    case 5: s->scale = clampi(s->scale + dir * (shift ? 10 : 5), 50, 100); break;
    case 6: s->brightness = clampi(s->brightness + dir * (shift ? 20 : 10), 20, 100); break;
    case 8: s->start_fullscreen = !s->start_fullscreen; break;
    case 9: s->animations = !s->animations; break;
    case 10: s->pomodoro_work = clampi(s->pomodoro_work + dir * (shift ? 5 : 1), 1, 180); break;
    case 11: s->pomodoro_break = clampi(s->pomodoro_break + dir * (shift ? 5 : 1), 1, 60); break;
    case 12: s->pomodoro_long_break = clampi(s->pomodoro_long_break + dir * (shift ? 5 : 1), 1, 120); break;
    case 13: s->pomodoro_cycles = clampi(s->pomodoro_cycles + dir, 1, 12); break;
    case 14: s->pomodoro_auto_continue = !s->pomodoro_auto_continue; break;
    case ROW_COLOR: s->digit_color = color_palette_step(s->digit_color, dir); break;
    default: break;
    }
    bool changed = memcmp(&old, s, sizeof old) != 0;
    if (changed) ui->dirty = true;
    return changed;
}

bool ui_settings_take_color_request(SettingsUI *ui)
{
    if (!ui || !ui->color_req) return false;
    ui->color_req = false;
    return true;
}

static void blit(SettingsUI *ui, const Label *l, float x, float y)
{
    if (!l->tex) return;
    SDL_FRect d = {x, y, (float)l->w, (float)l->h};
    SDL_RenderTexture(ui->ren, l->tex, NULL, &d);
}

void ui_settings_draw(SettingsUI *ui, const Settings *s)
{
    if (!ui || !ui->open || !s || !ui->font) return;
    if (ui->dirty || !ui->have_shown || memcmp(&ui->shown, s, sizeof *s) != 0) rebuild(ui, s);

    SDL_BlendMode prev = SDL_BLENDMODE_NONE;
    Uint8 pr, pg, pb, pa;
    SDL_GetRenderDrawBlendMode(ui->ren, &prev);
    SDL_GetRenderDrawColor(ui->ren, &pr, &pg, &pb, &pa);
    SDL_SetRenderDrawBlendMode(ui->ren, SDL_BLENDMODE_BLEND);

    SDL_SetRenderDrawColor(ui->ren, 0, 0, 0, 191);
    SDL_FRect all = {0, 0, (float)ui->win_w, (float)ui->win_h};
    SDL_RenderFillRect(ui->ren, &all);

    SDL_FRect pr_ = {ui->px, ui->py, ui->pw, ui->ph};
    if (ui->panel_tex) SDL_RenderTexture(ui->ren, ui->panel_tex, NULL, &pr_);
    else {
        SDL_SetRenderDrawColor(ui->ren, 0x1a, 0x1a, 0x1a, 255);
        SDL_RenderFillRect(ui->ren, &pr_);
    }

    float x0 = ui->px + ui->pad, x1 = ui->px + ui->pw - ui->pad;
    float y = ui->py + ui->pad * 0.8f;
    blit(ui, &ui->title, x0, y);
    y += ui->title.h + ui->pitch * 0.45f;

    for (int i = 0; i < ROW_COUNT; i++) {
        bool is_sel = (i == ui->sel);
        float ry = y + (float)i * ui->pitch;
        if (is_sel) {
            SDL_SetRenderDrawColor(ui->ren, 0x26, 0x26, 0x26, 255);
            SDL_FRect hl = {ui->px + ui->pad * 0.5f, ry, ui->pw - ui->pad, ui->pitch};
            SDL_RenderFillRect(ui->ren, &hl);
        }
        const Label *lb = &ui->label[i][is_sel], *vl = &ui->value[i][is_sel];
        blit(ui, lb, x0, ry + (ui->pitch - (float)lb->h) * 0.5f);
        float vx = x1 - (float)vl->w;
        if (i == ROW_COLOR) {
            float sw = ui->swatch, gap = ui->pad * 0.5f;
            vx -= sw + gap;
            SDL_FRect sr = {x1 - sw, ry + (ui->pitch - sw) * 0.5f, sw, sw};
            if (ui->swatch_tex) {
                SDL_SetTextureColorMod(ui->swatch_tex, (Uint8)(s->digit_color >> 16),
                                       (Uint8)(s->digit_color >> 8), (Uint8)s->digit_color);
                SDL_RenderTexture(ui->ren, ui->swatch_tex, NULL, &sr);
            } else {
                SDL_SetRenderDrawColor(ui->ren, (Uint8)(s->digit_color >> 16),
                                       (Uint8)(s->digit_color >> 8), (Uint8)s->digit_color, 255);
                SDL_RenderFillRect(ui->ren, &sr);
            }
        }
        blit(ui, vl, vx, ry + (ui->pitch - (float)vl->h) * 0.5f);
    }
    y += (float)ROW_COUNT * ui->pitch + ui->pitch * 0.35f;
    blit(ui, &ui->footer, x0, y);

    SDL_SetRenderDrawBlendMode(ui->ren, prev);
    SDL_SetRenderDrawColor(ui->ren, pr, pg, pb, pa);
}
