#include "ui_stats.h"
#include "stats.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <SDL3_ttf/SDL_ttf.h>

enum { SUM_ROWS = 4, DAYS = 7 };

typedef struct {
    SDL_Texture *tex;
    int w, h;
} Label;

struct StatsUI {
    SDL_Renderer *ren;
    char         *font_path;
    TTF_Font     *font;
    bool          open;
    bool          dirty;
    int           win_w, win_h;
    StatsSummary  sum;

    float         pw, ph, px, py, pitch, pad;
    SDL_Texture  *panel_tex;
    Label         title, section, footer;
    Label         row_label[SUM_ROWS], row_value[SUM_ROWS];
    Label         day_label[DAYS], day_value[DAYS];
    float         label_w, value_w;
};

static const char *const k_rows[SUM_ROWS] = {"Oggi", "Questo mese", "Giorni consecutivi", "Sessioni completate"};
static const char *const k_days[7] = {"dom", "lun", "mar", "mer", "gio", "ven", "sab"}; /* tm_wday */

static void label_free(Label *l)
{
    if (l->tex) SDL_DestroyTexture(l->tex);
    l->tex = NULL;
    l->w = l->h = 0;
}

static void label_make(StatsUI *ui, Label *l, const char *text, SDL_Color c)
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

static void free_textures(StatsUI *ui)
{
    if (ui->panel_tex) SDL_DestroyTexture(ui->panel_tex);
    ui->panel_tex = NULL;
    label_free(&ui->title);
    label_free(&ui->section);
    label_free(&ui->footer);
    for (int i = 0; i < SUM_ROWS; i++) { label_free(&ui->row_label[i]); label_free(&ui->row_value[i]); }
    for (int i = 0; i < DAYS; i++) { label_free(&ui->day_label[i]); label_free(&ui->day_value[i]); }
}

/* Pannello con angoli arrotondati, antialiasing per copertura (come ui_settings). */
static SDL_Texture *make_panel(StatsUI *ui, int w, int h, float radius)
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
            row[x * 4 + 0] = 0x1a; row[x * 4 + 1] = 0x1a; row[x * 4 + 2] = 0x1a;
            row[x * 4 + 3] = (Uint8)(cov * 255.0f + 0.5f);
        }
    }
    SDL_Texture *t = SDL_CreateTextureFromSurface(ui->ren, s);
    SDL_DestroySurface(s);
    if (t) SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
    return t;
}

static void rebuild(StatsUI *ui)
{
    free_textures(ui);
    if (!ui->font) return;

    const float W = (float)ui->win_w, H = (float)ui->win_h;
    float density = H / 1080.0f;
    if (density < 1.0f) density = 1.0f;
    float pw = fminf(W * 0.60f, 900.0f * density);

    /* righe: titolo(~1.6) + 4 righe + sezione(1.3) + 7 barre(0.95) + footer(1.4) + margini */
    float fs = fmaxf(12.0f, H / 28.0f);
    float ph;
    for (;;) {
        ph = fs * (1.5f * SUM_ROWS + 1.3f * 1.5f + 0.95f * 1.5f * DAYS + 5.8f);
        if (ph <= H * 0.94f || fs <= 8.0f) break;
        fs -= 1.0f;
    }
    TTF_SetFontSize(ui->font, fs);
    ui->pitch = fs * 1.5f;
    ui->pad = fs * 1.1f;
    ui->pw = pw;
    ui->ph = ph;
    ui->px = floorf((W - pw) * 0.5f);
    ui->py = floorf((H - ph) * 0.5f);
    ui->panel_tex = make_panel(ui, (int)pw, (int)ph, pw * 0.03f);

    const SDL_Color norm = {0xb7, 0xb7, 0xb7, 255}, white = {0xff, 0xff, 0xff, 255};
    const SDL_Color dim = {0x80, 0x80, 0x80, 255};
    label_make(ui, &ui->title, "STATISTICHE", dim);
    label_make(ui, &ui->section, "Ultimi 7 giorni", dim);
    label_make(ui, &ui->footer, "Esc chiudi", dim);

    char buf[64];
    stats_format_duration(ui->sum.today_s, buf, sizeof buf);
    label_make(ui, &ui->row_label[0], k_rows[0], norm);
    label_make(ui, &ui->row_value[0], buf, white);
    stats_format_duration(ui->sum.month_s, buf, sizeof buf);
    label_make(ui, &ui->row_label[1], k_rows[1], norm);
    label_make(ui, &ui->row_value[1], buf, norm);
    snprintf(buf, sizeof buf, "%d", ui->sum.streak_days);
    label_make(ui, &ui->row_label[2], k_rows[2], norm);
    label_make(ui, &ui->row_value[2], buf, norm);
    snprintf(buf, sizeof buf, "%d", ui->sum.completed_sessions);
    label_make(ui, &ui->row_label[3], k_rows[3], norm);
    label_make(ui, &ui->row_value[3], buf, norm);

    ui->label_w = 0.0f;
    ui->value_w = 0.0f;
    for (int i = 0; i < DAYS; i++) {
        bool today = (i == DAYS - 1);
        int wd = ui->sum.week_wday[i];
        label_make(ui, &ui->day_label[i], k_days[wd >= 0 && wd < 7 ? wd : 0], today ? white : norm);
        stats_format_duration(ui->sum.week_s[i], buf, sizeof buf);
        label_make(ui, &ui->day_value[i], buf, today ? white : norm);
        if ((float)ui->day_label[i].w > ui->label_w) ui->label_w = (float)ui->day_label[i].w;
        if ((float)ui->day_value[i].w > ui->value_w) ui->value_w = (float)ui->day_value[i].w;
    }
    ui->dirty = false;
}

/* ---- API ---- */

StatsUI *ui_stats_create(SDL_Renderer *ren, const char *font_path)
{
    StatsUI *ui = calloc(1, sizeof *ui);
    if (!ui) return NULL;
    ui->ren = ren;
    ui->font_path = font_path ? SDL_strdup(font_path) : NULL;
    return ui;
}

void ui_stats_destroy(StatsUI *ui)
{
    if (!ui) return;
    ui_stats_close(ui);
    SDL_free(ui->font_path);
    free(ui);
}

void ui_stats_open(StatsUI *ui, int pixel_w, int pixel_h)
{
    if (!ui) return;
    if (!ui->font && ui->font_path) ui->font = TTF_OpenFont(ui->font_path, 24.0f);
    stats_summary(&ui->sum, time(NULL));
    ui->open = true;
    ui->win_w = pixel_w;
    ui->win_h = pixel_h;
    ui->dirty = true;
}

void ui_stats_close(StatsUI *ui)
{
    if (!ui) return;
    free_textures(ui);
    if (ui->font) TTF_CloseFont(ui->font);
    ui->font = NULL;
    ui->open = false;
    ui->dirty = false;
}

bool ui_stats_is_open(const StatsUI *ui) { return ui && ui->open; }

void ui_stats_resize(StatsUI *ui, int pixel_w, int pixel_h)
{
    if (!ui || !ui->open) return;
    if (pixel_w != ui->win_w || pixel_h != ui->win_h) {
        ui->win_w = pixel_w;
        ui->win_h = pixel_h;
        ui->dirty = true;
    }
}

static void blit(StatsUI *ui, const Label *l, float x, float y)
{
    if (!l->tex) return;
    SDL_FRect d = {x, y, (float)l->w, (float)l->h};
    SDL_RenderTexture(ui->ren, l->tex, NULL, &d);
}

void ui_stats_draw(StatsUI *ui)
{
    if (!ui || !ui->open || !ui->font) return;
    if (ui->dirty) rebuild(ui);

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

    const float x0 = ui->px + ui->pad, x1 = ui->px + ui->pw - ui->pad;
    float y = ui->py + ui->pad * 0.8f;
    blit(ui, &ui->title, x0, y);
    y += (float)ui->title.h + ui->pitch * 0.45f;

    for (int i = 0; i < SUM_ROWS; i++) {
        float ry = y + (float)i * ui->pitch;
        const Label *lb = &ui->row_label[i], *vl = &ui->row_value[i];
        blit(ui, lb, x0, ry + (ui->pitch - (float)lb->h) * 0.5f);
        blit(ui, vl, x1 - (float)vl->w, ry + (ui->pitch - (float)vl->h) * 0.5f);
    }
    y += (float)SUM_ROWS * ui->pitch + ui->pitch * 0.3f;

    blit(ui, &ui->section, x0, y);
    y += ui->pitch * 1.0f;

    long maxv = 0;
    for (int i = 0; i < DAYS; i++) if (ui->sum.week_s[i] > maxv) maxv = ui->sum.week_s[i];
    const float bpitch = ui->pitch * 0.95f;
    const float gap = ui->pad * 0.7f;
    const float bx0 = x0 + ui->label_w + gap;
    const float bx1 = x1 - ui->value_w - gap;
    const float bw = bx1 > bx0 ? bx1 - bx0 : 0.0f;
    const float bh = floorf(bpitch * 0.42f) < 4.0f ? 4.0f : floorf(bpitch * 0.42f);
    for (int i = 0; i < DAYS; i++) {
        float ry = y + (float)i * bpitch;
        const Label *lb = &ui->day_label[i], *vl = &ui->day_value[i];
        blit(ui, lb, x0, ry + (bpitch - (float)lb->h) * 0.5f);
        blit(ui, vl, x1 - (float)vl->w, ry + (bpitch - (float)vl->h) * 0.5f);
        float by = floorf(ry + (bpitch - bh) * 0.5f);
        SDL_SetRenderDrawColor(ui->ren, 0xb7, 0xb7, 0xb7, 38);   /* pista 15% */
        SDL_FRect track = {bx0, by, bw, bh};
        SDL_RenderFillRect(ui->ren, &track);
        if (maxv > 0 && ui->sum.week_s[i] > 0) {
            float len = floorf(bw * (float)ui->sum.week_s[i] / (float)maxv);
            if (len < 2.0f) len = 2.0f;
            SDL_SetRenderDrawColor(ui->ren, 0xb7, 0xb7, 0xb7, 153);   /* barra 60% */
            SDL_FRect bar = {bx0, by, len, bh};
            SDL_RenderFillRect(ui->ren, &bar);
        }
    }
    y += (float)DAYS * bpitch + ui->pitch * 0.35f;
    blit(ui, &ui->footer, x0, y);

    SDL_SetRenderDrawBlendMode(ui->ren, prev);
    SDL_SetRenderDrawColor(ui->ren, pr, pg, pb, pa);
}
