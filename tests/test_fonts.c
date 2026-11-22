#include "platform/fonts.h"
#include "platform/paths.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main(void) {
    FontList l;
    Uint64 t0 = SDL_GetTicks();
    CHECK(fonts_list(&l));
    Uint64 elapsed = SDL_GetTicks() - t0;
    if (l.count == 0) fprintf(stderr, "NOTE: nessun font di sistema con le cifre (verifiche ridotte)\n");
#ifdef _WIN32
    /* su Windows (anche i runner di GitHub) Arial c'è sempre: l'elenco dal registro non può essere vuoto */
    CHECK(l.count > 0);
    bool has_arial = false;
    for (int i = 0; i < l.count; i++)
        if (SDL_strcasecmp(l.items[i].family, "Arial") == 0) has_arial = true;
    CHECK(has_arial);
#endif

    for (int i = 0; i < l.count; i++) {
        CHECK(paths_readable(l.items[i].file));
        CHECK(l.items[i].family[0] && paths_is_absolute(l.items[i].file));
        if (i > 0) {
            CHECK(SDL_strcasecmp(l.items[i - 1].family, l.items[i].family) <= 0);
            CHECK(strcmp(l.items[i - 1].family, l.items[i].family) != 0);
        }
        CHECK(fonts_find(&l, l.items[i].file) >= 0);
    }
    CHECK(fonts_find(&l, "") == -1);
    CHECK(fonts_find(&l, NULL) == -1);
    CHECK(fonts_find(&l, "/nonexistent/font.ttf") == -1);

    int n = l.count;
    char first[FONT_FILE_MAX] = "", last[FONT_FILE_MAX] = "";
    if (n > 0) {
        snprintf(first, sizeof first, "%s", l.items[0].file);
        snprintf(last, sizeof last, "%s", l.items[n - 1].file);
    }
    fonts_free(&l);
    CHECK(l.count == 0 && l.items == NULL);
    fonts_free(&l); /* sicura su lista vuota */

    FontEntry e;
    if (n > 0) {
        CHECK(fonts_step("", +1, &e) && strcmp(e.file, first) == 0);
        CHECK(fonts_step(NULL, +1, &e) && strcmp(e.file, first) == 0);
        CHECK(fonts_step(first, -1, &e) && e.family[0] == '\0' && e.file[0] == '\0');
        CHECK(fonts_step(last, +1, &e) && e.family[0] == '\0' && e.file[0] == '\0');
        CHECK(fonts_step("", -1, &e) && strcmp(e.file, last) == 0);
        if (n > 1) CHECK(fonts_step(first, +1, &e) && e.file[0] && strcmp(e.file, first) != 0);
    } else {
        CHECK(fonts_step("", +1, &e) && e.file[0] == '\0');
    }

    if (fails) { fprintf(stderr, "%d FAIL\n", fails); return 1; }
#ifdef _WIN32
    printf("OK (%d font, %llu ms)\n", n, (unsigned long long)elapsed);
#else
    (void)elapsed;
    printf("OK (%d font)\n", n);
#endif
    return 0;
}
