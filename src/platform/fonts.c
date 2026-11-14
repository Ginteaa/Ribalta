/* Elenco dei font di sistema tramite fontconfig (vedi fonts.h). */
#include "platform/fonts.h"

#include <fontconfig/fontconfig.h>
#ifdef __GLIBC__
#include <malloc.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

static int cmp_entry(const void *a, const void *b)
{
    const FontEntry *x = a, *y = b;
    int c = strcasecmp(x->family, y->family);
    return c ? c : strcmp(x->family, y->family);
}

/* Punteggio della variante: più basso = migliore (Bold non corsivo, poi Regular, poi il resto). */
static int variant_score(int weight, int slant)
{
    int s = 0;
    if (slant != FC_SLANT_ROMAN) s += 10000;
    if (weight >= FC_WEIGHT_BOLD) s += weight - FC_WEIGHT_BOLD;  /* Bold, o la più vicina */
    else s += 1000 + abs(weight - FC_WEIGHT_REGULAR);              /* altrimenti la più vicina a Regular */
    return s;
}

bool fonts_list(FontList *out)
{
    out->items = NULL;
    out->count = 0;
    if (!FcInit()) return false;

    bool ok = false;
    FcPattern *pat = FcPatternCreate();
    FcObjectSet *os = FcObjectSetBuild(FC_FAMILY, FC_FILE, FC_WEIGHT, FC_SLANT, FC_FONTFORMAT, FC_INDEX, NULL);
    FcCharSet *cs = FcCharSetCreate();
    FcFontSet *fs = NULL;
    int *score = NULL, cap = 0;
    if (!pat || !os || !cs) goto done;

    for (FcChar32 c = 0x30; c <= 0x39; c++) FcCharSetAddChar(cs, c);
    FcPatternAddBool(pat, FC_SCALABLE, FcTrue);
    FcPatternAddCharSet(pat, FC_CHARSET, cs);
    fs = FcFontList(NULL, pat, os);
    if (!fs) goto done;

    cap = fs->nfont > 0 ? fs->nfont : 1;
    out->items = calloc((size_t)cap, sizeof *out->items);
    score = calloc((size_t)cap, sizeof *score);
    if (!out->items || !score) goto done;

    for (int i = 0; i < fs->nfont; i++) {
        FcChar8 *fam = NULL, *file = NULL, *fmt = NULL;
        int weight = FC_WEIGHT_REGULAR, slant = FC_SLANT_ROMAN, index = 0;
        FcPattern *p = fs->fonts[i];
        if (FcPatternGetString(p, FC_FAMILY, 0, &fam) != FcResultMatch || !fam[0]) continue;
        if (FcPatternGetString(p, FC_FILE, 0, &file) != FcResultMatch || !file[0]) continue;
        if (FcPatternGetString(p, FC_FONTFORMAT, 0, &fmt) == FcResultMatch
            && strcmp((const char *)fmt, "TrueType") != 0 && strcmp((const char *)fmt, "CFF") != 0)
            continue;
        if (strlen((const char *)fam) >= FONT_FAMILY_MAX || strlen((const char *)file) >= FONT_FILE_MAX)
            continue;
        /* TTF_OpenFont apre solo la faccia 0: scarta le altre facce delle collezioni (.ttc)
         * e le istanze con nome dei font variabili (index != 0) */
        if (FcPatternGetInteger(p, FC_INDEX, 0, &index) == FcResultMatch && index != 0) continue;
        if (access((const char *)file, R_OK) != 0) continue;
        FcPatternGetInteger(p, FC_WEIGHT, 0, &weight);
        FcPatternGetInteger(p, FC_SLANT, 0, &slant);
        int sc = variant_score(weight, slant);

        int k = 0;
        while (k < out->count && strcmp(out->items[k].family, (const char *)fam) != 0) k++;
        if (k == out->count) out->count++;
        else if (sc >= score[k]) continue;
        score[k] = sc;
        strcpy(out->items[k].family, (const char *)fam);
        strcpy(out->items[k].file, (const char *)file);
    }
    qsort(out->items, (size_t)out->count, sizeof *out->items, cmp_entry);
    ok = true;
done:
    free(score);
    if (fs) FcFontSetDestroy(fs);
    if (cs) FcCharSetDestroy(cs);
    if (os) FcObjectSetDestroy(os);
    if (pat) FcPatternDestroy(pat);
    if (!ok) { free(out->items); out->items = NULL; out->count = 0; }
    return ok;
}

void fonts_free(FontList *l)
{
    if (!l) return;
    free(l->items);
    l->items = NULL;
    l->count = 0;
    FcFini();
#ifdef __GLIBC__
    /* la cache di fontconfig sono molti piccoli blocchi: senza trim glibc non li restituisce al
     * sistema e la RAM a riposo resterebbe ~5 MB più alta dopo aver chiuso il menu */
    malloc_trim(0);
#endif
}

int fonts_find(const FontList *l, const char *file)
{
    if (!l || !file || !file[0]) return -1;
    for (int i = 0; i < l->count; i++)
        if (strcmp(l->items[i].file, file) == 0) return i;
    return -1;
}

bool fonts_step(const char *current_file, int dir, FontEntry *out)
{
    FontList l;
    if (!fonts_list(&l)) { fonts_free(&l); return false; }
    int n = l.count + 1;                       /* anello: 0 = font incluso, 1..count = lista */
    int cur = fonts_find(&l, current_file) + 1; /* non trovato/vuoto -> 0 */
    int next = ((cur + (dir < 0 ? -1 : 1)) % n + n) % n;
    if (next == 0) { out->family[0] = '\0'; out->file[0] = '\0'; }
    else *out = l.items[next - 1];
    fonts_free(&l);
    return true;
}
