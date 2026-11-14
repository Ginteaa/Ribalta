#include "../src/timer.h"
#include <stdio.h>

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static int mins_of(const char *digits) {
    TimerEntry e;
    timer_entry_clear(&e);
    for (const char *p = digits; *p; p++) timer_entry_push(&e, *p - '0');
    return timer_entry_minutes(&e);
}

static void face_of(const char *digits, int *h, int *m) {
    TimerEntry e;
    int v[2];
    timer_entry_clear(&e);
    for (const char *p = digits; *p; p++) timer_entry_push(&e, *p - '0');
    timer_entry_face(&e, v);
    *h = v[0];
    *m = v[1];
}

int main(void) {
    TimerEntry e;
    timer_entry_clear(&e);
    CHECK(e.n == 0);
    CHECK(!timer_entry_pop(&e));
    CHECK(!timer_entry_push(&e, 10));
    CHECK(!timer_entry_push(&e, -1));
    CHECK(timer_entry_push(&e, 1) && timer_entry_push(&e, 2) && timer_entry_push(&e, 3) && timer_entry_push(&e, 4));
    CHECK(!timer_entry_push(&e, 5));
    CHECK(e.n == 4);
    CHECK(timer_entry_pop(&e) && e.n == 3);

    int h, m;
    face_of("", &h, &m);   CHECK(h == 0 && m == 0);
    face_of("1", &h, &m);  CHECK(h == 0 && m == 1);
    face_of("13", &h, &m); CHECK(h == 0 && m == 13);
    face_of("130", &h, &m); CHECK(h == 1 && m == 30);
    face_of("190", &h, &m); CHECK(h == 1 && m == 90);
    face_of("2359", &h, &m); CHECK(h == 23 && m == 59);

    CHECK(mins_of("") == 0);
    CHECK(mins_of("0") == 0);
    CHECK(mins_of("000") == 0);
    CHECK(mins_of("5") == 5);
    CHECK(mins_of("90") == 90);
    CHECK(mins_of("130") == 90);
    CHECK(mins_of("2359") == 1439);
    CHECK(mins_of("9999") == 1439);

    /* basi del timer */
    Timer t;
    Settings s;
    settings_defaults(&s);
    timer_init(&t, 5000);
    CHECK(t.minutes == 1439);
    timer_init(&t, 0);
    CHECK(t.minutes == 1);
    timer_init(&t, 2);
    timer_adjust(&t, 3);
    CHECK(t.minutes == 5);
    const Uint64 sec = 1000000000ULL;
    timer_toggle(&t, 0);
    CHECK(t.state == TIMER_RUNNING);
    CHECK(timer_remaining_ns(&t, 60 * sec) == 4 * 60 * sec);
    timer_toggle(&t, 60 * sec);
    CHECK(t.state == TIMER_PAUSED);
    CHECK(timer_remaining_ns(&t, 999 * sec) == 4 * 60 * sec);
    timer_toggle(&t, 100 * sec);
    CHECK(!timer_update(&t, 200 * sec));
    CHECK(timer_update(&t, 400 * sec));
    CHECK(t.state == TIMER_SETUP);
    int v[3], c;
    timer_init(&t, 90);
    timer_face(&t, &s, 0, v, &c);
    CHECK(c == 2 && v[0] == 1 && v[1] == 30);

    if (fails) return 1;
    puts("ok");
    return 0;
}
