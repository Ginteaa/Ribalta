#ifndef RIBALTA_APP_APP_H
#define RIBALTA_APP_APP_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <SDL3/SDL.h>
#include "core/clock.h"
#include "core/color.h"
#include "core/pomodoro.h"
#include "core/settings.h"
#include "core/timer.h"
#include "render/render.h"
#include "ui/ui_color.h"
#include "ui/ui_font.h"
#include "ui/ui_settings.h"
#include "ui/ui_stats.h"
#include "ui/ui_subject.h"

#define CURSOR_HIDE_NS (2000ULL * SDL_NS_PER_MS)
#define BLINK_PHASE_NS (300ULL * SDL_NS_PER_MS)
#define BLINK_PHASES   6
#define SAVER_GRACE_NS (1000ULL * SDL_NS_PER_MS)
#define SAVER_MOVE_PX  8.0f   /* movimento reale minimo per uscire dallo screensaver */


/* Animazione flip a 30 fps invece di 60: dimezza la CPU durante i flip
 * (misurato ~1,4 ms CPU per fotogramma con il renderer gpu). */
/* Pausa sulla voce del menu font prima dell'anteprima sulle tessere (riaprire il font è costoso). */
#define FONT_PREVIEW_NS (150ULL * SDL_NS_PER_MS)

#define ANIM_FRAME_NS (SDL_NS_PER_SECOND / 30)

typedef enum { VIEW_CLOCK, VIEW_TIMER, VIEW_POMODORO } View;

/* Stato desiderato/applicato di tessere, etichetta e opacità. */
typedef struct {
    View        view;
    ClockFace   face;
    RenderLabel label;
    bool        has_progress;
    float       alpha[2];   /* tessere 0 e 1; la 2 (secondi) segue la 1 */
    Uint32      color;      /* colore base delle cifre 0xRRGGBB */
    Uint32      accent;     /* colore di barra ed etichette 0xRRGGBB */
    char        text[64];   /* etichetta libera (Pomodoro, materia); "" = nessuna */
} Display;

typedef struct {
    SDL_Window   *win;
    SDL_Renderer *ren;
    Render       *render;
    Settings      cfg;
    SettingsUI   *ui;
    Timer         timer;
    Pomodoro      pomo;
    StatsUI      *sui;
    ColorUI      *cui;               /* selettore colore (NULL in screensaver) */
    SettingsColorRequest color_target; /* cosa modifica il selettore aperto: cifre o riposo */
    bool          preview_active;    /* anteprima dal vivo del selettore (non salvata): le tessere
                                      * mostrano preview_color in ogni vista, qualunque sia color_target */
    uint32_t      preview_color;
    FontUI       *fui;               /* menu dei font (NULL in screensaver) */
    SubjectUI    *subj;              /* menu delle materie (NULL in screensaver) */
    Uint64        font_preview_at;   /* scadenza anteprima font (0 = nessuna); entra nel timeout di attesa */
    char          bundled_font[1024];/* font incluso (assets/font.ttf): fallback e testo dei pannelli */
    char          render_font[1024]; /* file attualmente usato da Render per le cifre */
    TimerEntry    entry;             /* inserimento durata da tastiera (stile microonde) */
    bool          entry_active;
    bool          screensaver;
    Uint64        start_ns;          /* avvio app (grazia screensaver) */
    float         saver_moved;       /* movimento mouse accumulato dopo la grazia */
    time_t        timer_start;       /* inizio sessione timer (0 = nessuna) */
    time_t        pomo_start;        /* inizio sessione STUDIO (0 = nessuna) */
    char          timer_subject[SUBJECT_LEN]; /* materia della sessione timer, fissata all'inizio */
    char          pomo_subject[SUBJECT_LEN];  /* materia della sessione STUDIO, fissata all'inizio */
    bool          inhibit;           /* SDL_DisableScreenSaver attivo */
    int           timer_sel;         /* 0 = ore, 1 = minuti (SETUP) */
    View          view;
    bool          blink_active;
    Uint64        blink_start;
    Display       shown;             /* ultimo stato applicato al render */
    bool          has_shown;
    bool          running;
    bool          dirty;
    bool          resized;           /* resize da applicare (coalesced) */
    bool          was_animating;
    bool          cursor_hidden;
    Uint64        cursor_deadline;   /* 0 = nessuna scadenza pendente */
    Uint32        applied_color;
    Uint32        applied_accent;
} App;

#endif
