#include "timer.h"

#define NS_SEC 1000000000ULL
#define NS_MIN (60ULL * NS_SEC)
#define NS_HOUR (3600ULL * NS_SEC)
#define MAX_MINUTES 1439

static int clamp_minutes(int m) {
    return m < 1 ? 1 : (m > MAX_MINUTES ? MAX_MINUTES : m);
}

static Uint64 duration_ns(const Timer *t) {
    return (Uint64)t->minutes * NS_MIN;
}

static Uint64 ceil_div(Uint64 a, Uint64 b) {
    return (a + b - 1) / b;
}

void timer_init(Timer *t, int minutes) {
    t->state = TIMER_SETUP;
    t->minutes = clamp_minutes(minutes);
    t->end_ns = 0;
    t->remaining_ns = 0;
}

void timer_adjust(Timer *t, int delta_minutes) {
    if (t->state != TIMER_SETUP) return;
    long m = (long)t->minutes + delta_minutes;
    t->minutes = clamp_minutes(m < -1 ? -1 : (m > 100000 ? 100000 : (int)m));
}

void timer_reset(Timer *t) {
    timer_init(t, t->minutes);
}

Uint64 timer_remaining_ns(const Timer *t, Uint64 now) {
    switch (t->state) {
    case TIMER_RUNNING: return t->end_ns > now ? t->end_ns - now : 0;
    case TIMER_PAUSED:  return t->remaining_ns;
    default:            return duration_ns(t);
    }
}

void timer_toggle(Timer *t, Uint64 now) {
    switch (t->state) {
    case TIMER_SETUP:
        t->end_ns = now + duration_ns(t);
        t->state = TIMER_RUNNING;
        break;
    case TIMER_RUNNING:
        t->remaining_ns = timer_remaining_ns(t, now);
        t->state = TIMER_PAUSED;
        break;
    case TIMER_PAUSED:
        t->end_ns = now + t->remaining_ns;
        t->state = TIMER_RUNNING;
        break;
    }
}

bool timer_update(Timer *t, Uint64 now) {
    if (t->state == TIMER_RUNNING && now >= t->end_ns) {
        timer_reset(t);
        return true;
    }
    return false;
}

Uint64 timer_elapsed_ns(const Timer *t, Uint64 now) {
    if (t->state == TIMER_SETUP) return 0;
    Uint64 total = duration_ns(t);
    Uint64 rem = timer_remaining_ns(t, now);
    return rem >= total ? 0 : total - rem;
}

float timer_progress(const Timer *t, Uint64 now) {
    if (t->state == TIMER_SETUP) return 0.0f;
    Uint64 total = duration_ns(t);
    Uint64 rem = timer_remaining_ns(t, now);
    if (rem >= total) return 0.0f;
    return (float)((double)(total - rem) / (double)total);
}

void timer_face(const Timer *t, const Settings *s, Uint64 now, int values[3], int *count) {
    if (t->state == TIMER_SETUP) {
        values[0] = t->minutes / 60;
        values[1] = t->minutes % 60;
        *count = 2;
        return;
    }
    Uint64 rem = timer_remaining_ns(t, now);
    if (s->show_seconds) {
        Uint64 sec = ceil_div(rem, NS_SEC);
        values[0] = (int)(sec / 3600);
        values[1] = (int)(sec / 60 % 60);
        values[2] = (int)(sec % 60);
        *count = 3;
    } else if (rem >= NS_HOUR) {
        Uint64 min = ceil_div(rem, NS_MIN);
        values[0] = (int)(min / 60);
        values[1] = (int)(min % 60);
        *count = 2;
    } else {
        Uint64 sec = ceil_div(rem, NS_SEC);
        if (sec > 3599) sec = 3599; /* rem < 1h: evita "60:00" */
        values[0] = (int)(sec / 60);
        values[1] = (int)(sec % 60);
        *count = 2;
    }
}

uint32_t timer_ms_to_next_change(const Timer *t, const Settings *s, Uint64 now) {
    if (t->state != TIMER_RUNNING) return UINT32_MAX;
    Uint64 rem = timer_remaining_ns(t, now);
    if (rem == 0) return 1;
    Uint64 unit = (s->show_seconds || rem < NS_HOUR) ? NS_SEC : NS_MIN;
    /* il valore mostrato e' ceil(rem/unit): cambia quando rem scende a (k-1)*unit */
    Uint64 k = ceil_div(rem, unit);
    Uint64 wait_ns = rem - (k - 1) * unit;
    Uint64 ms = wait_ns / 1000000ULL + 2;
    if (ms > UINT32_MAX - 1) ms = UINT32_MAX - 1;
    return ms < 1 ? 1 : (uint32_t)ms;
}
