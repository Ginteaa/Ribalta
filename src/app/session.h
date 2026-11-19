#ifndef RIBALTA_APP_SESSION_H
#define RIBALTA_APP_SESSION_H

#include <SDL3/SDL.h>
#include "app/app.h"

/* Timer interrotto (R / uscita): da chiamare PRIMA di resettarlo. */
void session_log_timer_partial(const App *a, Uint64 now);
/* STUDIO avviato e interrotto (N / R / uscita): da chiamare PRIMA di cambiare fase. */
void session_log_pomo_partial(const App *a, Uint64 now);
void session_track_starts(App *a);

/* Da chiamare quando la materia attiva (cfg.subject) può essere cambiata, es. alla chiusura del
 * menu materie. Per ogni sessione avviata (timer non in SETUP con timer_start != 0; STUDIO non in
 * SETUP con pomo_start != 0) la cui materia differisce da cfg.subject:
 *  - tratto = timer_elapsed_ns(now) - *_seg_ns;
 *  - se il tratto è >= 60 s lo registra subito (stats_append, completata = false, materia vecchia,
 *    inizio = *_start), poi *_seg_ns = tempo trascorso e *_start = time(NULL);
 *  - se è < 60 s non registra nulla (il tempo passa alla nuova materia, *_start e *_seg_ns invariati);
 *  - in entrambi i casi la materia della sessione diventa cfg.subject.
 * Nei riposi Pomodoro e senza sessioni avviate non fa nulla. Niente in modalità screensaver. */
void session_subject_changed(App *a, Uint64 now);
void session_on_timer_finished(App *a, Uint64 now);
void session_on_pomo_finished(App *a, PomoPhase finished);
void session_blink_tick(App *a, Uint64 now);
void session_set_inhibit(App *a);

#endif
