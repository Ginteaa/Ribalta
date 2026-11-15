# 03 — Technical architecture

## 1. Stack

| Component | Choice | Reason |
|---|---|---|
| Language | **C17** | no runtime, small binary, full control over memory |
| Window / rendering / input / audio | **SDL3** (3.4.x at the time of writing) | native Wayland backend, GPU renderer, a single dependency |
| Font rasterisation | **SDL3_ttf** (3.2.x) | simple API; used only to generate textures once at startup/resize. The font is closed after generation to free memory |
| Font list | **fontconfig** (loaded with `dlopen`) | lists installed scalable fonts that contain the digits; loaded **only while the font menu is open** (or on ←/→ in the settings row), then released with `FcFini`. Startup opens `font_file` directly, without fontconfig |
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
> **Phase 5 measurements** (font menu, `gpu` renderer, PSS):
>
> | Situation | PSS |
> |---|---|
> | Idle at startup | ~18 MB (unchanged; no fontconfig at startup) |
> | Font menu open (127 families listed) | ~23.5 MB |
> | After closing the menu | ~20 MB (`FcFini` + `malloc_trim`); opening/closing the settings panel alone already settled at ~22 MB before this phase |
> | After switching to another font | +1–2 MB, stable on repeated switches |
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
   Changing the digit font (`render_set_font`) rebuilds glyphs and faces once; the font menu defers
   the live preview with a wait-timeout deadline (`font_preview_at`, ~150 ms), never with polling.
   The font list and the menu fonts exist only while the menu is open; `fonts_free` calls `FcFini`
   and `malloc_trim(0)` so the memory goes back to the system (see the Phase 5 measurements).
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
├── src/
│   ├── main.c              CLI parsing, SDL init/shutdown, main loop, wait-timeout selection
│   ├── app/
│   │   ├── app.h           App struct, views, display model, shared constants
│   │   ├── input.c/.h      keyboard and mouse handling, routing to the overlays
│   │   ├── display.c/.h    builds and applies the on-screen model (tiles, colours, labels)
│   │   └── session.c/.h    end of timer/Pomodoro, statistics logging, screen-lock handling
│   ├── core/               pure logic, testable without a window
│   │   ├── clock  timer  pomodoro  stats  settings  color   (.c/.h)
│   ├── platform/           OS integration
│   │   ├── notify.c/.h     notify-send (Linux) / osascript (macOS) + sound
│   │   ├── paths.c/.h      config/data directories (XDG; macOS: Application Support), asset lookup
│   │   ├── fonts.h         installed font list API; no state kept
│   │   ├── fonts.c         Linux: fontconfig (dlopen'd while needed)
│   │   ├── fonts_mac.c     macOS: CoreText
│   │   ├── fonts_common.c  sorting, lookup and ring stepping shared by both
│   │   └── gpu.c/.h        Flatpak only: load just the Vulkan drivers for the installed GPUs
│   ├── render/
│   │   └── render.c/.h     layout, digit/tile textures, flip animation, progress bar
│   └── ui/                 overlay panels
│       ├── ui_settings.c/.h
│       ├── ui_stats.c/.h
│       ├── ui_color.c/.h
│       ├── ui_font.c/.h    digit-font menu (search field, list, preview)
│       └── ui_subject.c/.h subjects menu (list, add with text input, remove)
├── assets/                 font, licence, chime, icon source
│   ├── icons/              PNG icons (16 ... 512 px)
│   └── ribalta.desktop     launcher entry
├── tests/                  unit tests run by `make test` and the CI
├── tools/
│   ├── install.sh          `make install`: build, install, progress UI
│   ├── uninstall.sh        `make uninstall`: removes only installed files, keeps user data
│   ├── icons.sh            regenerates the PNG icons from the SVG
│   └── macos-app.sh        `make app` on macOS: builds build/Ribalta.app (icon, bundled libs, ad-hoc signature)
├── packaging/flatpak/      Flatpak manifest and AppStream metainfo
├── packaging/macos/        Info.plist template for Ribalta.app
├── docs/                   specifications and design notes
└── .github/workflows/      CI (build, tests, install/uninstall smoke test, Flatpak bundle, macOS app)
```

Folder roles:

- `src/app/`: application state and behaviour that ties the other layers together.
- `src/core/`: logic with no SDL window or I/O dependency (time, timer, Pomodoro, statistics, settings, colours).
- `src/platform/`: everything that talks to the operating system (notifications, directories).
- `src/render/`: drawing only; textures and animation.
- `src/ui/`: settings, statistics, colour-picker and font-menu overlays.
- `assets/`, `tests/`, `tools/`, `docs/`: resources, unit tests, install/maintenance scripts, documentation.

Conventions: headers live next to their `.c`; includes are written from `src/`
(e.g. `#include "core/timer.h"`). `make` prints short lines (`  CC   core/timer.c`);
use `make V=1` to see the full compiler commands.

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

## Packaging

- **macOS** (experimental): the Makefile detects Darwin (`uname -s`) and then uses
  `-D_DARWIN_C_SOURCE`, builds `fonts_mac.c` instead of `fonts.c`, and links CoreText and
  CoreFoundation. Platform branches in the code: no video-driver hint (SDL picks Cocoa), renderer
  order `gpu,metal,opengl,software`, single-instance lock in `$TMPDIR`, notifications through
  `osascript`, config and data in `~/Library/Application Support/Ribalta/` (XDG variables still win,
  e.g. in tests). `make app` runs `tools/macos-app.sh`: `Contents/MacOS/ribalta`,
  `Contents/Resources/assets/` (found through `SDL_GetBasePath()`), an `.icns` made with `iconutil`,
  optional `BUNDLE_LIBS=1` (`dylibbundler` copies Homebrew's dylibs into `Contents/Frameworks`) and an
  ad-hoc `codesign`. The `macos.yml` workflow builds on `macos-14` (arm64) and `macos-15-intel`, runs
  the tests and an offscreen start, checks that no Homebrew path is left in the bundle, uploads the
  zips and attaches them to `v*` releases. No notarisation (no paid developer account).
  Resource figures on macOS have not been measured yet.

- **Flatpak**: `packaging/flatpak/io.github.ginteaa.Ribalta.yml` builds with `make release` and
  `make install-files PREFIX=/app APPID=io.github.ginteaa.Ribalta`; the AppStream metainfo is
  installed next to it. The CI workflow `flatpak.yml` produces the `ribalta.flatpak` bundle.
- **Dependencies**: the `org.freedesktop.Platform` 25.08 runtime ships fontconfig and
  `notify-send`. **SDL3 3.4.20** is built by the manifest as a shared library (no joystick, haptic,
  hidapi, sensor, camera): the runtime's SDL 3.2.30 crashes in `SDL_RenderPresent` with the `gpu`
  (Vulkan) renderer as soon as the window is resized or maximised. FreeType 2.13.3 and SDL3_ttf 3.2.2 are built by the manifest as **static, minimal**
  libraries (no harfbuzz, png, brotli, bzip2, zlib, plutosvg): the runtime's SDL3_ttf would load
  harfbuzz → glib, pcre2, graphite, png, brotli at startup (~3 MB). Ribalta only draws digits and
  Latin labels, so no shaping is needed. The app is linked with `PKGFLAGS=--static` and
  `-Wl,--as-needed`.
- **fontconfig is loaded with `dlopen`** (`fonts.c`) only while the font list is needed, and
  `dlclose`d afterwards; it is not a link-time dependency (headers only at build time).
- **Permissions** (`finish-args`):
  - `--socket=wayland`, `--socket=fallback-x11`: show the window on Wayland, or on X11 as fallback.
  - `--share=ipc`: required by X11 shared memory.
  - `--device=dri`: GPU rendering for the SDL renderer.
  - `--socket=pulseaudio`: play the end-of-timer chime.
  - `--talk-name=org.freedesktop.Notifications`: desktop notifications via `notify-send`.
  - `--env=SDL_APP_ID=io.github.ginteaa.Ribalta`: Wayland app_id / X11 class equal to the app id,
    so the launcher shows the right icon and name.
- **Asset lookup order** (`paths.c`): `$RIBALTA_DATA`, `./assets`, `<exe dir>/assets`,
  `<exe dir>/../share/ribalta` (prefix installs such as `/app`), `$XDG_DATA_HOME/ribalta`,
  `/usr/share/ribalta`.
- **`make install-files`** (for packagers): plain, quiet `install` commands for the binary, assets,
  icons and desktop file, honouring `PREFIX`, `DESTDIR` and `APPID` (renames the desktop file and
  icons, and sets `Icon=`/`StartupWMClass=`). It does not compile: run `make` or `make release` first.
  `make install` stays the interactive installer (`tools/install.sh`).
- **Vulkan drivers in the sandbox** (`platform/gpu.c`): the runtime's GL extension ships every Mesa
  Vulkan driver (radeon, lvp, nouveau, …) and the Vulkan loader loads all of them, including
  `libLLVM`. Inside Flatpak (`$FLATPAK_ID` set) Ribalta reads the PCI vendors from
  `/sys/class/drm/card*/device/vendor` and sets `VK_LOADER_DRIVERS_SELECT` to the matching drivers
  only, before `SDL_Init`. It does nothing if the user already set `VK_LOADER_DRIVERS_SELECT`,
  `VK_DRIVER_FILES` or `VK_ICD_FILENAMES`, or if no vendor is recognised.
- **Notifications** inside the sandbox go through the notification portal (libnotify does this
  automatically); the portal ignores the app name and icon passed to `notify-send`.

> **Flatpak measurements** (Intel, `gpu` renderer, idle clock, PSS):
>
> | Situation | PSS | RSS |
> |---|---|---|
> | All Mesa Vulkan drivers loaded (no filter) | ~70 MB | — |
> | `VK_LOADER_DRIVERS_SELECT=*intel*` (automatic) | ~27.5 MB | ~27.5 MB |
> | + static minimal FreeType/SDL3_ttf, fontconfig via `dlopen` | **~24.9 MB** | ~24.9 MB |
> | + bundled SDL 3.4.20 (same-session measurement: 21.9 → 22.1 MB) | ~+0.2 MB | — |
> | Native build, for comparison | ~18.8 MB | ~28.4 MB |
>
> RSS is lower than native; PSS is higher only because the runtime's libraries (Mesa Vulkan driver,
> libc, libstdc++, SDL3 …) are not shared with host processes. The remaining gap is mostly
> unavoidable: the runtime's Intel Vulkan driver alone is +2.3 MB, libc +1.8 MB, libstdc++ (needed
> by Mesa) +1.4 MB. Other renderers are worse (opengl/opengles2/software load EGL and `libLLVM`,
> 80–97 MB). CPU at idle: ~20 ms in 65 s.

## 4. Makefile

```make
CC      ?= cc
PKGS    := sdl3 sdl3-ttf fontconfig
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
