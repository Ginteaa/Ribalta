#include "core/stats.h"
#include "platform/paths.h"
#include "core/settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { HIST_DAYS = 366 };

/* Numero di giorno progressivo di una data civile (DST-safe: aritmetica pura). */
static long day_number(int year, int mon /*1-12*/, int mday)
{
    long y = year - (mon <= 2);
    long era = (y >= 0 ? y : y - 399) / 400;
    long yoe = y - era * 400;
    long doy = (153L * (mon + (mon > 2 ? -3 : 9)) + 2) / 5 + mday - 1;
    long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe;
}

static bool csv_path(char *out, size_t n)
{
    char dir[1024];
    if (!paths_data_dir(dir, sizeof dir)) return false;
    int r = snprintf(out, n, "%s/sessions.csv", dir);
    return r > 0 && (size_t)r < n;
}

/* Copia subject in out sostituendo , " ; e i caratteri di controllo con spazi e troncando a
 * STATS_SUBJECT_LEN-1 byte senza spezzare una sequenza UTF-8. */
static void sanitize_subject(char *out, const char *subject)
{
    size_t n = 0;
    for (const unsigned char *p = (const unsigned char *)(subject ? subject : ""); *p; p++) {
        if (n >= STATS_SUBJECT_LEN - 1) break;
        out[n++] = (*p == ',' || *p == '"' || *p == ';' || *p < 0x20 || *p == 0x7f) ? ' ' : (char)*p;
    }
    if (*(subject ? subject : "") && n == STATS_SUBJECT_LEN - 1 && subject[n]) {
        /* troncato: se il taglio cade dentro una sequenza, arretra fino al byte di testa */
        while (n > 0 && ((unsigned char)subject[n] & 0xC0) == 0x80) n--;
    }
    out[n] = '\0';
}

bool stats_append(time_t start, long duration_s, const char *type, bool completed, const char *subject)
{
    if (duration_s < 60) return true;
    char path[1100];
    if (!csv_path(path, sizeof path)) return false;
    struct tm tm;
    if (!localtime_r(&start, &tm)) return false;
    char subj[STATS_SUBJECT_LEN];
    sanitize_subject(subj, subject);
    FILE *f = fopen(path, "a");
    if (!f) return false;
    bool ok = true;
    if (fseek(f, 0, SEEK_END) == 0 && ftell(f) == 0)
        ok = fputs("inizio,durata_s,tipo,completata,materia\n", f) >= 0;
    if (ok)
        ok = fprintf(f, "%04d-%02d-%02dT%02d:%02d:%02d,%ld,%s,%d,%s\n", tm.tm_year + 1900, tm.tm_mon + 1,
                     tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, duration_s,
                     type ? type : "timer", completed ? 1 : 0, subj) > 0;
    if (fclose(f) != 0) ok = false;
    return ok;
}

typedef struct {
    int  year, mon, mday;
    long dur;
    bool done;
    char subject[STATS_SUBJECT_LEN];
} Row;

/* Legge la prossima riga valida (4 o 5 colonne); ignora righe malformate o troppo lunghe.
 * Ritorna false a fine file. */
static bool read_row(FILE *f, Row *r)
{
    char line[256];
    while (fgets(line, sizeof line, f)) {
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] != '\n' && !feof(f)) {   /* riga troppo lunga: scarta il resto */
            int c;
            while ((c = fgetc(f)) != EOF && c != '\n') {}
            continue;
        }
        int Y, M, D, h, mi, s, n = 0;
        if (sscanf(line, "%4d-%2d-%2dT%2d:%2d:%2d%n", &Y, &M, &D, &h, &mi, &s, &n) != 6) continue;
        if (M < 1 || M > 12 || D < 1 || D > 31 || h < 0 || h > 23 || mi < 0 || mi > 59 || s < 0 || s > 60)
            continue;
        if (line[n] != ',') continue;
        char *p = line + n + 1, *end;
        long dur = strtol(p, &end, 10);
        if (end == p || *end != ',' || dur <= 0) continue;
        p = end + 1;
        char *comma = strchr(p, ',');
        if (!comma || comma == p) continue;
        p = comma + 1;
        if (*p != '0' && *p != '1') continue;
        char *q = p + 1;
        r->subject[0] = '\0';
        if (*q == ',') {
            q++;
            size_t k = 0;
            while (*q && *q != '\r' && *q != '\n' && k < STATS_SUBJECT_LEN - 1) r->subject[k++] = *q++;
            r->subject[k] = '\0';
        } else if (*q != '\n' && *q != '\r' && *q != '\0') {
            continue;
        }
        r->year = Y; r->mon = M; r->mday = D;
        r->dur = dur;
        r->done = *p == '1';
        return true;
    }
    return false;
}

int stats_subject_totals(StatsSubjectTotal *out, int max, time_t now)
{
    if (!out || max <= 0) return 0;
    struct tm tn;
    if (!localtime_r(&now, &tn)) return 0;
    const long today = day_number(tn.tm_year + 1900, tn.tm_mon + 1, tn.tm_mday);
    char path[1100];
    if (!csv_path(path, sizeof path)) return 0;
    FILE *f = fopen(path, "r");
    if (!f) return 0;

    enum { MAX_NAMES = 32 };
    StatsSubjectTotal tab[MAX_NAMES];
    int cnt = 0;
    Row r;
    while (read_row(f, &r)) {
        long ago = today - day_number(r.year, r.mon, r.mday);
        if (ago < 0 || ago > 6) continue;
        int i = 0;
        while (i < cnt && strcmp(tab[i].name, r.subject) != 0) i++;
        if (i == cnt) {
            if (cnt == MAX_NAMES) continue;   /* troppe materie diverse: le extra sono ignorate */
            memcpy(tab[cnt].name, r.subject, sizeof tab[cnt].name);
            tab[cnt].week_s = 0;
            cnt++;
        }
        tab[i].week_s += r.dur;
    }
    fclose(f);

    /* ordinamento per inserimento: week_s decrescente, a parità nome */
    for (int i = 1; i < cnt; i++) {
        StatsSubjectTotal t = tab[i];
        int j = i - 1;
        while (j >= 0 && (tab[j].week_s < t.week_s ||
                          (tab[j].week_s == t.week_s && strcmp(tab[j].name, t.name) > 0))) {
            tab[j + 1] = tab[j];
            j--;
        }
        tab[j + 1] = t;
    }
    int n = cnt < max ? cnt : max;
    memcpy(out, tab, (size_t)n * sizeof tab[0]);
    return n;
}

bool stats_summary(StatsSummary *out, time_t now, const char *filter)
{
    if (!out) return false;
    memset(out, 0, sizeof *out);

    struct tm tn;
    if (!localtime_r(&now, &tn)) return false;
    const long today = day_number(tn.tm_year + 1900, tn.tm_mon + 1, tn.tm_mday);
    for (int i = 0; i < 7; i++) out->week_wday[i] = ((tn.tm_wday - (6 - i)) % 7 + 7) % 7;

    char path[1100];
    if (!csv_path(path, sizeof path)) return true;   /* niente directory dati: riepilogo vuoto */
    FILE *f = fopen(path, "r");
    if (!f) return true;

    static long per_day[HIST_DAYS];   /* [k] = k giorni fa */
    memset(per_day, 0, sizeof per_day);

    Row r;
    while (read_row(f, &r)) {
        if (filter && strcmp(filter, r.subject) != 0) continue;
        if (r.done) out->completed_sessions++;
        if (r.year == tn.tm_year + 1900 && r.mon == tn.tm_mon + 1) out->month_s += r.dur;
        long ago = today - day_number(r.year, r.mon, r.mday);
        if (ago < 0 || ago >= HIST_DAYS) continue;
        per_day[ago] += r.dur;
    }
    fclose(f);

    out->today_s = per_day[0];
    for (int i = 0; i < 7; i++) out->week_s[i] = per_day[6 - i];
    int k = per_day[0] > 0 ? 0 : 1;
    while (k < HIST_DAYS && per_day[k] > 0) { out->streak_days++; k++; }
    return true;
}

bool stats_goals(StatsGoals *out, time_t now, long daily_goal_s)
{
    if (!out) return false;
    memset(out, 0, sizeof *out);

    struct tm tn;
    if (!localtime_r(&now, &tn)) return false;
    const long today = day_number(tn.tm_year + 1900, tn.tm_mon + 1, tn.tm_mday);
    const long since_monday = (tn.tm_wday + 6) % 7;   /* 0 = lunedì */

    char path[1100];
    if (!csv_path(path, sizeof path)) return true;
    FILE *f = fopen(path, "r");
    if (!f) return true;

    /* Totali per giorno: [k] = k giorni fa. La serie di obiettivi è limitata agli ultimi GOAL_DAYS
     * giorni (oltre, la serie si ferma lì): nessuna allocazione. */
    enum { GOAL_DAYS = 400 };
    long per_day[GOAL_DAYS];
    memset(per_day, 0, sizeof per_day);

    Row r;
    while (read_row(f, &r)) {
        long ago = today - day_number(r.year, r.mon, r.mday);
        if (ago < 0) continue;
        if (ago == 0) out->today_s += r.dur;
        if (ago <= since_monday) out->week_s += r.dur;
        if (ago < GOAL_DAYS) per_day[ago] += r.dur;
    }
    fclose(f);

    if (daily_goal_s > 0) {
        int k = per_day[0] >= daily_goal_s ? 0 : 1;
        while (k < GOAL_DAYS && per_day[k] >= daily_goal_s) { out->goal_streak_days++; k++; }
    }
    return true;
}

void stats_format_duration(long seconds, char *out, size_t out_size)
{
    if (!out || out_size == 0) return;
    if (seconds < 0) seconds = 0;
    long m = seconds / 60, h = m / 60;
    if (h > 0 && m % 60 == 0) snprintf(out, out_size, "%ldh", h);
    else if (h > 0) snprintf(out, out_size, "%ldh %ldm", h, m % 60);
    else snprintf(out, out_size, "%ldm", m);
}
