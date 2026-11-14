#ifndef RIBALTA_SETTINGS_H
#define RIBALTA_SETTINGS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RECENT_COLORS_MAX 5

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
    /* Colori: 0xRRGGBB. Nel file: digit_color=#rrggbb, recent_colors=#rrggbb,#rrggbb,... */
    uint32_t digit_color;                        /* default 0xb7b7b7 */
    uint32_t recent_colors[RECENT_COLORS_MAX];   /* il più recente per primo */
    int      recent_count;                       /* 0..RECENT_COLORS_MAX */
    /* Font delle cifre. Entrambi vuoti = font incluso (default). Nel file: font_family=DejaVu Sans,
     * font_file=/usr/share/fonts/... (valore fino a fine riga, spazi interni conservati; un valore
     * troppo lungo viene ignorato). All'avvio si apre font_file senza fontconfig. */
    char     font_family[128];
    char     font_file[512];
} Settings;

void settings_defaults(Settings *s);

/* Legge $XDG_CONFIG_HOME/ribalta/config.ini (fallback ~/.config/...).
 * Applica prima i default; chiavi sconosciute/valori non validi ignorati.
 * Ritorna false se il file non esiste o non è leggibile (s resta coi default). */
bool settings_load(Settings *s);

/* Scrive il file (crea le directory se mancano). Ritorna false su errore. */
bool settings_save(const Settings *s);

/* Mette rgb in testa ai colori recenti (rimuove un eventuale duplicato, tronca a RECENT_COLORS_MAX). */
void settings_push_recent(Settings *s, uint32_t rgb);

#endif
