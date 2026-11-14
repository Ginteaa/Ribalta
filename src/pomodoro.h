#ifndef FLIPCLOCK_POMODORO_H
#define FLIPCLOCK_POMODORO_H
#include <stdbool.h>
#include <stdint.h>
#include <SDL3/SDL.h>
#include "settings.h"
#include "timer.h"

/* Pomodoro — docs/04-roadmap.md §3.1. Usa un Timer per la fase corrente. Nessuna allocazione. */
typedef enum { POMO_WORK, POMO_BREAK, POMO_LONG_BREAK } PomoPhase;

typedef struct {
    PomoPhase phase;
    int       cycle;     /* 1..pomodoro_cycles: sessione di studio corrente (o appena finita, nei riposi) */
    Timer     timer;     /* timer della fase corrente */
} Pomodoro;

/* -> STUDIO 1/N, timer in SETUP con pomodoro_work minuti. */
void pomo_init(Pomodoro *p, const Settings *s);

/* Passa alla fase successiva (STUDIO -> RIPOSO o RIPOSO LUNGO se cycle == cycles;
 * RIPOSO -> STUDIO cycle+1; RIPOSO LUNGO -> STUDIO 1). Il timer della nuova fase è in SETUP;
 * se start == true lo avvia subito a now. */
void pomo_next(Pomodoro *p, const Settings *s, bool start, Uint64 now);

/* Da chiamare a ogni giro del loop. Se la fase corrente è finita ritorna true e scrive in
 * *finished la fase appena conclusa; poi passa alla successiva (avviata se pomodoro_auto_continue). */
bool pomo_update(Pomodoro *p, const Settings *s, Uint64 now, PomoPhase *finished);

/* Etichetta: "STUDIO 2/4", "RIPOSO", "RIPOSO LUNGO"; + " · IN PAUSA" se in pausa. */
void pomo_label(const Pomodoro *p, const Settings *s, char *out, size_t out_size);

/* Se in SETUP, riallinea la durata della fase corrente alle impostazioni (dopo una modifica). */
void pomo_apply_settings(Pomodoro *p, const Settings *s);

#endif
