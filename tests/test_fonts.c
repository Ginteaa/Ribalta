#include "platform/fonts.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main(void) {
    FontList l;
    CHECK(fonts_list(&l));
    if (l.count == 0) fprintf(stderr, "NOTE: nessun font di sistema con le cifre (verifiche ridotte)\n");

    for (int i = 0; i < l.count; i++) {
        CHECK(access(l.items[i].file, R_OK) == 0);
        CHECK(l.items[i].family[0] && l.items[i].file[0] == '/');
        if (i > 0) {
            CHECK(strcasecmp(l.items[i - 1].family, l.items[i].family) <= 0);
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
    printf("OK (%d font)\n", n);
    return 0;
}
