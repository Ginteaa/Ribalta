#ifndef RIBALTA_STATS_H
#define RIBALTA_STATS_H
#include <stdbool.h>
#include <time.h>

/* Statistiche di studio — docs/04-roadmap.md §3.2.
 * File: paths_data_dir()/sessions.csv. Letto solo su richiesta, nulla tenuto in memoria. */

/* Formato CSV: inizio,durata_s,tipo,completata,materia
 * Le righe vecchie a 4 colonne (senza materia) restano valide e valgono come "senza materia"; un
 * file esistente non viene mai riscritto (l'intestazione a 5 colonne si scrive solo su file nuovo). */

#define STATS_SUBJECT_LEN 25   /* = SUBJECT_LEN di settings.h */

/* Aggiunge una riga (crea file + intestazione se mancano). Ignora durate < 60 s (ritorna true).
 * type: "timer" o "pomodoro". subject: materia, NULL o "" = nessuna; i caratteri , " ; e di
 * controllo vengono sostituiti da spazi, il nome è troncato a STATS_SUBJECT_LEN-1 byte (senza
 * spezzare un carattere UTF-8). Ritorna false su errore di I/O. */
bool stats_append(time_t start, long duration_s, const char *type, bool completed, const char *subject);

typedef struct {
    long today_s;
    long week_s[7];          /* [0] = 6 giorni fa ... [6] = oggi */
    int  week_wday[7];       /* giorno della settimana di ciascun elemento (0 = domenica) */
    long month_s;            /* mese di calendario corrente */
    int  streak_days;        /* giorni consecutivi con studio, fino a oggi (o fino a ieri se oggi 0) */
    int  completed_sessions; /* totale righe con completata = 1 */
} StatsSummary;

/* Legge il CSV e calcola il riepilogo rispetto a now (ora locale), contando solo le righe del
 * filtro: NULL = tutte (totale di tutto lo studio), "" = solo senza materia, altrimenti solo la
 * materia con quel nome esatto. Righe malformate ignorate. File assente -> tutto a zero e true. */
bool stats_summary(StatsSummary *out, time_t now, const char *filter);

/* Ore per materia negli ultimi 7 giorni (oggi compreso, come week_s). */
typedef struct {
    char name[STATS_SUBJECT_LEN];  /* "" = senza materia */
    long week_s;
} StatsSubjectTotal;

/* Riempie out con le materie (anche "", e anche materie non più in elenco) che hanno studio negli
 * ultimi 7 giorni, ordinate per week_s decrescente (a parità: nome). Ritorna quante ne ha scritte
 * (al massimo max); 0 se il file manca. */
int stats_subject_totals(StatsSubjectTotal *out, int max, time_t now);

/* Obiettivi (docs/01-functional-spec.md §1.4). Contano TUTTE le righe (con e senza materia, nessun filtro);
 * ogni riga vale per il giorno della sua data di inizio (ora locale), come in stats_summary. */
typedef struct {
    long today_s;          /* studio di oggi */
    long week_s;           /* studio della settimana di calendario corrente: da lunedì a oggi compreso */
    int  goal_streak_days; /* giorni consecutivi con studio >= daily_goal_s, fino a oggi compreso se oggi
                            * è già raggiunto, altrimenti fino a ieri; 0 se daily_goal_s <= 0 */
} StatsGoals;

/* Legge il CSV e calcola gli obiettivi rispetto a now. Si chiama solo all'apertura del pannello
 * statistiche e dopo una registrazione (mai a ogni fotogramma). File assente -> tutto a zero e true;
 * false solo su errore di lettura. */
bool stats_goals(StatsGoals *out, time_t now, long daily_goal_s);

/* Formatta una durata come "1h 25m", "2h", "45m", "0m". */
void stats_format_duration(long seconds, char *out, size_t out_size);

#endif
