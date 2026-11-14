#include "core/clock.h"
#include <time.h>

void clock_get(const Settings *s, ClockFace *out)
{
    struct timespec ts;
    struct tm tm;
    clock_gettime(CLOCK_REALTIME, &ts);
    time_t t = ts.tv_sec;
    localtime_r(&t, &tm);

    int h = tm.tm_hour;
    out->ampm = 0;
    out->hide_leading_zero = false;
    if (!s->clock_24h) {
        out->ampm = h >= 12 ? 2 : 1;
        h %= 12;
        if (h == 0)
            h = 12;
        out->hide_leading_zero = true;
    }
    out->values[0] = h;
    out->values[1] = tm.tm_min;
    out->values[2] = tm.tm_sec;
    out->count = s->show_seconds ? 3 : 2;
}

uint32_t clock_ms_to_next_change(const Settings *s)
{
    struct timespec ts;
    struct tm tm;
    clock_gettime(CLOCK_REALTIME, &ts);
    time_t t = ts.tv_sec;
    localtime_r(&t, &tm);

    long ms_in_sec = ts.tv_nsec / 1000000L;
    long ms = 1000 - ms_in_sec;
    if (!s->show_seconds)
        ms += (long)(59 - tm.tm_sec) * 1000;
    ms += 5;
    return ms < 1 ? 1 : (uint32_t)ms;
}
