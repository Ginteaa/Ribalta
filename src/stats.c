#include "stats.h"
#include "settings.h"

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
    if (!settings_data_dir(dir, sizeof dir)) return false;
    int r = snprintf(out, n, "%s/sessions.csv", dir);
    return r > 0 && (size_t)r < n;
}

bool stats_append(time_t start, long duration_s, const char *type, bool completed)
{
    if (duration_s < 60) return true;
    char path[1100];
    if (!csv_path(path, sizeof path)) return false;
    struct tm tm;
    if (!localtime_r(&start, &tm)) return false;
    FILE *f = fopen(path, "a");
    if (!f) return false;
    bool ok = true;
    if (fseek(f, 0, SEEK_END) == 0 && ftell(f) == 0)
        ok = fputs("inizio,durata_s,tipo,completata\n", f) >= 0;
    if (ok)
        ok = fprintf(f, "%04d-%02d-%02dT%02d:%02d:%02d,%ld,%s,%d\n", tm.tm_year + 1900, tm.tm_mon + 1,
                     tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, duration_s,
                     type ? type : "timer", completed ? 1 : 0) > 0;
    if (fclose(f) != 0) ok = false;
    return ok;
}

bool stats_summary(StatsSummary *out, time_t now)
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
        if ((*p != '0' && *p != '1') || (p[1] != '\n' && p[1] != '\r' && p[1] != '\0')) continue;
        bool done = *p == '1';

        if (done) out->completed_sessions++;
        if (Y == tn.tm_year + 1900 && M == tn.tm_mon + 1) out->month_s += dur;
        long ago = today - day_number(Y, M, D);
        if (ago < 0 || ago >= HIST_DAYS) continue;
        per_day[ago] += dur;
    }
    fclose(f);

    out->today_s = per_day[0];
    for (int i = 0; i < 7; i++) out->week_s[i] = per_day[6 - i];
    int k = per_day[0] > 0 ? 0 : 1;
    while (k < HIST_DAYS && per_day[k] > 0) { out->streak_days++; k++; }
    return true;
}

void stats_format_duration(long seconds, char *out, size_t out_size)
{
    if (!out || out_size == 0) return;
    if (seconds < 0) seconds = 0;
    long m = seconds / 60, h = m / 60;
    if (h > 0) snprintf(out, out_size, "%ldh %ldm", h, m % 60);
    else snprintf(out, out_size, "%ldm", m);
}
