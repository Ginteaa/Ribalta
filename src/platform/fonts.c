/* Elenco dei font di sistema tramite fontconfig (vedi fonts.h).
 * libfontconfig è caricata a richiesta con dlopen (solo mentre il menu font è aperto), così
 * all'avvio non si porta dietro freetype, harfbuzz, glib, ecc. */
#include "platform/fonts.h"
#include "platform/paths.h"

#include <dlfcn.h>
#include <fontconfig/fontconfig.h>  /* solo tipi e costanti: nessun simbolo linkato */
#ifdef __GLIBC__
#include <malloc.h>
#endif
#include <stdlib.h>
#include <string.h>

/* Punteggio della variante: più basso = migliore (Bold non corsivo, poi Regular, poi il resto). */
static int variant_score(int weight, int slant)
{
    int s = 0;
    if (slant != FC_SLANT_ROMAN) s += 10000;
    if (weight >= FC_WEIGHT_BOLD) s += weight - FC_WEIGHT_BOLD;  /* Bold, o la più vicina */
    else s += 1000 + abs(weight - FC_WEIGHT_REGULAR);              /* altrimenti la più vicina a Regular */
    return s;
}

/* Funzioni di fontconfig risolte con dlsym */
static struct {
    void *handle;
    FcBool (*Init)(void);
    void (*Fini)(void);
    FcPattern *(*PatternCreate)(void);
    void (*PatternDestroy)(FcPattern *);
    FcBool (*PatternAddBool)(FcPattern *, const char *, FcBool);
    FcBool (*PatternAddCharSet)(FcPattern *, const char *, const FcCharSet *);
    FcResult (*PatternGetString)(const FcPattern *, const char *, int, FcChar8 **);
    FcResult (*PatternGetInteger)(const FcPattern *, const char *, int, int *);
    FcObjectSet *(*ObjectSetCreate)(void);
    FcBool (*ObjectSetAdd)(FcObjectSet *, const char *);
    void (*ObjectSetDestroy)(FcObjectSet *);
    FcCharSet *(*CharSetCreate)(void);
    FcBool (*CharSetAddChar)(FcCharSet *, FcChar32);
    void (*CharSetDestroy)(FcCharSet *);
    FcFontSet *(*FontList)(FcConfig *, FcPattern *, FcObjectSet *);
    void (*FontSetDestroy)(FcFontSet *);
} fc;

/* Carica libfontconfig e risolve i simboli; false (e nulla resta caricato) se qualcosa manca */
static bool fc_load(void)
{
    if (fc.handle) return true;
    void *h = dlopen("libfontconfig.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!h) return false;
#define FC_SYM(field, name) do { *(void **)&fc.field = dlsym(h, name); if (!fc.field) goto fail; } while (0)
    FC_SYM(Init, "FcInit");
    FC_SYM(Fini, "FcFini");
    FC_SYM(PatternCreate, "FcPatternCreate");
    FC_SYM(PatternDestroy, "FcPatternDestroy");
    FC_SYM(PatternAddBool, "FcPatternAddBool");
    FC_SYM(PatternAddCharSet, "FcPatternAddCharSet");
    FC_SYM(PatternGetString, "FcPatternGetString");
    FC_SYM(PatternGetInteger, "FcPatternGetInteger");
    FC_SYM(ObjectSetCreate, "FcObjectSetCreate");
    FC_SYM(ObjectSetAdd, "FcObjectSetAdd");
    FC_SYM(ObjectSetDestroy, "FcObjectSetDestroy");
    FC_SYM(CharSetCreate, "FcCharSetCreate");
    FC_SYM(CharSetAddChar, "FcCharSetAddChar");
    FC_SYM(CharSetDestroy, "FcCharSetDestroy");
    FC_SYM(FontList, "FcFontList");
    FC_SYM(FontSetDestroy, "FcFontSetDestroy");
#undef FC_SYM
    fc.handle = h;
    return true;
fail:
    dlclose(h);
    memset(&fc, 0, sizeof fc);
    return false;
}

static void fc_unload(void)
{
    if (!fc.handle) return;
    fc.Fini();
    dlclose(fc.handle);
    memset(&fc, 0, sizeof fc);
}

bool fonts_list(FontList *out)
{
    out->items = NULL;
    out->count = 0;
    if (!fc_load()) return false;
    if (!fc.Init()) { fc_unload(); return false; }

    bool ok = false;
    FcPattern *pat = fc.PatternCreate();
    FcObjectSet *os = fc.ObjectSetCreate();
    FcCharSet *cs = fc.CharSetCreate();
    FcFontSet *fs = NULL;
    int *score = NULL, cap = 0;
    if (!pat || !os || !cs) goto done;
    {
        const char *props[] = { FC_FAMILY, FC_FILE, FC_WEIGHT, FC_SLANT, FC_FONTFORMAT, FC_INDEX };
        for (size_t i = 0; i < sizeof props / sizeof *props; i++)
            if (!fc.ObjectSetAdd(os, props[i])) goto done;
    }

    for (FcChar32 c = 0x30; c <= 0x39; c++) fc.CharSetAddChar(cs, c);
    fc.PatternAddBool(pat, FC_SCALABLE, FcTrue);
    fc.PatternAddCharSet(pat, FC_CHARSET, cs);
    fs = fc.FontList(NULL, pat, os);
    if (!fs) goto done;

    cap = fs->nfont > 0 ? fs->nfont : 1;
    out->items = calloc((size_t)cap, sizeof *out->items);
    score = calloc((size_t)cap, sizeof *score);
    if (!out->items || !score) goto done;

    for (int i = 0; i < fs->nfont; i++) {
        FcChar8 *fam = NULL, *file = NULL, *fmt = NULL;
        int weight = FC_WEIGHT_REGULAR, slant = FC_SLANT_ROMAN, index = 0;
        FcPattern *p = fs->fonts[i];
        if (fc.PatternGetString(p, FC_FAMILY, 0, &fam) != FcResultMatch || !fam[0]) continue;
        if (fc.PatternGetString(p, FC_FILE, 0, &file) != FcResultMatch || !file[0]) continue;
        if (fc.PatternGetString(p, FC_FONTFORMAT, 0, &fmt) == FcResultMatch
            && strcmp((const char *)fmt, "TrueType") != 0 && strcmp((const char *)fmt, "CFF") != 0)
            continue;
        if (strlen((const char *)fam) >= FONT_FAMILY_MAX || strlen((const char *)file) >= FONT_FILE_MAX)
            continue;
        /* TTF_OpenFont apre solo la faccia 0: scarta le altre facce delle collezioni (.ttc)
         * e le istanze con nome dei font variabili (index != 0) */
        if (fc.PatternGetInteger(p, FC_INDEX, 0, &index) == FcResultMatch && index != 0) continue;
        if (!paths_readable((const char *)file)) continue;
        fc.PatternGetInteger(p, FC_WEIGHT, 0, &weight);
        fc.PatternGetInteger(p, FC_SLANT, 0, &slant);
        int sc = variant_score(weight, slant);

        int k = 0;
        while (k < out->count && strcmp(out->items[k].family, (const char *)fam) != 0) k++;
        if (k == out->count) out->count++;
        else if (sc >= score[k]) continue;
        score[k] = sc;
        strcpy(out->items[k].family, (const char *)fam);
        strcpy(out->items[k].file, (const char *)file);
    }
    fonts_sort(out);
    ok = true;
done:
    free(score);
    if (fs) fc.FontSetDestroy(fs);
    if (cs) fc.CharSetDestroy(cs);
    if (os) fc.ObjectSetDestroy(os);
    if (pat) fc.PatternDestroy(pat);
    if (!ok) { free(out->items); out->items = NULL; out->count = 0; }
    return ok;
}

void fonts_free(FontList *l)
{
    if (!l) return;
    free(l->items);
    l->items = NULL;
    l->count = 0;
    fc_unload();
#ifdef __GLIBC__
    /* la cache di fontconfig sono molti piccoli blocchi: senza trim glibc non li restituisce al
     * sistema e la RAM a riposo resterebbe ~5 MB più alta dopo aver chiuso il menu */
    malloc_trim(0);
#endif
}
