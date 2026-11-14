#ifndef RIBALTA_TIMER_H
#define RIBALTA_TIMER_H
#include <stdbool.h>
#include <stdint.h>
#include <SDL3/SDL.h>
#include "settings.h"

/* Timer di studio — docs/01-specifiche-funzionali.md §1.2.
 * Tutti i tempi sono in ns da SDL_GetTicksNS() (monotono). Nessuna allocazione. */
typedef enum { TIMER_SETUP, TIMER_RUNNING, TIMER_PAUSED } TimerState;

typedef struct {
    TimerState state;
    int        minutes;       /* durata impostata, 1..1439 */
    Uint64     end_ns;        /* RUNNING: istante di fine */
    Uint64     remaining_ns;  /* PAUSED: tempo rimasto al momento della pausa */
} Timer;

void timer_init(Timer *t, int minutes);              /* -> SETUP con durata clampata */
void timer_adjust(Timer *t, int delta_minutes);      /* solo in SETUP; clamp 1..1439 */
void timer_toggle(Timer *t, Uint64 now);             /* SETUP->RUNNING, RUNNING<->PAUSED */
void timer_reset(Timer *t);                          /* -> SETUP, stessa durata */

/* Da chiamare a ogni giro del loop. Ritorna true UNA volta quando il timer arriva a 0:
 * in quel caso il timer torna in SETUP (stessa durata). */
bool timer_update(Timer *t, Uint64 now);

Uint64 timer_remaining_ns(const Timer *t, Uint64 now); /* SETUP: durata intera */
Uint64 timer_elapsed_ns(const Timer *t, Uint64 now);  /* durata - rimanente (pause escluse); 0 in SETUP */
float  timer_progress(const Timer *t, Uint64 now);     /* 0..1 tempo trascorso; 0 in SETUP */

/* Valori da mostrare sulle tessere (arrotondati per eccesso all'unità visibile):
 * SETUP -> {ore, minuti}, count 2.
 * RUNNING/PAUSED: show_seconds -> {h, m, s} count 3; altrimenti >= 1h -> {h, m}, < 1h -> {m, s}. */
void timer_face(const Timer *t, const Settings *s, Uint64 now, int values[3], int *count);

/* ms al prossimo cambio visibile delle tessere (RUNNING); UINT32_MAX se nulla cambia (SETUP/PAUSED). */
uint32_t timer_ms_to_next_change(const Timer *t, const Settings *s, Uint64 now);

/* --- Inserimento da tastiera "stile microonde" (docs: roadmap, inserimento timer) ---
 * Le cifre entrano da destra: 1,3,0 -> "130" -> 01:30. Massimo 4 cifre (HHMM). */
typedef struct {
    char d[4];   /* cifre '0'..'9', le più vecchie a sinistra */
    int  n;      /* 0..4 */
} TimerEntry;

void timer_entry_clear(TimerEntry *e);
bool timer_entry_push(TimerEntry *e, int digit);   /* false se già 4 cifre o digit fuori 0..9 */
bool timer_entry_pop(TimerEntry *e);               /* false se vuoto */
/* Valori grezzi da mostrare: {HH, MM} dalle cifre allineate a destra, es. "190" -> {1, 90}. */
void timer_entry_face(const TimerEntry *e, int values[2]);
/* Minuti totali normalizzati: HH*60 + MM (MM può superare 59: "90" -> 90 min = 1h30),
 * limitati a 1..1439. Ritorna 0 se il buffer è vuoto o vale zero (= nessuna durata valida). */
int  timer_entry_minutes(const TimerEntry *e);

#endif
