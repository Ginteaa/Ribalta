/* Elenco dei font di sistema su Windows (vedi fonts.h).
 * I file si leggono dal registro (HKLM e HKCU, ...\Windows NT\CurrentVersion\Fonts); famiglia
 * reale, peso, corsivo e presenza delle cifre si leggono aprendo il file con SDL_ttf. Aprire un
 * file costa (antivirus, cache fredda): dal NOME del valore del registro ("Arial Bold Italic
 * (TrueType)") si ricava prima una classificazione provvisoria (famiglia, peso, corsivo), i file
 * si raggruppano per famiglia provvisoria e di ogni gruppo si apre il candidato migliore; gli
 * altri si aprono solo se quello non conferma (non si apre, niente cifre, stile diverso dal
 * previsto): circa un'apertura per famiglia invece di una per file.
 * Tutto vive solo dentro fonts_list: a menu chiuso non resta nulla (niente cache, SDL_ttf è
 * inizializzata e chiusa qui se non lo è già). Le funzioni comuni (fonts_sort, fonts_find,
 * fonts_step) stanno in fonts_common.c. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "platform/fonts.h"
#include "platform/paths.h"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Pesi OS/2 (usWeightClass): Regular 400, Bold 700 */
#define W_REGULAR 400
#define W_BOLD    700
#define W_BOLDMIN 650  /* da qui in su la variante conta come Bold (SemiBold 600 no, come in fonts.c) */

/* Corpo del font usato solo per leggere nome, stile e glifi: nessun disegno */
#define PROBE_PT 12

/* Punteggio della variante: più basso = migliore (Bold non corsivo, poi Regular, poi il resto). */
static int variant_score(int weight, bool italic)
{
    int s = 0;
    if (italic) s += 10000;
    if (weight >= W_BOLDMIN) s += abs(weight - W_BOLD);  /* Bold, o la più vicina */
    else s += 1000 + abs(weight - W_REGULAR);            /* altrimenti la più vicina a Regular */
    return s;
}

/* Solo .ttf/.otf: TTF_OpenFont apre la faccia 0, quindi niente collezioni (.ttc/.otc) né .fon */
static bool ext_ok(const char *path)
{
    const char *dot = strrchr(path, '.');
    if (!dot) return false;
    return SDL_strcasecmp(dot, ".ttf") == 0 || SDL_strcasecmp(dot, ".otf") == 0;
}

/* Stringa wide (UTF-16) -> UTF-8 in out; false se vuota o se non entra in size */
static bool wide_to_utf8(const WCHAR *w, char *out, size_t size)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, out, (int)size, NULL, NULL);
    return n > 1;  /* 0 = errore o buffer troppo piccolo, 1 = solo il terminatore */
}

/* ---- classificazione provvisoria dal nome del valore del registro ---- */

/* Parole di peso finali nei nomi ("Segoe UI Semibold", "Arial Black"): restano nel nome del
 * gruppo (a volte sono famiglie vere, es. "Arial Black"; altre volte il file dichiara la famiglia
 * senza la parola, es. "Calibri Light" -> "Calibri": la famiglia reale la decide SDL_ttf) e danno
 * solo il peso provvisorio */
static const struct { const char *word; int weight; } WEIGHT_WORDS[] = {
    { "thin", 100 },      { "hairline", 100 },  { "extralight", 200 }, { "ultralight", 200 },
    { "light", 300 },     { "semilight", 350 }, { "medium", 500 },     { "semibold", 600 },
    { "demibold", 600 },  { "extrabold", 800 }, { "ultrabold", 800 },  { "black", 900 },
    { "heavy", 900 },
};

/* Tronca s a len caratteri, toglie gli spazi finali e ritorna la nuova lunghezza */
static size_t rtrim(char *s, size_t len)
{
    s[len] = '\0';
    while (len > 0 && s[len - 1] == ' ') s[--len] = '\0';
    return len;
}

/* Da "Arial Bold Italic (TrueType)" ricava la chiave del gruppo ("Arial": nome senza il suffisso
 * tra parentesi e senza le parole finali Bold/Italic/Oblique/Regular/Normal), il peso e il
 * corsivo provvisori. Un nome troppo lungo o vuoto dà chiave = fallback (il gruppo di un file solo). */
static void classify(const char *regname, const char *fallback, char *key, size_t key_size,
                     int *weight, bool *italic)
{
    char s[256];
    bool bold = false;
    *italic = false;
    *weight = W_REGULAR;
    size_t len = strlen(regname);
    if (len >= sizeof s) goto fallback;
    memcpy(s, regname, len + 1);
    len = rtrim(s, len);
    if (len > 0 && s[len - 1] == ')') {  /* suffisso " (TrueType)" / " (OpenType)" */
        char *open = strrchr(s, '(');
        if (open) len = rtrim(s, (size_t)(open - s));
    }
    for (;;) {  /* parole di stile finali, finché resta almeno un'altra parola */
        char *sp = strrchr(s, ' ');
        if (!sp) break;
        const char *tok = sp + 1;
        if (SDL_strcasecmp(tok, "italic") == 0 || SDL_strcasecmp(tok, "oblique") == 0) *italic = true;
        else if (SDL_strcasecmp(tok, "bold") == 0) bold = true;
        else if (SDL_strcasecmp(tok, "regular") != 0 && SDL_strcasecmp(tok, "normal") != 0) break;
        len = rtrim(s, (size_t)(sp - s));
    }
    if (len == 0 || len >= key_size) goto fallback;
    if (bold) {
        *weight = W_BOLD;
    } else {
        const char *last = strrchr(s, ' ');
        last = last ? last + 1 : s;
        for (size_t i = 0; i < sizeof WEIGHT_WORDS / sizeof *WEIGHT_WORDS; i++)
            if (SDL_strcasecmp(last, WEIGHT_WORDS[i].word) == 0) *weight = WEIGHT_WORDS[i].weight;
    }
    memcpy(key, s, len + 1);
    return;
fallback:
    *italic = false;
    *weight = W_REGULAR;
    snprintf(key, key_size, "%s", fallback);
}

/* ---- candidati ---- */

typedef struct {
    char *path;    /* percorso UTF-8 del file; il blocco contiene anche key */
    char *key;     /* famiglia provvisoria (vedi classify) */
    int   weight;  /* peso provvisorio */
    bool  italic;  /* corsivo provvisorio */
    int   score;   /* variant_score provvisorio */
} Cand;

typedef struct {
    Cand *items;
    int   count, cap;
} Cands;

static bool cands_add(Cands *c, const char *file, const char *regname)
{
    if (c->count == c->cap) {
        int ncap = c->cap ? c->cap * 2 : 256;
        Cand *p = realloc(c->items, (size_t)ncap * sizeof *p);
        if (!p) return false;
        c->items = p;
        c->cap = ncap;
    }
    char key[FONT_FILE_MAX];  /* la chiave di riserva è il percorso */
    Cand cand;
    classify(regname, file, key, sizeof key, &cand.weight, &cand.italic);
    cand.score = variant_score(cand.weight, cand.italic);
    size_t plen = strlen(file) + 1, klen = strlen(key) + 1;
    cand.path = malloc(plen + klen);  /* percorso e chiave in un blocco solo */
    if (!cand.path) return false;
    memcpy(cand.path, file, plen);
    cand.key = cand.path + plen;
    memcpy(cand.key, key, klen);
    c->items[c->count++] = cand;
    return true;
}

static void cands_free(Cands *c)
{
    for (int i = 0; i < c->count; i++) free(c->items[i].path);
    free(c->items);
    c->items = NULL;
    c->count = c->cap = 0;
}

/* Il filesystem di Windows non distingue maiuscole e minuscole */
static int cmp_by_path(const void *a, const void *b)
{
    const Cand *x = a, *y = b;
    int c = SDL_strcasecmp(x->path, y->path);
    return c ? c : SDL_strcasecmp(x->key, y->key);
}

/* Per gruppo (chiave), poi dal candidato migliore al peggiore, poi per percorso (ordine stabile) */
static int cmp_by_group(const void *a, const void *b)
{
    const Cand *x = a, *y = b;
    int c = SDL_strcasecmp(x->key, y->key);
    if (c) return c;
    if (x->score != y->score) return x->score < y->score ? -1 : 1;
    return SDL_strcasecmp(x->path, y->path);
}

/* Toglie i duplicati (lo stesso file registrato due volte, o una volta con nome relativo e una
 * assoluto): così ogni file si apre al massimo una volta */
static void cands_dedup(Cands *c)
{
    if (c->count < 2) return;
    qsort(c->items, (size_t)c->count, sizeof *c->items, cmp_by_path);
    int w = 1;
    for (int i = 1; i < c->count; i++) {
        if (SDL_strcasecmp(c->items[i].path, c->items[w - 1].path) == 0) free(c->items[i].path);
        else c->items[w++] = c->items[i];
    }
    c->count = w;
}

/* Aggiunge a c i file .ttf/.otf di una chiave del registro (HKLM o HKCU). I nomi relativi sono
 * file di %WINDIR%\Fonts, quelli assoluti (font dell'utente) si usano così come sono.
 * Una chiave assente o illeggibile non è un errore; false solo se manca memoria. */
static bool collect_key(HKEY root, const char *fonts_dir, Cands *c)
{
    HKEY key;
    if (RegOpenKeyExW(root, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts",
                      0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return true;

    DWORD nvalues = 0, max_name = 0, max_data = 0;
    if (RegQueryInfoKeyW(key, NULL, NULL, NULL, NULL, NULL, NULL,
                         &nvalues, &max_name, &max_data, NULL, NULL) != ERROR_SUCCESS) {
        RegCloseKey(key);
        return true;
    }

    /* max_name è in caratteri senza terminatore, max_data in byte; un WCHAR in più sui dati
     * permette di terminare a mano le stringhe salvate senza il NUL */
    WCHAR *name = malloc(((size_t)max_name + 1) * sizeof *name);
    BYTE *data = malloc((size_t)max_data + 2 * sizeof(WCHAR));
    bool ok = name && data;

    for (DWORD i = 0; ok && i < nvalues; i++) {
        DWORD name_len = max_name + 1, data_len = max_data, type = 0;
        LONG r = RegEnumValueW(key, i, name, &name_len, NULL, &type, data, &data_len);
        if (r == ERROR_NO_MORE_ITEMS) break;
        if (r != ERROR_SUCCESS || type != REG_SZ) continue;  /* anche ERROR_MORE_DATA: valore cresciuto */
        ((WCHAR *)data)[data_len / sizeof(WCHAR)] = L'\0';

        char val[FONT_FILE_MAX], file[FONT_FILE_MAX], regname[256];
        if (!wide_to_utf8((const WCHAR *)data, val, sizeof val)) continue;
        if (paths_is_absolute(val)) {
            memcpy(file, val, strlen(val) + 1);
        } else {
            if (!fonts_dir[0]) continue;
            int n = snprintf(file, sizeof file, "%s" PATHS_SEP "%s", fonts_dir, val);
            if (n < 0 || (size_t)n >= sizeof file) continue;
        }
        if (!ext_ok(file)) continue;
        if (!wide_to_utf8(name, regname, sizeof regname)) regname[0] = '\0';  /* -> gruppo di un file solo */
        if (!cands_add(c, file, regname)) ok = false;
    }

    free(name);
    free(data);
    RegCloseKey(key);
    return ok;
}

/* "<cartella di Windows>\Fonts" in UTF-8; stringa vuota se non determinabile */
static void windows_fonts_dir(char *out, size_t size)
{
    WCHAR wdir[MAX_PATH];
    char dir[FONT_FILE_MAX];
    out[0] = '\0';
    UINT n = GetWindowsDirectoryW(wdir, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return;
    if (!wide_to_utf8(wdir, dir, sizeof dir)) return;
    size_t len = strlen(dir);
    while (len > 0 && paths_is_sep(dir[len - 1])) dir[--len] = '\0';  /* "C:\" -> "C:" */
    if (len == 0) return;
    int w = snprintf(out, size, "%s" PATHS_SEP "Fonts", dir);
    if (w < 0 || (size_t)w >= size) out[0] = '\0';
}

/* Il font contiene tutte le cifre 0..9? */
static bool has_digits(TTF_Font *font)
{
    for (Uint32 ch = '0'; ch <= '9'; ch++)
        if (!TTF_FontHasGlyph(font, ch)) return false;
    return true;
}

/* Apre il file del candidato; se ha le cifre e migliora la variante già scelta per la sua
 * famiglia REALE (quella dichiarata nel file, può differire dalla chiave) lo registra in out.
 * Ritorna true se il file conferma la classificazione provvisoria (cifre presenti, stesso corsivo
 * e stessa classe Bold/non Bold attesi): allora gli altri file del gruppo non servono. */
static bool probe(const Cand *c, FontList *out, int *score)
{
    const char *file = c->path;
    if (strlen(file) >= FONT_FILE_MAX) return false;
    if (!paths_is_absolute(file) || !paths_readable(file)) return false;

    TTF_Font *font = TTF_OpenFont(file, PROBE_PT);
    if (!font) return false;
    bool confirmed = false;
    const char *fam = TTF_GetFontFamilyName(font);
    if (fam && fam[0] && strlen(fam) < FONT_FAMILY_MAX && has_digits(font)) {
        int weight = TTF_GetFontWeight(font);
        bool italic = (TTF_GetFontStyle(font) & TTF_STYLE_ITALIC) != 0;
        int sc = variant_score(weight, italic);

        int k = 0;
        while (k < out->count && strcmp(out->items[k].family, fam) != 0) k++;
        if (k == out->count || sc < score[k]) {
            if (k == out->count) out->count++;
            score[k] = sc;
            strcpy(out->items[k].family, fam);
            strcpy(out->items[k].file, file);
        }
        confirmed = italic == c->italic && (weight >= W_BOLDMIN) == (c->weight >= W_BOLDMIN);
    }
    TTF_CloseFont(font);
    return confirmed;
}

bool fonts_list(FontList *out)
{
    out->items = NULL;
    out->count = 0;

    /* SDL_ttf serve ad aprire i file: se l'app non l'ha inizializzata (test) la si apre e chiude qui */
    bool own_ttf = false;
    if (!TTF_WasInit()) {
        if (!TTF_Init()) return false;
        own_ttf = true;
    }

    bool ok = false;
    Cands cands = {0};
    int *score = NULL;
    char fonts_dir[FONT_FILE_MAX];

    windows_fonts_dir(fonts_dir, sizeof fonts_dir);
    if (!collect_key(HKEY_LOCAL_MACHINE, fonts_dir, &cands)) goto done;
    if (!collect_key(HKEY_CURRENT_USER, fonts_dir, &cands)) goto done;
    cands_dedup(&cands);
    if (cands.count > 1)
        qsort(cands.items, (size_t)cands.count, sizeof *cands.items, cmp_by_group);

    int cap = cands.count;  /* le famiglie reali non superano i file */
    if (cap > 0) {
        out->items = calloc((size_t)cap, sizeof *out->items);
        score = calloc((size_t)cap, sizeof *score);
        if (!out->items || !score) goto done;
    }

    /* per ogni gruppo: dal candidato migliore, ci si ferma al primo che conferma */
    for (int i = 0; i < cands.count;) {
        int j = i;
        while (j < cands.count && SDL_strcasecmp(cands.items[j].key, cands.items[i].key) == 0) j++;
        for (int k = i; k < j; k++)
            if (probe(&cands.items[k], out, score)) break;
        i = j;
    }

    if (out->count == 0) {
        free(out->items);
        out->items = NULL;
    } else if (out->count < cap) {
        /* di solito le famiglie sono meno dei file: restituisce lo spazio in più */
        FontEntry *p = realloc(out->items, (size_t)out->count * sizeof *p);
        if (p) out->items = p;
    }
    fonts_sort(out);
    ok = true;
done:
    free(score);
    cands_free(&cands);
    if (!ok) { free(out->items); out->items = NULL; out->count = 0; }
    if (own_ttf) TTF_Quit();
    return ok;
}

void fonts_free(FontList *l)
{
    if (!l) return;
    free(l->items);
    l->items = NULL;
    l->count = 0;
}
