#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app/session.h"
#include "core/pomodoro.h"
#include "test_util.h"

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

#define S SDL_NS_PER_SECOND
static const Uint64 T0 = 1000 * S;

static char csv[700];

static void reset_csv(void) { test_remove(csv); }

/* Contenuto del CSV senza intestazione */
static char *rows(void)
{
    static char buf[4096];
    size_t n = 0;
    FILE *f = fopen(csv, "r");
    if (f) { n = fread(buf, 1, sizeof buf - 1, f); fclose(f); }
    buf[n] = '\0';
    char *nl = strchr(buf, '\n'); /* salta l'intestazione */
    return nl ? nl + 1 : buf;
}

static int nrows(void)
{
    int n = 0;
    for (const char *p = rows(); *p; p++)
        if (*p == '\n') n++;
    return n;
}

/* Riga i (0-based) -> campi dopo la data: durata,tipo,completata,materia */
static int row_field(int i, long *dur, char *type, int *done, char *subj)
{
    char *p = rows();
    while (i-- > 0) { p = strchr(p, '\n'); if (!p) return 0; p++; }
    char *comma = strchr(p, ',');
    if (!comma) return 0;
    char tmp[128];
    snprintf(tmp, sizeof tmp, "%s", comma + 1);
    char *nl = strchr(tmp, '\n');
    if (nl) *nl = '\0';
    subj[0] = '\0';
    return sscanf(tmp, "%ld,%15[^,],%d,%24[^\n]", dur, type, done, subj) >= 3;
}

static void setup(App *a, int minutes)
{
    memset(a, 0, sizeof *a);
    settings_defaults(&a->cfg);
    timer_init(&a->timer, minutes);
    pomo_init(&a->pomo, &a->cfg);
    reset_csv();
}

static void choose(App *a, const char *s) { snprintf(a->cfg.subject, sizeof a->cfg.subject, "%s", s); }

static void start_timer(App *a, const char *subj)
{
    choose(a, subj);
    timer_toggle(&a->timer, T0);
    session_track_starts(a);
}

int main(void)
{
    char dir[512];
    if (!test_mkdtemp(dir, sizeof dir)) { fprintf(stderr, "test_mkdtemp fallita\n"); return 1; }
    test_setenv("XDG_CONFIG_HOME", dir);
    test_setenv("XDG_DATA_HOME", dir);
    char d[600];
    snprintf(d, sizeof d, "%s/ribalta", dir);
    SDL_CreateDirectory(d);
    snprintf(csv, sizeof csv, "%s/ribalta/sessions.csv", dir);

    App a;
    long dur; char type[16], subj[32]; int done;

    /* timer: cambio dopo 70 s, poi fine */
    setup(&a, 90);
    start_timer(&a, "Analisi");
    choose(&a, "Fisica");
    session_subject_changed(&a, T0 + 70 * S);
    CHECK(nrows() == 1);
    CHECK(row_field(0, &dur, type, &done, subj));
    CHECK(dur == 70 && strcmp(type, "timer") == 0 && done == 0 && strcmp(subj, "Analisi") == 0);
    CHECK(strcmp(a.timer_subject, "Fisica") == 0);
    CHECK(a.timer_seg_ns == 70 * S);
    session_subject_changed(&a, T0 + 80 * S); /* stessa materia: niente */
    CHECK(nrows() == 1);
    session_on_timer_finished(&a, T0 + 90 * 60 * S);
    CHECK(nrows() == 2);
    CHECK(row_field(1, &dur, type, &done, subj));
    CHECK(dur == 90 * 60 - 70 && done == 1 && strcmp(subj, "Fisica") == 0);

    /* timer: cambio dopo 30 s -> nessuna riga, materia sostituita, durata intera */
    setup(&a, 90);
    start_timer(&a, "Analisi");
    choose(&a, "Fisica");
    session_subject_changed(&a, T0 + 30 * S);
    CHECK(nrows() == 0);
    CHECK(strcmp(a.timer_subject, "Fisica") == 0 && a.timer_seg_ns == 0);
    session_on_timer_finished(&a, T0 + 90 * 60 * S);
    CHECK(nrows() == 1);
    CHECK(row_field(0, &dur, type, &done, subj));
    CHECK(dur == 90 * 60 && done == 1 && strcmp(subj, "Fisica") == 0);

    /* interruzione dopo un cambio: il parziale sottrae il tratto già registrato */
    setup(&a, 90);
    start_timer(&a, "Analisi");
    choose(&a, "");
    session_subject_changed(&a, T0 + 100 * S);
    session_log_timer_partial(&a, T0 + 250 * S);
    CHECK(nrows() == 2);
    CHECK(row_field(1, &dur, type, &done, subj));
    CHECK(dur == 150 && done == 0 && subj[0] == '\0');

    /* Pomodoro: cambio in STUDIO */
    setup(&a, 90);
    a.cfg.pomodoro_work = 25;
    pomo_init(&a.pomo, &a.cfg);
    choose(&a, "Analisi");
    timer_toggle(&a.pomo.timer, T0);
    session_track_starts(&a);
    choose(&a, "Fisica");
    session_subject_changed(&a, T0 + 600 * S);
    CHECK(nrows() == 1);
    CHECK(row_field(0, &dur, type, &done, subj));
    CHECK(dur == 600 && strcmp(type, "pomodoro") == 0 && done == 0 && strcmp(subj, "Analisi") == 0);
    session_on_pomo_finished(&a, POMO_WORK);
    CHECK(nrows() == 2);
    CHECK(row_field(1, &dur, type, &done, subj));
    CHECK(dur == 25 * 60 - 600 && done == 1 && strcmp(subj, "Fisica") == 0);

    /* Pomodoro in riposo: niente */
    setup(&a, 90);
    pomo_init(&a.pomo, &a.cfg);
    choose(&a, "Analisi");
    pomo_next(&a.pomo, &a.cfg, true, T0); /* -> RIPOSO avviato */
    a.pomo_start = 12345;
    snprintf(a.pomo_subject, sizeof a.pomo_subject, "Analisi");
    choose(&a, "Fisica");
    session_subject_changed(&a, T0 + 300 * S);
    CHECK(nrows() == 0);
    CHECK(strcmp(a.pomo_subject, "Analisi") == 0);

    /* timer e Pomodoro entrambi avviati: due righe */
    setup(&a, 90);
    choose(&a, "Analisi");
    timer_toggle(&a.timer, T0);
    timer_toggle(&a.pomo.timer, T0);
    session_track_starts(&a);
    choose(&a, "Fisica");
    session_subject_changed(&a, T0 + 120 * S);
    CHECK(nrows() == 2);

    /* nessuna sessione avviata: niente */
    setup(&a, 90);
    session_track_starts(&a);
    choose(&a, "Fisica");
    session_subject_changed(&a, T0 + 500 * S);
    CHECK(nrows() == 0);

    /* screensaver: niente */
    setup(&a, 90);
    start_timer(&a, "Analisi");
    a.screensaver = true;
    choose(&a, "Fisica");
    session_subject_changed(&a, T0 + 500 * S);
    CHECK(nrows() == 0);

    /* reset dei tratti quando la sessione non è avviata */
    setup(&a, 90);
    start_timer(&a, "Analisi");
    choose(&a, "Fisica");
    session_subject_changed(&a, T0 + 100 * S);
    timer_reset(&a.timer);
    session_track_starts(&a);
    CHECK(a.timer_seg_ns == 0 && a.timer_start == 0);

    test_rmtree(dir);

    if (fails) { fprintf(stderr, "%d fallimenti\n", fails); return 1; }
    puts("ok");
    return 0;
}
