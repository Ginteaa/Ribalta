/* Elenco dei font di sistema su Windows (vedi fonts.h).
 * Per ora è uno stub: la lista resta vuota e il menu font offre solo il font incluso. L'elenco
 * vero (registro HKLM/HKCU ...\Windows NT\CurrentVersion\Fonts) arriva nello step 6 del porting.
 * Le funzioni comuni (fonts_sort, fonts_find, fonts_step) stanno in fonts_common.c. */
#include "platform/fonts.h"

#include <stdlib.h>

bool fonts_list(FontList *out)
{
    out->items = NULL;
    out->count = 0;
    return true;
}

void fonts_free(FontList *l)
{
    if (!l) return;
    free(l->items);
    l->items = NULL;
    l->count = 0;
}
