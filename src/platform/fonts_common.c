/* Parte comune a tutte le piattaforme dell'elenco font (vedi fonts.h): ordinamento, ricerca e
 * scorrimento ←/→. fonts_list/fonts_free stanno in fonts.c (Linux, fontconfig) o in
 * fonts_mac.c (macOS, CoreText). */
#include "platform/fonts.h"

#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>

static int cmp_entry(const void *a, const void *b)
{
    const FontEntry *x = a, *y = b;
    int c = SDL_strcasecmp(x->family, y->family);
    return c ? c : strcmp(x->family, y->family);
}

void fonts_sort(FontList *l)
{
    if (l && l->items && l->count > 1)
        qsort(l->items, (size_t)l->count, sizeof *l->items, cmp_entry);
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
