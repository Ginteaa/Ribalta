#ifndef RIBALTA_CLOCK_H
#define RIBALTA_CLOCK_H
#include <stdbool.h>
#include <stdint.h>
#include "core/settings.h"

typedef struct {
    int  values[3];          /* ore, minuti, [secondi]; ciascuno 0..99 */
    int  count;              /* 2 (HH:MM) o 3 (HH:MM:SS) secondo show_seconds */
    bool hide_leading_zero;  /* true in formato 12h: " 9" invece di "09" sulla tessera ore */
    int  ampm;               /* 0 = nessuno (24h), 1 = AM, 2 = PM */
} ClockFace;

/* Ora locale corrente (clock_gettime REALTIME + localtime_r).
 * 12h: 0 -> 12, 13 -> 1, ecc. */
void clock_get(const Settings *s, ClockFace *out);

/* Millisecondi fino al prossimo cambio visibile (prossimo minuto, o secondo se
 * show_seconds), + 5 ms di margine. Sempre >= 1. */
uint32_t clock_ms_to_next_change(const Settings *s);

#endif
