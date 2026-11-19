#include "ui/ui_stats.h"
#include "core/stats.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <SDL3_ttf/SDL_ttf.h>

enum { SUM_ROWS = 4, DAYS = 7, SUBJ_ROWS = 4 };

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
    char          subjects[SUBJECTS_MAX][SUBJECT_LEN];
    int           subject_count;
    int           filter;      /* 0 = Tutte, 1..n = materie, n+1 = Senza materia */
    StatsSubjectTotal totals[SUBJ_ROWS];
    int           total_count; /* righe "Per materia" (solo vista Tutte) */
    int           daily_goal_min, weekly_goal_h;
    StatsGoals    goals;       /* calcolati all'apertura, indipendenti dal filtro */

    float         pw, ph, px, py, pitch, pad;
    SDL_Texture  *panel_tex;
    Label         title, filter_lbl, section, subj_section, footer;
    Label         subj_name[SUBJ_ROWS], subj_value[SUBJ_ROWS];
    Label         row_label[SUM_ROWS], row_value[SUM_ROWS];
    Label         day_label[DAYS], day_value[DAYS];
    Label         goal_section, goal_label[2], goal_value[2], goal_streak;
    float         goal_label_w, goal_value_w;
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
    label_free(&ui->filter_lbl);
    label_free(&ui->subj_section);
    label_free(&ui->goal_section);
    label_free(&ui->goal_streak);
    for (int i = 0; i < 2; i++) { label_free(&ui->goal_label[i]); label_free(&ui->goal_value[i]); }
    for (int i = 0; i < SUBJ_ROWS; i++) { label_free(&ui->subj_name[i]); label_free(&ui->subj_value[i]); }
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

/* nome del filtro corrente; NULL come valore di stats_summary per "Tutte" */
static const char *filter_value(const StatsUI *ui)
{
    if (ui->filter == 0) return NULL;
    if (ui->filter <= ui->subject_count) return ui->subjects[ui->filter - 1];
    return "";
}

static const char *filter_name(const StatsUI *ui)
{
    if (ui->filter == 0) return "Tutte";
    if (ui->filter <= ui->subject_count) return ui->subjects[ui->filter - 1];
    return "Senza materia";
}

/* rilegge il CSV per il filtro corrente (solo apertura e cambio filtro) */
static void refresh(StatsUI *ui)
{
    time_t now = time(NULL);
    stats_summary(&ui->sum, now, filter_value(ui));
    ui->total_count = 0;
    if (ui->filter == 0) {
        int n = stats_subject_totals(ui->totals, SUBJ_ROWS, now);
        ui->total_count = n < 0 ? 0 : (n > SUBJ_ROWS ? SUBJ_ROWS : n);
    }
    ui->dirty = true;
}

static bool goals_on(const StatsUI *ui) { return ui->daily_goal_min > 0 || ui->weekly_goal_h > 0; }

/* barre obiettivo da mostrare: 0 = oggi, 1 = settimana */
static bool goal_active(const StatsUI *ui, int i)
{
    return i == 0 ? ui->daily_goal_min > 0 : ui->weekly_goal_h > 0;
}

static long goal_target_s(const StatsUI *ui, int i)
{
    return i == 0 ? (long)ui->daily_goal_min * 60L : (long)ui->weekly_goal_h * 3600L;
}

static long goal_done_s(const StatsUI *ui, int i) { return i == 0 ? ui->goals.today_s : ui->goals.week_s; }

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
    const int nt = ui->total_count;
    for (;;) {
        /* + riga del filtro (0.9 passi) + sezione "Per materia" (1.0 + 0.95 per riga + 0.35 passi) */
        float units = SUM_ROWS + 1.3f + 0.95f * DAYS + 0.9f + (nt > 0 ? 1.35f + 0.95f * (float)nt : 0.0f);
        if (goals_on(ui)) {
            int nb = (ui->daily_goal_min > 0) + (ui->weekly_goal_h > 0);
            units += 1.35f + 0.95f * (float)(nb + (ui->daily_goal_min > 0 ? 1 : 0));
        }
        ph = fs * (1.5f * units + 5.8f);
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
    label_make(ui, &ui->subj_section, "Per materia (7 giorni)", dim);

    /* frecce e angolari solo se il font li ha, altrimenti ASCII */
    bool ang = TTF_FontHasGlyph(ui->font, 0x2039) && TTF_FontHasGlyph(ui->font, 0x203A);
    bool arr = TTF_FontHasGlyph(ui->font, 0x2190) && TTF_FontHasGlyph(ui->font, 0x2192);
    char buf[64], fbuf[160];
    snprintf(fbuf, sizeof fbuf, ang ? "\xe2\x80\xb9  %s  \xe2\x80\xba" : "<  %s  >", filter_name(ui));
    label_make(ui, &ui->filter_lbl, fbuf, white);
    label_make(ui, &ui->footer, arr ? "\xe2\x86\x90/\xe2\x86\x92 materia  \xc2\xb7  Esc chiudi"
                                    : "Frecce materia  -  Esc chiudi", dim);

    for (int i = 0; i < nt; i++) {
        const char *nm = ui->totals[i].name[0] ? ui->totals[i].name : "Senza materia";
        stats_format_duration(ui->totals[i].week_s, buf, sizeof buf);
        label_make(ui, &ui->subj_name[i], nm, norm);
        label_make(ui, &ui->subj_value[i], buf, norm);
    }

    ui->goal_label_w = ui->goal_value_w = 0.0f;
    if (goals_on(ui)) {
        label_make(ui, &ui->goal_section, "Obiettivi", dim);
        static const char *const names[2] = {"Oggi", "Settimana"};
        for (int i = 0; i < 2; i++) {
            if (!goal_active(ui, i)) continue;
            char a[32], b[32], vb[80];
            long done = goal_done_s(ui, i), tgt = goal_target_s(ui, i);
            stats_format_duration(done, a, sizeof a);
            stats_format_duration(tgt, b, sizeof b);
            snprintf(vb, sizeof vb, "%s / %s", a, b);
            label_make(ui, &ui->goal_label[i], names[i], norm);
            label_make(ui, &ui->goal_value[i], vb, done >= tgt ? white : norm);
            if ((float)ui->goal_label[i].w > ui->goal_label_w) ui->goal_label_w = (float)ui->goal_label[i].w;
            if ((float)ui->goal_value[i].w > ui->goal_value_w) ui->goal_value_w = (float)ui->goal_value[i].w;
        }
        if (ui->daily_goal_min > 0) {
            int n = ui->goals.goal_streak_days;
            snprintf(buf, sizeof buf, "Obiettivo raggiunto %d %s di fila", n, n == 1 ? "giorno" : "giorni");
            label_make(ui, &ui->goal_streak, buf, norm);
        }
    }

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

void ui_stats_open(StatsUI *ui, int pixel_w, int pixel_h,
                   const char subjects[][SUBJECT_LEN], int subject_count,
                   int daily_goal_min, int weekly_goal_h)
{
    if (!ui) return;
    ui->daily_goal_min = daily_goal_min > 0 ? daily_goal_min : 0;
    ui->weekly_goal_h = weekly_goal_h > 0 ? weekly_goal_h : 0;
    memset(&ui->goals, 0, sizeof ui->goals);
    if (goals_on(ui))
        stats_goals(&ui->goals, time(NULL), (long)ui->daily_goal_min * 60L);
    ui->subject_count = 0;
    for (int i = 0; subjects && i < subject_count && i < SUBJECTS_MAX; i++) {
        snprintf(ui->subjects[i], SUBJECT_LEN, "%s", subjects[i]);
        ui->subject_count++;
    }
    ui->filter = 0;
    if (!ui->font && ui->font_path) ui->font = TTF_OpenFont(ui->font_path, 24.0f);
    refresh(ui);
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
    y += (float)ui->title.h + ui->pitch * 0.2f;
    blit(ui, &ui->filter_lbl, x0 + (x1 - x0 - (float)ui->filter_lbl.w) * 0.5f, y);
    y += (float)ui->filter_lbl.h + ui->pitch * 0.45f;

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
    if (ui->total_count > 0) {
        blit(ui, &ui->subj_section, x0, y);
        y += ui->pitch * 1.0f;
        for (int i = 0; i < ui->total_count; i++) {
            float ry = y + (float)i * bpitch;
            const Label *lb = &ui->subj_name[i], *vl = &ui->subj_value[i];
            blit(ui, lb, x0, ry + (bpitch - (float)lb->h) * 0.5f);
            blit(ui, vl, x1 - (float)vl->w, ry + (bpitch - (float)vl->h) * 0.5f);
        }
        y += (float)ui->total_count * bpitch + ui->pitch * 0.35f;
    }
    if (goals_on(ui)) {
        blit(ui, &ui->goal_section, x0, y);
        y += ui->pitch * 1.0f;
        const float gx0 = x0 + ui->goal_label_w + gap;
        const float gx1 = x1 - ui->goal_value_w - gap;
        const float gw = gx1 > gx0 ? gx1 - gx0 : 0.0f;
        int k = 0;
        for (int i = 0; i < 2; i++) {
            if (!goal_active(ui, i)) continue;
            float ry = y + (float)k++ * bpitch;
            const Label *lb = &ui->goal_label[i], *vl = &ui->goal_value[i];
            blit(ui, lb, x0, ry + (bpitch - (float)lb->h) * 0.5f);
            blit(ui, vl, x1 - (float)vl->w, ry + (bpitch - (float)vl->h) * 0.5f);
            float by = floorf(ry + (bpitch - bh) * 0.5f);
            SDL_SetRenderDrawColor(ui->ren, 0xb7, 0xb7, 0xb7, 38);
            SDL_FRect track = {gx0, by, gw, bh};
            SDL_RenderFillRect(ui->ren, &track);
            long done = goal_done_s(ui, i), tgt = goal_target_s(ui, i);
            if (done > 0) {
                bool full = done >= tgt;
                float len = full ? gw : floorf(gw * (float)done / (float)tgt);
                if (len < 2.0f) len = 2.0f;
                SDL_SetRenderDrawColor(ui->ren, 0xb7, 0xb7, 0xb7, full ? 255 : 153);
                SDL_FRect bar = {gx0, by, len, bh};
                SDL_RenderFillRect(ui->ren, &bar);
            }
        }
        y += (float)k * bpitch;
        if (ui->daily_goal_min > 0) {
            blit(ui, &ui->goal_streak, x0, y + (bpitch - (float)ui->goal_streak.h) * 0.5f);
            y += bpitch;
        }
        y += ui->pitch * 0.35f;
    }
    blit(ui, &ui->footer, x0, y);

    SDL_SetRenderDrawBlendMode(ui->ren, prev);
    SDL_SetRenderDrawColor(ui->ren, pr, pg, pb, pa);
}

bool ui_stats_key(StatsUI *ui, SDL_Keycode key)
{
    if (!ui || !ui->open) return false;
    int n = ui->subject_count + 2;   /* Tutte + materie + Senza materia */
    if (key == SDLK_LEFT) ui->filter = (ui->filter + n - 1) % n;
    else if (key == SDLK_RIGHT) ui->filter = (ui->filter + 1) % n;
    else return false;
    refresh(ui);
    return true;
}
