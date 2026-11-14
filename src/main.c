#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include "clock.h"
#include "render.h"
#include "settings.h"

#define CURSOR_HIDE_NS (2000ULL * SDL_NS_PER_MS)

typedef struct {
    SDL_Window   *win;
    SDL_Renderer *ren;
    Render       *render;
    Settings      cfg;
    ClockFace     face;
    bool          running;
    bool          dirty;
    bool          resized;           /* resize da applicare (coalesced) */
    bool          was_animating;
    bool          cursor_hidden;
    Uint64        cursor_deadline;   /* 0 = nessuna scadenza pendente */
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

static void on_key(App *a, const SDL_KeyboardEvent *k)
{
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
    case SDLK_Q:
        a->running = false;
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
    case SDL_EVENT_KEY_DOWN:
        on_key(a, &ev->key);
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (ev->button.button == SDL_BUTTON_LEFT && ev->button.clicks == 2)
            toggle_fullscreen(a);
        break;
    case SDL_EVENT_MOUSE_MOTION:
        cursor_activity(a);
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

static void apply_face(App *a, const ClockFace *f, bool animate)
{
    render_set_tiles(a->render, f->values, f->count, f->hide_leading_zero, animate,
                     SDL_GetTicksNS());
    render_set_ampm(a->render, f->ampm);
    a->face = *f;
}

static void update_clock(App *a)
{
    ClockFace f;
    clock_get(&a->cfg, &f);
    if (!face_equal(&f, &a->face)) {
        apply_face(a, &f, a->cfg.animations);
        a->dirty = true;
    }
}

static void apply_resize(App *a)
{
    int w = 0, h = 0;
    SDL_GetCurrentRenderOutputSize(a->ren, &w, &h);
    if (w > 0 && h > 0)
        render_resize(a->render, w, h);
    a->resized = false;
}

/* Timeout di attesa in ms: prossimo cambio d'ora o scadenza cursore. */
static int32_t wait_timeout_ms(const App *a, Uint64 now)
{
    uint32_t ms = clock_ms_to_next_change(&a->cfg);
    if (a->cursor_deadline) {
        Uint64 left = a->cursor_deadline > now ? a->cursor_deadline - now : 0;
        uint32_t c = (uint32_t)((left + SDL_NS_PER_MS - 1) / SDL_NS_PER_MS);
        if (c < ms)
            ms = c;
    }
    return ms > 0x7fffffffu ? 0x7fffffff : (int32_t)ms;
}

static void main_loop(App *a)
{
    while (a->running) {
        Uint64 now = SDL_GetTicksNS();
        if (render_is_animating(a->render, now)) {
            drain_events(a);
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
        cursor_tick(a, SDL_GetTicksNS());
        update_clock(a);

        now = SDL_GetTicksNS();
        bool animating = render_is_animating(a->render, now);
        /* un ultimo frame dopo la fine dell'animazione */
        if (animating || a->was_animating)
            a->dirty = true;
        a->was_animating = animating;

        if (a->dirty) {
            render_draw(a->render, now);
            SDL_RenderPresent(a->ren);
            a->dirty = false;
        }
    }
}

static bool app_init(void)
{
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "wayland,x11");
    SDL_SetHint(SDL_HINT_APP_ID, "flipclock");
    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");
    /* "gpu" (SDL_GPU/Vulkan) misurato: ~17 MB PSS contro ~37 MB di opengl.
     * La variabile d'ambiente SDL_RENDER_DRIVER ha comunque la precedenza. */
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "gpu,opengl,opengles2,software");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "flipclock: SDL_Init: %s\n", SDL_GetError());
        return false;
    }
    if (!TTF_Init()) {
        fprintf(stderr, "flipclock: TTF_Init: %s\n", SDL_GetError());
        return false;
    }
    return true;
}

static bool app_open(App *a, const char *font_path)
{
    SDL_WindowFlags fl = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (a->cfg.start_fullscreen)
        fl |= SDL_WINDOW_FULLSCREEN;
    a->win = SDL_CreateWindow("flipclock", 960, 400, fl);
    if (!a->win) {
        fprintf(stderr, "flipclock: SDL_CreateWindow: %s\n", SDL_GetError());
        return false;
    }
    a->ren = SDL_CreateRenderer(a->win, NULL);
    if (!a->ren) {
        fprintf(stderr, "flipclock: SDL_CreateRenderer: %s\n", SDL_GetError());
        return false;
    }
    SDL_SetRenderVSync(a->ren, 1);
    if (getenv("FLIPCLOCK_DEBUG"))
        fprintf(stderr, "flipclock: renderer=%s\n", SDL_GetRendererName(a->ren));

    a->render = render_create(a->ren, font_path);
    if (!a->render) {
        fprintf(stderr, "flipclock: render_create failed (%s)\n", font_path);
        return false;
    }
    render_set_style(a->render, a->cfg.scale, a->cfg.brightness);
    apply_resize(a);
    ClockFace f;
    clock_get(&a->cfg, &f);
    apply_face(a, &f, false);
    a->dirty = true;
    if (a->cfg.start_fullscreen)
        cursor_activity(a);
    return true;
}

static void app_close(App *a)
{
    if (a->render)
        render_destroy(a->render);
    if (a->ren)
        SDL_DestroyRenderer(a->ren);
    if (a->win)
        SDL_DestroyWindow(a->win);
    TTF_Quit();
    SDL_Quit();
}

/* Istanza singola: lock esclusivo su $XDG_RUNTIME_DIR/flipclock.lock (fallback /tmp).
 * Il descrittore resta aperto per tutta la vita del processo; il kernel rilascia
 * il lock all'uscita, anche in caso di crash. Ritorna false se un'altra istanza è attiva. */
static bool acquire_single_instance(void)
{
    const char *dir = getenv("XDG_RUNTIME_DIR");
    char path[512];
    snprintf(path, sizeof path, "%s/flipclock.lock", (dir && *dir) ? dir : "/tmp");
    int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (fd < 0)
        return true; /* non bloccare l'avvio se il lock non è creabile */
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        close(fd);
        return false;
    }
    return true; /* fd lasciato aperto di proposito */
}

int main(void)
{
    if (!acquire_single_instance()) {
        fprintf(stderr, "flipclock: già in esecuzione\n");
        return 0;
    }

    App app;
    memset(&app, 0, sizeof app);
    app.running = true;
    settings_load(&app.cfg);

    char font[1024];
    if (!settings_find_asset("font.ttf", font, sizeof font)) {
        fprintf(stderr, "flipclock: font.ttf not found (searched $FLIPCLOCK_DATA, ./assets, "
                        "<exe>/assets, ~/.local/share/flipclock, /usr/share/flipclock)\n");
        return 1;
    }
    int rc = 1;
    if (app_init() && app_open(&app, font)) {
        main_loop(&app);
        rc = 0;
    }
    app_close(&app);
    return rc;
}
