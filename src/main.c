#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/file.h>
#include <fcntl.h>
#include <sys/file.h>
#include <time.h>
#include <unistd.h>
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <SDL3_ttf/SDL_ttf.h>
#include "app/app.h"
#include "app/display.h"
#include "app/input.h"
#include "app/session.h"
#include "platform/paths.h"
#include "platform/notify.h"

static void drain_events(App *a)
{
    SDL_Event ev;
    while (SDL_PollEvent(&ev))
        input_handle_event(a, &ev);
}

static void apply_resize(App *a)
{
    int w = 0, h = 0;
    SDL_GetCurrentRenderOutputSize(a->ren, &w, &h);
    if (w > 0 && h > 0) {
        render_resize(a->render, w, h);
        if (input_panel_open(a))
            ui_settings_resize(a->ui, w, h);
        if (input_stats_open(a))
            ui_stats_resize(a->sui, w, h);
        if (input_color_open(a))
            ui_color_resize(a->cui, w, h);
        if (input_font_open(a))
            ui_font_resize(a->fui, w, h);
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
    if (a->font_preview_at)
        ms = min_u32(ms, until_ms(a->font_preview_at, now));
    return ms > 0x7fffffffu ? 0x7fffffff : (int32_t)ms;
}

static void main_loop(App *a)
{
    Uint64 next_frame = 0;
    while (a->running) {
        Uint64 now = SDL_GetTicksNS();
        if (render_is_animating(a->render, now)) {
            /* dorme fino al prossimo fotogramma, restando reattivo agli eventi */
            SDL_Event ev;
            if (next_frame > now && SDL_WaitEventTimeout(&ev, (Sint32)until_ms(next_frame, now)))
                input_handle_event(a, &ev);
            drain_events(a);
            next_frame = SDL_GetTicksNS() + ANIM_FRAME_NS;
        } else {
            SDL_Event ev;
            if (SDL_WaitEventTimeout(&ev, wait_timeout_ms(a, now)))
                input_handle_event(a, &ev);
            drain_events(a);
        }
        if (!a->running)
            break;

        if (a->resized)
            apply_resize(a);
        now = SDL_GetTicksNS();
        input_cursor_tick(a, now);
        if (a->font_preview_at && now >= a->font_preview_at)
            input_font_preview_apply(a);
        if (timer_update(&a->timer, now))
            session_on_timer_finished(a, now);
        PomoPhase fin;
        if (pomo_update(&a->pomo, &a->cfg, now, &fin))
            session_on_pomo_finished(a, fin);
        session_track_starts(a);
        session_set_inhibit(a);
        session_blink_tick(a, now);
        notify_poll();
        display_update(a, now);

        now = SDL_GetTicksNS();
        bool animating = render_is_animating(a->render, now);
        /* un ultimo frame dopo la fine dell'animazione */
        if (animating || a->was_animating)
            a->dirty = true;
        a->was_animating = animating;

        if (a->dirty) {
            render_draw(a->render, now);
            if (input_color_open(a))
                ui_color_draw(a->cui);
            else if (input_font_open(a))
                ui_font_draw(a->fui);
            else if (input_panel_open(a))
                ui_settings_draw(a->ui, &a->cfg);
            else if (input_stats_open(a))
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

static bool app_open(App *a)
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

    a->render = render_create(a->ren, a->render_font);
    if (!a->render && strcmp(a->render_font, a->bundled_font) != 0) {
        fprintf(stderr, "ribalta: font non utilizzabile: %s, uso il font incluso\n", a->render_font);
        snprintf(a->render_font, sizeof a->render_font, "%s", a->bundled_font);
        a->render = render_create(a->ren, a->render_font);
    }
    if (!a->render) {
        fprintf(stderr, "ribalta: render_create failed (%s)\n", a->render_font);
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
    notify_init(paths_find_asset("ding.wav", wav, sizeof wav) ? wav : NULL,
                paths_find_asset("ribalta.svg", icon, sizeof icon) ? icon : NULL);
    if (!a->screensaver) {
        a->ui = ui_settings_create(a->ren, a->bundled_font);
        a->sui = ui_stats_create(a->ren, a->bundled_font);
        a->cui = ui_color_create(a->ren, a->bundled_font);
        a->fui = ui_font_create(a->ren, a->bundled_font, a->bundled_font);
    }
    display_update(a, SDL_GetTicksNS());
    a->dirty = true;
    a->start_ns = SDL_GetTicksNS();
    if (a->screensaver) {
        SDL_HideCursor();
        a->cursor_hidden = true;
    } else if (a->cfg.start_fullscreen) {
        input_cursor_activity(a);
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
    if (a->fui)
        ui_font_destroy(a->fui);
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

    if (!paths_find_asset("font.ttf", app.bundled_font, sizeof app.bundled_font)) {
        fprintf(stderr, "ribalta: font.ttf not found (searched $RIBALTA_DATA, ./assets, "
                        "<exe>/assets, ~/.local/share/ribalta, /usr/share/ribalta)\n");
        return 1;
    }
    /* font delle cifre: quello scelto se il file è leggibile (niente fontconfig all'avvio),
     * altrimenti l'incluso; l'impostazione non viene cancellata */
    snprintf(app.render_font, sizeof app.render_font, "%s", app.bundled_font);
    if (app.cfg.font_file[0]) {
        if (access(app.cfg.font_file, R_OK) == 0)
            snprintf(app.render_font, sizeof app.render_font, "%s", app.cfg.font_file);
        else
            fprintf(stderr, "ribalta: font non trovato: %s, uso il font incluso\n", app.cfg.font_file);
    }
    int rc = 1;
    if (app_init() && app_open(&app)) {
        main_loop(&app);
        if (!app.screensaver) {
            Uint64 now = SDL_GetTicksNS();
            session_log_timer_partial(&app, now);
            session_log_pomo_partial(&app, now);
        }
        rc = 0;
    }
    app_close(&app);
    return rc;
}
