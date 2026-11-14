#ifndef RIBALTA_SETTINGS_H
#define RIBALTA_SETTINGS_H
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
    /* Fase 3 — Pomodoro (docs/04-roadmap.md §3.1) */
    int  pomodoro_work;          /* default 25, range 1..180 */
    int  pomodoro_break;         /* default 5,  range 1..60  */
    int  pomodoro_long_break;    /* default 15, range 1..120 */
    int  pomodoro_cycles;        /* default 4,  range 1..12  */
    bool pomodoro_auto_continue; /* default true */
} Settings;

void settings_defaults(Settings *s);

/* Legge $XDG_CONFIG_HOME/ribalta/config.ini (fallback ~/.config/...).
 * Applica prima i default; chiavi sconosciute/valori non validi ignorati.
 * Ritorna false se il file non esiste o non è leggibile (s resta coi default). */
bool settings_load(Settings *s);

/* Scrive il file (crea le directory se mancano). Ritorna false su errore. */
bool settings_save(const Settings *s);

/* Cerca un file asset (es. "font.ttf") in ordine:
 * $RIBALTA_DATA, ./assets, <dir dell'eseguibile>/assets,
 * $XDG_DATA_HOME/ribalta (fallback ~/.local/share/ribalta), /usr/share/ribalta.
 * Scrive il percorso trovato in out. Ritorna false se non trovato. */
/* Directory dati: $XDG_DATA_HOME/ribalta (fallback ~/.local/share/ribalta), creata se manca.
 * Ritorna false su errore. */
bool settings_data_dir(char *out, size_t out_size);

bool settings_find_asset(const char *name, char *out, size_t out_size);

#endif
