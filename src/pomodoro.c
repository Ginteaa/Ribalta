#include "pomodoro.h"
#include <stdio.h>

static int phase_minutes(PomoPhase ph, const Settings *s)
{
    switch (ph) {
    case POMO_WORK:  return s->pomodoro_work;
    case POMO_BREAK: return s->pomodoro_break;
    default:         return s->pomodoro_long_break;
    }
}

void pomo_init(Pomodoro *p, const Settings *s)
{
    p->phase = POMO_WORK;
    p->cycle = 1;
    timer_init(&p->timer, s->pomodoro_work);
}

void pomo_next(Pomodoro *p, const Settings *s, bool start, Uint64 now)
{
    switch (p->phase) {
    case POMO_WORK:
        p->phase = (p->cycle >= s->pomodoro_cycles) ? POMO_LONG_BREAK : POMO_BREAK;
        break;
    case POMO_BREAK:
        p->phase = POMO_WORK;
        p->cycle++;
        break;
    case POMO_LONG_BREAK:
        p->phase = POMO_WORK;
        p->cycle = 1;
        break;
    }
    timer_init(&p->timer, phase_minutes(p->phase, s));
    if (start)
        timer_toggle(&p->timer, now);
}

bool pomo_update(Pomodoro *p, const Settings *s, Uint64 now, PomoPhase *finished)
{
    if (!timer_update(&p->timer, now))
        return false;
    if (finished)
        *finished = p->phase;
    pomo_next(p, s, s->pomodoro_auto_continue, now);
    return true;
}

void pomo_label(const Pomodoro *p, const Settings *s, char *out, size_t out_size)
{
    char base[48];
    switch (p->phase) {
    case POMO_WORK:
        snprintf(base, sizeof base, "STUDIO %d/%d", p->cycle, s->pomodoro_cycles);
        break;
    case POMO_BREAK:
        snprintf(base, sizeof base, "RIPOSO");
        break;
    default:
        snprintf(base, sizeof base, "RIPOSO LUNGO");
        break;
    }
    snprintf(out, out_size, "%s%s", base, p->timer.state == TIMER_PAUSED ? " \xC2\xB7 IN PAUSA" : "");
}

void pomo_apply_settings(Pomodoro *p, const Settings *s)
{
    if (p->cycle > s->pomodoro_cycles)
        p->cycle = s->pomodoro_cycles;
    if (p->cycle < 1)
        p->cycle = 1;
    if (p->timer.state == TIMER_SETUP)
        timer_init(&p->timer, phase_minutes(p->phase, s));
}
