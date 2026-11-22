#ifndef RIBALTA_COMPAT_H
#define RIBALTA_COMPAT_H
#include <stdbool.h>
#include <time.h>

/* Piccole differenze tra POSIX e Windows (MinGW UCRT), solo header.
 *
 * Altre sostituzioni portabili usate nel progetto, senza wrapper:
 *   strcasecmp            -> SDL_strcasecmp
 *   strtok_r              -> SDL_strtok_r
 *   clock_gettime(REALTIME) -> timespec_get(&ts, TIME_UTC)   (C11)
 *   access(p, R_OK)       -> paths_readable(p)               (platform/paths.h) */

/* Ora locale di t in *out (thread-safe). Ritorna false se la conversione fallisce. */
static inline bool compat_localtime(time_t t, struct tm *out)
{
#ifdef _WIN32
    return localtime_s(out, &t) == 0;
#else
    return localtime_r(&t, out) != NULL;
#endif
}

#endif
