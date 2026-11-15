#include "core/stats.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static char csv[700];

static void stamp(time_t t, char *out, size_t n) {
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(out, n, "%Y-%m-%dT%H:%M:%S", &tm);
}

static void write_file(const char *body) {
    FILE *f = fopen(csv, "w");
    if (f) { fputs(body, f); fclose(f); }
}

static char *slurp(void) {
    static char buf[8192];
    FILE *f = fopen(csv, "r");
    size_t n = 0;
    if (f) { n = fread(buf, 1, sizeof buf - 1, f); fclose(f); }
    buf[n] = '\0';
    return buf;
}

int main(void) {
    char tmpl[] = "/tmp/ribalta-test-XXXXXX";
    char *dir = mkdtemp(tmpl);
    if (!dir) { perror("mkdtemp"); return 1; }
    setenv("XDG_CONFIG_HOME", dir, 1);
    setenv("XDG_DATA_HOME", dir, 1);
    char d[600];
    snprintf(d, sizeof d, "%s/ribalta", dir);
    mkdir(d, 0755);
    snprintf(csv, sizeof csv, "%s/ribalta/sessions.csv", dir);

    time_t now = time(NULL);
    time_t yest = now - 86400;
    StatsSummary s;
    StatsSubjectTotal tot[8];

    /* file assente */
    CHECK(stats_summary(&s, now, NULL) && s.today_s == 0);
    CHECK(stats_subject_totals(tot, 8, now) == 0);

    /* append nuovo file, < 60 s ignorato */
    CHECK(stats_append(now, 30, "timer", true, "Analisi"));
    CHECK(access(csv, F_OK) != 0);
    CHECK(stats_append(now, 600, "timer", true, "Analisi"));
    CHECK(stats_append(now, 300, "pomodoro", false, NULL));
    CHECK(stats_append(now, 120, "timer", true, ""));
    char *c = slurp();
    CHECK(strncmp(c, "inizio,durata_s,tipo,completata,materia\n", 40) == 0);
    CHECK(strstr(c, ",600,timer,1,Analisi\n") != NULL);
    CHECK(strstr(c, ",300,pomodoro,0,\n") != NULL);
    CHECK(strstr(c, ",120,timer,1,\n") != NULL);
    CHECK(stats_summary(&s, now, NULL) && s.today_s == 1020 && s.completed_sessions == 2);
    CHECK(stats_summary(&s, now, "Analisi") && s.today_s == 600 && s.completed_sessions == 1);
    CHECK(stats_summary(&s, now, "") && s.today_s == 420);
    CHECK(stats_summary(&s, now, "Altro") && s.today_s == 0 && s.streak_days == 0);

    /* sanitizzazione */
    unlink(csv);
    CHECK(stats_append(now, 600, "timer", true, "A,b\"c;d\te"));
    CHECK(strstr(slurp(), ",1,A b c d e\n") != NULL);
    CHECK(stats_summary(&s, now, "A b c d e") && s.today_s == 600);

    /* troncamento UTF-8: 20 'è' = 40 byte */
    unlink(csv);
    CHECK(stats_append(now, 600, "timer", true, "èèèèèèèèèèèèèèèèèèèè"));
    {
        char *line = strrchr(slurp(), ',') + 1;
        size_t len = strlen(line) - 1;   /* senza \n */
        CHECK(len <= STATS_SUBJECT_LEN - 1);
        CHECK(len == 24);   /* 12 'è' */
        int ok = 1;
        for (size_t i = 0; i < len; i += 2)
            if ((unsigned char)line[i] != 0xC3 || (unsigned char)line[i + 1] != 0xA8) ok = 0;
        CHECK(ok);
    }
    unlink(csv);
    CHECK(stats_append(now, 600, "timer", true, "abcdefghijklmnopqrstuvwèx"));   /* taglio dentro la è */
    {
        char *line = strrchr(slurp(), ',') + 1;
        CHECK(strcmp(line, "abcdefghijklmnopqrstuvw\n") == 0);
    }

    /* file legacy a 4 colonne + righe a 5 colonne, malformate */
    char t0[32], t1[32], t2[32];
    stamp(now, t0, sizeof t0);
    stamp(yest, t1, sizeof t1);
    stamp(now - 86400 * 20, t2, sizeof t2);   /* fuori dalla settimana */
    char legacy[1024];
    snprintf(legacy, sizeof legacy,
             "inizio,durata_s,tipo,completata\n%s,600,timer,1\n%s,900,pomodoro,0\r\n", t1, t1);
    write_file(legacy);
    size_t legacy_len = strlen(legacy);
    CHECK(stats_append(now, 1200, "timer", true, "Analisi"));
    CHECK(stats_append(now, 60, "timer", true, "Fisica"));
    CHECK(stats_append(now, 60, "timer", true, "Fisica"));
    CHECK(strncmp(slurp(), legacy, legacy_len) == 0);   /* mai riscritto */
    {
        FILE *f = fopen(csv, "a");
        fprintf(f, "garbage\n%s,abc,timer,1,X\n%s,-5,timer,1,X\n%s,100,timer,2,X\n%s,100,timer,1x\n"
                   "%s,0,timer,1\nsolo,virgole,,\n%s,3000,timer,1,Vecchia\n",
                t0, t0, t0, t0, t0, t2);
        fclose(f);
    }
    CHECK(strncmp(slurp(), legacy, legacy_len) == 0);
    CHECK(stats_summary(&s, now, NULL));
    CHECK(s.today_s == 1320);
    CHECK(s.week_s[6] == 1320 && s.week_s[5] == 1500);
    CHECK(s.completed_sessions == 5);   /* 1 legacy + Analisi + 2 Fisica + Vecchia */
    CHECK(s.streak_days == 2);
    CHECK(stats_summary(&s, now, "") && s.today_s == 0 && s.week_s[5] == 1500 && s.completed_sessions == 1);
    CHECK(stats_summary(&s, now, "Analisi") && s.today_s == 1200 && s.week_s[5] == 0 && s.completed_sessions == 1);
    CHECK(stats_summary(&s, now, "Fisica") && s.today_s == 120);

    /* totali per materia */
    int n = stats_subject_totals(tot, 8, now);
    CHECK(n == 3);   /* Vecchia fuori finestra */
    CHECK(strcmp(tot[0].name, "") == 0 && tot[0].week_s == 1500);
    CHECK(strcmp(tot[1].name, "Analisi") == 0 && tot[1].week_s == 1200);
    CHECK(strcmp(tot[2].name, "Fisica") == 0 && tot[2].week_s == 120);
    CHECK(stats_subject_totals(tot, 2, now) == 2);
    CHECK(strcmp(tot[1].name, "Analisi") == 0);

    /* parità: ordine per nome */
    snprintf(legacy, sizeof legacy, "%s,600,timer,1,Zeta\n%s,600,timer,1,Alfa\n", t0, t0);
    write_file(legacy);
    CHECK(stats_subject_totals(tot, 8, now) == 2);
    CHECK(strcmp(tot[0].name, "Alfa") == 0 && strcmp(tot[1].name, "Zeta") == 0);

    if (fails) { fprintf(stderr, "%d failure(s)\n", fails); return 1; }
    puts("ok");
    return 0;
}
