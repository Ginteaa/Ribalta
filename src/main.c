#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h> /* solo su Windows rinomina main() e fornisce WinMain (build -mwindows) */
#include <SDL3_ttf/SDL_ttf.h>
#include "app/app.h"
#include "app/display.h"
#include "app/input.h"
#include "app/session.h"
#include "platform/gpu.h"
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
        if (input_subject_open(a))
            ui_subject_resize(a->subj, w, h);
        if (input_help_open(a))
            ui_help_resize(a->hui, w, h);
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
    if (a->hint_deadline)
        ms = min_u32(ms, until_ms(a->hint_deadline, now));
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
        input_hint_tick(a, now);
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
            else if (input_subject_open(a))
                ui_subject_draw(a->subj);
            else if (input_panel_open(a))
                ui_settings_draw(a->ui, &a->cfg);
            else if (input_stats_open(a))
                ui_stats_draw(a->sui);
            else if (input_help_open(a))
                ui_help_draw(a->hui);
            SDL_RenderPresent(a->ren);
            a->dirty = false;
        }
    }
}

static bool app_init(void)
{
    gpu_limit_vulkan_drivers(); /* solo in Flatpak: evita di caricare driver Vulkan inutili */
#ifdef __linux__
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "wayland,x11");
#endif
    SDL_SetHint(SDL_HINT_APP_ID, "ribalta");
#ifdef _WIN32
    /* icona della finestra e della barra delle applicazioni: risorsa con id 1 di ribalta.rc */
    SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON, "1");
    SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON_SMALL, "1");
#endif
    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");
    /* "gpu" (SDL_GPU: Vulkan su Linux, Metal su macOS) misurato su Linux: ~17 MB PSS contro
     * ~37 MB di opengl. La variabile d'ambiente SDL_RENDER_DRIVER ha comunque la precedenza. */
#if defined(__APPLE__)
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "gpu,metal,opengl,software");
#elif defined(_WIN32)
    /* ordine provvisorio: la scelta definitiva si fa misurando RAM e CPU su Windows (step 7) */
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "gpu,direct3d11,direct3d12,opengl,software");
#else
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "gpu,opengl,opengles2,software");
#endif
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
    notify_set_window(a->win);
    if (!a->screensaver) {
        a->ui = ui_settings_create(a->ren, a->bundled_font);
        a->sui = ui_stats_create(a->ren, a->bundled_font);
        a->cui = ui_color_create(a->ren, a->bundled_font);
        a->fui = ui_font_create(a->ren, a->bundled_font, a->bundled_font);
        a->subj = ui_subject_create(a->ren, a->bundled_font);
        a->hui = ui_help_create(a->ren, a->bundled_font);
    }
    display_update(a, SDL_GetTicksNS());
    a->dirty = true;
    a->start_ns = SDL_GetTicksNS();
    if (a->screensaver) {
        SDL_HideCursor();
        a->cursor_hidden = true;
    } else {
        a->hint_visible = true; /* suggerimento "H · legenda" all'avvio */
        a->hint_deadline = a->start_ns + HINT_NS;
        if (a->cfg.start_fullscreen)
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
    if (a->subj)
        ui_subject_destroy(a->subj);
    if (a->hui)
        ui_help_destroy(a->hui);
    if (a->render)
        render_destroy(a->render);
    if (a->ren)
        SDL_DestroyRenderer(a->ren);
    if (a->win)
        SDL_DestroyWindow(a->win);
    TTF_Quit();
    SDL_Quit();
}

#ifdef _WIN32
/* Istanza singola: mutex con nome nella sessione dell'utente (Local\Ribalta).
 * L'handle resta aperto per tutta la vita del processo; il sistema rilascia il mutex
 * all'uscita, anche in caso di crash. Ritorna false se un'altra istanza è attiva. */
static bool acquire_single_instance(void)
{
    HANDLE h = CreateMutexW(NULL, FALSE, L"Local\\Ribalta");
    if (!h)
        return true; /* non bloccare l'avvio se il mutex non è creabile */
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(h);
        return false;
    }
    return true; /* handle lasciato aperto di proposito */
}
#else
/* Istanza singola: lock esclusivo su $XDG_RUNTIME_DIR/ribalta.lock (fallback $TMPDIR, poi /tmp;
 * su macOS XDG_RUNTIME_DIR di solito manca e TMPDIR è la cartella temporanea dell'utente).
 * Il descrittore resta aperto per tutta la vita del processo; il kernel rilascia
 * il lock all'uscita, anche in caso di crash. Ritorna false se un'altra istanza è attiva. */
static bool acquire_single_instance(void)
{
    const char *dir = getenv("XDG_RUNTIME_DIR");
    if (!dir || !*dir)
        dir = getenv("TMPDIR");
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
#endif

#ifdef _WIN32
/* Codepage di uscita della console prima di passare a UTF-8 (0 = non modificata). */
static UINT g_console_cp;

/* Rimette la codepage originale: la console è condivisa con la shell che ha lanciato Ribalta e
 * una codepage cambiata resterebbe tale anche dopo. Idempotente; registrata con atexit. */
static void console_restore_cp(void)
{
    if (g_console_cp) {
        fflush(stdout); /* il testo UTF-8 ancora in buffer va scritto prima del cambio */
        fflush(stderr);
        SetConsoleOutputCP(g_console_cp);
        g_console_cp = 0;
    }
}

/* true se stdout/stderr (f) puntano a qualcosa di valido (file, pipe o console). */
static bool std_stream_valid(DWORD which, FILE *f)
{
    HANDLE h = GetStdHandle(which);
    return h && h != INVALID_HANDLE_VALUE && _fileno(f) >= 0;
}

/* Ribalta è un'app GUI (-mwindows): lanciata da cmd o PowerShell non ha stdout/stderr, quindi
 * --version, --help, i comandi e i messaggi d'errore non stampano nulla. Se uno dei due non è
 * valido ci si aggancia alla console del processo padre (nessuna console nuova: da Esplora
 * risorse l'aggancio fallisce e non succede nulla) e lo si riapre su CONOUT$. Se l'output è
 * reindirizzato a un file o a una pipe (ribalta.exe --version > v.txt) gli handle sono validi e
 * restano quelli: la CI conta su questo. Limite noto: cmd e PowerShell non aspettano le app GUI,
 * quindi il prompt può tornare prima che il testo compaia. La console viene messa in UTF-8 (i
 * messaggi sono in italiano) fino a console_release(), poi la codepage torna com'era. */
static void console_attach(void)
{
    bool out_ok = std_stream_valid(STD_OUTPUT_HANDLE, stdout);
    bool err_ok = std_stream_valid(STD_ERROR_HANDLE, stderr);
    if ((out_ok && err_ok) || !AttachConsole(ATTACH_PARENT_PROCESS))
        return;
    if (!out_ok)
        freopen("CONOUT$", "w", stdout);
    if (!err_ok)
        freopen("CONOUT$", "w", stderr);
    UINT cp = GetConsoleOutputCP();
    if (cp && cp != CP_UTF8 && SetConsoleOutputCP(CP_UTF8)) {
        g_console_cp = cp;
        atexit(console_restore_cp);
    }
}

/* Da chiamare quando l'app sta per aprire la finestra: da qui i messaggi sono rari e in
 * inglese (SDL), e la shell che ha lanciato Ribalta ritrova la sua codepage mentre l'app gira. */
static void console_release(void)
{
    console_restore_cp();
}
#endif

#ifndef RIBALTA_VERSION
#define RIBALTA_VERSION "dev"
#endif

#define MAX_EXTRA_ARGS 32

/* Sottocomandi "update" / "uninstall": passano il controllo a share/ribalta/<nome>.sh
 * dell'installazione (make install). Non ritornano: exit 1 o execvp. */
static void run_subcommand(const char *name, int argc, char **argv)
{
    bool is_update = strcmp(name, "update") == 0;
    const char *fp = getenv("FLATPAK_ID");
    if (fp && *fp) {
        if (is_update) {
            fputs("ribalta: Ribalta è installato come Flatpak: scarica il nuovo ribalta.flatpak da "
                  "https://github.com/Ginteaa/Ribalta/releases/latest\n"
                  "e reinstallalo con  flatpak install --user ribalta.flatpak\n",
                  stderr);
        } else {
            fputs("ribalta: Ribalta è installato come Flatpak: "
                  "disinstallalo con  flatpak uninstall io.github.ginteaa.Ribalta\n",
                  stderr);
        }
        exit(1);
    }
#if defined(_WIN32)
    /* Windows: nessuno script bash da lanciare, si aggiorna e si disinstalla a mano */
    (void)argc;
    (void)argv;
    if (is_update)
        fputs("ribalta: scarica l'ultima versione per Windows (zip portatile o installer) da "
              "https://github.com/Ginteaa/Ribalta/releases/latest\n",
              stderr);
    else
        fputs("ribalta: disinstalla Ribalta da Impostazioni → App (se l'hai installato con "
              "l'installer) oppure elimina la cartella di Ribalta (versione portatile); "
              "impostazioni e statistiche restano\n",
              stderr);
    exit(1);
#elif defined(__APPLE__)
    (void)argc;
    (void)argv;
    if (is_update)
        fputs("ribalta: scarica l'ultimo Ribalta.app da https://github.com/Ginteaa/Ribalta/releases/latest "
              "oppure, se l'hai compilato dai sorgenti, usa  git pull && make install  nel repository\n",
              stderr);
    else
        fputs("ribalta: rimuovi Ribalta.app da Applicazioni "
              "(impostazioni e statistiche restano in ~/Library/Application Support/Ribalta)\n",
              stderr);
    exit(1);
#else
    char exe[1024] = "", prefix[1024], script[1100];
    size_t el = 0;
    bool ok = paths_exe_dir(exe, sizeof exe);
    if (ok) {
        el = strlen(exe);
        ok = el > 4 && strcmp(exe + el - 4, "/bin") == 0;
    }
    if (ok) {
        memcpy(prefix, exe, el - 4);
        prefix[el - 4] = '\0';
        ok = snprintf(script, sizeof script, "%s/share/ribalta/%s.sh", prefix, name) < (int)sizeof script
             && paths_readable(script);
    }
    if (!ok) {
        fprintf(stderr, "ribalta: Questa copia di Ribalta non risulta installata con make install (%s): "
                        "nel repository usa  %s\n",
                exe, is_update ? "git pull && make install" : "make uninstall");
        exit(1);
    }
    if (argc - 2 > MAX_EXTRA_ARGS) {
        fprintf(stderr, "ribalta: troppi argomenti per %s\n", name);
        exit(1);
    }
    const char *args[MAX_EXTRA_ARGS + 7];
    int n = 0;
    args[n++] = "bash";
    args[n++] = script;
    args[n++] = "--prefix";
    args[n++] = prefix;
    args[n++] = "--current";
    args[n++] = RIBALTA_VERSION;
    for (int i = 2; i < argc; i++)
        args[n++] = argv[i];
    args[n] = NULL;
    execvp("bash", (char *const *)args);
    perror("ribalta: bash");
    exit(1);
#endif
}

static void usage(FILE *out)
{
    fputs("Uso: ribalta [opzioni]\n"
          "\n"
          "Orologio flip con timer di studio, Pomodoro e statistiche.\n"
          "\n"
          "Opzioni:\n"
          "  --screensaver   schermo intero, esce con tasto, clic o movimento del mouse\n"
          "  -V, --version   mostra la versione\n"
          "  -h, --help      mostra questo aiuto\n"
          "\n"
          "Comandi:\n"
          "  update [--check] [-y]   aggiorna all'ultima versione (--check: controlla soltanto)\n"
          "  uninstall [-y]          disinstalla (impostazioni e statistiche restano)\n"
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
#ifdef _WIN32
    console_attach();
#endif
    if (argc > 1 && (strcmp(argv[1], "update") == 0 || strcmp(argv[1], "uninstall") == 0))
        run_subcommand(argv[1], argc, argv);
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--screensaver") == 0) {
            screensaver = true;
        } else if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-V") == 0) {
            printf("ribalta %s\n", RIBALTA_VERSION);
            return 0;
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
        if (paths_readable(app.cfg.font_file))
            snprintf(app.render_font, sizeof app.render_font, "%s", app.cfg.font_file);
        else
            fprintf(stderr, "ribalta: font non trovato: %s, uso il font incluso\n", app.cfg.font_file);
    }
    int rc = 1;
#ifdef _WIN32
    console_release();
#endif
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
