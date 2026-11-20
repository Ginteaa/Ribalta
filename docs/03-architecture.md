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
> **0.7.0 measurements** (Wayland/niri, `gpu` renderer, idle clock, 65 s, same session, 0.6.0 →
> 0.7.0): PSS 17.8 → 17.7 MB, RSS 27.4 → 27.3 MB, CPU from 5 to 65 s ≤ 10 ms (one clock tick).
> Break colour, subject change, goals and the terminal commands cost nothing at idle.
>
> **Phase 13 measurements** (same setup, 0.7.0 → legend build): PSS 17.6 → 17.9 MB, CPU from 5 to 65 s
> ≤ 30 ms in both (the startup hint hides after 3 s and then nothing wakes up). The legend costs
> ~+2.2 MB only while it is open; after closing it PSS is back within 0.05 MB.
>
> Renderer order: `gpu,opengl,opengles2,software`; `SDL_RENDER_DRIVER=<name> ribalta` overrides it
> (OpenGL uses noticeably more memory, see the Phase 1 table).
> Release binary: ~60 KB (stripped, LTO; the ~27 KB figure measured in Phase 1 predates the
> later features).

## 2. Strategy for using very few resources

1. **Event-driven loop, not fixed frames.**
   When nothing is animating the main thread calls
   `SDL_WaitEventTimeout(&ev, ms_to_next_change)`: the process sleeps until the next
   minute (or second, if visible), the legend-hint deadline (3 s after startup or a mouse movement),
   the cursor-hide and font-preview deadlines, or an input event. No polling at 60 fps.
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
8. **Load on demand.** The overlay font (legend, settings, statistics, colour picker, font menu), the
   audio for the chime and fontconfig exist only while they are needed. The font list is freed when
   the font menu closes (`FcFini`, `malloc_trim`); the legend's font is opened only while it is open.
9. **No libnotify.** Notifications run `notify-send` (or `osascript` on macOS) through `fork`+`execvp`,
   only when a session, a Pomodoro phase or a goal ends; no background process is kept.

**Tips for the absolute minimum**: `show_seconds=false` removes the per-second wake-ups and
`animations=false` removes the flip frames (the digits change at once). Both are settings in the panel
(`S`) or in `config.ini`. To try another renderer, run `SDL_RENDER_DRIVER=<name> ribalta`; the default
`gpu` is the lightest of the four measured.

Checking resource use: `ps -o rss,pcpu -C ribalta`, `pidstat -p <pid> 1`, `/usr/bin/time -v`.

## 3. Repository layout

```
ribalta/
├── Makefile
├── VERSION                 release version (e.g. 0.7.0): the only source, read by the Makefile
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
│   │   └── render.c/.h     layout, digit/tile textures, flip animation, progress bar, hint
│   └── ui/                 overlay panels
│       ├── ui_help.c/.h     legend overlay (H / F1): keys and file paths, 2 or 1 columns
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
│   ├── uninstall.sh        `make uninstall` / `ribalta uninstall`: removes only installed files, keeps user data
│   ├── update.sh           `ribalta update [--check]`: latest GitHub release, build, install in the same prefix
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
- `src/ui/`: settings, statistics, legend, colour-picker, font-menu and subjects overlays.
- The legend hint (`render_set_hint`) is drawn by `render/`; its 3 s deadline (`hint_deadline`) is
  handled by `app/input.c` and waits in the main loop like the other deadlines.
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

- **macOS install**: users download `Ribalta-macos-arm64.zip` (Apple Silicon) or
  `Ribalta-macos-x86_64.zip` (Intel) from the latest release (SDL3 bundled). The app is ad hoc signed
  but **not notarised** (that needs a paid Apple Developer account), so the first launch is blocked:
  open it once, then *System Settings → Privacy & Security → Open Anyway*, or run
  `xattr -dr com.apple.quarantine /Applications/Ribalta.app`.
  **Build from source** (a locally built app is not quarantined), with Homebrew and the Xcode
  command-line tools: `brew install sdl3 sdl3_ttf pkg-config`, clone the repository, then `make install`,
  which builds `build/Ribalta.app` and copies it to `~/Applications`. `make app` only builds
  `build/Ribalta.app`. Such a bundle links Homebrew's SDL3, so it runs on the Mac that built it;
  `BUNDLE_LIBS=1 make app` (needs `brew install dylibbundler`) copies the libraries inside, as the CI
  does. Notifications (`osascript`) are listed by macOS under *Script Editor*.
- **Flatpak**: `packaging/flatpak/io.github.ginteaa.Ribalta.yml` builds with `make release` and
  `make install-files PREFIX=/app APPID=io.github.ginteaa.Ribalta`; the AppStream metainfo is
  installed next to it. The CI workflow `flatpak.yml` produces the `ribalta.flatpak` bundle on every
  run, and on `v*` tags attaches it to the GitHub release (`softprops/action-gh-release`).
- **Flatpak install** (users): Ribalta is not on Flathub yet. On Ubuntu install Flatpak first
  (`sudo apt install flatpak`; Mint already has it), then
  `flatpak remote-add --user --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo`.
  Download `ribalta.flatpak` from the [latest release](https://github.com/Ginteaa/Ribalta/releases/latest)
  and run `flatpak install --user ribalta.flatpak` (the runtime is fetched from Flathub), then
  `flatpak run io.github.ginteaa.Ribalta`. Before a release exists, the CI artifact of the latest
  successful *Flatpak* workflow run (`ribalta-x86_64.flatpak`, a zip that contains `ribalta.flatpak`)
  does the same. Updating: download the new bundle and run `flatpak install --user ribalta.flatpak`
  again; settings and statistics are kept. `flatpak update` does not update a bundle install.
  Uninstall: `flatpak uninstall io.github.ginteaa.Ribalta`.
- **Flatpak sandbox paths**: `~/.var/app/io.github.ginteaa.Ribalta/config/ribalta/config.ini` and
  `~/.var/app/io.github.ginteaa.Ribalta/data/ribalta/sessions.csv`. The system fonts are visible to the
  digit-font menu.
- **Flatpak local build**: `flatpak install --user flathub org.freedesktop.Sdk//25.08`, then
  `flatpak-builder --user --install --force-clean build-flatpak packaging/flatpak/io.github.ginteaa.Ribalta.yml`.
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

The real `Makefile` reads the version from `VERSION` (`VERSION := $(shell cat VERSION ...)`) and
passes it to the compiler as `RIBALTA_VERSION`, so `ribalta --version` and the release tag share one
source. `ribalta update` and `ribalta uninstall` are not Makefile targets: the binary runs
`tools/update.sh` or `tools/uninstall.sh` from `<prefix>/share/ribalta/` with `execvp`.

### 4.1 Dependencies and install

**Dependencies**

- `sdl3` and `sdl3_ttf` (SDL3 ≥ 3.2 and SDL3_ttf ≥ 3.2; build and runtime).
- `fontconfig`: headers at build time. The library is `dlopen`ed only when the digit-font menu opens;
  without it the menu lists just the bundled font.
- `libnotify` (optional): provides `notify-send` for desktop notifications.
- A C compiler, `make` and `pkg-config`.

Distributions that ship SDL3 ≥ 3.2 (Arch, Fedora, openSUSE Tumbleweed…) can use the native build; the
others (Ubuntu 24.04, Mint 22) use the Flatpak. On Arch: `sudo pacman -S sdl3 sdl3_ttf fontconfig libnotify`.
On other distributions install the equivalent SDL3 and SDL3_ttf development packages (`pkg-config` must
find `sdl3`, `sdl3-ttf` and `fontconfig`).

**Targets**

- `make`: development build; `make V=1` shows the full compiler commands.
- `make release`: optimised build (LTO, stripped). `make test`, `make debug` (ASan + UBSan), `make icons`.
- `make install`: builds the release binary, then installs to `~/.local`:
  - `~/.local/bin/ribalta`;
  - `~/.local/share/applications/ribalta.desktop`;
  - the icons in `~/.local/share/icons/hicolor`;
  - the assets (font, licence, chime) in `~/.local/share/ribalta`.
  Make sure `~/.local/bin` is in your `PATH`.
- Installer output (`tools/install.sh`): in a terminal a banner and a progress bar; in logs or pipes
  plain lines. The requirement checks come first (`✓ C compiler (cc)`, `make`, `pkg-config`, `SDL3`,
  `SDL3_ttf`, `fontconfig`), then five steps, each ending with `✓`: `[1/5] Building...`,
  `[2/5] Installing binary...`, `[3/5] Installing assets...`, `[4/5] Installing icons...`,
  `[5/5] Desktop entry and icon cache...`.
- System-wide: `PREFIX=/usr make install`, as root.
- `make uninstall` (with the same `PREFIX`) removes only the installed files; settings and statistics
  are kept. `ribalta uninstall` does the same from the installed binary.
- `make install-files PREFIX=... APPID=...` installs without building (used by the Flatpak manifest and
  by packagers; see Packaging above).

## 5. Development with agents

- **Orchestrator — Claude Opus 5.5** (main session): maintains the specs in `docs/`,
  splits each phase into per-module tasks, defines the interfaces (`*.h`) before the code, reviews,
  integrates, builds with `make` / `make debug` and measures RAM/CPU.
- **Implementation — Claude Sonnet 5.5 subagents** (`model: "sonnet"`): one per task/module
  (e.g. `settings.c`, `render.c` + `font.c`, `timer.c`). Each prompt names the `docs/` files to read,
  the header to respect and the acceptance criteria (compiles without warnings, expected behaviour).
- Independent tasks run in parallel; those touching the same files run sequentially.
- After each phase: `make debug` (ASan/UBSan), manual run, resource measurement, docs update.
