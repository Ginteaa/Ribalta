# 03 — Technical architecture

## 1. Stack

| Component | Choice | Reason |
|---|---|---|
| Language | **C17** | no runtime, small binary, full control over memory |
| Window / rendering / input / audio | **SDL3** (3.4.x at the time of writing) | native Wayland backend, GPU renderer, a single dependency |
| Font rasterisation | **SDL3_ttf** (3.2.x) | simple API; used only to generate textures once at startup/resize. The font is closed after generation to free memory |
| Notifications | `notify-send` process (`fork`+`execvp`) | avoids linking libnotify/GLib (several MB of RAM) |
| Build | **Makefile** + `pkg-config` | simple, no extra build system |

### Comparison (indicative steady-state RAM)

| Option | RAM | CPU at rest | Notes |
|---|---|---|---|
| **C + SDL3 (chosen)** | ~8–15 MB | ~0% | good trade-off between lightness and simplicity |
| C + Wayland + Cairo directly | ~3–6 MB | ~0% | much more code (xdg-shell protocols, shm buffers, input) |
| C + GTK4 | ~40–70 MB | low | GTK4 loads GL, CSS, GLib/GObject |
| Tauri / webview | ~80–150 MB | medium | a full web engine |
| Electron | ~150–300 MB | medium/high | a whole Chromium |

> **Phase 1 measurements** (Wayland/niri, 65 s including one flip, PSS from `/proc/<pid>/smaps_rollup`):
>
> | SDL renderer | PSS | RSS | CPU in 65 s |
> |---|---|---|---|
> | `gpu` (SDL_GPU / Vulkan) — **default** | ~17 MB | ~27 MB | ~50 ms |
> | `opengl` | ~37 MB | ~103 MB | ~40 ms |
> | `software` | RSS ~113 MB (still loads EGL) | | |
>
> **Phase 3 measurements** (`gpu` renderer, window visible):
>
> | Situation | CPU |
> |---|---|
> | Clock HH:MM (one flip per minute) | ~40 ms per minute (~0.07%) |
> | Seconds visible (clock with seconds, timer/Pomodoro under one hour) | ~2.2% (was ~3% at 60 fps) |
> | Window not in view (e.g. another column in niri) | ~0.15% even with seconds |
>
> Measured cost ≈ 1.2–1.4 ms of CPU per frame. For the absolute minimum: `show_seconds=false`
> or `animations=false`.
>
> Renderer order: `gpu,opengl,opengles2,software`; `SDL_RENDER_DRIVER=<name>` overrides it.
> Release binary: ~60 KB (stripped, LTO; the ~27 KB figure measured in Phase 1 predates the
> later features).

## 2. Strategy for using very few resources

1. **Event-driven loop, not fixed frames.**
   When nothing is animating the main thread calls
   `SDL_WaitEventTimeout(&ev, ms_to_next_change)`: the process sleeps until the next
   minute (or second, if visible) or an input event. No polling at 60 fps.
2. **Render only when needed.** Redraw only if: an animation starts/continues, the window
   is resized/exposed, a panel opens. During a flip the animation is capped at
   **30 fps** (the loop sleeps in `SDL_WaitEventTimeout` between frames, vsync on):
   ~12 frames per flip, then back to sleep.
3. **Pre-rendered textures.** At startup (and on every resize) the textures for the digits
   `0–9` are generated at the exact size with SDL3_ttf, plus the empty tile texture. During a flip
   only rectangles already on the GPU are composited: zero rasterisation per frame.
   Textures are regenerated only on resize (debounced by 150 ms).
4. **Drift-free timekeeping.**
   - Clock: `clock_gettime(CLOCK_REALTIME)` + `localtime_r`; the wait timeout is computed
     up to the next minute/second boundary, +5 ms margin.
   - Timer: `SDL_GetTicksNS()` (monotonic); the end instant is stored instead of decrementing a counter.
5. **No extra threads**, no dynamic allocation in the main loop.
6. **SDL hints**: `SDL_HINT_VIDEO_DRIVER="wayland,x11"`, `SDL_HINT_APP_ID="ribalta"`,
   `SDL_HINT_VIDEO_ALLOW_SCREENSAVER="1"` (except while a timer is running, Phase 3).
7. **Release build** with `-O2`, `-flto`, `-s` (strip) for a small binary.

Checking resource use: `ps -o rss,pcpu -C ribalta`, `pidstat -p <pid> 1`, `/usr/bin/time -v`.

## 3. Repository layout

```
ribalta/
├── Makefile
├── README.md
├── docs/
├── assets/
│   ├── font.ttf          (IBM Plex Sans Condensed Bold, OFL) + OFL.txt
│   └── ding.wav          (end-of-timer sound, short, royalty-free)
└── src/
    ├── main.c            SDL init, event loop, key dispatch, timeout selection
    ├── app.h             shared App struct (global state, mode)
    ├── clock.c/.h        local time → tile values, next change boundary
    ├── timer.c/.h        timer state machine (SETUP/RUNNING/PAUSED/FINISHED)
    ├── render.c/.h       layout, digit/tile textures, flip animation, progress bar
    ├── font.c/.h         TTF loading, glyph rasterisation with SDL3_ttf → SDL_Texture
    ├── settings.c/.h     read/write config.ini (XDG)
    ├── ui_settings.c/.h  settings panel (overlay)
    └── notify.c/.h       notify-send + SDL sound
```

(This is the original Phase 1–2 layout; later phases added further modules, e.g. for the Pomodoro
and statistics.)

Asset files are searched in this order: `$RIBALTA_DATA`, `./assets`, `<executable dir>/assets`,
`~/.local/share/ribalta` (`$XDG_DATA_HOME/ribalta`), `/usr/share/ribalta`.

### Main interfaces (draft)

```c
/* app.h */
typedef enum { MODE_CLOCK, MODE_TIMER } Mode;

typedef struct {
    int  value;          /* 0..99 shown on the tile, -1 = blank */
    int  prev_value;
    Uint64 anim_start_ns;/* 0 = no animation */
} Tile;

typedef struct {
    SDL_Window   *win;
    SDL_Renderer *ren;
    Settings      cfg;
    Mode          mode;
    Timer         timer;
    Tile          tiles[3];
    int           tile_count;     /* 2 or 3 */
    bool          settings_open;
    bool          dirty;          /* a redraw is needed */
} App;

/* render.h */
bool render_init(App *a);                /* creates textures */
void render_resize(App *a);              /* regenerates textures at the new size */
bool render_frame(App *a, Uint64 now);   /* true if an animation is still running */
void tile_set(Tile *t, int v, Uint64 now, bool animate);

/* clock.h */
void   clock_values(const Settings *s, int out[3], bool *pm);
Uint32 clock_ms_to_next_change(const Settings *s);
```

## 4. Makefile

```make
CC      ?= cc
PKGS    := sdl3 sdl3-ttf
CFLAGS  ?= -std=c17 -Wall -Wextra -Wpedantic -O2
CFLAGS  += $(shell pkg-config --cflags $(PKGS))
LDLIBS  += $(shell pkg-config --libs $(PKGS)) -lm
SRC     := $(wildcard src/*.c)
OBJ     := $(SRC:src/%.c=build/%.o)
BIN     := ribalta
PREFIX  ?= $(HOME)/.local

all: $(BIN)
$(BIN): $(OBJ)            ; $(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)
build/%.o: src/%.c | build ; $(CC) $(CFLAGS) -MMD -c -o $@ $<
build:                    ; mkdir -p build

debug:   CFLAGS += -O0 -g -fsanitize=address,undefined
debug:   LDFLAGS += -fsanitize=address,undefined
debug:   clean $(BIN)
release: CFLAGS += -flto -DNDEBUG
release: LDFLAGS += -flto -s
release: clean $(BIN)

install: release
	install -Dm755 $(BIN) $(PREFIX)/bin/$(BIN)
	install -Dm644 -t $(PREFIX)/share/ribalta assets/*
	install -Dm644 ribalta.desktop $(PREFIX)/share/applications/ribalta.desktop
uninstall:
	rm -rf $(PREFIX)/bin/$(BIN) $(PREFIX)/share/ribalta $(PREFIX)/share/applications/ribalta.desktop
clean:
	rm -rf build $(BIN)

-include $(OBJ:.o=.d)
.PHONY: all debug release install uninstall clean
```

(This is a condensed sketch: recipes are shown on one line for readability. The real `Makefile`
in the repository is the reference.)

## 5. Development with agents

- **Orchestrator — Claude Opus 5.5** (main session): maintains the specs in `docs/`,
  splits each phase into per-module tasks, defines the interfaces (`*.h`) before the code, reviews,
  integrates, builds with `make` / `make debug` and measures RAM/CPU.
- **Implementation — Claude Sonnet 5.5 subagents** (`model: "sonnet"`): one per task/module
  (e.g. `settings.c`, `render.c` + `font.c`, `timer.c`). Each prompt names the `docs/` files to read,
  the header to respect and the acceptance criteria (compiles without warnings, expected behaviour).
- Independent tasks run in parallel; those touching the same files run sequentially.
- After each phase: `make debug` (ASan/UBSan), manual run, resource measurement, docs update.
