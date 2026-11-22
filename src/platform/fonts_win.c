/* Elenco dei font di sistema su Windows (vedi fonts.h).
 * I file si leggono dal registro (HKLM e HKCU, ...\Windows NT\CurrentVersion\Fonts); famiglia,
 * peso, corsivo e presenza delle cifre si leggono aprendo ogni file con SDL_ttf. Tutto vive solo
 * dentro fonts_list: a menu chiuso non resta nulla (niente cache, SDL_ttf è inizializzata e
 * chiusa qui se non lo è già). Le funzioni comuni (fonts_sort, fonts_find, fonts_step) stanno in
 * fonts_common.c. */
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

/* ---- candidati: percorsi dei file, ognuno in un blocco allocato a parte ---- */

typedef struct {
    char **paths;
    int    count, cap;
} Cands;

static bool cands_add(Cands *c, const char *file)
{
    if (c->count == c->cap) {
        int ncap = c->cap ? c->cap * 2 : 256;
        char **p = realloc(c->paths, (size_t)ncap * sizeof *p);
        if (!p) return false;
        c->paths = p;
        c->cap = ncap;
    }
    size_t n = strlen(file) + 1;
    char *copy = malloc(n);
    if (!copy) return false;
    memcpy(copy, file, n);
    c->paths[c->count++] = copy;
    return true;
}

static void cands_free(Cands *c)
{
    for (int i = 0; i < c->count; i++) free(c->paths[i]);
    free(c->paths);
    c->paths = NULL;
    c->count = c->cap = 0;
}

/* Il filesystem di Windows non distingue maiuscole e minuscole */
static int cmp_path(const void *a, const void *b)
{
    return SDL_strcasecmp(*(char *const *)a, *(char *const *)b);
}

/* Ordina i percorsi e toglie i duplicati (lo stesso file registrato due volte, o una volta con
 * nome relativo e una assoluto): così ogni file si apre una volta sola */
static void cands_dedup(Cands *c)
{
    if (c->count < 2) return;
    qsort(c->paths, (size_t)c->count, sizeof *c->paths, cmp_path);
    int w = 1;
    for (int i = 1; i < c->count; i++) {
        if (SDL_strcasecmp(c->paths[i], c->paths[w - 1]) == 0) free(c->paths[i]);
        else c->paths[w++] = c->paths[i];
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

        char val[FONT_FILE_MAX], file[FONT_FILE_MAX];
        if (!wide_to_utf8((const WCHAR *)data, val, sizeof val)) continue;
        if (paths_is_absolute(val)) {
            memcpy(file, val, strlen(val) + 1);
        } else {
            if (!fonts_dir[0]) continue;
            int n = snprintf(file, sizeof file, "%s" PATHS_SEP "%s", fonts_dir, val);
            if (n < 0 || (size_t)n >= sizeof file) continue;
        }
        if (!ext_ok(file)) continue;
        if (!cands_add(c, file)) ok = false;
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

    int cap = cands.count;
    if (cap > 0) {
        out->items = calloc((size_t)cap, sizeof *out->items);
        score = calloc((size_t)cap, sizeof *score);
        if (!out->items || !score) goto done;
    }

    for (int i = 0; i < cands.count; i++) {
        const char *file = cands.paths[i];
        if (strlen(file) >= FONT_FILE_MAX) continue;
        if (!paths_is_absolute(file) || !paths_readable(file)) continue;

        TTF_Font *font = TTF_OpenFont(file, PROBE_PT);
        if (!font) continue;
        const char *fam = TTF_GetFontFamilyName(font);
        if (fam && fam[0] && strlen(fam) < FONT_FAMILY_MAX) {
            bool italic = (TTF_GetFontStyle(font) & TTF_STYLE_ITALIC) != 0;
            int sc = variant_score(TTF_GetFontWeight(font), italic);

            int k = 0;
            while (k < out->count && strcmp(out->items[k].family, fam) != 0) k++;
            /* le cifre si controllano solo se la variante migliora quella già scelta */
            if ((k == out->count || sc < score[k]) && has_digits(font)) {
                if (k == out->count) out->count++;
                score[k] = sc;
                strcpy(out->items[k].family, fam);
                strcpy(out->items[k].file, file);
            }
        }
        TTF_CloseFont(font);
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
