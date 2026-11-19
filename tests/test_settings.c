#include "core/settings.h"
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

    /* font: round-trip con spazi e percorso; chiavi mancanti -> vuoto */
    settings_defaults(&s);
    CHECK(s.font_family[0] == '\0' && s.font_file[0] == '\0');
    snprintf(s.font_family, sizeof s.font_family, "DejaVu Sans Mono");
    snprintf(s.font_file, sizeof s.font_file, "/usr/share/fonts/TTF/My Font Bold.ttf");
    CHECK(settings_save(&s));
    CHECK(settings_load(&l));
    CHECK(!strcmp(l.font_family, "DejaVu Sans Mono"));
    CHECK(!strcmp(l.font_file, "/usr/share/fonts/TTF/My Font Bold.ttf"));
    write_cfg(dir, "scale=80\n");
    CHECK(settings_load(&l));
    CHECK(l.font_family[0] == '\0' && l.font_file[0] == '\0');
    /* CRLF e spazi finali tagliati */
    write_cfg(dir, "font_family=Foo Bar  \r\nfont_file=/a/b c.ttf\r\n");
    CHECK(settings_load(&l));
    CHECK(!strcmp(l.font_family, "Foo Bar") && !strcmp(l.font_file, "/a/b c.ttf"));
    /* valore troppo lungo ignorato */
    {
        char big[900], body[1000];
        memset(big, 'x', sizeof big - 1);
        big[sizeof big - 1] = 0;
        snprintf(body, sizeof body, "font_family=Ok\nfont_file=%s\n", big);
        write_cfg(dir, body);
        CHECK(settings_load(&l));
        CHECK(!strcmp(l.font_family, "Ok") && l.font_file[0] == '\0');
    }

    /* materie */
    settings_defaults(&s);
    CHECK(s.subject_count == 0 && s.subject[0] == '\0');
    CHECK(settings_subject_add(&s, "  Analisi "));
    CHECK(settings_subject_add(&s, "Fisica"));
    CHECK(settings_subject_add(&s, "Chimica"));
    CHECK(!settings_subject_add(&s, "analisi"));      /* duplicato senza maiuscole */
    CHECK(!settings_subject_add(&s, ""));
    CHECK(!settings_subject_add(&s, "   "));
    CHECK(!settings_subject_add(&s, "A,B"));
    CHECK(!settings_subject_add(&s, "A;B"));
    CHECK(!settings_subject_add(&s, "A\"B"));
    CHECK(!settings_subject_add(&s, "A\tB"));
    CHECK(!settings_subject_add(&s, NULL));
    CHECK(s.subject_count == 3 && !strcmp(s.subjects[0], "Analisi"));
    CHECK(settings_subject_find(&s, "Fisica") == 1);
    CHECK(settings_subject_find(&s, "fisica") == -1);
    CHECK(settings_subject_find(&s, "") == -1 && settings_subject_find(&s, NULL) == -1);
    snprintf(s.subject, sizeof s.subject, "Fisica");
    CHECK(settings_save(&s));
    CHECK(settings_load(&l));
    CHECK(l.subject_count == 3 && !strcmp(l.subjects[0], "Analisi") && !strcmp(l.subjects[1], "Fisica") &&
          !strcmp(l.subjects[2], "Chimica") && !strcmp(l.subject, "Fisica"));
    /* rimozione: ordine conservato, attiva -> "" */
    settings_subject_remove(&s, 5);
    settings_subject_remove(&s, -1);
    CHECK(s.subject_count == 3 && !strcmp(s.subject, "Fisica"));
    settings_subject_remove(&s, 0);
    CHECK(s.subject_count == 2 && !strcmp(s.subjects[0], "Fisica") && !strcmp(s.subjects[1], "Chimica"));
    CHECK(!strcmp(s.subject, "Fisica"));
    settings_subject_remove(&s, 0);
    CHECK(s.subject_count == 1 && !strcmp(s.subjects[0], "Chimica") && s.subject[0] == '\0');
    /* elenco pieno */
    settings_defaults(&s);
    for (int i = 0; i < SUBJECTS_MAX; i++) {
        char nm[16];
        snprintf(nm, sizeof nm, "M%d", i);
        CHECK(settings_subject_add(&s, nm));
    }
    CHECK(!settings_subject_add(&s, "Extra") && s.subject_count == SUBJECTS_MAX);
    /* lunghezza: 24 byte ok (anche con accenti), 25 no */
    settings_defaults(&s);
    CHECK(settings_subject_add(&s, "abcdefghijklmnopqrstuvwx"));            /* 24 */
    CHECK(!settings_subject_add(&s, "abcdefghijklmnopqrstuvwxy"));          /* 25 */
    CHECK(settings_subject_add(&s, "\xc3\xa0\xc3\xa0\xc3\xa0\xc3\xa0\xc3\xa0\xc3\xa0\xc3\xa0\xc3\xa0\xc3\xa0\xc3\xa0\xc3\xa0\xc3\xa0")); /* 24 byte */
    CHECK(!settings_subject_add(&s, "\xc3\xa0\xc3\xa0\xc3\xa0\xc3\xa0\xc3\xa0\xc3\xa0\xc3\xa0\xc3\xa0\xc3\xa0\xc3\xa0\xc3\xa0\xc3\xa0x")); /* 25 */
    CHECK(settings_subject_valid("Fisica 2") && !settings_subject_valid(" x") && !settings_subject_valid("x "));
    /* file: voci non valide/duplicate ignorate, subject prima di subjects */
    write_cfg(dir, "subject=Fisica\nsubjects=Analisi; Fisica ;A,B;;analisi;\"x\";Chimica\n");
    CHECK(settings_load(&l));
    CHECK(l.subject_count == 3 && !strcmp(l.subjects[0], "Analisi") && !strcmp(l.subjects[1], "Fisica") &&
          !strcmp(l.subjects[2], "Chimica") && !strcmp(l.subject, "Fisica"));
    write_cfg(dir, "subjects=Analisi;Fisica\nsubject=Storia\n");
    CHECK(settings_load(&l));
    CHECK(l.subject_count == 2 && l.subject[0] == '\0');
    write_cfg(dir, "subject=Analisi\n");   /* senza elenco */
    CHECK(settings_load(&l));
    CHECK(l.subject_count == 0 && l.subject[0] == '\0');
    write_cfg(dir, "subjects=A;B;C;D;E;F;G;H;I;J\n");
    CHECK(settings_load(&l));
    CHECK(l.subject_count == SUBJECTS_MAX);
    /* le chiavi si scrivono sempre */
    settings_defaults(&s);
    CHECK(settings_save(&s));
    f = fopen(p, "r");
    memset(buf, 0, sizeof buf);
    if (f) { size_t n = fread(buf, 1, sizeof buf - 1, f); buf[n] = 0; fclose(f); }
    CHECK(strstr(buf, "subjects=\n") && strstr(buf, "subject=\n"));

    /* colore del riposo Pomodoro */
    settings_defaults(&s);
    CHECK(s.pomodoro_break_mode == BREAK_COLOR_DIGITS && s.pomodoro_break_color == 0x6fa8dc);
    for (int m = 0; m < 3; m++) {
        settings_defaults(&s);
        s.pomodoro_break_mode = m;
        s.pomodoro_break_color = 0x123456;
        CHECK(settings_save(&s));
        settings_defaults(&l);
        CHECK(settings_load(&l) && l.pomodoro_break_mode == m);
        if (m == BREAK_COLOR_CUSTOM) CHECK(l.pomodoro_break_color == 0x123456);
        f = fopen(p, "r");
        memset(buf, 0, sizeof buf);
        if (f) { size_t n = fread(buf, 1, sizeof buf - 1, f); buf[n] = 0; fclose(f); }
        CHECK(!strstr(buf, "pomodoro_break_blue"));
        CHECK(strstr(buf, "pomodoro_break_color="));
    }
    write_cfg(dir, "pomodoro_break_color=AUTO\n");
    CHECK(settings_load(&l) && l.pomodoro_break_mode == BREAK_COLOR_AUTO);
    write_cfg(dir, "pomodoro_break_color=#FF8800\n");
    CHECK(settings_load(&l) && l.pomodoro_break_mode == BREAK_COLOR_CUSTOM && l.pomodoro_break_color == 0xff8800);
    /* legacy */
    write_cfg(dir, "pomodoro_break_blue=true\n");
    CHECK(settings_load(&l) && l.pomodoro_break_mode == BREAK_COLOR_AUTO);
    write_cfg(dir, "pomodoro_break_blue=true\npomodoro_break_color=digits\n");
    CHECK(settings_load(&l) && l.pomodoro_break_mode == BREAK_COLOR_DIGITS);
    write_cfg(dir, "pomodoro_break_color=#102030\npomodoro_break_blue=true\n");
    CHECK(settings_load(&l) && l.pomodoro_break_mode == BREAK_COLOR_CUSTOM && l.pomodoro_break_color == 0x102030);
    write_cfg(dir, "pomodoro_break_blue=false\n");
    CHECK(settings_load(&l) && l.pomodoro_break_mode == BREAK_COLOR_DIGITS);
    write_cfg(dir, "pomodoro_break_blue=boh\n");
    CHECK(settings_load(&l) && l.pomodoro_break_mode == BREAK_COLOR_DIGITS);
    /* valori non validi ignorati; un nuovo valore non valido non annulla il legacy */
    write_cfg(dir, "pomodoro_break_color=boh\n");
    CHECK(settings_load(&l) && l.pomodoro_break_mode == BREAK_COLOR_DIGITS && l.pomodoro_break_color == 0x6fa8dc);
    write_cfg(dir, "pomodoro_break_color=#12\npomodoro_break_blue=true\n");
    CHECK(settings_load(&l) && l.pomodoro_break_mode == BREAK_COLOR_AUTO);

    char cmd[600];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", dir);
    if (system(cmd) != 0) { /* ignora */ }

    if (fails) return 1;
    puts("ok");
    return 0;
}
