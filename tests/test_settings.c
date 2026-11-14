#include "../src/settings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void write_cfg(const char *dir, const char *body) {
    char p[600];
    snprintf(p, sizeof p, "%s/ribalta", dir);
    mkdir(p, 0755);
    snprintf(p, sizeof p, "%s/ribalta/config.ini", dir);
    FILE *f = fopen(p, "w");
    if (f) { fputs(body, f); fclose(f); }
}

int main(void) {
    char tmpl[] = "/tmp/ribalta-test-XXXXXX";
    char *dir = mkdtemp(tmpl);
    if (!dir) { perror("mkdtemp"); return 1; }
    setenv("XDG_CONFIG_HOME", dir, 1);
    setenv("XDG_DATA_HOME", dir, 1);

    Settings s;
    settings_defaults(&s);
    CHECK(s.digit_color == 0xb7b7b7 && s.recent_count == 0);
    CHECK(!settings_load(&s));   /* nessun file */
    CHECK(s.digit_color == 0xb7b7b7);

    /* round-trip */
    s.digit_color = 0xffb347;
    settings_push_recent(&s, 0x111111);
    settings_push_recent(&s, 0x222222);
    CHECK(settings_save(&s));
    Settings l;
    CHECK(settings_load(&l));
    CHECK(l.digit_color == 0xffb347);
    CHECK(l.recent_count == 2 && l.recent_colors[0] == 0x222222 && l.recent_colors[1] == 0x111111);

    /* senza recenti la riga non viene scritta */
    settings_defaults(&s);
    CHECK(settings_save(&s));
    char p[600];
    snprintf(p, sizeof p, "%s/ribalta/config.ini", dir);
    FILE *f = fopen(p, "r");
    char buf[4096] = {0};
    if (f) { size_t n = fread(buf, 1, sizeof buf - 1, f); buf[n] = 0; fclose(f); }
    CHECK(strstr(buf, "digit_color=#b7b7b7") != NULL);
    CHECK(strstr(buf, "recent_colors") == NULL);

    /* valori non validi */
    write_cfg(dir, "digit_color=zzz\nrecent_colors=#aabbcc, nope,#112233,#12,#445566,#778899,#99aabb,#ccddee,#010203\n");
    CHECK(settings_load(&l));
    CHECK(l.digit_color == 0xb7b7b7);
    CHECK(l.recent_count == RECENT_COLORS_MAX);
    CHECK(l.recent_colors[0] == 0xaabbcc && l.recent_colors[1] == 0x112233 && l.recent_colors[2] == 0x445566);
    CHECK(l.recent_colors[4] == 0x99aabb);

    write_cfg(dir, "digit_color=#00FF7f\nrecent_colors=garbage\n");
    CHECK(settings_load(&l));
    CHECK(l.digit_color == 0x00ff7f && l.recent_count == 0);

    /* push_recent */
    settings_defaults(&s);
    for (uint32_t i = 1; i <= 7; i++) settings_push_recent(&s, i);
    CHECK(s.recent_count == RECENT_COLORS_MAX);
    CHECK(s.recent_colors[0] == 7 && s.recent_colors[4] == 3);
    settings_push_recent(&s, 5);   /* dedupe: sposta in testa */
    CHECK(s.recent_count == 5 && s.recent_colors[0] == 5 && s.recent_colors[1] == 7 &&
          s.recent_colors[2] == 6 && s.recent_colors[3] == 4 && s.recent_colors[4] == 3);
    settings_push_recent(&s, 5);
    CHECK(s.recent_count == 5 && s.recent_colors[0] == 5 && s.recent_colors[1] == 7);

    char cmd[600];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", dir);
    if (system(cmd) != 0) { /* ignora */ }

    if (fails) return 1;
    puts("ok");
    return 0;
}
