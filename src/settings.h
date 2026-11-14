#ifndef FLIPCLOCK_SETTINGS_H
#define FLIPCLOCK_SETTINGS_H
#include <stdbool.h>
#include <stddef.h>

/* Vedi docs/01-specifiche-funzionali.md §1.3 */
typedef struct {
    bool show_seconds;          /* default false */
    bool clock_24h;             /* default true  */
    int  timer_default_minutes; /* default 60, range 1..1439 */
    bool timer_sound;           /* default true  */
    bool timer_notify;          /* default true  */
    int  scale;                 /* default 90,  range 50..100 */
    int  brightness;            /* default 100, range 20..100 */
    bool start_fullscreen;      /* default false */
    bool animations;            /* default true  */
} Settings;

void settings_defaults(Settings *s);

/* Legge $XDG_CONFIG_HOME/flipclock/config.ini (fallback ~/.config/...).
 * Applica prima i default; chiavi sconosciute/valori non validi ignorati.
 * Ritorna false se il file non esiste o non è leggibile (s resta coi default). */
bool settings_load(Settings *s);

/* Scrive il file (crea le directory se mancano). Ritorna false su errore. */
bool settings_save(const Settings *s);

/* Cerca un file asset (es. "font.ttf") in ordine:
 * $FLIPCLOCK_DATA, ./assets, <dir dell'eseguibile>/assets,
 * $XDG_DATA_HOME/flipclock (fallback ~/.local/share/flipclock), /usr/share/flipclock.
 * Scrive il percorso trovato in out. Ritorna false se non trovato. */
bool settings_find_asset(const char *name, char *out, size_t out_size);

#endif
