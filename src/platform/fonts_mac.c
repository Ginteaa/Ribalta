/* Elenco dei font di sistema tramite CoreText su macOS (vedi fonts.h).
 * Le funzioni comuni (fonts_sort, fonts_find, fonts_step) stanno in fonts_common.c.
 * CoreText viene usato solo mentre il menu font è aperto: tutto ciò che si crea si rilascia
 * prima di tornare. */
#include "platform/fonts.h"

#include <CoreFoundation/CoreFoundation.h>
#include <CoreText/CoreText.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

/* Pesi CoreText (-1..1) in millesimi: Regular 0, Bold circa 0.4 */
#define W_REGULAR 0
#define W_BOLD    400
#define W_BOLDMIN 350  /* da qui in su la variante conta come Bold */

/* Punteggio della variante: più basso = migliore (Bold non corsivo, poi Regular, poi il resto). */
static int variant_score(int weight, bool italic)
{
    int s = 0;
    if (italic) s += 10000;
    if (weight >= W_BOLDMIN) s += abs(weight - W_BOLD);  /* Bold, o la più vicina */
    else s += 1000 + abs(weight - W_REGULAR);            /* altrimenti la più vicina a Regular */
    return s;
}

/* Copia una CFString in buf (UTF-8); false se troppo lunga o vuota */
static bool cfstr_to_buf(CFTypeRef v, char *buf, size_t size)
{
    if (!v || CFGetTypeID(v) != CFStringGetTypeID()) return false;
    if (!CFStringGetCString((CFStringRef)v, buf, (CFIndex)size, kCFStringEncodingUTF8)) return false;
    return buf[0] != '\0';
}

/* Solo .ttf/.otf: TTF_OpenFont apre la faccia 0, quindi niente collezioni (.ttc/.otc/.dfont) */
static bool ext_ok(const char *path)
{
    const char *dot = strrchr(path, '.');
    if (!dot) return false;
    return strcasecmp(dot, ".ttf") == 0 || strcasecmp(dot, ".otf") == 0;
}

/* Il font contiene tutte le cifre 0..9? */
static bool has_digits(CTFontDescriptorRef desc)
{
    UniChar chars[10];
    CGGlyph glyphs[10];
    for (int i = 0; i < 10; i++) chars[i] = (UniChar)('0' + i);
    CTFontRef font = CTFontCreateWithFontDescriptor(desc, 12.0, NULL);
    if (!font) return false;
    bool ok = CTFontGetGlyphsForCharacters(font, chars, glyphs, 10);
    CFRelease(font);
    return ok;
}

/* Legge peso (millesimi) e corsivo dai tratti del descrittore */
static void read_traits(CTFontDescriptorRef desc, int *weight, bool *italic)
{
    *weight = W_REGULAR;
    *italic = false;
    CFTypeRef t = CTFontDescriptorCopyAttribute(desc, kCTFontTraitsAttribute);
    if (!t) return;
    if (CFGetTypeID(t) == CFDictionaryGetTypeID()) {
        CFDictionaryRef traits = (CFDictionaryRef)t;
        CFTypeRef w = CFDictionaryGetValue(traits, kCTFontWeightTrait);
        if (w && CFGetTypeID(w) == CFNumberGetTypeID()) {
            float f = 0.0f;
            if (CFNumberGetValue((CFNumberRef)w, kCFNumberFloatType, &f))
                *weight = (int)(f * 1000.0f);
        }
        CFTypeRef sy = CFDictionaryGetValue(traits, kCTFontSymbolicTrait);
        if (sy && CFGetTypeID(sy) == CFNumberGetTypeID()) {
            int32_t v = 0;
            if (CFNumberGetValue((CFNumberRef)sy, kCFNumberSInt32Type, &v))
                *italic = (((uint32_t)v) & (uint32_t)kCTFontItalicTrait) != 0;
        }
    }
    CFRelease(t);
}

bool fonts_list(FontList *out)
{
    out->items = NULL;
    out->count = 0;

    bool ok = false;
    CTFontCollectionRef coll = CTFontCollectionCreateFromAvailableFonts(NULL);
    CFArrayRef descs = NULL;
    int *score = NULL;
    if (!coll) goto done;
    descs = CTFontCollectionCreateMatchingFontDescriptors(coll);
    if (!descs) goto done;

    CFIndex n = CFArrayGetCount(descs);
    int cap = n > 0 ? (int)n : 1;
    out->items = calloc((size_t)cap, sizeof *out->items);
    score = calloc((size_t)cap, sizeof *score);
    if (!out->items || !score) goto done;

    for (CFIndex i = 0; i < n; i++) {
        CTFontDescriptorRef desc = (CTFontDescriptorRef)CFArrayGetValueAtIndex(descs, i);
        if (!desc) continue;

        char fam[FONT_FAMILY_MAX], file[FONT_FILE_MAX];
        CFTypeRef famv = CTFontDescriptorCopyAttribute(desc, kCTFontFamilyNameAttribute);
        bool fam_ok = cfstr_to_buf(famv, fam, sizeof fam);
        if (famv) CFRelease(famv);
        if (!fam_ok) continue;

        CFTypeRef urlv = CTFontDescriptorCopyAttribute(desc, kCTFontURLAttribute);
        bool file_ok = false;
        if (urlv) {
            if (CFGetTypeID(urlv) == CFURLGetTypeID())
                file_ok = CFURLGetFileSystemRepresentation((CFURLRef)urlv, true,
                                                           (UInt8 *)file, (CFIndex)sizeof file);
            CFRelease(urlv);
        }
        if (!file_ok || file[0] != '/') continue;
        if (!ext_ok(file)) continue;
        if (access(file, R_OK) != 0) continue;

        int weight;
        bool italic;
        read_traits(desc, &weight, &italic);
        int sc = variant_score(weight, italic);

        int k = 0;
        while (k < out->count && strcmp(out->items[k].family, fam) != 0) k++;
        if (k < out->count && sc >= score[k]) continue;  /* c'è già una variante migliore */
        if (!has_digits(desc)) continue;
        if (k == out->count) out->count++;
        score[k] = sc;
        strcpy(out->items[k].family, fam);
        strcpy(out->items[k].file, file);
    }
    fonts_sort(out);
    ok = true;
done:
    free(score);
    if (descs) CFRelease(descs);
    if (coll) CFRelease(coll);
    if (!ok) { free(out->items); out->items = NULL; out->count = 0; }
    return ok;
}

void fonts_free(FontList *l)
{
    if (!l) return;
    free(l->items);
    l->items = NULL;
    l->count = 0;
}
