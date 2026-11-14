#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <SDL3/SDL.h>
#include "app/input.h"
#include "app/display.h"
#include "app/session.h"

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
void input_cursor_activity(App *a)
{
    if (a->screensaver)
        return; /* cursore sempre nascosto */
    cursor_show(a);
    if (is_fullscreen(a))
        a->cursor_deadline = SDL_GetTicksNS() + CURSOR_HIDE_NS;
}

void input_cursor_tick(App *a, Uint64 now)
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

bool input_panel_open(const App *a)
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

bool input_color_open(const App *a)
{
    return a->cui && ui_color_is_open(a->cui);
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
    if (input_color_open(a))
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

bool input_font_open(const App *a)
{
    return a->fui && ui_font_is_open(a->fui);
}

/* Applica a Render il font `file` ("" = incluso). Ritorna false se non si apre (font precedente
 * intatto). Se è già quello in uso non fa nulla. */
static bool font_set(App *a, const char *file)
{
    const char *path = file && file[0] ? file : a->bundled_font;
    if (strcmp(path, a->render_font) == 0)
        return true;
    if (!render_set_font(a->render, path))
        return false;
    snprintf(a->render_font, sizeof a->render_font, "%s", path);
    a->dirty = true;
    return true;
}

/* Come font_set, ma su errore ripiega sul font incluso. */
static void font_set_or_bundled(App *a, const char *file)
{
    if (!font_set(a, file))
        font_set(a, "");
}

static void open_font(App *a)
{
    int w, h;
    if (!a->fui)
        return;
    pixel_size(a, &w, &h);
    ui_font_open(a->fui, w, h, a->cfg.font_file);
    a->font_preview_at = 0;
    a->dirty = true;
}

/* Chiude il menu; con apply conferma la voce selezionata, altrimenti ripristina il font iniziale. */
static void font_finish(App *a, bool apply)
{
    a->font_preview_at = 0;
    if (!input_font_open(a))
        return;
    if (apply) {
        char fam[sizeof a->cfg.font_family], file[sizeof a->cfg.font_file];
        snprintf(fam, sizeof fam, "%s", ui_font_family(a->fui));
        snprintf(file, sizeof file, "%s", ui_font_file(a->fui));
        if (font_set(a, file)) {
            snprintf(a->cfg.font_family, sizeof a->cfg.font_family, "%s", fam);
            snprintf(a->cfg.font_file, sizeof a->cfg.font_file, "%s", file);
            settings_save(&a->cfg);
        } else {
            font_set_or_bundled(a, a->cfg.font_file);
        }
    } else {
        font_set_or_bundled(a, ui_font_original_file(a->fui));
    }
    ui_font_close(a->fui);
    a->dirty = true;
}

/* Scaduta la pausa sulla voce: anteprima del font selezionato sulle tessere. */
void input_font_preview_apply(App *a)
{
    a->font_preview_at = 0;
    if (!input_font_open(a))
        return;
    font_set(a, ui_font_file(a->fui));
    a->dirty = true;
}

static void font_result(App *a, UiFontResult r)
{
    switch (r) {
    case UIFONT_CHANGED:
        a->font_preview_at = SDL_GetTicksNS() + FONT_PREVIEW_NS;
        a->dirty = true;
        break;
    case UIFONT_APPLY:
        font_finish(a, true);
        break;
    case UIFONT_CANCEL:
        font_finish(a, false);
        break;
    default:
        break;
    }
    if (a->fui && ui_font_dirty(a->fui))
        a->dirty = true;
}

static void font_mouse(App *a, const SDL_Event *ev)
{
    SDL_Event copy = *ev;
    SDL_ConvertEventToRenderCoordinates(a->ren, &copy);
    font_result(a, ui_font_mouse(a->fui, &copy));
}

bool input_stats_open(const App *a)
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
    if (input_panel_open(a))
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
    if (input_stats_open(a))
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
    display_invalidate(a); /* riapplica il volto (24h/12h, secondi) senza animazione */
    a->dirty = true;
}

static void on_key_panel(App *a, const SDL_KeyboardEvent *k)
{
    if (!k->repeat && (k->key == SDLK_ESCAPE || k->key == SDLK_S)) {
        close_panel(a);
        return;
    }
    int old = a->cfg.timer_default_minutes;
    char old_fam[sizeof a->cfg.font_family], old_file[sizeof a->cfg.font_file];
    memcpy(old_fam, a->cfg.font_family, sizeof old_fam);
    memcpy(old_file, a->cfg.font_file, sizeof old_file);
    if (ui_settings_key(a->ui, k->key, k->mod, &a->cfg)) {
        if (strcmp(old_file, a->cfg.font_file) != 0 && !font_set(a, a->cfg.font_file)) {
            /* il file non si apre: si tiene il font precedente e le stringhe precedenti */
            memcpy(a->cfg.font_family, old_fam, sizeof old_fam);
            memcpy(a->cfg.font_file, old_file, sizeof old_file);
        }
        settings_changed(a, old);
    }
    if (a->cui && ui_settings_take_color_request(a->ui))
        open_color(a);
    if (a->fui && ui_settings_take_font_request(a->ui))
        open_font(a);
    a->dirty = true;
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
                session_log_timer_partial(a, SDL_GetTicksNS());
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
        session_log_pomo_partial(a, now);
        pomo_next(p, &a->cfg, was_running, now);
        break;
    }
    case SDLK_R:
        session_log_pomo_partial(a, now);
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
        if (input_color_open(a))
            color_finish(a); /* annulla l'anteprima */
        if (input_font_open(a))
            font_finish(a, false); /* ripristina il font */
        if (input_panel_open(a))
            settings_save(&a->cfg);
        a->running = false;
        return;
    }
    if (input_color_open(a)) {
        color_result(a, ui_color_key(a->cui, k->key, k->mod));
        return;
    }
    if (input_font_open(a)) {
        font_result(a, ui_font_key(a->fui, k->key, k->mod));
        return;
    }
    if (input_stats_open(a)) {
        if (k->repeat)
            return;
        if (k->key == SDLK_ESCAPE || k->key == SDLK_I)
            close_stats(a);
        else if (k->key == SDLK_S)
            open_panel(a);
        return;
    }
    if (input_panel_open(a) && !k->repeat && k->key == SDLK_I) {
        open_stats(a);
        return;
    }
    if (input_panel_open(a)) {
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

void input_handle_event(App *a, const SDL_Event *ev)
{
    switch (ev->type) {
    case SDL_EVENT_QUIT:
        a->running = false;
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (input_color_open(a))
            color_mouse(a, ev);
        else if (input_font_open(a))
            font_mouse(a, ev);
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        if (input_font_open(a))
            font_mouse(a, ev);
        break;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_MOTION:
        if (ev->type != SDL_EVENT_KEY_DOWN && input_color_open(a)) {
            input_cursor_activity(a);
            color_mouse(a, ev);
            break;
        }
        if (ev->type != SDL_EVENT_KEY_DOWN && input_font_open(a)) {
            input_cursor_activity(a);
            font_mouse(a, ev);
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
            input_cursor_activity(a);
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
        input_cursor_activity(a);
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

