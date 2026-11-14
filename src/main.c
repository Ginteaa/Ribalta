#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/file.h>
#include <time.h>
#include <unistd.h>
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include "clock.h"
#include "color.h"
#include "notify.h"
#include "pomodoro.h"
#include "render.h"
#include "settings.h"
#include "stats.h"
#include "timer.h"
#include "ui_color.h"
#include "ui_settings.h"
#include "ui_stats.h"

#define CURSOR_HIDE_NS (2000ULL * SDL_NS_PER_MS)
#define BLINK_PHASE_NS (300ULL * SDL_NS_PER_MS)
#define BLINK_PHASES   6
#define SAVER_GRACE_NS (1000ULL * SDL_NS_PER_MS)
#define SAVER_MOVE_PX  8.0f   /* movimento reale minimo per uscire dallo screensaver */
#define COLOR_BREAK    0x6fa8dcu
#define COLOR_BREAK_ALT 0xffb347u /* riposo se il colore scelto e gia blu */

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
    char        text[40];   /* etichetta libera (Pomodoro); "" = nessuna */
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
    bool          preview_active;    /* anteprima dal vivo del selettore (non salvata) */
    uint32_t      preview_color;
    TimerEntry    entry;             /* inserimento durata da tastiera (stile microonde) */
    bool          entry_active;
    bool          screensaver;
    Uint64        start_ns;          /* avvio app (grazia screensaver) */
    float         saver_moved;       /* movimento mouse accumulato dopo la grazia */
    time_t        timer_start;       /* inizio sessione timer (0 = nessuna) */
    time_t        pomo_start;        /* inizio sessione STUDIO (0 = nessuna) */
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

static bool is_fullscreen(const App *a)
{
    return (SDL_GetWindowFlags(a->win) & SDL_WINDOW_FULLSCREEN) != 0;
}

static void cursor_show(App *a)
{
    if (a->cursor_hidden) {
        SDL_ShowCursor();
        a->cursor_hidden = false;
    }
    a->cursor_deadline = 0;
}

/* Da chiamare su movimento mouse o ingresso in fullscreen. */
static void cursor_activity(App *a)
{
    if (a->screensaver)
        return; /* cursore sempre nascosto */
    cursor_show(a);
    if (is_fullscreen(a))
        a->cursor_deadline = SDL_GetTicksNS() + CURSOR_HIDE_NS;
}

static void cursor_tick(App *a, Uint64 now)
{
    if (a->cursor_deadline && now >= a->cursor_deadline) {
        a->cursor_deadline = 0;
        if (is_fullscreen(a) && !a->cursor_hidden) {
            SDL_HideCursor();
            a->cursor_hidden = true;
        }
    }
}

static void toggle_fullscreen(App *a)
{
    SDL_SetWindowFullscreen(a->win, !is_fullscreen(a));
    a->dirty = true;
}

static bool panel_open(const App *a)
{
    return a->ui && ui_settings_is_open(a->ui);
}

static void pixel_size(const App *a, int *w, int *h)
{
    *w = 0;
    *h = 0;
    SDL_GetCurrentRenderOutputSize(a->ren, w, h);
}

static void close_panel(App *a);

static bool color_open(const App *a)
{
    return a->cui && ui_color_is_open(a->cui);
}

/* Colore base delle cifre: anteprima del selettore se attiva, altrimenti quello salvato. */
static uint32_t base_color(const App *a)
{
    return a->preview_active ? a->preview_color : a->cfg.digit_color;
}

static void open_color(App *a)
{
    int w, h;
    if (!a->cui)
        return;
    pixel_size(a, &w, &h);
    ui_color_open(a->cui, w, h, a->cfg.digit_color, a->cfg.recent_colors, a->cfg.recent_count);
    a->dirty = true;
}

static void color_finish(App *a)
{
    a->preview_active = false;
    if (color_open(a))
        ui_color_close(a->cui);
    a->dirty = true;
}

static void color_result(App *a, UiColorResult r)
{
    switch (r) {
    case UICOLOR_CHANGED:
        a->preview_active = true;
        a->preview_color = ui_color_value(a->cui);
        a->dirty = true;
        break;
    case UICOLOR_APPLY:
        a->cfg.digit_color = ui_color_value(a->cui);
        settings_push_recent(&a->cfg, a->cfg.digit_color);
        settings_save(&a->cfg);
        color_finish(a);
        break;
    case UICOLOR_CANCEL:
        color_finish(a);
        break;
    default:
        break;
    }
    if (a->cui && ui_color_dirty(a->cui))
        a->dirty = true;
}

static void color_mouse(App *a, const SDL_Event *ev)
{
    SDL_Event copy = *ev;
    SDL_ConvertEventToRenderCoordinates(a->ren, &copy);
    color_result(a, ui_color_mouse(a->cui, &copy));
}

static bool stats_open(const App *a)
{
    return a->sui && ui_stats_is_open(a->sui);
}

static void close_stats(App *a)
{
    ui_stats_close(a->sui);
    a->dirty = true;
}

static void open_stats(App *a)
{
    int w, h;
    if (!a->sui)
        return;
    if (panel_open(a))
        close_panel(a);
    pixel_size(a, &w, &h);
    ui_stats_open(a->sui, w, h);
    a->dirty = true;
}

static void open_panel(App *a)
{
    int w, h;
    if (!a->ui)
        return;
    if (stats_open(a))
        close_stats(a);
    pixel_size(a, &w, &h);
    ui_settings_open(a->ui, w, h);
    a->dirty = true;
}

static void close_panel(App *a)
{
    ui_settings_close(a->ui);
    settings_save(&a->cfg);
    a->dirty = true;
}

static void settings_changed(App *a, int old_default_minutes)
{
    render_set_style(a->render, a->cfg.scale, a->cfg.brightness);
    if (a->timer.state == TIMER_SETUP && a->cfg.timer_default_minutes != old_default_minutes)
        timer_init(&a->timer, a->cfg.timer_default_minutes);
    pomo_apply_settings(&a->pomo, &a->cfg);
    a->has_shown = false; /* riapplica il volto (24h/12h, secondi) senza animazione */
    a->dirty = true;
}

static void on_key_panel(App *a, const SDL_KeyboardEvent *k)
{
    if (!k->repeat && (k->key == SDLK_ESCAPE || k->key == SDLK_S)) {
        close_panel(a);
        return;
    }
    int old = a->cfg.timer_default_minutes;
    if (ui_settings_key(a->ui, k->key, k->mod, &a->cfg))
        settings_changed(a, old);
    if (a->cui && ui_settings_take_color_request(a->ui))
        open_color(a);
    a->dirty = true;
}

/* --- Statistiche (roadmap §3.2): nulla in modalità screensaver --- */

static void log_session(const App *a, time_t start, Uint64 elapsed_ns, const char *type, bool completed)
{
    if (a->screensaver)
        return;
    long secs = (long)(elapsed_ns / SDL_NS_PER_SECOND);
    stats_append(start ? start : time(NULL) - secs, secs, type, completed);
}

/* Timer interrotto (R / uscita): da chiamare PRIMA di resettarlo. */
static void log_timer_partial(const App *a, Uint64 now)
{
    if (a->timer.state != TIMER_SETUP)
        log_session(a, a->timer_start, timer_elapsed_ns(&a->timer, now), "timer", false);
}

/* STUDIO avviato e interrotto (N / R / uscita): da chiamare PRIMA di cambiare fase. */
static void log_pomo_partial(const App *a, Uint64 now)
{
    if (a->pomo.phase == POMO_WORK && a->pomo.timer.state != TIMER_SETUP)
        log_session(a, a->pomo_start, timer_elapsed_ns(&a->pomo.timer, now), "pomodoro", false);
}

/* Inizio sessione: istante di prima transizione SETUP -> RUNNING (rilevata a ogni giro). */
static void track_starts(App *a)
{
    if (a->timer.state == TIMER_SETUP)
        a->timer_start = 0;
    else if (!a->timer_start)
        a->timer_start = time(NULL);
    if (a->pomo.phase != POMO_WORK || a->pomo.timer.state == TIMER_SETUP)
        a->pomo_start = 0;
    else if (!a->pomo_start)
        a->pomo_start = time(NULL);
}

static void set_view(App *a, View v)
{
    a->view = v;
    a->blink_active = false;
    a->dirty = true;
}

static void entry_cancel(App *a)
{
    timer_entry_clear(&a->entry);
    a->entry_active = false;
    a->dirty = true;
}

/* Conferma l'inserimento: durata valida -> timer_init (e avvio se start). 0 = solo annulla. */
static void entry_commit(App *a, bool start)
{
    if (!a->entry_active)
        return;
    int m = timer_entry_minutes(&a->entry);
    entry_cancel(a);
    if (m > 0) {
        timer_init(&a->timer, m);
        if (start)
            timer_toggle(&a->timer, SDL_GetTicksNS());
    }
}

/* Cifra 0..9 dal tasto (riga alfanumerica o tastierino), -1 altrimenti. */
static int key_digit(SDL_Keycode key)
{
    if (key >= SDLK_0 && key <= SDLK_9)
        return (int)(key - SDLK_0);
    if (key >= SDLK_KP_1 && key <= SDLK_KP_9)
        return (int)(key - SDLK_KP_1) + 1;
    if (key == SDLK_KP_0)
        return 0;
    return -1;
}

/* Tasti del timer; le frecce accettano il repeat. Ritorna true se il tasto e consumato
 * (Esc che annulla l'inserimento: non deve uscire dallo schermo intero). */
static bool on_key_timer(App *a, const SDL_KeyboardEvent *k)
{
    Timer *t = &a->timer;
    bool shift = (k->mod & SDL_KMOD_SHIFT) != 0;
    int digit = key_digit(k->key);
    if (digit >= 0) {
        if (!k->repeat && t->state == TIMER_SETUP) {
            a->blink_active = false;
            timer_entry_push(&a->entry, digit);
            a->entry_active = true;
            a->dirty = true;
        }
        return false;
    }
    switch (k->key) {
    case SDLK_BACKSPACE:
        if (a->entry_active) {
            timer_entry_pop(&a->entry);
            a->dirty = true;
        }
        break;
    case SDLK_ESCAPE:
        if (a->entry_active) {
            entry_cancel(a);
            return true;
        }
        break;
    case SDLK_T:
    case SDLK_P:
    case SDLK_S:
    case SDLK_I:
        entry_commit(a, false);
        break;
    case SDLK_SPACE:
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        if (k->repeat)
            break;
        if (a->entry_active)
            entry_commit(a, true);
        else
            timer_toggle(t, SDL_GetTicksNS());
        break;
    case SDLK_R:
        if (!k->repeat) {
            if (a->entry_active) {
                entry_cancel(a);
            } else {
                log_timer_partial(a, SDL_GetTicksNS());
                timer_reset(t);
            }
        }
        break;
    case SDLK_LEFT:
        entry_commit(a, false);
        if (t->state == TIMER_SETUP)
            a->timer_sel = 0;
        break;
    case SDLK_RIGHT:
        entry_commit(a, false);
        if (t->state == TIMER_SETUP)
            a->timer_sel = 1;
        break;
    case SDLK_UP:
    case SDLK_DOWN: {
        entry_commit(a, false);
        int step = a->timer_sel == 0 ? 60 : (shift ? 10 : 1);
        timer_adjust(t, k->key == SDLK_UP ? step : -step);
        break;
    }
    default:
        break;
    }
    return false;
}

/* Tasti della vista Pomodoro. */
static void on_key_pomo(App *a, const SDL_KeyboardEvent *k)
{
    Pomodoro *p = &a->pomo;
    if (k->repeat)
        return;
    Uint64 now = SDL_GetTicksNS();
    switch (k->key) {
    case SDLK_SPACE:
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        timer_toggle(&p->timer, now);
        break;
    case SDLK_N: {
        bool was_running = p->timer.state == TIMER_RUNNING;
        log_pomo_partial(a, now);
        pomo_next(p, &a->cfg, was_running, now);
        break;
    }
    case SDLK_R:
        log_pomo_partial(a, now);
        pomo_init(p, &a->cfg);
        break;
    default:
        break;
    }
}

static void on_key(App *a, const SDL_KeyboardEvent *k)
{
    if (a->screensaver)
        return;
    if (k->key == SDLK_Q && !k->repeat) {
        if (color_open(a))
            color_finish(a); /* annulla l'anteprima */
        if (panel_open(a))
            settings_save(&a->cfg);
        a->running = false;
        return;
    }
    if (color_open(a)) {
        color_result(a, ui_color_key(a->cui, k->key, k->mod));
        return;
    }
    if (stats_open(a)) {
        if (k->repeat)
            return;
        if (k->key == SDLK_ESCAPE || k->key == SDLK_I)
            close_stats(a);
        else if (k->key == SDLK_S)
            open_panel(a);
        return;
    }
    if (panel_open(a) && !k->repeat && k->key == SDLK_I) {
        open_stats(a);
        return;
    }
    if (panel_open(a)) {
        on_key_panel(a, k);
        return;
    }
    if (a->view == VIEW_TIMER) {
        if (on_key_timer(a, k))
            return;
    } else if (a->view == VIEW_POMODORO)
        on_key_pomo(a, k);
    if (k->repeat)
        return;
    switch (k->key) {
    case SDLK_F:
    case SDLK_F11:
        toggle_fullscreen(a);
        break;
    case SDLK_ESCAPE:
        if (is_fullscreen(a))
            toggle_fullscreen(a);
        break;
    case SDLK_S:
        open_panel(a);
        break;
    case SDLK_I:
        open_stats(a);
        break;
    case SDLK_T:
        set_view(a, a->view == VIEW_TIMER ? VIEW_CLOCK : VIEW_TIMER);
        break;
    case SDLK_P:
        set_view(a, a->view == VIEW_POMODORO ? VIEW_CLOCK : VIEW_POMODORO);
        break;
    default:
        break;
    }
}

static void handle_event(App *a, const SDL_Event *ev)
{
    switch (ev->type) {
    case SDL_EVENT_QUIT:
        a->running = false;
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (color_open(a))
            color_mouse(a, ev);
        break;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_MOTION:
        if (ev->type != SDL_EVENT_KEY_DOWN && color_open(a)) {
            cursor_activity(a);
            color_mouse(a, ev);
            break;
        }
        if (a->screensaver) {
            if (SDL_GetTicksNS() - a->start_ns < SAVER_GRACE_NS)
                break;
            /* ignora i movimenti "finti" (ingresso del puntatore, finestra che compare):
             * esce solo con tasto, clic o uno spostamento reale del mouse */
            if (ev->type == SDL_EVENT_MOUSE_MOTION) {
                a->saver_moved += SDL_fabsf(ev->motion.xrel) + SDL_fabsf(ev->motion.yrel);
                if (a->saver_moved < SAVER_MOVE_PX)
                    break;
            }
            a->running = false;
            break;
        }
        if (ev->type == SDL_EVENT_KEY_DOWN)
            on_key(a, &ev->key);
        else if (ev->type == SDL_EVENT_MOUSE_MOTION)
            cursor_activity(a);
        else if (ev->button.button == SDL_BUTTON_LEFT && ev->button.clicks == 2)
            toggle_fullscreen(a);
        break;
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    case SDL_EVENT_RENDER_TARGETS_RESET:
    case SDL_EVENT_RENDER_DEVICE_RESET:
        a->resized = true;
        a->dirty = true;
        break;
    case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
    case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
        cursor_activity(a);
        a->dirty = true;
        break;
    case SDL_EVENT_WINDOW_EXPOSED:
    case SDL_EVENT_WINDOW_SHOWN:
        a->dirty = true;
        break;
    default:
        break;
    }
}

static void drain_events(App *a)
{
    SDL_Event ev;
    while (SDL_PollEvent(&ev))
        handle_event(a, &ev);
}

static bool face_equal(const ClockFace *x, const ClockFace *y)
{
    if (x->count != y->count || x->ampm != y->ampm || x->hide_leading_zero != y->hide_leading_zero)
        return false;
    for (int i = 0; i < x->count; i++)
        if (x->values[i] != y->values[i])
            return false;
    return true;
}

static void build_timer_display(const App *a, Uint64 now, Display *d)
{
    const Timer *t = &a->timer;
    timer_face(t, &a->cfg, now, d->face.values, &d->face.count);
    d->label = t->state == TIMER_PAUSED ? LABEL_PAUSA : LABEL_TIMER;
    d->has_progress = t->state != TIMER_SETUP;
    d->alpha[0] = d->alpha[1] = t->state == TIMER_PAUSED ? 0.5f : 1.0f;
    if (a->entry_active && t->state == TIMER_SETUP) {
        timer_entry_face(&a->entry, d->face.values);
        d->face.count = 2;
        d->label = LABEL_TIMER;
        d->has_progress = false;
        d->alpha[0] = d->alpha[1] = 1.0f;
    } else if (t->state == TIMER_SETUP) {
        d->alpha[0] = a->timer_sel == 0 ? 1.0f : 0.4f;
        d->alpha[1] = a->timer_sel == 1 ? 1.0f : 0.4f;
    }
}

static void build_pomo_display(const App *a, Uint64 now, Display *d)
{
    const Pomodoro *p = &a->pomo;
    timer_face(&p->timer, &a->cfg, now, d->face.values, &d->face.count);
    pomo_label(p, &a->cfg, d->text, sizeof d->text);
    d->has_progress = p->timer.state != TIMER_SETUP;
    d->alpha[0] = d->alpha[1] = p->timer.state == TIMER_PAUSED ? 0.5f : 1.0f;
    uint32_t base = base_color(a);
    if (p->phase == POMO_WORK)
        d->color = base;
    else
        d->color = color_is_bluish(base) ? COLOR_BREAK_ALT : COLOR_BREAK;
}

static void build_blink_display(const App *a, Uint64 now, Display *d)
{
    unsigned phase = (unsigned)((now - a->blink_start) / BLINK_PHASE_NS);
    d->face.values[0] = d->face.values[1] = 0;
    d->face.count = 2;
    d->label = LABEL_TIMER;
    d->has_progress = false;
    d->alpha[0] = d->alpha[1] = (phase % 2 == 0) ? 0.15f : 1.0f;
}

static void build_display(const App *a, Uint64 now, Display *d)
{
    memset(d, 0, sizeof *d);
    d->view = a->view;
    d->alpha[0] = d->alpha[1] = 1.0f;
    d->label = LABEL_NONE;
    d->color = base_color(a);
    d->accent = base_color(a);
    if (a->view == VIEW_CLOCK) {
        clock_get(&a->cfg, &d->face);
    } else if (a->view == VIEW_POMODORO) {
        build_pomo_display(a, now, d);
    } else if (a->blink_active) {
        build_blink_display(a, now, d);
    } else {
        build_timer_display(a, now, d);
    }
}

static bool display_equal(const Display *x, const Display *y)
{
    return x->view == y->view && x->label == y->label && x->has_progress == y->has_progress &&
           x->alpha[0] == y->alpha[0] && x->alpha[1] == y->alpha[1] &&
           x->color == y->color && x->accent == y->accent && strcmp(x->text, y->text) == 0 && face_equal(&x->face, &y->face);
}

static void apply_alpha(App *a, const Display *d)
{
    if (d->alpha[0] == d->alpha[1]) {
        render_set_tile_alpha(a->render, -1, d->alpha[0]);
    } else {
        render_set_tile_alpha(a->render, 0, d->alpha[0]);
        render_set_tile_alpha(a->render, 1, d->alpha[1]);
    }
}

static void apply_display(App *a, const Display *d, Uint64 now)
{
    bool animate = a->cfg.animations && a->has_shown && a->shown.view == d->view;
    const ClockFace *f = &d->face;
    if (d->color != a->applied_color) {
        render_set_digit_color(a->render, (Uint8)(d->color >> 16), (Uint8)(d->color >> 8), (Uint8)d->color);
        a->applied_color = d->color;
    }
    if (d->accent != a->applied_accent) {
        render_set_accent(a->render, (Uint8)(d->accent >> 16), (Uint8)(d->accent >> 8), (Uint8)d->accent);
        a->applied_accent = d->accent;
    }
    render_set_tiles(a->render, f->values, f->count, f->hide_leading_zero, animate, now);
    render_set_ampm(a->render, f->ampm);
    render_set_label(a->render, d->label);
    render_set_label_text(a->render, d->text[0] ? d->text : NULL);
    apply_alpha(a, d);
    float p = -1.0f;
    if (d->has_progress)
        p = timer_progress(d->view == VIEW_POMODORO ? &a->pomo.timer : &a->timer, now);
    render_set_progress(a->render, p);
    a->shown = *d;
    a->has_shown = true;
}

/* Il progresso si aggiorna solo quando cambia qualcosa di visibile (nessun risveglio extra). */
static void update_display(App *a, Uint64 now)
{
    Display d;
    build_display(a, now, &d);
    if (!a->has_shown || !display_equal(&d, &a->shown)) {
        apply_display(a, &d, now);
        a->dirty = true;
    }
}

/* Fine timer: notifiche, vista Timer a 00:00 e lampeggio. */
static void on_timer_finished(App *a, Uint64 now)
{
    log_session(a, a->timer_start, (Uint64)a->timer.minutes * 60 * SDL_NS_PER_SECOND, "timer", true);
    if (a->cfg.timer_notify) {
        int m = a->timer.minutes;
        char body[96];
        snprintf(body, sizeof body, "Sessione di studio completata (%dh %02dmin)", m / 60, m % 60);
        notify_desktop("Ribalta", body);
    }
    if (a->cfg.timer_sound)
        notify_sound();
    a->view = VIEW_TIMER;
    a->blink_active = true;
    a->blink_start = now;
}

/* Fine fase Pomodoro: log dello STUDIO, notifica e suono; la vista non cambia. */
static void on_pomo_finished(App *a, PomoPhase finished)
{
    const char *msg = "Riposo finito \u2014 si torna a studiare";
    char body[96];
    if (finished == POMO_WORK) {
        log_session(a, a->pomo_start, (Uint64)a->cfg.pomodoro_work * 60 * SDL_NS_PER_SECOND, "pomodoro", true);
        if (a->pomo.phase == POMO_LONG_BREAK)
            snprintf(body, sizeof body, "Fine studio \u2014 riposo lungo di %d min", a->cfg.pomodoro_long_break);
        else
            snprintf(body, sizeof body, "Fine studio \u2014 riposo di %d min", a->cfg.pomodoro_break);
        msg = body;
    }
    if (a->cfg.timer_notify)
        notify_desktop("Ribalta", msg);
    if (a->cfg.timer_sound)
        notify_sound();
}

static void blink_tick(App *a, Uint64 now)
{
    if (a->blink_active && now - a->blink_start >= BLINK_PHASES * BLINK_PHASE_NS) {
        a->blink_active = false;
        a->view = VIEW_CLOCK;
    }
}

static void apply_resize(App *a)
{
    int w = 0, h = 0;
    SDL_GetCurrentRenderOutputSize(a->ren, &w, &h);
    if (w > 0 && h > 0) {
        render_resize(a->render, w, h);
        if (panel_open(a))
            ui_settings_resize(a->ui, w, h);
        if (stats_open(a))
            ui_stats_resize(a->sui, w, h);
        if (color_open(a))
            ui_color_resize(a->cui, w, h);
    }
    a->resized = false;
}

static uint32_t ns_to_ms_ceil(Uint64 ns)
{
    Uint64 ms = (ns + SDL_NS_PER_MS - 1) / SDL_NS_PER_MS;
    return ms > 0x7fffffffu ? 0x7fffffffu : (uint32_t)ms;
}

static uint32_t until_ms(Uint64 deadline, Uint64 now)
{
    return ns_to_ms_ceil(deadline > now ? deadline - now : 0);
}

static uint32_t blink_ms(const App *a, Uint64 now)
{
    if (!a->blink_active)
        return UINT32_MAX;
    Uint64 phase = (now - a->blink_start) / BLINK_PHASE_NS;
    return until_ms(a->blink_start + (phase + 1) * BLINK_PHASE_NS, now);
}

static uint32_t min_u32(uint32_t x, uint32_t y)
{
    return x < y ? x : y;
}

/* Timeout di attesa in ms: minimo tra cambio orologio, fine/cambio timer, fase di
 * lampeggio, scadenza cursore e poll delle notifiche. */
static int32_t wait_timeout_ms(const App *a, Uint64 now)
{
    uint32_t ms = clock_ms_to_next_change(&a->cfg);
    if (a->timer.state == TIMER_RUNNING)
        ms = min_u32(ms, timer_ms_to_next_change(&a->timer, &a->cfg, now));
    if (a->pomo.timer.state == TIMER_RUNNING)
        ms = min_u32(ms, timer_ms_to_next_change(&a->pomo.timer, &a->cfg, now));
    ms = min_u32(ms, blink_ms(a, now));
    ms = min_u32(ms, notify_ms_to_next_poll());
    if (a->cursor_deadline)
        ms = min_u32(ms, until_ms(a->cursor_deadline, now));
    return ms > 0x7fffffffu ? 0x7fffffff : (int32_t)ms;
}

/* Blocca lo spegnimento schermo finché timer o Pomodoro sono IN CORSO (solo al cambio di stato). */
static void set_inhibit(App *a)
{
    bool want = a->timer.state == TIMER_RUNNING || a->pomo.timer.state == TIMER_RUNNING;
    if (want == a->inhibit)
        return;
    a->inhibit = want;
    if (want)
        SDL_DisableScreenSaver();
    else
        SDL_EnableScreenSaver();
}

/* Animazione flip a 30 fps invece di 60: dimezza la CPU durante i flip
 * (misurato ~1,4 ms CPU per fotogramma con il renderer gpu). */
#define ANIM_FRAME_NS (SDL_NS_PER_SECOND / 30)

static void main_loop(App *a)
{
    Uint64 next_frame = 0;
    while (a->running) {
        Uint64 now = SDL_GetTicksNS();
        if (render_is_animating(a->render, now)) {
            /* dorme fino al prossimo fotogramma, restando reattivo agli eventi */
            SDL_Event ev;
            if (next_frame > now && SDL_WaitEventTimeout(&ev, (Sint32)until_ms(next_frame, now)))
                handle_event(a, &ev);
            drain_events(a);
            next_frame = SDL_GetTicksNS() + ANIM_FRAME_NS;
        } else {
            SDL_Event ev;
            if (SDL_WaitEventTimeout(&ev, wait_timeout_ms(a, now)))
                handle_event(a, &ev);
            drain_events(a);
        }
        if (!a->running)
            break;

        if (a->resized)
            apply_resize(a);
        now = SDL_GetTicksNS();
        cursor_tick(a, now);
        if (timer_update(&a->timer, now))
            on_timer_finished(a, now);
        PomoPhase fin;
        if (pomo_update(&a->pomo, &a->cfg, now, &fin))
            on_pomo_finished(a, fin);
        track_starts(a);
        set_inhibit(a);
        blink_tick(a, now);
        notify_poll();
        update_display(a, now);

        now = SDL_GetTicksNS();
        bool animating = render_is_animating(a->render, now);
        /* un ultimo frame dopo la fine dell'animazione */
        if (animating || a->was_animating)
            a->dirty = true;
        a->was_animating = animating;

        if (a->dirty) {
            render_draw(a->render, now);
            if (color_open(a))
                ui_color_draw(a->cui);
            else if (panel_open(a))
                ui_settings_draw(a->ui, &a->cfg);
            else if (stats_open(a))
                ui_stats_draw(a->sui);
            SDL_RenderPresent(a->ren);
            a->dirty = false;
        }
    }
}

static bool app_init(void)
{
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "wayland,x11");
    SDL_SetHint(SDL_HINT_APP_ID, "ribalta");
    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");
    /* "gpu" (SDL_GPU/Vulkan) misurato: ~17 MB PSS contro ~37 MB di opengl.
     * La variabile d'ambiente SDL_RENDER_DRIVER ha comunque la precedenza. */
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "gpu,opengl,opengles2,software");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "ribalta: SDL_Init: %s\n", SDL_GetError());
        return false;
    }
    if (!TTF_Init()) {
        fprintf(stderr, "ribalta: TTF_Init: %s\n", SDL_GetError());
        return false;
    }
    return true;
}

static bool app_open(App *a, const char *font_path)
{
    SDL_WindowFlags fl = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (a->cfg.start_fullscreen || a->screensaver)
        fl |= SDL_WINDOW_FULLSCREEN;
    a->win = SDL_CreateWindow("Ribalta", 960, 400, fl);
    if (!a->win) {
        fprintf(stderr, "ribalta: SDL_CreateWindow: %s\n", SDL_GetError());
        return false;
    }
    a->ren = SDL_CreateRenderer(a->win, NULL);
    if (!a->ren) {
        fprintf(stderr, "ribalta: SDL_CreateRenderer: %s\n", SDL_GetError());
        return false;
    }
    SDL_SetRenderVSync(a->ren, 1);
    if (getenv("RIBALTA_DEBUG"))
        fprintf(stderr, "ribalta: renderer=%s\n", SDL_GetRendererName(a->ren));

    a->render = render_create(a->ren, font_path);
    if (!a->render) {
        fprintf(stderr, "ribalta: render_create failed (%s)\n", font_path);
        return false;
    }
    render_set_style(a->render, a->cfg.scale, a->cfg.brightness);
    apply_resize(a);
    timer_init(&a->timer, a->cfg.timer_default_minutes);
    pomo_init(&a->pomo, &a->cfg);
    a->applied_color = ~0u; /* forza la prima applicazione */
    a->applied_accent = ~0u;
    char wav[1024];
    char icon[1024];
    notify_init(settings_find_asset("ding.wav", wav, sizeof wav) ? wav : NULL,
                settings_find_asset("ribalta.svg", icon, sizeof icon) ? icon : NULL);
    if (!a->screensaver) {
        a->ui = ui_settings_create(a->ren, font_path);
        a->sui = ui_stats_create(a->ren, font_path);
        a->cui = ui_color_create(a->ren, font_path);
    }
    update_display(a, SDL_GetTicksNS());
    a->dirty = true;
    a->start_ns = SDL_GetTicksNS();
    if (a->screensaver) {
        SDL_HideCursor();
        a->cursor_hidden = true;
    } else if (a->cfg.start_fullscreen) {
        cursor_activity(a);
    }
    return true;
}

static void app_close(App *a)
{
    notify_shutdown();
    if (a->ui)
        ui_settings_destroy(a->ui);
    if (a->sui)
        ui_stats_destroy(a->sui);
    if (a->cui)
        ui_color_destroy(a->cui);
    if (a->render)
        render_destroy(a->render);
    if (a->ren)
        SDL_DestroyRenderer(a->ren);
    if (a->win)
        SDL_DestroyWindow(a->win);
    TTF_Quit();
    SDL_Quit();
}

/* Istanza singola: lock esclusivo su $XDG_RUNTIME_DIR/ribalta.lock (fallback /tmp).
 * Il descrittore resta aperto per tutta la vita del processo; il kernel rilascia
 * il lock all'uscita, anche in caso di crash. Ritorna false se un'altra istanza è attiva. */
static bool acquire_single_instance(void)
{
    const char *dir = getenv("XDG_RUNTIME_DIR");
    char path[512];
    snprintf(path, sizeof path, "%s/ribalta.lock", (dir && *dir) ? dir : "/tmp");
    int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (fd < 0)
        return true; /* non bloccare l'avvio se il lock non è creabile */
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        close(fd);
        return false;
    }
    return true; /* fd lasciato aperto di proposito */
}

static void usage(FILE *out)
{
    fputs("Uso: ribalta [opzioni]\n"
          "\n"
          "Orologio flip con timer di studio, Pomodoro e statistiche.\n"
          "\n"
          "Opzioni:\n"
          "  --screensaver   schermo intero, esce con tasto, clic o movimento del mouse\n"
          "  -h, --help      mostra questo aiuto\n"
          "\n"
          "Tasti:\n"
          "  F/F11 schermo intero   T timer   P Pomodoro   S impostazioni   I statistiche\n"
          "  0-9 durata timer (stile microonde)   Backspace cancella\n"
          "  Spazio/Invio avvia/pausa   R reset   N fase successiva (Pomodoro)\n"
          "  Q esci   Esc chiude pannello / esce dallo schermo intero\n",
          out);
}

int main(int argc, char **argv)
{
    bool screensaver = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--screensaver") == 0) {
            screensaver = true;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(stdout);
            return 0;
        } else {
            fprintf(stderr, "ribalta: opzione sconosciuta: %s\n\n", argv[i]);
            usage(stderr);
            return 2;
        }
    }

    if (!acquire_single_instance()) {
        fprintf(stderr, "ribalta: già in esecuzione\n");
        return 0;
    }

    App app;
    memset(&app, 0, sizeof app);
    app.running = true;
    app.screensaver = screensaver;
    settings_load(&app.cfg);

    char font[1024];
    if (!settings_find_asset("font.ttf", font, sizeof font)) {
        fprintf(stderr, "ribalta: font.ttf not found (searched $RIBALTA_DATA, ./assets, "
                        "<exe>/assets, ~/.local/share/ribalta, /usr/share/ribalta)\n");
        return 1;
    }
    int rc = 1;
    if (app_init() && app_open(&app, font)) {
        main_loop(&app);
        if (!app.screensaver) {
            Uint64 now = SDL_GetTicksNS();
            log_timer_partial(&app, now);
            log_pomo_partial(&app, now);
        }
        rc = 0;
    }
    app_close(&app);
    return rc;
}
