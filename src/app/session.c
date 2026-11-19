#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <SDL3/SDL.h>
#include "app/session.h"
#include "core/stats.h"
#include "platform/notify.h"

/* --- Statistiche (roadmap §3.2): nulla in modalità screensaver --- */

static void log_session(const App *a, time_t start, Uint64 elapsed_ns, const char *type, bool completed,
                        const char *subject)
{
    if (a->screensaver)
        return;
    long secs = (long)(elapsed_ns / SDL_NS_PER_SECOND);
    stats_append(start ? start : time(NULL) - secs, secs, type, completed, subject);
}

/* Tempo del tratto corrente: totale meno quanto già registrato (senza underflow). */
static Uint64 minus_seg(Uint64 total_ns, Uint64 seg_ns)
{
    return total_ns > seg_ns ? total_ns - seg_ns : 0;
}

/* Timer interrotto (R / uscita): da chiamare PRIMA di resettarlo. */
void session_log_timer_partial(const App *a, Uint64 now)
{
    if (a->timer.state != TIMER_SETUP)
        log_session(a, a->timer_start, minus_seg(timer_elapsed_ns(&a->timer, now), a->timer_seg_ns), "timer", false, a->timer_subject);
}

/* STUDIO avviato e interrotto (N / R / uscita): da chiamare PRIMA di cambiare fase. */
void session_log_pomo_partial(const App *a, Uint64 now)
{
    if (a->pomo.phase == POMO_WORK && a->pomo.timer.state != TIMER_SETUP)
        log_session(a, a->pomo_start, minus_seg(timer_elapsed_ns(&a->pomo.timer, now), a->pomo_seg_ns), "pomodoro", false,
                    a->pomo_subject);
}

/* Inizio sessione: istante di prima transizione SETUP -> RUNNING (rilevata a ogni giro).
 * La materia attiva in quel momento resta quella della sessione anche se poi si cambia. */
void session_track_starts(App *a)
{
    if (a->timer.state == TIMER_SETUP) {
        a->timer_start = 0;
        a->timer_seg_ns = 0;
    } else if (!a->timer_start) {
        a->timer_start = time(NULL);
        snprintf(a->timer_subject, sizeof a->timer_subject, "%s", a->cfg.subject);
    }
    if (a->pomo.phase != POMO_WORK || a->pomo.timer.state == TIMER_SETUP) {
        a->pomo_start = 0;
        a->pomo_seg_ns = 0;
    } else if (!a->pomo_start) {
        a->pomo_start = time(NULL);
        snprintf(a->pomo_subject, sizeof a->pomo_subject, "%s", a->cfg.subject);
    }
}

/* Cambio materia a sessione avviata: chiude il tratto corrente (se >= 60 s) e ne apre uno nuovo. */
static void split_segment(const App *a, time_t *start, Uint64 *seg_ns, char *subject, Uint64 elapsed_ns,
                          const char *type)
{
    if (strcmp(subject, a->cfg.subject) == 0)
        return;
    Uint64 part = minus_seg(elapsed_ns, *seg_ns);
    if (part >= 60 * SDL_NS_PER_SECOND) {
        log_session(a, *start, part, type, false, subject);
        *seg_ns = elapsed_ns;
        *start = time(NULL);
    }
    snprintf(subject, SUBJECT_LEN, "%s", a->cfg.subject);
}

void session_subject_changed(App *a, Uint64 now)
{
    if (a->screensaver)
        return;
    if (a->timer.state != TIMER_SETUP && a->timer_start)
        split_segment(a, &a->timer_start, &a->timer_seg_ns, a->timer_subject, timer_elapsed_ns(&a->timer, now),
                      "timer");
    if (a->pomo.phase == POMO_WORK && a->pomo.timer.state != TIMER_SETUP && a->pomo_start)
        split_segment(a, &a->pomo_start, &a->pomo_seg_ns, a->pomo_subject,
                      timer_elapsed_ns(&a->pomo.timer, now), "pomodoro");
}

/* Fine timer: notifiche, vista Timer a 00:00 e lampeggio. */
void session_on_timer_finished(App *a, Uint64 now)
{
    log_session(a, a->timer_start, minus_seg((Uint64)a->timer.minutes * 60 * SDL_NS_PER_SECOND, a->timer_seg_ns), "timer",
                true, a->timer_subject);
    if (a->cfg.timer_notify) {
        int m = a->timer.minutes;
        char body[96];
        snprintf(body, sizeof body, "Sessione di studio completata (%dh %02dmin)", m / 60, m % 60);
        notify_desktop("Ribalta", body);
    }
    if (a->cfg.timer_sound)
        notify_sound();
    a->view = VIEW_TIMER;
    a->blink_active = true;
    a->blink_start = now;
}

/* Fine fase Pomodoro: log dello STUDIO, notifica e suono; la vista non cambia. */
void session_on_pomo_finished(App *a, PomoPhase finished)
{
    const char *msg = "Riposo finito \u2014 si torna a studiare";
    char body[96];
    if (finished == POMO_WORK) {
        log_session(a, a->pomo_start, minus_seg((Uint64)a->cfg.pomodoro_work * 60 * SDL_NS_PER_SECOND, a->pomo_seg_ns),
                    "pomodoro", true, a->pomo_subject);
        if (a->pomo.phase == POMO_LONG_BREAK)
            snprintf(body, sizeof body, "Fine studio \u2014 riposo lungo di %d min", a->cfg.pomodoro_long_break);
        else
            snprintf(body, sizeof body, "Fine studio \u2014 riposo di %d min", a->cfg.pomodoro_break);
        msg = body;
    }
    if (a->cfg.timer_notify)
        notify_desktop("Ribalta", msg);
    if (a->cfg.timer_sound)
        notify_sound();
}

void session_blink_tick(App *a, Uint64 now)
{
    if (a->blink_active && now - a->blink_start >= BLINK_PHASES * BLINK_PHASE_NS) {
        a->blink_active = false;
        a->view = VIEW_CLOCK;
    }
}

/* Blocca lo spegnimento schermo finché timer o Pomodoro sono IN CORSO (solo al cambio di stato). */
void session_set_inhibit(App *a)
{
    bool want = a->timer.state == TIMER_RUNNING || a->pomo.timer.state == TIMER_RUNNING;
    if (want == a->inhibit)
        return;
    a->inhibit = want;
    if (want)
        SDL_DisableScreenSaver();
    else
        SDL_EnableScreenSaver();
}
