#ifndef RIBALTA_APP_SESSION_H
#define RIBALTA_APP_SESSION_H

#include <SDL3/SDL.h>
#include "app/app.h"

/* Timer interrotto (R / uscita): da chiamare PRIMA di resettarlo. */
void session_log_timer_partial(const App *a, Uint64 now);
/* STUDIO avviato e interrotto (N / R / uscita): da chiamare PRIMA di cambiare fase. */
void session_log_pomo_partial(const App *a, Uint64 now);
void session_track_starts(App *a);
void session_on_timer_finished(App *a, Uint64 now);
void session_on_pomo_finished(App *a, PomoPhase finished);
void session_blink_tick(App *a, Uint64 now);
void session_set_inhibit(App *a);

#endif
