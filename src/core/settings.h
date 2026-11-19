#ifndef RIBALTA_SETTINGS_H
#define RIBALTA_SETTINGS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RECENT_COLORS_MAX 5
/* Materie (docs/plans/materie.md): al massimo SUBJECTS_MAX nomi da 1..SUBJECT_LEN-1 byte UTF-8. */
#define SUBJECTS_MAX 8
#define SUBJECT_LEN  25

/* Colore delle cifre nei riposi Pomodoro (docs/plans/colore-riposo.md).
 * Nel file: pomodoro_break_color=digits|auto|#rrggbb (default digits). Compatibilità: un file vecchio
 * con pomodoro_break_blue=true e senza pomodoro_break_color vale auto (qualsiasi ordine delle righe);
 * al salvataggio si scrive solo pomodoro_break_color. */
enum {
    BREAK_COLOR_DIGITS = 0,   /* come le cifre (default) */
    BREAK_COLOR_AUTO   = 1,   /* blu COLOR_BREAK, ambra COLOR_BREAK_ALT se le cifre sono bluastre */
    BREAK_COLOR_CUSTOM = 2    /* pomodoro_break_color */
};

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
    int      pomodoro_break_mode;   /* BREAK_COLOR_*, default BREAK_COLOR_DIGITS */
    uint32_t pomodoro_break_color;  /* colore scelto (usato con BREAK_COLOR_CUSTOM), default 0x6fa8dc;
                                     * conservato anche quando la modalità è un'altra */
    /* Colori: 0xRRGGBB. Nel file: digit_color=#rrggbb, recent_colors=#rrggbb,#rrggbb,... */
    uint32_t digit_color;                        /* default 0xb7b7b7 */
    uint32_t recent_colors[RECENT_COLORS_MAX];   /* il più recente per primo */
    int      recent_count;                       /* 0..RECENT_COLORS_MAX */
    /* Font delle cifre. Entrambi vuoti = font incluso (default). Nel file: font_family=DejaVu Sans,
     * font_file=/usr/share/fonts/... (valore fino a fine riga, spazi interni conservati; un valore
     * troppo lungo viene ignorato). All'avvio si apre font_file senza fontconfig. */
    char     font_family[128];
    char     font_file[512];
    /* Materie facoltative. Nel file: subjects=Analisi;Fisica (ordine conservato) e subject=Analisi
     * (materia attiva, "" = nessuna; un nome non in elenco viene ignorato). Le chiavi si scrivono
     * sempre. Nomi validi: vedi settings_subject_valid. */
    char     subjects[SUBJECTS_MAX][SUBJECT_LEN];
    int      subject_count;                      /* 0..SUBJECTS_MAX */
    char     subject[SUBJECT_LEN];               /* default "" */
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

/* Nome di materia valido: non vuoto, lungo < SUBJECT_LEN byte, senza spazi iniziali/finali e senza
 * i caratteri , ; " né caratteri di controllo (< 0x20, 0x7f). UTF-8 ammesso (accenti). */
bool settings_subject_valid(const char *name);

/* Aggiunge una materia in fondo all'elenco. Toglie prima gli spazi iniziali/finali; rifiuta (false)
 * nomi non validi, duplicati (confronto senza distinzione di maiuscole ASCII) ed elenco pieno.
 * Non cambia la materia attiva. */
bool settings_subject_add(Settings *s, const char *name);

/* Toglie la materia di indice index (0..subject_count-1, altrimenti nulla) conservando l'ordine.
 * Se era la materia attiva, la materia attiva diventa "" (nessuna). */
void settings_subject_remove(Settings *s, int index);

/* Indice della materia con quel nome (confronto esatto), -1 se assente o name vuoto/NULL. */
int settings_subject_find(const Settings *s, const char *name);

#endif
