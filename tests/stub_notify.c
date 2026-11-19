#include "platform/notify.h"

/* Stub per i test: session.c chiama solo queste due funzioni; si contano le chiamate. */
int stub_notify_desktop_calls, stub_notify_sound_calls;

void notify_desktop(const char *title, const char *body)
{
    (void)title;
    (void)body;
    stub_notify_desktop_calls++;
}

void notify_sound(void)
{
    stub_notify_sound_calls++;
}
