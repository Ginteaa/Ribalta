#include "ui/ui_help.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3_ttf/SDL_ttf.h>

#include "platform/paths.h"

#define SEC_N   5
#define ROW_MAX 32
#define STR_MAX 520

typedef struct {
    SDL_Texture *tex;
    int w, h;
} Label;

/* Una riga della legenda: testi già risolti (con i ripieghi ASCII dei glifi mancanti). */
typedef struct {
    int  sec;
    char key[STR_MAX];
    char desc[STR_MAX];
    bool path;                /* la descrizione è un percorso: si accorcia da sinistra */
} Row;

struct HelpUI {
    SDL_Renderer *ren;
    char         *font_path;
    TTF_Font     *font;
    bool          open;
    bool          dirty;
    int           win_w, win_h;
    char          config_path[512], stats_path[512];

    int           row_count;
    Row           rows[ROW_MAX];
    bool          ell_ok;     /* il font ha "…" */

    float         pw, ph, px, py;
    SDL_Texture  *panel_tex;
    Label         title, footer, sec_lbl[SEC_N];
    Label         key_lbl[ROW_MAX], desc_lbl[ROW_MAX];
    float         sec_x[SEC_N], sec_y[SEC_N];
    float         key_x[ROW_MAX], desc_x[ROW_MAX], row_y[ROW_MAX];
    float         footer_y, title_x, title_y;
};

static const char *const k_sections[SEC_N] = {"Generale", "Timer", "Pomodoro", "Pannelli", "File"};

/* Segnaposto: \x01 = freccia destra "→" (ripiego "->"). */
typedef struct { int sec; const char *key; const char *desc; } Item;
static const Item k_items[] = {
    {0, "S", "Impostazioni"},
    {0, "I", "Statistiche"},
    {0, "M", "Materie"},
    {0, "T", "Timer / orologio"},
    {0, "P", "Pomodoro / orologio"},
    {0, "F \xc2\xb7 F11 \xc2\xb7 doppio clic", "Schermo intero"},
    {0, "H \xc2\xb7 F1", "Questa legenda"},
    {0, "Esc", "Chiude il pannello o esce dallo schermo intero"},
    {0, "Q \xc2\xb7 Ctrl+Q", "Esci"},
    {1, "Spazio \xc2\xb7 Invio", "Avvia, pausa, riprendi"},
    {1, "0\xe2\x80\x93" "9", "Scrivi la durata (1 3 0 \x01 01:30)"},
    {1, "Backspace", "Cancella l'ultima cifra"},
    {1, "\xe2\x86\x90 \xe2\x86\x92", "Ore o minuti"},
    {1, "\xe2\x86\x91 \xe2\x86\x93", "Regola (Shift \xc2\xb1" "10 min)"},
    {1, "R", "Azzera"},
    {2, "Spazio \xc2\xb7 Invio", "Avvia, pausa, riprendi"},
    {2, "N", "Fase successiva"},
    {2, "R", "Ricomincia da capo"},
    {3, "\xe2\x86\x91 \xe2\x86\x93", "Scegli la voce"},
    {3, "\xe2\x86\x90 \xe2\x86\x92", "Cambia il valore (Shift: passo pi\xc3\xb9 grande)"},
    {3, "Invio", "Selettore colore o menu dei font"},
    {3, "\xe2\x86\x90 \xe2\x86\x92 in Statistiche", "Filtra per materia"},
    {3, "N \xc2\xb7 Canc in Materie", "Nuova materia, elimina"},
};
#define ITEM_N ((int)(sizeof k_items / sizeof k_items[0]))

/* Sostituzioni per i glifi che il font potrebbe non avere. group: 0 frecce, 1 punto mediano,
 * 2 trattino lungo, 3 "più/meno". */
typedef struct { const char *src, *fallback; int group; } Subst;
static const Subst k_subst[] = {
    {"\xe2\x86\x90", "sx", 0}, {"\xe2\x86\x92", "dx", 0},
    {"\xe2\x86\x91", "su", 0}, {"\xe2\x86\x93", "gi\xc3\xb9", 0},
    {"\x01", "->", 0},
    {"\xc2\xb7", "-", 1},
    {"\xe2\x80\x93", "-", 2},
    {"\xc2\xb1", "+/-", 3},
};

static void label_free(Label *l)
{
    if (l->tex) SDL_DestroyTexture(l->tex);
    l->tex = NULL;
    l->w = l->h = 0;
}

static void label_make(HelpUI *ui, Label *l, const char *text, SDL_Color c)
{
    label_free(l);
    if (!text || !text[0]) return;
    SDL_Surface *s = TTF_RenderText_Blended(ui->font, text, 0, c);
    if (!s) return;
    l->tex = SDL_CreateTextureFromSurface(ui->ren, s);
    l->w = s->w;
    l->h = s->h;
    SDL_DestroySurface(s);
    if (l->tex) SDL_SetTextureScaleMode(l->tex, SDL_SCALEMODE_LINEAR);
}

static void free_textures(HelpUI *ui)
{
    if (ui->panel_tex) SDL_DestroyTexture(ui->panel_tex);
    ui->panel_tex = NULL;
    label_free(&ui->title);
    label_free(&ui->footer);
    for (int i = 0; i < SEC_N; i++) label_free(&ui->sec_lbl[i]);
    for (int i = 0; i < ROW_MAX; i++) { label_free(&ui->key_lbl[i]); label_free(&ui->desc_lbl[i]); }
}

/* Pannello con angoli arrotondati, antialiasing per copertura (come ui_settings). */
static SDL_Texture *make_panel(HelpUI *ui, int w, int h, float radius)
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

/* Copia src in dst sostituendo i caratteri che il font non ha con i ripieghi ASCII. */
static void resolve(const bool have[4], const char *src, char *dst, size_t cap)
{
    size_t o = 0;
    while (*src && o + 8 < cap) {
        bool done = false;
        for (size_t i = 0; i < sizeof k_subst / sizeof k_subst[0]; i++) {
            size_t n = strlen(k_subst[i].src);
            if (strncmp(src, k_subst[i].src, n) != 0) continue;
            const char *out = have[k_subst[i].group]
                ? (k_subst[i].src[0] == '\x01' ? "\xe2\x86\x92" : k_subst[i].src)
                : k_subst[i].fallback;
            size_t m = strlen(out);
            memcpy(dst + o, out, m);
            o += m;
            src += n;
            done = true;
            break;
        }
        if (!done) dst[o++] = *src++;
    }
    dst[o] = '\0';
}

/* Percorso da mostrare: "~" al posto della home, oppure solo il nome file se mancante. */
static void display_path(const char *path, const char *fallback, char *out, size_t cap)
{
    if (!path || !path[0]) { snprintf(out, cap, "%s", fallback); return; }
    const char *home = getenv("HOME");
    size_t hl = home ? strlen(home) : 0;
    if (hl > 1 && strncmp(path, home, hl) == 0 && paths_is_sep(path[hl]))
        snprintf(out, cap, "~%s", path + hl);
    else
        snprintf(out, cap, "%s", path);
}

static void build_rows(HelpUI *ui)
{
    const bool have[4] = {
        TTF_FontHasGlyph(ui->font, 0x2190) && TTF_FontHasGlyph(ui->font, 0x2191) &&
            TTF_FontHasGlyph(ui->font, 0x2192) && TTF_FontHasGlyph(ui->font, 0x2193),
        TTF_FontHasGlyph(ui->font, 0xB7),
        TTF_FontHasGlyph(ui->font, 0x2013),
        TTF_FontHasGlyph(ui->font, 0xB1),
    };
    ui->ell_ok = TTF_FontHasGlyph(ui->font, 0x2026);
    int n = 0;
    for (int i = 0; i < ITEM_N && n < ROW_MAX; i++, n++) {
        ui->rows[n].sec = k_items[i].sec;
        ui->rows[n].path = false;
        resolve(have, k_items[i].key, ui->rows[n].key, STR_MAX);
        resolve(have, k_items[i].desc, ui->rows[n].desc, STR_MAX);
    }
    static const char *const fkeys[3] = {"Impostazioni", "Statistiche", "Terminale"};
    char p[512];
    for (int i = 0; i < 3 && n < ROW_MAX; i++, n++) {
        Row *r = &ui->rows[n];
        r->sec = 4;
        snprintf(r->key, STR_MAX, "%s", fkeys[i]);
        r->path = i < 2;
        if (i == 0) display_path(ui->config_path, "config.ini", p, sizeof p);
        else if (i == 1) display_path(ui->stats_path, "sessions.csv", p, sizeof p);
        else snprintf(p, sizeof p, "ribalta --help");
        snprintf(r->desc, STR_MAX, "%s", p);
    }
    ui->row_count = n;
}

/* Accorcia (in place) da sinistra: "…/ribalta/config.ini", finché entra in maxw pixel. */
static void ellipsize_left(const HelpUI *ui, char *buf, float maxw)
{
    int w = 0, h = 0;
    if (TTF_GetStringSize(ui->font, buf, 0, &w, &h) && (float)w <= maxw) return;
    const char *dots = ui->ell_ok ? "\xe2\x80\xa6" : "...";
    char src[STR_MAX];
    snprintf(src, sizeof src, "%s", buf);
    size_t len = strlen(src);
    for (size_t cut = 1; cut < len; cut++) {
        if (((unsigned char)src[cut] & 0xC0) == 0x80) continue;   /* mai a metà di un carattere */
        if (!paths_is_sep(src[cut])) continue;                    /* si taglia solo davanti a un separatore */
        char tmp[STR_MAX];
        snprintf(tmp, sizeof tmp, "%s%s", dots, src + cut);
        if (!TTF_GetStringSize(ui->font, tmp, 0, &w, &h) || (float)w <= maxw) {
            snprintf(buf, STR_MAX, "%s", tmp);
            return;
        }
    }
    /* nessun separatore utile: si tiene la coda del testo */
    for (size_t cut = 1; cut < len; cut++) {
        if (((unsigned char)src[cut] & 0xC0) == 0x80) continue;
        char tmp[STR_MAX];
        snprintf(tmp, sizeof tmp, "%s%s", dots, src + cut);
        if (!TTF_GetStringSize(ui->font, tmp, 0, &w, &h) || (float)w <= maxw) {
            snprintf(buf, STR_MAX, "%s", tmp);
            return;
        }
    }
    snprintf(buf, STR_MAX, "%s", dots);
}

typedef struct {
    int   cols;
    float fs, pitch, pad, gap, cgap;
    float keyw[2], descw[2], colx[2], colh[2];
    float pw, ph;
    char  desc[ROW_MAX][STR_MAX];   /* descrizioni finali (percorsi accorciati) */
} Layout;

static int col_of(int cols, int sec) { return cols == 1 ? 0 : ((sec == 0 || sec == 4) ? 0 : 1); }

static int text_w(TTF_Font *f, const char *s)
{
    int w = 0, h = 0;
    if (!TTF_GetStringSize(f, s, 0, &w, &h)) return 0;
    return w;
}

/* Calcola le misure con corpo fs; true se entra in ~90% della larghezza e ~94% dell'altezza. */
static bool layout(HelpUI *ui, Layout *L, int cols, float fs)
{
    const float W = (float)ui->win_w, H = (float)ui->win_h;
    memset(L->keyw, 0, sizeof L->keyw);
    memset(L->descw, 0, sizeof L->descw);
    memset(L->colh, 0, sizeof L->colh);
    TTF_SetFontSize(ui->font, fs);
    L->cols = cols;
    L->fs = fs;
    L->pitch = fs * 1.45f;
    L->pad = fs * 1.2f;
    L->gap = fs * 1.4f;
    L->cgap = L->pad * 2.0f;

    int rows_in[2] = {0, 0}, secs_in[2] = {0, 0};
    for (int s = 0; s < SEC_N; s++) secs_in[col_of(cols, s)]++;
    for (int i = 0; i < ui->row_count; i++) {
        const Row *r = &ui->rows[i];
        int c = col_of(cols, r->sec);
        rows_in[c]++;
        float kw = (float)text_w(ui->font, r->key);
        if (kw > L->keyw[c]) L->keyw[c] = kw;
        snprintf(L->desc[i], STR_MAX, "%s", r->desc);
        if (!r->path) {
            float dw = (float)text_w(ui->font, r->desc);
            if (dw > L->descw[c]) L->descw[c] = dw;
        }
    }
    const float avail = W * 0.90f - 2.0f * L->pad - (float)(cols - 1) * L->cgap;
    if (avail <= 0.0f) return false;
    for (int i = 0; i < ui->row_count; i++) {
        const Row *r = &ui->rows[i];
        if (!r->path) continue;
        int c = col_of(cols, r->sec);
        float other = 0.0f;
        for (int o = 0; o < cols; o++)
            if (o != c) other += L->keyw[o] + L->gap + L->descw[o];
        float maxw = avail - other - L->keyw[c] - L->gap;
        if (maxw < fs * 4.0f) maxw = fs * 4.0f;
        ellipsize_left(ui, L->desc[i], maxw);
        float dw = (float)text_w(ui->font, L->desc[i]);
        if (dw > L->descw[c]) L->descw[c] = dw;
    }
    float total = 0.0f;
    for (int c = 0; c < cols; c++) {
        L->colx[c] = total;
        total += L->keyw[c] + L->gap + L->descw[c];
        if (c + 1 < cols) total += L->cgap;
        /* intestazione di sezione: 1.25 passi; spazio fra sezioni: 0.6 passi */
        L->colh[c] = L->pitch * ((float)rows_in[c] + 1.25f * (float)secs_in[c] + 0.6f * (float)(secs_in[c] - 1));
    }
    float colh = L->colh[0] > L->colh[1] ? L->colh[0] : L->colh[1];
    L->pw = total + 2.0f * L->pad;
    L->ph = colh + L->pad * 1.6f + L->pitch * 1.5f /* titolo */ + L->pitch * 1.6f /* piè di pagina */;
    return L->pw <= W * 0.90f && L->ph <= H * 0.94f;
}

static float best_fit(HelpUI *ui, Layout *L, int cols)
{
    float fs = fmaxf(8.0f, (float)ui->win_h / 34.0f);
    for (;;) {
        if (layout(ui, L, cols, fs)) return fs;
        if (fs <= 8.0f) return -fs;   /* non entra neanche al minimo */
        fs -= 1.0f;
    }
}

static void rebuild(HelpUI *ui)
{
    free_textures(ui);
    if (!ui->font) return;

    static Layout A, B;   /* fuori dallo stack (~13 KB ciascuno); mai concorrenti */
    float fa = -1.0f, fb;
    bool two = (float)ui->win_w >= 1.25f * (float)ui->win_h;
    if (two) fa = best_fit(ui, &A, 2);
    fb = best_fit(ui, &B, 1);
    Layout *L;
    if (two && fa > 0.0f && fa >= fb) L = &A;
    else if (fb > 0.0f) L = &B;
    else if (two) L = &A;     /* nessuno entra: si disegna comunque al minimo, a 2 colonne */
    else L = &B;
    if (L == &A && fa < 0.0f) layout(ui, L, 2, 8.0f);
    if (L == &B && fb < 0.0f) layout(ui, L, 1, 8.0f);
    TTF_SetFontSize(ui->font, L->fs);

    const float W = (float)ui->win_w, H = (float)ui->win_h;
    ui->pw = L->pw;
    ui->ph = L->ph;
    ui->px = floorf((W - L->pw) * 0.5f);
    ui->py = floorf((H - L->ph) * 0.5f);
    ui->panel_tex = make_panel(ui, (int)ceilf(L->pw), (int)ceilf(L->ph), L->pw < L->ph ? L->pw * 0.03f : L->ph * 0.04f);

    const SDL_Color norm = {0xb7, 0xb7, 0xb7, 255}, white = {0xff, 0xff, 0xff, 255};
    const SDL_Color dim = {0x80, 0x80, 0x80, 255};
    label_make(ui, &ui->title, "LEGENDA", dim);
    const bool dot = TTF_FontHasGlyph(ui->font, 0xB7);
    label_make(ui, &ui->footer, dot ? "H \xc2\xb7 F1 \xc2\xb7 Esc chiudi" : "H - F1 - Esc chiudi", dim);

    /* posizioni: ogni colonna impila le proprie sezioni dall'alto */
    float ytop = ui->py + L->pad * 0.8f + L->pitch * 1.5f;
    float cy[2] = {ytop, ytop};
    int last_sec[2] = {-1, -1};
    for (int i = 0; i < ui->row_count; i++) {
        const Row *r = &ui->rows[i];
        int c = col_of(L->cols, r->sec);
        float cx = ui->px + L->pad + L->colx[c];
        if (last_sec[c] != r->sec) {
            if (last_sec[c] >= 0) cy[c] += L->pitch * 0.6f;
            label_make(ui, &ui->sec_lbl[r->sec], k_sections[r->sec], dim);
            ui->sec_x[r->sec] = cx;
            ui->sec_y[r->sec] = cy[c] + (L->pitch - (float)ui->sec_lbl[r->sec].h) * 0.5f;
            cy[c] += L->pitch * 1.25f;
            last_sec[c] = r->sec;
        }
        label_make(ui, &ui->key_lbl[i], r->key, white);
        label_make(ui, &ui->desc_lbl[i], L->desc[i], norm);
        ui->key_x[i] = cx;
        ui->desc_x[i] = cx + L->keyw[c] + L->gap;
        ui->row_y[i] = cy[c] + (L->pitch - (float)ui->key_lbl[i].h) * 0.5f;
        cy[c] += L->pitch;
    }
    ui->footer_y = ui->py + L->ph - L->pad * 0.8f - (float)ui->footer.h;
    ui->title_x = ui->px + L->pad;
    ui->title_y = ui->py + L->pad * 0.8f;
    ui->dirty = false;
}

/* ---- API ---- */

HelpUI *ui_help_create(SDL_Renderer *ren, const char *font_path)
{
    HelpUI *ui = calloc(1, sizeof *ui);
    if (!ui) return NULL;
    ui->ren = ren;
    ui->font_path = font_path ? SDL_strdup(font_path) : NULL;
    return ui;
}

void ui_help_destroy(HelpUI *ui)
{
    if (!ui) return;
    ui_help_close(ui);
    SDL_free(ui->font_path);
    free(ui);
}

void ui_help_open(HelpUI *ui, int pixel_w, int pixel_h, const char *config_path, const char *stats_path)
{
    if (!ui) return;
    snprintf(ui->config_path, sizeof ui->config_path, "%s", config_path ? config_path : "");
    snprintf(ui->stats_path, sizeof ui->stats_path, "%s", stats_path ? stats_path : "");
    if (!ui->font && ui->font_path) ui->font = TTF_OpenFont(ui->font_path, 24.0f);
    if (ui->font) build_rows(ui);
    ui->open = true;
    ui->win_w = pixel_w;
    ui->win_h = pixel_h;
    ui->dirty = true;
}

void ui_help_close(HelpUI *ui)
{
    if (!ui) return;
    free_textures(ui);
    if (ui->font) TTF_CloseFont(ui->font);
    ui->font = NULL;
    ui->open = false;
    ui->dirty = false;
}

bool ui_help_is_open(const HelpUI *ui) { return ui && ui->open; }

void ui_help_resize(HelpUI *ui, int pixel_w, int pixel_h)
{
    if (!ui || !ui->open) return;
    if (pixel_w != ui->win_w || pixel_h != ui->win_h) {
        ui->win_w = pixel_w;
        ui->win_h = pixel_h;
        ui->dirty = true;
    }
}

static void blit(HelpUI *ui, const Label *l, float x, float y)
{
    if (!l->tex) return;
    SDL_FRect d = {x, y, (float)l->w, (float)l->h};
    SDL_RenderTexture(ui->ren, l->tex, NULL, &d);
}

void ui_help_draw(HelpUI *ui)
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

    /* titolo e piè di pagina centrati; sezioni e righe dai valori calcolati */
    blit(ui, &ui->title, ui->title_x, ui->title_y);
    blit(ui, &ui->footer, ui->px + (ui->pw - (float)ui->footer.w) * 0.5f, ui->footer_y);
    for (int s = 0; s < SEC_N; s++) blit(ui, &ui->sec_lbl[s], ui->sec_x[s], ui->sec_y[s]);
    for (int i = 0; i < ui->row_count; i++) {
        const Label *k = &ui->key_lbl[i], *d = &ui->desc_lbl[i];
        blit(ui, k, ui->key_x[i], ui->row_y[i]);
        blit(ui, d, ui->desc_x[i], ui->row_y[i]);
    }

    SDL_SetRenderDrawBlendMode(ui->ren, prev);
    SDL_SetRenderDrawColor(ui->ren, pr, pg, pb, pa);
}
