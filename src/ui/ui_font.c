#include "ui/ui_font.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3_ttf/SDL_ttf.h>

#include "platform/fonts.h"

enum { MAX_ROWS = 48, FILTER_MAX = 48, NAME_MAX = FONT_FAMILY_MAX + 8 };

typedef struct {
    SDL_Texture *tex;
    int  w, h;
    int  key;          /* per le righe: indice della voce (-1 = vuota) */
} Label;

enum { LAB_TITLE, LAB_HINT, LAB_SEARCH, LAB_PREVIEW, LAB_COUNT };

struct FontUI {
    SDL_Renderer *ren;
    char         *font_path, *bundled_path;
    TTF_Font     *font;      /* testo del pannello */
    TTF_Font     *pfont;     /* anteprima, aperto solo per la voce selezionata */
    int           pfont_entry;
    float         pfont_size;
    bool          open, dirty, laid_out;
    int           win_w, win_h;

    FontList      list;
    int          *filt;      /* indici di voce (0 = font incluso, i+1 = list.items[i]) */
    int           nfilt, total;
    char          filter[FILTER_MAX + 1];
    int           flen;
    char          sfilter[FILTER_MAX + 1];   /* filtro con cui è fatta la texture di ricerca */
    int           cur;       /* voce selezionata (indice di voce) */
    int           selpos;    /* posizione di cur in filt, -1 se filtro vuoto */
    int           top;       /* prima riga visibile (posizione in filt) */
    char          orig_file[FONT_FILE_MAX], orig_family[FONT_FAMILY_MAX];

    /* layout (pixel) */
    float fs, pad, px, py, pw, ph, radius, rh, sbw;
    int   rows;
    SDL_FRect field, listr, prev;
    float title_y, hint_y;

    SDL_Texture *panel_tex;
    Label        lab[LAB_COUNT];
    Label        row[MAX_ROWS];
};

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static int   clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* ---- voci ---- */

static const char *entry_name(const FontUI *ui, int e)
{
    return e == 0 ? FONT_BUNDLED_NAME : ui->list.items[e - 1].family;
}

static const char *entry_family(const FontUI *ui, int e)
{
    return e == 0 ? "" : ui->list.items[e - 1].family;
}

static const char *entry_file(const FontUI *ui, int e)
{
    return e == 0 ? "" : ui->list.items[e - 1].file;
}

static bool contains_ci(const char *hay, const char *needle)
{
    size_t n = strlen(needle);
    if (n == 0) return true;
    for (; *hay; hay++) {
        size_t i = 0;
        while (i < n && hay[i] &&
               SDL_tolower((unsigned char)hay[i]) == SDL_tolower((unsigned char)needle[i]))
            i++;
        if (i == n) return true;
    }
    return false;
}

/* ---- texture ---- */

static void label_free(Label *l)
{
    if (l->tex) SDL_DestroyTexture(l->tex);
    l->tex = NULL;
    l->w = l->h = 0;
    l->key = -1;
}

static void labels_free(FontUI *ui)
{
    for (int i = 0; i < LAB_COUNT; i++) label_free(&ui->lab[i]);
    for (int i = 0; i < MAX_ROWS; i++) label_free(&ui->row[i]);
}

/* Testo bianco con font f, tinto al disegno. */
static void label_render(FontUI *ui, Label *l, TTF_Font *f, const char *text)
{
    if (l->tex) SDL_DestroyTexture(l->tex);
    l->tex = NULL;
    l->w = l->h = 0;
    if (!f || !text[0]) return;
    SDL_Surface *s = TTF_RenderText_Blended(f, text, 0, (SDL_Color){255, 255, 255, 255});
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
    size_t len = 0;
    size_t full = strlen(text);
    if (TTF_MeasureString(f, text, 0, maxw, &w, &len) && len >= full) {
        snprintf(out, outsz, "%s", text);
        return;
    }
    size_t n = len < outsz - 4 ? len : outsz - 4;
    while (n > 0) {
        while (n > 0 && ((unsigned char)text[n] & 0xC0) == 0x80) n--;
        memcpy(out, text, n);
        memcpy(out + n, "\xE2\x80\xA6", 4);   /* "…" + NUL */
        int tw = 0;
        if (!TTF_GetStringSize(f, out, 0, &tw, NULL) || tw <= maxw) return;
        n--;
    }
    snprintf(out, outsz, "\xE2\x80\xA6");
}

static SDL_Texture *make_panel(FontUI *ui, int w, int h, float radius)
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

/* ---- layout ---- */

static void layout(FontUI *ui)
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
    int fw = 0, fh = 0;
    if (TTF_GetStringSize(ui->font, "Invio applica · Esc annulla", 0, &fw, &fh) && (float)fw > inner) {
        f = fmaxf(8.0f, f * inner / (float)fw);
        TTF_SetFontSize(ui->font, f);
    }

    float title_h = 1.5f * f, g1 = 0.8f * f, field_h = 2.2f * f, g2 = 0.8f * f;
    float rh = 1.9f * f, g3 = 0.8f * f, prev_h = 4.4f * f, g4 = 0.7f * f, hint_h = 1.4f * f;
    float fixed = 2.0f * pad + title_h + g1 + field_h + g2 + g3 + prev_h + g4 + hint_h;
    int rows = (int)floorf((H - 2.0f * margin - fixed) / rh);
    rows = clampi(rows, 1, MAX_ROWS);
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
    ui->field = (SDL_FRect){x0, y, inner, field_h};
    y += field_h + g2;
    ui->listr = (SDL_FRect){x0, y, inner, list_h};
    y += list_h + g3;
    ui->prev = (SDL_FRect){x0, y, inner, prev_h};
    y += prev_h + g4;
    ui->hint_y = y;
    ui->laid_out = true;

    /* la finestra può essere cambiata: ricontrolla lo scorrimento */
    int maxtop = ui->nfilt > rows ? ui->nfilt - rows : 0;
    ui->top = clampi(ui->top, 0, maxtop);
}

/* ---- selezione e scorrimento ---- */

static void ensure_visible(FontUI *ui)
{
    if (ui->selpos < 0 || ui->rows <= 0) return;
    if (ui->selpos < ui->top) ui->top = ui->selpos;
    else if (ui->selpos >= ui->top + ui->rows) ui->top = ui->selpos - ui->rows + 1;
    int maxtop = ui->nfilt > ui->rows ? ui->nfilt - ui->rows : 0;
    ui->top = clampi(ui->top, 0, maxtop);
}

/* Seleziona la posizione pos di filt (già nei limiti). */
static UiFontResult select_pos(FontUI *ui, int pos)
{
    if (ui->nfilt == 0) return UIFONT_NONE;
    pos = clampi(pos, 0, ui->nfilt - 1);
    int e = ui->filt[pos];
    UiFontResult r = e != ui->cur ? UIFONT_CHANGED : UIFONT_NONE;
    ui->cur = e;
    ui->selpos = pos;
    ensure_visible(ui);
    ui->dirty = true;
    return r;
}

static void refilter(FontUI *ui)
{
    ui->nfilt = 0;
    for (int e = 0; e < ui->total; e++)
        if (contains_ci(entry_name(ui, e), ui->filter)) ui->filt[ui->nfilt++] = e;
    ui->selpos = -1;
    ui->top = 0;
}

/* Dopo un cambio del filtro: la voce corrente resta selezionata se è ancora nella lista filtrata
 * (es. Esc o Backspace che allargano il filtro), altrimenti si passa alla prima voce filtrata. */
static UiFontResult filter_changed(FontUI *ui)
{
    refilter(ui);
    ui->dirty = true;
    if (ui->nfilt == 0) return UIFONT_NONE;   /* lista vuota: la voce corrente resta */
    for (int i = 0; i < ui->nfilt; i++)
        if (ui->filt[i] == ui->cur) {
            ui->selpos = i;
            ui->top = clampi(i - ui->rows / 2, 0, ui->nfilt > ui->rows ? ui->nfilt - ui->rows : 0);
            return UIFONT_NONE;
        }
    return select_pos(ui, 0);
}

/* ---- API ---- */

FontUI *ui_font_create(SDL_Renderer *ren, const char *ui_font_path, const char *bundled_path)
{
    FontUI *ui = calloc(1, sizeof *ui);
    if (!ui) return NULL;
    ui->ren = ren;
    ui->font_path = ui_font_path ? SDL_strdup(ui_font_path) : NULL;
    ui->bundled_path = bundled_path ? SDL_strdup(bundled_path) : NULL;
    ui->pfont_entry = -1;
    return ui;
}

void ui_font_destroy(FontUI *ui)
{
    if (!ui) return;
    ui_font_close(ui);
    SDL_free(ui->font_path);
    SDL_free(ui->bundled_path);
    free(ui);
}

void ui_font_open(FontUI *ui, int pixel_w, int pixel_h, const char *current_file)
{
    if (!ui) return;
    if (ui->open) ui_font_close(ui);
    if (!ui->font && ui->font_path) ui->font = TTF_OpenFont(ui->font_path, 24.0f);

    fonts_list(&ui->list);   /* su errore la lista è vuota: resta il font incluso */
    ui->total = ui->list.count + 1;
    ui->filt = malloc((size_t)ui->total * sizeof *ui->filt);
    if (!ui->filt) {
        fonts_free(&ui->list);
        ui->total = 0;
        return;
    }

    int idx = fonts_find(&ui->list, current_file);
    ui->cur = idx >= 0 ? idx + 1 : 0;
    snprintf(ui->orig_file, sizeof ui->orig_file, "%s", entry_file(ui, ui->cur));
    snprintf(ui->orig_family, sizeof ui->orig_family, "%s", entry_family(ui, ui->cur));

    ui->open = true;
    ui->win_w = pixel_w;
    ui->win_h = pixel_h;
    ui->filter[0] = '\0';
    ui->flen = 0;
    refilter(ui);               /* filtro vuoto: filt[i] = i */
    ui->selpos = ui->cur;
    layout(ui);
    ui->top = clampi(ui->selpos - ui->rows / 2, 0, ui->nfilt > ui->rows ? ui->nfilt - ui->rows : 0);
    ui->dirty = true;
}

void ui_font_close(FontUI *ui)
{
    if (!ui) return;
    labels_free(ui);
    if (ui->panel_tex) SDL_DestroyTexture(ui->panel_tex);
    ui->panel_tex = NULL;
    if (ui->pfont) TTF_CloseFont(ui->pfont);
    ui->pfont = NULL;
    ui->pfont_entry = -1;
    ui->pfont_size = 0.0f;
    if (ui->font) TTF_CloseFont(ui->font);
    ui->font = NULL;
    free(ui->filt);
    ui->filt = NULL;
    ui->nfilt = ui->total = 0;
    fonts_free(&ui->list);
    ui->open = false;
    ui->dirty = false;
    ui->laid_out = false;
}

bool ui_font_is_open(const FontUI *ui) { return ui && ui->open; }

void ui_font_resize(FontUI *ui, int pixel_w, int pixel_h)
{
    if (!ui || !ui->open) return;
    if (pixel_w == ui->win_w && pixel_h == ui->win_h) return;
    ui->win_w = pixel_w;
    ui->win_h = pixel_h;
    layout(ui);
    ensure_visible(ui);
    ui->dirty = true;
}

const char *ui_font_family(const FontUI *ui)
{
    return ui && ui->open && ui->cur < ui->total ? entry_family(ui, ui->cur) : "";
}

const char *ui_font_file(const FontUI *ui)
{
    return ui && ui->open && ui->cur < ui->total ? entry_file(ui, ui->cur) : "";
}

const char *ui_font_original_file(const FontUI *ui) { return ui ? ui->orig_file : ""; }
const char *ui_font_original_family(const FontUI *ui) { return ui ? ui->orig_family : ""; }
bool ui_font_dirty(const FontUI *ui) { return ui && ui->open && ui->dirty; }

UiFontResult ui_font_key(FontUI *ui, SDL_Keycode key, SDL_Keymod mod)
{
    if (!ui || !ui->open) return UIFONT_NONE;
    switch (key) {
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        return ui->nfilt > 0 ? UIFONT_APPLY : UIFONT_NONE;
    case SDLK_ESCAPE:
        if (ui->flen > 0) {
            ui->flen = 0;
            ui->filter[0] = '\0';
            return filter_changed(ui);
        }
        return UIFONT_CANCEL;
    case SDLK_UP:    return ui->selpos < 0 ? UIFONT_NONE : select_pos(ui, ui->selpos - 1);
    case SDLK_DOWN:  return ui->selpos < 0 ? UIFONT_NONE : select_pos(ui, ui->selpos + 1);
    case SDLK_PAGEUP:   return ui->selpos < 0 ? UIFONT_NONE : select_pos(ui, ui->selpos - ui->rows);
    case SDLK_PAGEDOWN: return ui->selpos < 0 ? UIFONT_NONE : select_pos(ui, ui->selpos + ui->rows);
    case SDLK_HOME:  return select_pos(ui, 0);
    case SDLK_END:   return select_pos(ui, ui->nfilt - 1);
    case SDLK_BACKSPACE:
        if (ui->flen == 0) return UIFONT_NONE;
        ui->filter[--ui->flen] = '\0';
        return filter_changed(ui);
    default: break;
    }
    if (mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI)) return UIFONT_NONE;
    bool ok = (key >= SDLK_A && key <= SDLK_Z) || (key >= SDLK_0 && key <= SDLK_9) ||
              key == SDLK_SPACE || key == SDLK_MINUS;
    if (!ok || ui->flen >= FILTER_MAX) return UIFONT_NONE;
    ui->filter[ui->flen++] = (char)key;
    ui->filter[ui->flen] = '\0';
    return filter_changed(ui);
}

static bool in_rect(const SDL_FRect *r, float x, float y)
{
    return x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h;
}

UiFontResult ui_font_mouse(FontUI *ui, const SDL_Event *ev)
{
    if (!ui || !ui->open || !ev || !ui->laid_out) return UIFONT_NONE;
    switch (ev->type) {
    case SDL_EVENT_MOUSE_WHEEL: {
        float y = ev->wheel.y;
        if (ev->wheel.direction == SDL_MOUSEWHEEL_FLIPPED) y = -y;
        int d = (int)lroundf(y * 3.0f);
        if (d == 0 && y != 0.0f) d = y > 0 ? 1 : -1;
        int maxtop = ui->nfilt > ui->rows ? ui->nfilt - ui->rows : 0;
        int nt = clampi(ui->top - d, 0, maxtop);
        if (nt != ui->top) { ui->top = nt; ui->dirty = true; }
        return UIFONT_NONE;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        if (ev->button.button != SDL_BUTTON_LEFT) return UIFONT_NONE;
        float x = ev->button.x, y = ev->button.y;
        if (!in_rect(&ui->listr, x, y)) return UIFONT_NONE;
        int pos = ui->top + (int)((y - ui->listr.y) / ui->rh);
        if (pos < 0 || pos >= ui->nfilt) return UIFONT_NONE;
        UiFontResult r = select_pos(ui, pos);
        if (ev->button.clicks >= 2) return UIFONT_APPLY;
        return r;
    }
    default:
        return UIFONT_NONE;
    }
}

/* ---- disegno ---- */

static void tint_blit(FontUI *ui, const Label *l, float x, float y, uint32_t rgb, Uint8 a)
{
    if (!l->tex) return;
    SDL_SetTextureColorMod(l->tex, (Uint8)(rgb >> 16), (Uint8)(rgb >> 8), (Uint8)rgb);
    SDL_SetTextureAlphaMod(l->tex, a);
    SDL_FRect d = {floorf(x), floorf(y), (float)l->w, (float)l->h};
    SDL_RenderTexture(ui->ren, l->tex, NULL, &d);
}

static void outline(FontUI *ui, SDL_FRect r, float t, uint32_t rgb, Uint8 a)
{
    SDL_SetRenderDrawColor(ui->ren, (Uint8)(rgb >> 16), (Uint8)(rgb >> 8), (Uint8)rgb, a);
    SDL_FRect e[4] = {{r.x, r.y, r.w, t}, {r.x, r.y + r.h - t, r.w, t},
                      {r.x, r.y + t, t, r.h - 2 * t}, {r.x + r.w - t, r.y + t, t, r.h - 2 * t}};
    SDL_RenderFillRects(ui->ren, e, 4);
}

/* Riapre il font dell'anteprima solo se è cambiata la voce; ricostruisce la texture. */
static void preview_update(FontUI *ui)
{
    float psize = ui->fs * 3.0f;
    bool fresh = ui->pfont_entry != ui->cur;
    if (fresh) {
        if (ui->pfont) TTF_CloseFont(ui->pfont);
        ui->pfont = NULL;
        const char *path = ui->cur == 0 ? ui->bundled_path : entry_file(ui, ui->cur);
        if (path && path[0]) ui->pfont = TTF_OpenFont(path, psize);
        ui->pfont_entry = ui->cur;
        ui->pfont_size = psize;
    } else if (ui->pfont && ui->pfont_size != psize) {
        TTF_SetFontSize(ui->pfont, psize);
        ui->pfont_size = psize;
        fresh = true;
    }
    Label *l = &ui->lab[LAB_PREVIEW];
    if (fresh || (!l->tex && l->key != ui->cur)) {
        if (ui->pfont) label_render(ui, l, ui->pfont, "12:34");
        else label_render(ui, l, ui->font, "Anteprima non disponibile");
        l->key = ui->cur;
    }
}

void ui_font_draw(FontUI *ui)
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

    /* testi fissi */
    if (!ui->lab[LAB_TITLE].tex) {
        TTF_SetFontSize(ui->font, f * 1.25f);
        label_render(ui, &ui->lab[LAB_TITLE], ui->font, "Font cifre");
        TTF_SetFontSize(ui->font, f);
    }
    if (!ui->lab[LAB_HINT].tex) label_render(ui, &ui->lab[LAB_HINT], ui->font, "Invio applica · Esc annulla");

    /* campo di ricerca: testo = filtro, oppure segnaposto */
    Label *sl = &ui->lab[LAB_SEARCH];
    if (!sl->tex || strcmp(ui->sfilter, ui->filter) != 0) {
        label_render(ui, sl, ui->font, ui->flen > 0 ? ui->filter : "Cerca\xE2\x80\xA6");
        snprintf(ui->sfilter, sizeof ui->sfilter, "%s", ui->filter);
    }

    /* righe visibili: cache per indice di voce, si rende solo ciò che manca */
    int vis = ui->nfilt - ui->top;
    if (vis > ui->rows) vis = ui->rows;
    if (vis < 0) vis = 0;
    int slot_of[MAX_ROWS];
    bool used[MAX_ROWS] = {false};
    float name_w = ui->listr.w - ui->sbw - 1.2f * f;
    for (int r = 0; r < vis; r++) {
        int e = ui->filt[ui->top + r];
        slot_of[r] = -1;
        for (int s = 0; s < MAX_ROWS; s++)
            if (!used[s] && ui->row[s].tex && ui->row[s].key == e) { slot_of[r] = s; used[s] = true; break; }
    }
    for (int r = 0; r < vis; r++) {
        if (slot_of[r] >= 0) continue;
        int s = 0;
        while (s < MAX_ROWS && used[s]) s++;
        if (s >= MAX_ROWS) break;
        used[s] = true;
        slot_of[r] = s;
        int e = ui->filt[ui->top + r];
        char buf[NAME_MAX + 8];
        ellipsize(ui->font, entry_name(ui, e), (int)name_w, buf, sizeof buf);
        label_render(ui, &ui->row[s], ui->font, buf);
        ui->row[s].key = e;
    }
    preview_update(ui);

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

    tint_blit(ui, &ui->lab[LAB_TITLE], ui->listr.x, ui->title_y, WHITE, 255);

    /* campo di ricerca */
    SDL_SetRenderDrawColor(ren, 0x11, 0x11, 0x11, 255);
    SDL_RenderFillRect(ren, &ui->field);
    outline(ui, ui->field, th, ACC, 255);
    float tx = ui->field.x + f * 0.7f;
    float avail = ui->field.w - f * 1.4f - th * 2.0f;
    float ty = ui->field.y + (ui->field.h - (float)sl->h) * 0.5f;
    float caret = tx;
    if (sl->tex) {
        if (ui->flen == 0) {
            tint_blit(ui, sl, tx, ty, WHITE, 110);
        } else {
            float w = (float)sl->w, shown = fminf(w, avail);
            SDL_FRect src = {w - shown, 0.0f, shown, (float)sl->h};
            SDL_FRect dst = {floorf(tx), floorf(ty), shown, (float)sl->h};
            SDL_SetTextureColorMod(sl->tex, 255, 255, 255);
            SDL_SetTextureAlphaMod(sl->tex, 255);
            SDL_RenderTexture(ren, sl->tex, &src, &dst);
            caret = tx + shown;
        }
    }
    SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
    SDL_FRect cr = {floorf(caret) + 1.0f, ui->field.y + ui->field.h * 0.2f, th, ui->field.h * 0.6f};
    SDL_RenderFillRect(ren, &cr);

    /* lista (le righe si ritagliano da sole: sono quante ne entrano) */
    for (int r = 0; r < vis; r++) {
        int pos = ui->top + r, e = ui->filt[pos];
        float ry = ui->listr.y + (float)r * ui->rh;
        bool sel = pos == ui->selpos;
        if (sel) {
            SDL_SetRenderDrawColor(ren, 0x6f, 0xa8, 0xdc, 70);
            SDL_FRect bar = {ui->listr.x, ry + 1.0f, ui->listr.w - ui->sbw - 3.0f, ui->rh - 2.0f};
            SDL_RenderFillRect(ren, &bar);
            SDL_SetRenderDrawColor(ren, 0x6f, 0xa8, 0xdc, 255);
            SDL_FRect mk = {ui->listr.x, ry + 1.0f, th, ui->rh - 2.0f};
            SDL_RenderFillRect(ren, &mk);
        }
        const Label *l = &ui->row[slot_of[r] >= 0 ? slot_of[r] : 0];
        if (slot_of[r] >= 0 && l->key == e)
            tint_blit(ui, l, ui->listr.x + 0.6f * f, ry + (ui->rh - (float)l->h) * 0.5f,
                      sel ? WHITE : TXT, 255);
    }

    /* indicatore di scorrimento */
    if (ui->nfilt > ui->rows) {
        float tr_x = ui->listr.x + ui->listr.w - ui->sbw, tr_h = ui->listr.h;
        SDL_SetRenderDrawColor(ren, 0x33, 0x33, 0x33, 255);
        SDL_FRect track = {tr_x, ui->listr.y, ui->sbw, tr_h};
        SDL_RenderFillRect(ren, &track);
        float frac = (float)ui->rows / (float)ui->nfilt;
        float th_h = fmaxf(tr_h * frac, ui->sbw * 3.0f);
        float maxtop = (float)(ui->nfilt - ui->rows);
        float ty2 = ui->listr.y + (tr_h - th_h) * ((float)ui->top / maxtop);
        SDL_SetRenderDrawColor(ren, 0x80, 0x80, 0x80, 255);
        SDL_FRect thumb = {tr_x, ty2, ui->sbw, th_h};
        SDL_RenderFillRect(ren, &thumb);
    }

    /* anteprima */
    SDL_SetRenderDrawColor(ren, 0x11, 0x11, 0x11, 255);
    SDL_RenderFillRect(ren, &ui->prev);
    outline(ui, ui->prev, 1.0f, 0x444444, 255);
    const Label *pl = &ui->lab[LAB_PREVIEW];
    if (pl->tex) {
        float maxw = ui->prev.w - 1.4f * f, maxh = ui->prev.h - 0.6f * f;
        float sc = fminf(1.0f, fminf(maxw / (float)pl->w, maxh / (float)pl->h));
        float dw = (float)pl->w * sc, dh = (float)pl->h * sc;
        SDL_FRect d = {floorf(ui->prev.x + (ui->prev.w - dw) * 0.5f),
                       floorf(ui->prev.y + (ui->prev.h - dh) * 0.5f), dw, dh};
        SDL_SetTextureColorMod(pl->tex, 255, 255, 255);
        SDL_SetTextureAlphaMod(pl->tex, 255);
        SDL_RenderTexture(ren, pl->tex, NULL, &d);
    }

    tint_blit(ui, &ui->lab[LAB_HINT], ui->listr.x, ui->hint_y, 0x808080, 255);

    ui->dirty = false;
    SDL_SetRenderDrawBlendMode(ren, prev);
    SDL_SetRenderDrawColor(ren, pr, pg, pb, pa);
}
