# 03 — Technical architecture

## 1. Stack

| Component | Choice | Reason |
|---|---|---|
| Language | **C17** | no runtime, small binary, full control over memory |
| Window / rendering / input / audio | **SDL3** (3.4.x at the time of writing) | native Wayland backend, GPU renderer, a single dependency |
| Font rasterisation | **SDL3_ttf** (3.2.x) | simple API; used only to generate textures once at startup/resize. The font is closed after generation to free memory |
| Font list | **fontconfig** (loaded with `dlopen`) on Linux; the installed-fonts registry on Windows (`fonts_win.c`), CoreText on macOS | lists installed scalable fonts that contain the digits; loaded **only while the font menu is open** (or on ←/→ in the settings row), then released (`FcFini` on Linux). Startup opens `font_file` directly, without any font list |
| Notifications | `notify-send` process (`fork`+`execvp`) on Linux, `osascript` on macOS; on Windows `SDL_FlashWindow` (no process) | avoids linking libnotify/GLib (several MB of RAM) |
| Windows glue | Win32 named mutex (single instance), `%APPDATA%`, `windres` resources (icon, manifest, version), `-mwindows` | only a few `#ifdef _WIN32` branches; the rest is portable C17 (`platform/compat.h` for the time functions) |
| Build | **Makefile** + `pkg-config` (MSYS2 MinGW-w64 UCRT64 on Windows) | simple, no extra build system |

### Comparison (indicative steady-state RAM)

| Option | RAM | CPU at rest | Notes |
|---|---|---|---|
| **C + SDL3 (chosen)** | ~17–18 MB PSS (measured, see below) | ~0% | good trade-off between lightness and simplicity |
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

> **Windows measurements**: to be measured on a real PC (idle PSS and CPU over 65 s, as for the other
> platforms). The renderer order on Windows, `gpu,direct3d11,direct3d12,opengl,software` (`main.c`), is
> provisional until these numbers exist.

## 2. Strategy for using very few resources

1. **Event-driven loop, not fixed frames.**
   When nothing is animating the main thread calls
   `SDL_WaitEventTimeout(&ev, ms_to_next_change)`: the process sleeps until the next
   minute (or second, if visible) +5 ms, or the earliest of the other deadlines (`wait_timeout_ms` in
   `main.c`): ticks of a running timer/Pomodoro, the end-of-session blink phases (300 ms), the
   notification poll (100 ms, only while a `notify-send` child or the chime is alive; see
   `platform/notify.c`), the cursor hide (2 s, fullscreen), the legend hint (3 s after startup or a
   mouse movement) and the font-menu preview (150 ms). Otherwise it waits for an input event.
   No polling at 60 fps.
2. **Render only when needed.** Redraw only if: an animation starts/continues, the window
   is resized/exposed, a panel opens. During a flip the animation is capped at
   **30 fps** (the loop sleeps in `SDL_WaitEventTimeout` between frames, vsync on):
   ~12 frames per flip, then back to sleep.
3. **Pre-rendered textures.** At startup (and on every resize) the textures for the digits
   `0–9` are generated at the exact size with SDL3_ttf, plus the empty tile texture. During a flip
   only rectangles already on the GPU are composited: zero rasterisation per frame.
   Textures are rebuilt only when the window size changes (the resize is flagged by the event and
   applied once on the next loop pass: `main.c`, `input.c`) and when the digit font changes.
4. **Drift-free timekeeping.**
   - Clock: `timespec_get(TIME_UTC)` + `compat_localtime` (`localtime_r`, or `localtime_s` on Windows;
     `platform/compat.h`); the wait timeout is computed up to the next minute/second boundary, +5 ms margin.
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
   only when a session, a Pomodoro phase or a goal ends; no background process is kept. On Windows there
   is no process at all: the taskbar button flashes (`SDL_FlashWindow`) until the window is focused.

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
│   │   ├── compat.h        small POSIX/Windows differences (localtime); header only
│   │   ├── notify.c/.h     notify-send (Linux) / osascript (macOS) / taskbar flash (Windows) + sound
│   │   ├── paths.c/.h      config/data directories (XDG; macOS: Application Support; Windows: %APPDATA%), asset lookup
│   │   ├── fonts.h         installed font list API; no state kept
│   │   ├── fonts.c         Linux: fontconfig (dlopen'd while needed)
│   │   ├── fonts_mac.c     macOS: CoreText
│   │   ├── fonts_win.c     Windows: installed-fonts registry, families checked with SDL_ttf
│   │   ├── fonts_common.c  sorting, lookup and ring stepping shared by all three
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
│   ├── ribalta.ico         Windows icon (generated from the SVG by make icons)
│   ├── icons/              PNG icons (16 ... 512 px)
│   └── ribalta.desktop     launcher entry
├── tests/                  unit tests run by `make test` and the CI (test_util.h: helpers without POSIX calls)
├── tools/
│   ├── install.sh          `make install`: build, install, progress UI
│   ├── uninstall.sh        `make uninstall` / `ribalta uninstall`: removes only installed files, keeps user data
│   ├── update.sh           `ribalta update [--check]`: latest GitHub release, build, install in the same prefix
│   ├── icons.sh            regenerates the PNG icons from the SVG
│   └── macos-app.sh        `make app` on macOS: builds build/Ribalta.app (icon, bundled libs, ad-hoc signature)
├── packaging/flatpak/      Flatpak manifest and AppStream metainfo
├── packaging/macos/        Info.plist template for Ribalta.app
├── packaging/windows/      exe resources (ribalta.rc, ribalta.manifest) and the Inno Setup script (ribalta.iss)
├── docs/                   specifications and design notes
└── .github/workflows/      CI (build, tests, install/uninstall smoke test, Flatpak bundle, macOS app, Windows zip and installer)
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

Asset files are searched in the order listed under *Asset lookup order* in section 3 (Packaging):
`$RIBALTA_DATA` first, `/usr/share/ribalta` last.

### Main entry points

The headers are the reference; this is the map.

- **`app/app.h`**: `View` (`VIEW_CLOCK`, `VIEW_TIMER`, `VIEW_POMODORO`), `Display` (what the tiles
  show: view, `ClockFace`, label, progress, tile alphas, digit and accent colours, free text) and
  `App`, the single state struct. Its fields fall in groups: SDL handles (`win`, `ren`, `render`);
  settings and logic (`cfg`, `timer`, `pomo`, `entry`); overlays (`ui`, `sui`, `cui`, `fui`, `subj`,
  `hui`, all `NULL` in screensaver mode); deadlines (`cursor_deadline`, `hint_deadline`,
  `font_preview_at`, `blink_start`; 0 = none); session bookkeeping (`timer_start`, `pomo_start`,
  subjects, segments); and loop flags (`dirty`, `resized`, `running`, `shown`/`has_shown`).
- **`render/render.h`**: `render_create` / `render_destroy`, `render_resize` (rebuilds the textures),
  `render_set_*` (`tiles`, `ampm`, `label`, `label_text`, `progress`, `tile_alpha`, `digit_color`,
  `accent`, `style`, `hint`, `font`), `render_is_animating`, `render_draw`.
- **`ui/ui_*.h`** (settings, stats, color, font, subject, help): every overlay follows the same
  pattern: `ui_x_create(renderer, font_path)` / `ui_x_destroy`, `ui_x_open` / `ui_x_close` (fonts and
  textures exist only while open), `ui_x_is_open`, `ui_x_resize`, `ui_x_draw` (no present), and, for
  interactive ones, `ui_x_key` / `ui_x_mouse` returning a result enum that `input.c` acts on.
- **`app/input.h`**: `input_handle_event` (keyboard, mouse, window events, routing to the overlays),
  `input_*_open` queries, `input_cursor_tick`, `input_hint_tick`, `input_font_preview_apply`.
- **`app/display.h`**: `display_update(app, now)` computes the desired `Display` and applies only
  the differences to `Render`; `display_invalidate` forces a re-apply.
- **`app/session.h`**: `session_on_timer_finished`, `session_on_pomo_finished`, `session_blink_tick`,
  `session_track_starts`, `session_subject_changed`, `session_log_*_partial` (statistics) and
  `session_set_inhibit` (screensaver inhibition while a timer runs).
- **`core/`**: `clock_*`, `timer_*` (monotonic end instant, `timer_ms_to_next_change`), `pomo_*`,
  `stats_*`, `settings_*`, `color_*`: no SDL window needed, covered by `make test`.

## Packaging

- **macOS** (experimental): the Makefile detects Darwin (`uname -s`) and then uses
  `-D_DARWIN_C_SOURCE`, builds `fonts_mac.c` instead of `fonts.c`, and links CoreText and
  CoreFoundation. Platform branches in the code: no video-driver hint (SDL picks Cocoa), renderer
  order `gpu,metal,opengl,software`, single-instance lock in `$TMPDIR`, notifications through
  `osascript`, config and data in `~/Library/Application Support/Ribalta/` (XDG variables still win,
  e.g. in tests). `make app` runs `tools/macos-app.sh`: `Contents/MacOS/ribalta`,
  `Contents/Resources/assets/` (found through `SDL_GetBasePath()`), an `.icns` made with `iconutil`,
  optional `BUNDLE_LIBS=1` (`dylibbundler` copies Homebrew's dylibs into `Contents/Frameworks`) and an
  ad-hoc `codesign`. The `macos.yml` workflow builds on `macos-15` (arm64) and `macos-15-intel`, runs
  the tests and a real Cocoa start for 3 s (no offscreen driver: the runners have a graphical
  session), builds the bundle with `BUNDLE_LIBS=1`, checks that no Homebrew path is left in it and
  `codesign --verify`, starts the bundled app for 3 s from outside the repository (so the assets must
  come from `Contents/Resources`), uploads the zips and attaches them to `v*` releases. No
  notarisation (no paid developer account).
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
- **Windows** (new, to be checked on real PCs): the same `Makefile`, in its `Windows_NT` branch, built
  with MSYS2 MinGW-w64 UCRT64 (packages `mingw-w64-ucrt-x86_64-{gcc,pkgconf,sdl3,sdl3-ttf}`, `make`,
  `zip`). `-mwindows` makes it a GUI program: `--version`, `--help`, the update/uninstall messages and
  errors are attached to the parent console (`AttachConsole`). `windres` compiles
  `packaging/windows/ribalta.rc`: icon (`assets/ribalta.ico`), `ribalta.manifest` (UTF-8 code page,
  per-monitor DPI v2, long paths) and version information from `VERSION`. The digit-font list comes from
  `fonts_win.c`; there is no `make debug` (MinGW has no ASan).
  - `make dist`: `build/dist/Ribalta` with `ribalta.exe`, the DLLs found under the MSYS2 prefix with
    `ldd` (`/ucrt64/bin`; the system DLLs in `C:\Windows` stay out), `assets/` and `LICENSE`, zipped to
    `build/Ribalta-windows-x86_64.zip` (portable: extract and run `ribalta.exe`).
  - `make installer` (Inno Setup 6, `ISCC=<path>` if it is not in `PATH`): `packaging/windows/ribalta.iss`
    turns the `make dist` folder into `build/Ribalta-windows-x86_64-setup.exe`. Per-user install in
    `%LOCALAPPDATA%\Programs\Ribalta` without administrator rights (all users is an option), Start menu
    entry, optional desktop icon, Italian and English. The AppId is fixed, so installing a newer version
    over an older one is an upgrade. The uninstaller removes the program and keeps `%APPDATA%\Ribalta`.
  - Neither file is code-signed: SmartScreen warns on the first run.
  - `windows.yml` (`windows-latest`, MSYS2 shell): `-Werror` build, unit tests, command-line smoke test,
    embedded-resource check, a run with only `APPDATA` set (no XDG variables), the single-instance check,
    `make dist` and a run of the bundle without MSYS2 in `PATH`, `make installer`, then a silent installer
    test (per-user install, upgrade, uninstall keeping the user data). It uploads the zip and the setup as
    artifacts and attaches them to `v*` releases.
- **Flatpak**: `packaging/flatpak/io.github.ginteaa.Ribalta.yml` builds with `make release` and
  `make install-files PREFIX=/app APPID=io.github.ginteaa.Ribalta`; the AppStream metainfo is
  installed next to it. The CI workflow `flatpak.yml` produces the `ribalta.flatpak` bundle on every
  run, and on `v*` tags attaches it to the GitHub release (`softprops/action-gh-release`;
  `permissions: contents: write`). Before building, it copies the container's Flatpak installations
  (`/var/lib/flatpak`, `~/.local/share/flatpak`, `/root/.local/share/flatpak`) into the writable
  layer: on some runners the overlayfs refuses to rename directories that come from the image
  ("Invalid cross-device link") and `flatpak-builder` crashes while updating the SDK.
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
> | Native build, for comparison (different session) | ~18.8 MB | ~28.4 MB |
>
> RSS is lower than native; PSS is higher only because the runtime's libraries (Mesa Vulkan driver,
> libc, libstdc++, SDL3 …) are not shared with host processes. The remaining gap is mostly
> unavoidable: the runtime's Intel Vulkan driver alone is +2.3 MB, libc +1.8 MB, libstdc++ (needed
> by Mesa) +1.4 MB. Other renderers are worse (opengl/opengles2/software load EGL and `libLLVM`,
> 80–97 MB). CPU at idle: ~20 ms in 65 s.

## 4. Makefile

The `Makefile` in the repository is the reference; in short:

- **Libraries**: `PKGS := sdl3 sdl3-ttf` (linked); `FCPKG := fontconfig` only on Linux, for its headers
  (the library is `dlopen`ed, never linked). `PKGFLAGS=--static` is used by the Flatpak manifest.
- **Platform switch** (`uname -s`, and `OS` on Windows): on Linux `-D_POSIX_C_SOURCE=200809L`, `fonts.c`, `-s` for
  stripping; on macOS `-D_DARWIN_C_SOURCE`, `fonts_mac.c`, `-framework CoreText -framework
  CoreFoundation`, `-Wl,-dead_strip`, plus `make app` (`tools/macos-app.sh`) and a `make install` that
  copies `Ribalta.app` to `~/Applications`. On Linux `make app` just says it is macOS only. On Windows
  (`OS=Windows_NT`): `-mwindows`, `fonts_win.c`, `windres` for `packaging/windows/ribalta.rc`, and the
  `dist` and `installer` targets; `install` and `uninstall` just say they are not available there.
- **Sources**: `SRC` is every `*.c` found by `find src -name '*.c'` minus the other platform's fonts
  file; objects go to `build/`, with `-Isrc` and dependency files (`-MMD -MP`).
- **Version**: read from the `VERSION` file (works from a release tarball without `.git`) and passed as
  `-DRIBALTA_VERSION="..."` through `VERSION_DEF`, kept outside `CFLAGS` because `debug`/`release`
  re-invoke make with `CFLAGS` on the command line. `build/main.o` depends on `VERSION`.
- **Output**: short lines (`  CC   core/timer.c`); `make V=1` prints the full commands.
- **`debug` / `release`**: add their flags (ASan + UBSan, or `-flto -DNDEBUG` + strip), run `clean`,
  then re-invoke make with the extended `CFLAGS`/`LDFLAGS`.
- **`install`** (Linux) runs `tools/install.sh "$(PREFIX)"` (default `~/.local`); **`uninstall`** runs
  `tools/uninstall.sh "$(PREFIX)"`, which removes only Ribalta's own files and keeps the user's data.
- **`install-files`**: no compile; installs binary, assets, icons and desktop file honouring `PREFIX`,
  `DESTDIR` and `APPID` (see Packaging).
- **`icons`**: `tools/icons.sh` (needs `rsvg-convert`; the PNGs are in the repository).
- **Tests**: every `tests/test_*.c` is a standalone program built into `build/tests/` and linked with
  `TEST_COMMON` (settings, color, timer, paths, fonts, stats sources); `test_session` additionally
  links `app/session.c`, `core/pomodoro.c` and `tests/stub_notify.c`. `make test` runs them in turn and
  stops at the first failure.

`ribalta update` and `ribalta uninstall` are not Makefile targets: the binary runs
`update.sh` or `uninstall.sh` from `<prefix>/share/ribalta/` with `execvp` (see 4.2).

### 4.1 Dependencies and install

**Dependencies**

- `sdl3` and `sdl3_ttf` (SDL3 ≥ 3.2 and SDL3_ttf ≥ 3.2; build and runtime).
- `fontconfig`: headers at build time. The library is `dlopen`ed only when the digit-font menu opens;
  without it the menu lists just the bundled font.
- `libnotify` (optional): provides `notify-send` for desktop notifications.
- A C compiler, `make` and `pkg-config`.
- Windows: MSYS2 with the UCRT64 environment (`pacman -S mingw-w64-ucrt-x86_64-{gcc,pkgconf,sdl3,sdl3-ttf} make zip`),
  then `make` in the UCRT64 shell; `make installer` also needs Inno Setup 6. The zip and the installer
  are described in Packaging.

Distributions that ship SDL3 ≥ 3.2 (Arch, Fedora, openSUSE Tumbleweed…) can use the native build; the
others (Ubuntu 24.04, Mint 22) use the Flatpak. On Arch: `sudo pacman -S sdl3 sdl3_ttf fontconfig libnotify`.
On other distributions install the equivalent SDL3 and SDL3_ttf development packages (`pkg-config` must
find `sdl3`, `sdl3-ttf` and `fontconfig`).

**Targets**

- `make`: development build; `make V=1` shows the full compiler commands.
- `make release`: optimised build (LTO, stripped). `make test`, `make debug` (ASan + UBSan), `make icons`.
- Windows: `make` builds `ribalta.exe` in the repository root, `make test` runs the same tests, `make dist` builds the
  portable zip and `make installer` the setup (both in `build/`). `make install` and `make uninstall` are
  not available there.
- `make install`: builds the release binary, then installs to `~/.local`:
  - `~/.local/bin/ribalta`;
  - `~/.local/share/applications/ribalta.desktop`;
  - the icons in `~/.local/share/icons/hicolor`;
  - the assets (font, licence, chime) in `~/.local/share/ribalta`.
  Make sure `~/.local/bin` is in your `PATH`.
- Installer (`tools/install.sh [PREFIX]`):
  - The banner is always printed (uncoloured without a terminal). *Fancy* mode (colours and a progress
    bar such as `[███░░░] 35%`) needs stdout to be a terminal, `NO_COLOR` unset and `TERM` not `dumb`;
    otherwise plain `[n/5] ...` lines.
  - The requirement checks come first (`✓ C compiler (cc)`, `make`, `pkg-config`, `SDL3`, `SDL3_ttf`,
    `fontconfig`). If something is missing it prints hints for Arch, Debian/Ubuntu and Fedora and
    exits 1.
  - Five steps, weighted 60/5/10/15/10 in the global bar: building, binary, assets, icons, desktop
    entry and icon cache. The assets installed into `<prefix>/share/ribalta/` are `font.ttf`,
    `OFL.txt`, `ding.wav`, `ribalta.svg`, `update.sh` and `uninstall.sh`.
  - The build runs as `make release` in the background; its log goes to a temporary file (because
    `make release` runs `clean`, which deletes `build/`) copied to `build/install.log` at the end.
    The progress count (objects in `build/`) must never fail: `build/` is missing for a moment, and
    with `pipefail` a failing `find` killed the script (the 0.7.2 fix). If the script stops early, the
    background build is stopped too.
  - On a build failure it prints the last 15 log lines and the log path, and exits 1. On success a
    summary with the paths, the `update`/`uninstall` hints and a warning if `<prefix>/bin` is not in
    `PATH`.
  - `ribalta update` calls the same script.
- System-wide: `PREFIX=/usr make install`, as root.
- `make uninstall` (with the same `PREFIX`) removes only the installed files; settings and statistics
  are kept. `ribalta uninstall` does the same from the installed binary.
- `make install-files PREFIX=... APPID=...` installs without building (used by the Flatpak manifest and
  by packagers; see Packaging above).

### 4.2 Update and uninstall

**`ribalta update [--check] [-y]`**: `main.c` finds the prefix from the executable's directory
(`<prefix>/bin`) and does `execvp("bash")` of `<prefix>/share/ribalta/update.sh --prefix P --current V`
plus the user's arguments. The script:

1. fetches `https://api.github.com/repos/Ginteaa/Ribalta/releases/latest` with `curl` or `wget`, reads
   `tag_name` and `body` with `jq` (or a `sed` fallback), requires a tag shaped `vX.Y.Z` and compares
   it with the current version using `sort -V`;
2. `--check` prints the notes and exits 0 (up to date) or 10 (update available), 1 on error;
3. otherwise it checks that `<prefix>/bin` is writable (no `sudo`), shows the notes and asks
   `Update now? [Y/n]` (it needs a terminal; `-y` skips the question; with no terminal and no `-y` it
   exits 1);
4. downloads `https://github.com/Ginteaa/Ribalta/archive/refs/tags/{tag}.tar.gz` into a `mktemp`
   directory (removed by a trap), checks that the archive has a single top-level folder, and runs
   **that release's** `tools/install.sh <prefix>`, so an installer fixed in the new release applies to
   the update itself;
5. prints "Restart Ribalta" if it is running.

Test hooks: `RIBALTA_UPDATE_API` (URL of the release JSON) and `RIBALTA_UPDATE_TARBALL` (template with
`{tag}`); `file://` URLs are accepted (used by the CI). The whole script body is a function and the
last line is `main "$@"; exit $?`, because `install.sh` overwrites `update.sh` while it runs and bash
must have parsed it all first. `uninstall.sh` does the same since it deletes itself.

**`ribalta uninstall [-y]`**: runs `uninstall.sh --prefix P --current V [-y]`. It asks `[y/N]` (no
terminal and no `-y` → exit 1; answering no → "Cancelled", exit 0), then removes the binary, the
desktop file, the icons and its own files in `share/ribalta` (`font.ttf`, `OFL.txt`, `ding.wav`,
`ribalta.svg`, `update.sh`, `uninstall.sh`), removing the folder only if it is then empty. Settings and
`sessions.csv` are kept (and the script prints how to delete them). `make uninstall` runs the same
script without the prompt.

Both subcommands exit 1 with a pointer message inside the Flatpak (`FLATPAK_ID` set), on macOS, on
Windows (no script there: `update` prints the releases page, `uninstall` says to use *Settings → Apps* or
to delete the folder), or when the script is not installed next to the binary (e.g. a build run from the
repository).

### 4.3 CI

- **`build.yml`** (push to `main`, `v*` tags, pull requests to `main`), in an `archlinux:latest`
  container:
  1. install the packages (`base-devel`, SDL3, SDL3_ttf, fontconfig, a font, pkgconf, git, curl,
     util-linux for `script`);
  2. on `v*` tags, check that the tag equals `v$(cat VERSION)`;
  3. build with `-Werror` (zero warnings), then `make test`;
  4. `make release` and a `--help` smoke test;
  5. `make install` into a temporary prefix, checking the binary, desktop file, font, chime, icons,
     `update.sh`/`uninstall.sh` and `ribalta --version`;
  6. update from a local release: a `git archive` tarball of `HEAD` as `v9.9.9` and `file://` JSON
     files through `RIBALTA_UPDATE_API` / `RIBALTA_UPDATE_TARBALL`; `update --check` must exit 10 for
     the newer release and 0 for the same version; `ribalta update -y` runs inside a pseudo-terminal
     (`script -qec`, `TERM=xterm`) and the log must contain the progress bar (`%`): the regression
     test for the 0.7.0/0.7.1 bug fixed in 0.7.2, where the fancy output path broke the update;
  7. `make uninstall` must keep `sessions.csv` and remove the binary and `update.sh`;
  8. reinstall and `ribalta uninstall -y`: same checks, plus `uninstall.sh` gone.
- **`flatpak.yml`**: builds the `ribalta.flatpak` bundle and attaches it to releases (see Packaging).
- **`macos.yml`**: builds, tests and zips `Ribalta.app` for arm64 and Intel (see Packaging).
- **`windows.yml`**: on `windows-latest` in an MSYS2 UCRT64 shell: builds with `-Werror`, runs the tests and
  the command-line, resource, `APPDATA`-only, single-instance and bundle checks, builds the zip and the
  installer, and tests the installer silently (install, upgrade, uninstall that keeps the user data). It
  attaches both files to `v*` releases (see Packaging). The Windows figures are not measured yet.

## 5. Development with agents

- **Orchestrator — Claude Opus 5.5** (main session): maintains the specs in `docs/`,
  splits each phase into per-module tasks, defines the interfaces (`*.h`) before the code, reviews,
  integrates, builds with `make` / `make debug` and measures RAM/CPU.
- **Implementation — Claude Sonnet 5.5 subagents** (`model: "sonnet"`): one per task/module
  (e.g. `settings.c`, `render.c` + `font.c`, `timer.c`). Each prompt names the `docs/` files to read,
  the header to respect and the acceptance criteria (compiles without warnings, expected behaviour).
- Independent tasks run in parallel; those touching the same files run sequentially.
- After each phase: `make debug` (ASan/UBSan), manual run, resource measurement, docs update.
