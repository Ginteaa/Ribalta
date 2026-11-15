#include "ui/ui_subject.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <SDL3_ttf/SDL_ttf.h>

/* Righe: "Nessuna materia", le materie, (in inserimento) la riga del nome nuovo. */
enum { MAX_ROWS = SUBJECTS_MAX + 2 };

typedef struct {
    SDL_Texture *tex;
    int          w, h;
} Label;

enum { LAB_TITLE, LAB_HINT, LAB_HINT_EDIT, LAB_MARK, LAB_ERR, LAB_EDIT, LAB_COUNT };
enum { ERR_NONE, ERR_INVALID, ERR_DUP, ERR_FULL };

struct SubjectUI {
    SDL_Renderer *ren;
    char         *font_path;
    TTF_Font     *font;
    bool          open, dirty, stale, laid_out;
    int           win_w, win_h;
    Settings     *cfg;

    int   cur;                 /* riga selezionata: 0 = Nessuna materia, i = subjects[i-1] */
    int   top;                 /* prima riga visibile */
    bool  editing;
    char  name[SUBJECT_LEN];
    int   err;
    bool  has_mark, has_dot;

    float fs, pad, px, py, pw, ph, radius, rh, sbw, title_y, err_y, hint_y;
    int   rows;                /* righe visibili al massimo */
    SDL_FRect listr;

    SDL_Texture *panel_tex;
    Label        lab[LAB_COUNT];
    Label        row[MAX_ROWS];
};

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static int   clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

static int nrows(const SubjectUI *ui)
{
    return ui->cfg->subject_count + 1 + (ui->editing ? 1 : 0);
}

static int maxtop(const SubjectUI *ui)
{
    int n = nrows(ui);
    return n > ui->rows ? n - ui->rows : 0;
}

/* ---- texture ---- */

static void label_free(Label *l)
{
    if (l->tex) SDL_DestroyTexture(l->tex);
    l->tex = NULL;
    l->w = l->h = 0;
}

static void labels_free(SubjectUI *ui)
{
    for (int i = 0; i < LAB_COUNT; i++) label_free(&ui->lab[i]);
    for (int i = 0; i < MAX_ROWS; i++) label_free(&ui->row[i]);
}

static void label_render(SubjectUI *ui, Label *l, const char *text)
{
    label_free(l);
    if (!ui->font || !text[0]) return;
    SDL_Surface *s = TTF_RenderText_Blended(ui->font, text, 0, (SDL_Color){255, 255, 255, 255});
    if (!s) return;
    l->tex = SDL_CreateTextureFromSurface(ui->ren, s);
    l->w = s->w;
    l->h = s->h;
    SDL_DestroySurface(s);
    if (l->tex) SDL_SetTextureScaleMode(l->tex, SDL_SCALEMODE_LINEAR);
}

/* Accorcia con "…" finché entra in maxw pixel. */
static void ellipsize(TTF_Font *f, const char *text, int maxw, char *out, size_t outsz)
{
    int w = 0;
    size_t len = 0, full = strlen(text);
    if (TTF_MeasureString(f, text, 0, maxw, &w, &len) && len >= full) {
        snprintf(out, outsz, "%s", text);
        return;
    }
    size_t n = len < outsz - 4 ? len : outsz - 4;
    while (n > 0) {
        while (n > 0 && ((unsigned char)text[n] & 0xC0) == 0x80) n--;
        memcpy(out, text, n);
        memcpy(out + n, "\xE2\x80\xA6", 4);
        int tw = 0;
        if (!TTF_GetStringSize(f, out, 0, &tw, NULL) || tw <= maxw) return;
        n--;
    }
    snprintf(out, outsz, "\xE2\x80\xA6");
}

static SDL_Texture *make_panel(SubjectUI *ui, int w, int h, float radius)
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

static const char *hint_text(const SubjectUI *ui, bool edit)
{
    if (edit) return "Invio aggiunge \xC2\xB7 Esc annulla";
    (void)ui;
    return "Invio sceglie \xC2\xB7 N nuova \xC2\xB7 Canc elimina \xC2\xB7 Esc chiude";
}

/* Sostituisce il punto mediano se il font non lo ha. */
static void hint_fix(const SubjectUI *ui, const char *in, char *out, size_t sz)
{
    size_t o = 0;
    for (const char *p = in; *p && o + 2 < sz; p++) {
        if (ui->has_dot || (unsigned char)p[0] != 0xC2 || (unsigned char)p[1] != 0xB7) {
            out[o++] = *p;
        } else {
            out[o++] = '-';
            p++;
        }
    }
    out[o] = '\0';
}

/* ---- layout ---- */

static void layout(SubjectUI *ui)
{
    labels_free(ui);
    if (ui->panel_tex) SDL_DestroyTexture(ui->panel_tex);
    ui->panel_tex = NULL;
    ui->laid_out = false;
    if (!ui->font) return;

    const float W = (float)ui->win_w, H = (float)ui->win_h;
    float density = H / 1080.0f;
    if (density < 1.0f) density = 1.0f;
    float margin = H * 0.025f;
    float pw = fminf(W * 0.42f, 560.0f * density);
    float f = clampf(H / 52.0f, 11.0f, 40.0f);
    float pad = pw * 0.045f;
    float inner = pw - 2.0f * pad;

    TTF_SetFontSize(ui->font, f);
    char buf[96];
    hint_fix(ui, hint_text(ui, false), buf, sizeof buf);
    int fw = 0, fh = 0;
    if (TTF_GetStringSize(ui->font, buf, 0, &fw, &fh) && (float)fw > inner) {
        f = fmaxf(8.0f, f * inner / (float)fw);
        TTF_SetFontSize(ui->font, f);
    }
    ui->has_mark = TTF_FontHasGlyph(ui->font, 0x25CF);

    float title_h = 1.5f * f, g1 = 0.8f * f, rh = 1.9f * f, g3 = 0.5f * f, err_h = 1.4f * f,
          g4 = 0.5f * f, hint_h = 1.4f * f;
    float fixed = 2.0f * pad + title_h + g1 + g3 + err_h + g4 + hint_h;
    int avail = (int)floorf((H - 2.0f * margin - fixed) / rh);
    int rows = clampi(avail, 1, MAX_ROWS);
    int need = nrows(ui);
    if (rows > need) rows = need;
    float list_h = (float)rows * rh;
    float ph = fixed + list_h;
    if (ph > H) ph = H;

    ui->fs = f; ui->pad = pad; ui->pw = pw; ui->ph = ph; ui->rh = rh; ui->rows = rows;
    ui->sbw = fmaxf(3.0f, roundf(f * 0.3f));
    ui->px = floorf(W - margin - pw);
    ui->py = floorf((H - ph) * 0.5f);
    ui->radius = fminf(pw * 0.03f, 18.0f * density);
    ui->panel_tex = make_panel(ui, (int)pw, (int)ph, ui->radius);

    float x0 = ui->px + pad, y = ui->py + pad;
    ui->title_y = y;
    y += title_h + g1;
    ui->listr = (SDL_FRect){x0, y, inner, list_h};
    y += list_h + g3;
    ui->err_y = y;
    y += err_h + g4;
    ui->hint_y = y;
    ui->laid_out = true;
    ui->top = clampi(ui->top, 0, maxtop(ui));
}

static void ensure_visible(SubjectUI *ui)
{
    int sel = ui->editing ? nrows(ui) - 1 : ui->cur;
    if (ui->rows <= 0) return;
    if (sel < ui->top) ui->top = sel;
    else if (sel >= ui->top + ui->rows) ui->top = sel - ui->rows + 1;
    ui->top = clampi(ui->top, 0, maxtop(ui));
}

static void touch(SubjectUI *ui)
{
    ui->dirty = true;
    ui->stale = true;
}

/* L'elenco cambia (riga d'inserimento, eliminazione): il pannello può cambiare altezza. */
static void relayout(SubjectUI *ui)
{
    layout(ui);
    ensure_visible(ui);
    touch(ui);
}

/* ---- API ---- */

SubjectUI *ui_subject_create(SDL_Renderer *ren, const char *font_path)
{
    SubjectUI *ui = calloc(1, sizeof *ui);
    if (!ui) return NULL;
    ui->ren = ren;
    ui->font_path = font_path ? SDL_strdup(font_path) : NULL;
    return ui;
}

void ui_subject_destroy(SubjectUI *ui)
{
    if (!ui) return;
    ui_subject_close(ui);
    SDL_free(ui->font_path);
    free(ui);
}

void ui_subject_open(SubjectUI *ui, int pixel_w, int pixel_h, Settings *cfg)
{
    if (!ui || !cfg) return;
    if (ui->open) ui_subject_close(ui);
    if (!ui->font && ui->font_path) ui->font = TTF_OpenFont(ui->font_path, 24.0f);
    if (ui->font) ui->has_dot = TTF_FontHasGlyph(ui->font, 0xB7);
    ui->cfg = cfg;
    ui->win_w = pixel_w;
    ui->win_h = pixel_h;
    ui->editing = false;
    ui->name[0] = '\0';
    ui->err = ERR_NONE;
    int idx = settings_subject_find(cfg, cfg->subject);
    ui->cur = idx >= 0 ? idx + 1 : 0;
    ui->top = 0;
    ui->open = true;
    layout(ui);
    ensure_visible(ui);
    touch(ui);
}

void ui_subject_close(SubjectUI *ui)
{
    if (!ui) return;
    labels_free(ui);
    if (ui->panel_tex) SDL_DestroyTexture(ui->panel_tex);
    ui->panel_tex = NULL;
    if (ui->font) TTF_CloseFont(ui->font);
    ui->font = NULL;
    ui->open = ui->dirty = ui->stale = ui->laid_out = ui->editing = false;
    ui->cfg = NULL;
}

bool ui_subject_is_open(const SubjectUI *ui) { return ui && ui->open; }
bool ui_subject_editing(const SubjectUI *ui) { return ui && ui->open && ui->editing; }
bool ui_subject_dirty(const SubjectUI *ui) { return ui && ui->open && ui->dirty; }

void ui_subject_resize(SubjectUI *ui, int pixel_w, int pixel_h)
{
    if (!ui || !ui->open) return;
    if (pixel_w == ui->win_w && pixel_h == ui->win_h) return;
    ui->win_w = pixel_w;
    ui->win_h = pixel_h;
    relayout(ui);
}

static void trim_copy(const char *in, char *out, size_t sz)
{
    while (*in == ' ' || *in == '\t') in++;
    size_t n = strlen(in);
    while (n > 0 && (in[n - 1] == ' ' || in[n - 1] == '\t')) n--;
    if (n >= sz) n = sz - 1;
    memcpy(out, in, n);
    out[n] = '\0';
}

static void choose_current(SubjectUI *ui)
{
    Settings *c = ui->cfg;
    if (ui->cur <= 0 || ui->cur > c->subject_count) c->subject[0] = '\0';
    else snprintf(c->subject, sizeof c->subject, "%s", c->subjects[ui->cur - 1]);
}

static void select_row(SubjectUI *ui, int r)
{
    r = clampi(r, 0, ui->cfg->subject_count);
    if (r != ui->cur) {
        ui->cur = r;
        ensure_visible(ui);
        touch(ui);
    }
}

static void begin_edit(SubjectUI *ui)
{
    if (ui->cfg->subject_count >= SUBJECTS_MAX) {
        ui->err = ERR_FULL;
        touch(ui);
        return;
    }
    ui->editing = true;
    ui->name[0] = '\0';
    ui->err = ERR_NONE;
    relayout(ui);
}

static void end_edit(SubjectUI *ui)
{
    ui->editing = false;
    ui->name[0] = '\0';
    ui->err = ERR_NONE;
    relayout(ui);
}

static void commit_edit(SubjectUI *ui)
{
    Settings *c = ui->cfg;
    char t[SUBJECT_LEN];
    trim_copy(ui->name, t, sizeof t);
    int err = ERR_NONE;
    if (!settings_subject_valid(t)) err = ERR_INVALID;
    else {
        for (int i = 0; i < c->subject_count; i++)
            if (strcasecmp(c->subjects[i], t) == 0) err = ERR_DUP;
        if (err == ERR_NONE && c->subject_count >= SUBJECTS_MAX) err = ERR_FULL;
    }
    if (err == ERR_NONE && !settings_subject_add(c, t)) err = ERR_INVALID;
    if (err != ERR_NONE) {
        ui->err = err;
        touch(ui);
        return;
    }
    ui->editing = false;
    ui->name[0] = '\0';
    ui->err = ERR_NONE;
    ui->cur = c->subject_count;
    relayout(ui);
}

UiSubjectResult ui_subject_key(SubjectUI *ui, SDL_Keycode key, SDL_Keymod mod)
{
    if (!ui || !ui->open || !ui->cfg) return UISUBJ_NONE;
    if (ui->err != ERR_NONE && !ui->editing) { ui->err = ERR_NONE; touch(ui); }

    if (ui->editing) {
        switch (key) {
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            commit_edit(ui);
            break;
        case SDLK_ESCAPE:
            end_edit(ui);
            break;
        case SDLK_BACKSPACE: {
            size_t n = strlen(ui->name);
            if (n > 0) {
                n--;
                while (n > 0 && ((unsigned char)ui->name[n] & 0xC0) == 0x80) n--;
                ui->name[n] = '\0';
            }
            ui->err = ERR_NONE;
            touch(ui);
            break;
        }
        default: break;
        }
        return UISUBJ_NONE;
    }

    switch (key) {
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        choose_current(ui);
        return UISUBJ_CLOSE;
    case SDLK_ESCAPE:
    case SDLK_M:
        return UISUBJ_CLOSE;
    case SDLK_UP:   select_row(ui, ui->cur - 1); break;
    case SDLK_DOWN: select_row(ui, ui->cur + 1); break;
    case SDLK_HOME: select_row(ui, 0); break;
    case SDLK_END:  select_row(ui, ui->cfg->subject_count); break;
    case SDLK_N:
        if (!(mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI))) begin_edit(ui);
        break;
    case SDLK_DELETE:
        if (ui->cur > 0) {
            settings_subject_remove(ui->cfg, ui->cur - 1);
            if (ui->cur > ui->cfg->subject_count) ui->cur = ui->cfg->subject_count;
            relayout(ui);
        }
        break;
    default: break;
    }
    return UISUBJ_NONE;
}

void ui_subject_text(SubjectUI *ui, const char *utf8)
{
    if (!ui || !ui->open || !ui->editing || !utf8) return;
    size_t len = strlen(ui->name);
    bool changed = false;
    while (*utf8) {
        unsigned char c = (unsigned char)*utf8;
        size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        if (n == 0) { utf8++; continue; }
        size_t avail = strlen(utf8);
        if (n > avail) break;
        bool ok = true;
        for (size_t i = 1; i < n; i++)
            if (((unsigned char)utf8[i] & 0xC0) != 0x80) ok = false;
        if (n == 1 && (c < 0x20 || c == 0x7f || c == ',' || c == ';' || c == '"')) ok = false;
        if (ok && len + n < SUBJECT_LEN) {
            memcpy(ui->name + len, utf8, n);
            len += n;
            ui->name[len] = '\0';
            changed = true;
        }
        utf8 += n;
    }
    if (changed) {
        ui->err = ERR_NONE;
        touch(ui);
    }
}

static bool in_rect(const SDL_FRect *r, float x, float y)
{
    return x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h;
}

UiSubjectResult ui_subject_mouse(SubjectUI *ui, const SDL_Event *ev)
{
    if (!ui || !ui->open || !ev || !ui->laid_out) return UISUBJ_NONE;
    switch (ev->type) {
    case SDL_EVENT_MOUSE_WHEEL: {
        float y = ev->wheel.y;
        if (ev->wheel.direction == SDL_MOUSEWHEEL_FLIPPED) y = -y;
        int d = (int)lroundf(y * 3.0f);
        if (d == 0 && y != 0.0f) d = y > 0 ? 1 : -1;
        int nt = clampi(ui->top - d, 0, maxtop(ui));
        if (nt != ui->top) { ui->top = nt; touch(ui); }
        return UISUBJ_NONE;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        if (ev->button.button != SDL_BUTTON_LEFT) return UISUBJ_NONE;
        float x = ev->button.x, y = ev->button.y;
        SDL_FRect pr = {ui->px, ui->py, ui->pw, ui->ph};
        if (!in_rect(&pr, x, y)) return UISUBJ_CLOSE;
        if (ui->editing || !in_rect(&ui->listr, x, y)) return UISUBJ_NONE;
        int row = ui->top + (int)((y - ui->listr.y) / ui->rh);
        if (row < 0 || row >= nrows(ui)) return UISUBJ_NONE;
        select_row(ui, row);
        if (ev->button.clicks >= 2) {
            choose_current(ui);
            return UISUBJ_CLOSE;
        }
        return UISUBJ_NONE;
    }
    default:
        return UISUBJ_NONE;
    }
}

/* ---- disegno ---- */

static void tint_blit(SubjectUI *ui, const Label *l, float x, float y, uint32_t rgb, Uint8 a)
{
    if (!l->tex) return;
    SDL_SetTextureColorMod(l->tex, (Uint8)(rgb >> 16), (Uint8)(rgb >> 8), (Uint8)rgb);
    SDL_SetTextureAlphaMod(l->tex, a);
    SDL_FRect d = {floorf(x), floorf(y), (float)l->w, (float)l->h};
    SDL_RenderTexture(ui->ren, l->tex, NULL, &d);
}

static void rebuild(SubjectUI *ui)
{
    const Settings *c = ui->cfg;
    const float f = ui->fs;
    char buf[SUBJECT_LEN + 8], hb[96];
    float name_w = ui->listr.w - ui->sbw - 1.8f * f - 0.6f * f;

    for (int i = 0; i < LAB_COUNT; i++) label_free(&ui->lab[i]);
    for (int i = 0; i < MAX_ROWS; i++) label_free(&ui->row[i]);

    TTF_SetFontSize(ui->font, f * 1.25f);
    label_render(ui, &ui->lab[LAB_TITLE], "MATERIE");
    TTF_SetFontSize(ui->font, f);
    hint_fix(ui, hint_text(ui, false), hb, sizeof hb);
    label_render(ui, &ui->lab[LAB_HINT], hb);
    hint_fix(ui, hint_text(ui, true), hb, sizeof hb);
    label_render(ui, &ui->lab[LAB_HINT_EDIT], hb);
    label_render(ui, &ui->lab[LAB_MARK], ui->has_mark ? "\xE2\x97\x8F" : "*");

    static const char *const errs[] = {"", "Nome non valido", "Materia gi\xC3\xA0 presente",
                                       "Elenco pieno (max 8)"};
    label_render(ui, &ui->lab[LAB_ERR], errs[ui->err]);

    int total = nrows(ui);
    int last = ui->top + ui->rows;
    if (last > total) last = total;
    for (int r = ui->top; r < last; r++) {
        if (r == 0) {
            TTF_SetFontStyle(ui->font, TTF_STYLE_ITALIC);
            label_render(ui, &ui->row[r], "Nessuna materia");
            TTF_SetFontStyle(ui->font, TTF_STYLE_NORMAL);
        } else if (r <= c->subject_count) {
            ellipsize(ui->font, c->subjects[r - 1], (int)name_w, buf, sizeof buf);
            label_render(ui, &ui->row[r], buf);
        } else if (ui->name[0]) {
            ellipsize(ui->font, ui->name, (int)name_w, buf, sizeof buf);
            label_render(ui, &ui->row[r], buf);
        }
    }
    if (ui->editing && !ui->name[0]) label_render(ui, &ui->lab[LAB_EDIT], "Nome della materia");
    ui->stale = false;
}

void ui_subject_draw(SubjectUI *ui)
{
    if (!ui || !ui->open || !ui->font || !ui->laid_out) return;
    SDL_Renderer *ren = ui->ren;
    if (ui->stale) rebuild(ui);

    SDL_BlendMode prev = SDL_BLENDMODE_NONE;
    Uint8 pr, pg, pb, pa;
    SDL_GetRenderDrawBlendMode(ren, &prev);
    SDL_GetRenderDrawColor(ren, &pr, &pg, &pb, &pa);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);

    const uint32_t TXT = 0xb7b7b7, DIM = 0x808080, WHITE = 0xffffff, ACC = 0x6fa8dc, RED = 0xe06060;
    const float f = ui->fs;
    float th = fmaxf(1.0f, roundf(f * 0.1f));
    const Settings *c = ui->cfg;

    SDL_SetRenderDrawColor(ren, 0, 0, 0, 89);
    SDL_FRect all = {0, 0, (float)ui->win_w, (float)ui->win_h};
    SDL_RenderFillRect(ren, &all);
    SDL_FRect pr_ = {ui->px, ui->py, ui->pw, ui->ph};
    if (ui->panel_tex) SDL_RenderTexture(ren, ui->panel_tex, NULL, &pr_);
    else {
        SDL_SetRenderDrawColor(ren, 0x1a, 0x1a, 0x1a, 255);
        SDL_RenderFillRect(ren, &pr_);
    }

    tint_blit(ui, &ui->lab[LAB_TITLE], ui->listr.x, ui->title_y, WHITE, 255);

    int active = settings_subject_find(c, c->subject) + 1;   /* 0 = Nessuna materia */
    int total = nrows(ui);
    int last = ui->top + ui->rows;
    if (last > total) last = total;
    float textx = ui->listr.x + 1.8f * f;
    for (int r = ui->top; r < last; r++) {
        float ry = ui->listr.y + (float)(r - ui->top) * ui->rh;
        bool is_edit = ui->editing && r == total - 1;
        bool sel = is_edit || (!ui->editing && r == ui->cur);
        if (!is_edit && ui->editing && r == ui->cur) sel = false;
        SDL_FRect bar = {ui->listr.x, ry + 1.0f, ui->listr.w - (total > ui->rows ? ui->sbw + 3.0f : 0.0f),
                         ui->rh - 2.0f};
        if (is_edit) {
            SDL_SetRenderDrawColor(ren, 0x11, 0x11, 0x11, 255);
            SDL_RenderFillRect(ren, &bar);
            SDL_FRect o[4] = {{bar.x, bar.y, bar.w, th}, {bar.x, bar.y + bar.h - th, bar.w, th},
                              {bar.x, bar.y + th, th, bar.h - 2 * th},
                              {bar.x + bar.w - th, bar.y + th, th, bar.h - 2 * th}};
            SDL_SetRenderDrawColor(ren, 0x6f, 0xa8, 0xdc, 255);
            SDL_RenderFillRects(ren, o, 4);
        } else if (sel) {
            SDL_SetRenderDrawColor(ren, 0x6f, 0xa8, 0xdc, 70);
            SDL_RenderFillRect(ren, &bar);
            SDL_SetRenderDrawColor(ren, 0x6f, 0xa8, 0xdc, 255);
            SDL_FRect mk = {ui->listr.x, ry + 1.0f, th, ui->rh - 2.0f};
            SDL_RenderFillRect(ren, &mk);
        }

        if (is_edit) {
            const Label *l = &ui->row[r];
            float tx = ui->listr.x + 0.7f * f, avail = bar.w - 1.4f * f - th * 2.0f;
            float caret = tx;
            if (c && ui->name[0] && l->tex) {
                float shown = fminf((float)l->w, avail);
                SDL_FRect src = {(float)l->w - shown, 0.0f, shown, (float)l->h};
                SDL_FRect dst = {floorf(tx), floorf(ry + (ui->rh - (float)l->h) * 0.5f), shown, (float)l->h};
                SDL_SetTextureColorMod(l->tex, 255, 255, 255);
                SDL_SetTextureAlphaMod(l->tex, 255);
                SDL_RenderTexture(ren, l->tex, &src, &dst);
                caret = tx + shown;
            } else {
                const Label *p = &ui->lab[LAB_EDIT];
                tint_blit(ui, p, tx, ry + (ui->rh - (float)p->h) * 0.5f, WHITE, 110);
            }
            SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
            SDL_FRect cr = {floorf(caret) + 1.0f, ry + ui->rh * 0.2f, th, ui->rh * 0.6f};
            SDL_RenderFillRect(ren, &cr);
            continue;
        }

        const Label *l = &ui->row[r];
        uint32_t col = r == 0 ? (sel ? 0xd0d0d0 : DIM) : (sel ? WHITE : TXT);
        if (r == active) {
            const Label *m = &ui->lab[LAB_MARK];
            tint_blit(ui, m, ui->listr.x + 0.6f * f, ry + (ui->rh - (float)m->h) * 0.5f, ACC, 255);
        }
        tint_blit(ui, l, textx, ry + (ui->rh - (float)l->h) * 0.5f, col, 255);
    }

    if (total > ui->rows) {
        float tr_x = ui->listr.x + ui->listr.w - ui->sbw, tr_h = ui->listr.h;
        SDL_SetRenderDrawColor(ren, 0x33, 0x33, 0x33, 255);
        SDL_FRect track = {tr_x, ui->listr.y, ui->sbw, tr_h};
        SDL_RenderFillRect(ren, &track);
        float frac = (float)ui->rows / (float)total;
        float th_h = fmaxf(tr_h * frac, ui->sbw * 3.0f);
        float ty2 = ui->listr.y + (tr_h - th_h) * ((float)ui->top / (float)(total - ui->rows));
        SDL_SetRenderDrawColor(ren, 0x80, 0x80, 0x80, 255);
        SDL_FRect thumb = {tr_x, ty2, ui->sbw, th_h};
        SDL_RenderFillRect(ren, &thumb);
    }

    tint_blit(ui, &ui->lab[LAB_ERR], ui->listr.x, ui->err_y, RED, 255);
    tint_blit(ui, ui->editing ? &ui->lab[LAB_HINT_EDIT] : &ui->lab[LAB_HINT], ui->listr.x, ui->hint_y,
              DIM, 255);

    ui->dirty = false;
    SDL_SetRenderDrawBlendMode(ren, prev);
    SDL_SetRenderDrawColor(ren, pr, pg, pb, pa);
}
