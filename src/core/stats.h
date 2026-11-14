#ifndef RIBALTA_STATS_H
#define RIBALTA_STATS_H
#include <stdbool.h>
#include <time.h>

/* Statistiche di studio — docs/04-roadmap.md §3.2.
 * File: paths_data_dir()/sessions.csv. Letto solo su richiesta, nulla tenuto in memoria. */

/* Aggiunge una riga (crea file + intestazione se mancano). Ignora durate < 60 s (ritorna true).
 * type: "timer" o "pomodoro". Ritorna false su errore di I/O. */
bool stats_append(time_t start, long duration_s, const char *type, bool completed);

typedef struct {
    long today_s;
    long week_s[7];          /* [0] = 6 giorni fa ... [6] = oggi */
    int  week_wday[7];       /* giorno della settimana di ciascun elemento (0 = domenica) */
    long month_s;            /* mese di calendario corrente */
    int  streak_days;        /* giorni consecutivi con studio, fino a oggi (o fino a ieri se oggi 0) */
    int  completed_sessions; /* totale righe con completata = 1 */
} StatsSummary;

/* Legge il CSV e calcola il riepilogo rispetto a now (ora locale). Righe malformate ignorate.
 * File assente -> riepilogo tutto a zero e ritorna true. */
bool stats_summary(StatsSummary *out, time_t now);

/* Formatta una durata come "1h 25m", "2h", "45m", "0m". */
void stats_format_duration(long seconds, char *out, size_t out_size);

#endif
