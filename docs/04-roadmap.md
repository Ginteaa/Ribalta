# 04 — Roadmap

## Phase 1 — Clock MVP ✅

- Project skeleton, `Makefile`, OFL font (IBM Plex Sans Condensed Bold) in `assets/`.
- SDL3 window on Wayland, app-id `ribalta`, HiDPI, resizable.
- Pre-rendered tiles and digits, `HH:MM` and `HH:MM:SS` layouts.
- Flip animation (section 3 of [02-visual-design.md](02-visual-design.md)).
- Event-driven loop waking at the next minute/second.
- `config.ini`: `show_seconds`, `clock_24h`, `scale`, `brightness`, `start_fullscreen`, `animations`.
- Fullscreen (`F`/`F11`/double click), `Q` to quit, hidden cursor.

**Done when**: the time is correct and flips every minute; PSS < 20 MB (measured ~17 MB; RSS ~27 MB
includes shared libraries); CPU at rest ~0%
(measured with `pidstat` for 2 minutes); `make debug` runs without ASan errors.

## Phase 2 — Study timer + settings ✅

- Full Timer mode (section 1.2 of [01-functional-spec.md](01-functional-spec.md)).
- Progress bar, `TIMER`/`PAUSA` labels.
- `notify-send` notification + `ding.wav` sound.
- Overlay settings panel with all entries.
- `ribalta.desktop` file and `make install` target.

**Done when**: a 2-minute timer ends with a notification and sound; the countdown does not
accumulate error (±1 s over 1 h); settings survive a restart.

## Phase 3 — Additions ✅

### 3.1 Pomodoro mode (`P` key)
- `P` toggles the Pomodoro view (like `T` for the timer; `T`/`P` from one view to the other switch
  directly). Timer and Pomodoro are independent and can both run in the background.
- Cycle: STUDIO (`pomodoro_work`, default 25 min) → RIPOSO (`pomodoro_break`, 5) → … ; after
  `pomodoro_cycles` (default 4) study sessions the break is RIPOSO LUNGO (`pomodoro_long_break`, 15),
  then the cycle restarts from 1.
- Keys in the view: `Space`/`Enter` start/pause, `N` skip to the next phase, `R` reset everything
  (STUDIO phase 1/N, stopped).
- Label: `STUDIO 2/4`, `RIPOSO`, `RIPOSO LUNGO`; when paused ` · IN PAUSA` is appended and the tiles
  go to 50%. During RIPOSO/RIPOSO LUNGO the digits keep the chosen colour (the break colour is
  configurable since Phase 9).
- At the end of a phase: notification ("Fine studio — riposo di 5 min" / "Riposo finito — si torna a
  studiare", i.e. study over — 5 min break / break over — back to studying) + sound; if
  `pomodoro_auto_continue=true` (default) the next phase starts by itself, otherwise it stays
  stopped, ready to start. The view does not change (no blinking, it stays on Pomodoro).
- New config keys (also in the settings panel): `pomodoro_work` (1..180),
  `pomodoro_break` (1..60), `pomodoro_long_break` (1..120), `pomodoro_cycles` (1..12),
  `pomodoro_auto_continue` (bool), `pomodoro_break_blue` (bool, default false; replaced by
  `pomodoro_break_color` in Phase 9).

### 3.2 Study statistics (`I` key)
- File `$XDG_DATA_HOME/ribalta/sessions.csv` (fallback `~/.local/share/ribalta/`), one row
  per session: `start_iso8601_local,duration_seconds,type,completed,subject` with type
  `timer|pomodoro` and an empty subject when none (Phase 8). A header row is written if the file is
  new (Italian column names `inizio,durata_s,tipo,completata,materia`). Rows written before Phase 8
  have 4 columns: they stay valid and count as "no subject"; an existing file is never rewritten.
- What is recorded (study time only, never breaks), if the studied duration is ≥ 60 s:
  - timer: when the timer ends (completed=1); on `R` or on quitting the app with a started timer (completed=0);
  - pomodoro: at the end of a STUDIO phase (1); on `N`/`R`/quit during a started STUDIO (0).
  - Duration = time actually elapsed, pauses excluded.
  - A session may be split into several rows when the subject changes during it (Phase 10): each
    earlier part is a row with `completed=0`, and only the last part is `completed=1`.
- `I` opens/closes an overlay panel (same style as the settings panel): **Today**, **Last 7 days**
  (horizontal bars per day, abbreviated Italian day label and `1h 25m`), **This month**,
  **Streak** (consecutive days up to today, or up to yesterday if today is still 0),
  total **Completed sessions**. `Esc`/`I` closes it. `←`/`→` change the filter (Phase 8):
  **Tutte** (all study) → each subject → **Senza materia**; in "Tutte" a section "Per materia"
  lists the last 7 days' hours of up to 4 subjects.
- The file is read only when the panel opens; font and textures exist only while it is open.

### 3.3 Screensaver mode
- `ribalta --screensaver`: fullscreen, cursor hidden immediately; exits on any key, click or
  mouse movement (ignoring events during the first second). Saves nothing.
- `ribalta --help` shows the options.
- Automatic start after inactivity with `swayidle` (see [01-functional-spec.md](01-functional-spec.md) §1.7 for a niri example:
  `spawn-at-startup "swayidle" "-w" "timeout" "300" "ribalta --screensaver"`).
  If ribalta is already open, the screensaver instance exits immediately (single instance).
- **Screen-blank inhibition**: while a timer or a Pomodoro is RUNNING the app calls
  `SDL_DisableScreenSaver()` (idle-inhibit on Wayland); when none is running,
  `SDL_EnableScreenSaver()`.

## Phase 4 — Keyboard entry & colours ✅

- Microwave-style timer entry: digits `0`–`9` enter from the right (`1` `3` `0` → `01:30`), `Backspace`
  deletes, `Enter`/`Space` confirm and start, arrows / `T` / `P` / `S` / `I` confirm, `R`/`Esc` cancel.
- Configurable digit colour (`digit_color`) applied to digits, progress bar and labels (accent);
  cheap re-colouring (white glyphs + colour modulation, no font reopening).
- Colour picker overlay (saturation/value square, hue bar, HEX, palette, recent colours via
  `recent_colors`), live preview, mouse and keyboard.
- Pomodoro breaks keep the digit colour; optional blue breaks with `pomodoro_break_blue` (amber if
  the chosen colour is blue; replaced by `pomodoro_break_color` in Phase 9).
- Automated tests (`make test`).

## Phase 5 — Digit font picker ✅

- `font_family` / `font_file` settings; empty = bundled font. Startup opens `font_file` directly
  (no fontconfig), with a stderr warning and fallback to the bundled font if it is missing.
- `src/platform/fonts.c` (fontconfig, used only while needed) and `render_set_font`.
- "Digit font" row in the settings panel (←/→ next/previous, Enter opens the menu) and the font menu
  overlay (search field, scrolling list, "12:34" preview, live preview on the tiles).
- `fontconfig` added to the install checks and CI; `tests/test_fonts.c`.

## Phase 6 — Flatpak package ✅

- Manifest `packaging/flatpak/io.github.ginteaa.Ribalta.yml` on `org.freedesktop.Platform` 25.08
  (fontconfig and `notify-send` come from the runtime; SDL3 3.4, FreeType and SDL3_ttf are built
  by the manifest — the runtime's SDL 3.2 crashed on resize with the `gpu` renderer).
- AppStream metainfo, app-id `io.github.ginteaa.Ribalta` (also set as `SDL_APP_ID`).
- `make install-files` target; `paths.c` also finds assets in `<exe dir>/../share/ribalta`.
- GitHub Actions workflow `flatpak.yml` builds the `ribalta.flatpak` bundle.

## Phase 7 — macOS port (experimental) ✅

- Native build with Homebrew SDL3 (`make`, `make app`, `make install` → `~/Applications`);
  notifications via `osascript`, font list via CoreText (`fonts_mac.c`, shared code in
  `fonts_common.c`), config and data in `~/Library/Application Support/Ribalta/`.
- CI workflow `macos.yml` on `macos-15` (arm64) and `macos-15-intel`: `-Werror` build, tests, a real
  Cocoa start (renderer `gpu` on arm64, `metal` on Intel), zipped `Ribalta.app` with bundled
  libraries, ad-hoc signed (no notarisation: "Open Anyway" on first launch), attached to `v*` releases.

## Phase 8 — Study subjects ✅

- Optional subject per session (`M` key overlay); CSV gains a `materia` column, old rows stay
  valid and count as "no subject".
- Statistics filter: all study, each subject, no subject; weekly hours per subject.
- Session subject fixed when the session starts; label `TIMER · Analisi` / `STUDIO 2/4 · Analisi`
  (changeable during the session since Phase 10).
- New modules `ui/ui_subject.c` (menu, SDL text input only while typing a name) and
  `tests/test_stats.c`. Idle PSS unchanged (17.8 MB).

## Phase 9 — Pomodoro break colour ✅

- `pomodoro_break_color=digits|auto|custom` (default `digits`) replaces `pomodoro_break_blue`:
  `auto` is soft blue `#6fa8dc` (amber `#ffb347` if the digit colour is already bluish), `custom`
  uses `pomodoro_break_custom=#rrggbb` (default `#6fa8dc`), kept even when another mode is selected.
  A `#rrggbb` written by hand in `pomodoro_break_color` selects `custom` and sets that colour.
- An old `pomodoro_break_blue=true` without a valid `pomodoro_break_color` is read as `auto`; on save
  only the new keys are written.
- Settings row "Pomodoro: colore riposo": ←/→ cycle "come le cifre" → "blu automatico" → custom
  colour; Enter opens the colour picker on the break colour, with live preview and Esc to restore.
- The accent (progress bar and labels) always stays the digit colour.

## Phase 10 — Change subject during a session ✅

- Choosing another subject with `M` while a timer or a started Pomodoro STUDIO phase (running or
  paused) applies at once; the label changes (`TIMER · Fisica`, `STUDIO 2/4 · Fisica`).
- The part studied so far is saved at once as its own CSV row with `completata=0` (pauses
  excluded) and the new part starts now. At the end only the last part is saved with `completata=1`,
  so the "Completed sessions" count does not grow and the total time stays correct.
- A part shorter than 60 s is not saved: the subject is just replaced and that time goes to the new
  one. Breaks have nothing to split; with a timer and a Pomodoro both started, the change applies to
  both. Removing the active subject from the list means "no subject".
- Known limit: a last part shorter than 60 s is not recorded (existing rule).

## Phase 11 — Daily and weekly goals ✅

- `daily_goal_min` (0 = off, up to 720 min) and `weekly_goal_h` (0 = off, up to 100 h) settings, with
  two new rows at the end of the settings panel (19 rows in total).
- Statistics panel: an "Obiettivi" block (shown only if a goal is set) with a today bar, a week bar
  and "goal reached N days in a row". Weeks start on Monday (calendar week).
- Goals always count total study time, regardless of the subject filter; each session counts for the
  day it started.
- One notification and chime when a recorded session makes the total cross a goal; checked only on
  record, so it fires once even after a restart, and never on quit.

## Phase 12 — Terminal commands ✅

- `VERSION` file as the single source of the version: the Makefile passes it as `RIBALTA_VERSION`;
  `ribalta --version` / `-V` prints it.
- `ribalta update`: checks the latest GitHub release, shows its notes, asks, then builds and installs
  the source tarball in the same prefix; `-y` skips the question; settings and statistics untouched.
- `ribalta update --check`: reports current and latest version only (exit 0 up to date, 10 update
  available, 1 error), for scripts or a status bar.
- `ribalta uninstall`: removes exactly what `make install` installed, keeps settings and statistics.
- Not for the Flatpak, macOS or source-tree builds, which get a pointer to the right update path;
  installs older than 0.7.0 update once with `git pull && make install`.

## Phase 13 — In-app legend and slimmer README ✅

- `H` / `F1` open the legend overlay (every key, the paths of `config.ini` and `sessions.csv`, the
  command line); `Esc`, `H` or `F1` close it. Two columns when the window is wide, one otherwise.
- A discreet "H · legenda" hint in the bottom-right corner at startup and on mouse movement, hidden
  after 3 s; event-driven (its deadline joins the wait timeout, no extra wake-ups).
- Settings panel: a description line above the footer explains the selected setting (meaning, range,
  Shift step).
- README slimmed down (from 424 to about 200 lines): the keys, the configuration table, the CSV format,
  the "How it works" notes and the build details moved to `docs/` (sections 01 and 03), nothing removed.
- Flatpak: the `flatpak.yml` workflow attaches `ribalta.flatpak` to each `v*` release; the update is
  done by installing the new bundle again (`flatpak update` does not apply to a bundle).

## Phase 14 — Installer fix and CI hardening (0.7.2, 0.7.3) ✅

- Installer: the progress loop no longer dies under `set -o pipefail` when `find build` runs while
  `make release` cleans `build/`; `make install` and `ribalta update` used to stop at 0% in a terminal
  (0.7.0–0.7.1).
- The installer stops the background build if it quits early.
- `build.yml` runs `ribalta update -y` inside a pseudo-terminal (`script`) and checks that the progress
  bar is drawn.
- `flatpak.yml` copies the container's Flatpak installations into the writable layer (overlayfs
  "Invalid cross-device link" on some runners) and attaches `ribalta.flatpak` to `v*` releases.
- 0.7.3: `Q` no longer quits while typing in the font search or a subject name (`Ctrl+Q` always quits).
- Docs aligned with the code after a full audit.
- Releases: 0.7.0 (Phases 9–12), 0.7.1 (Phase 13), 0.7.2 and 0.7.3 (this phase).

## Phase 15 — Windows port (in progress)

- Toolchain: MSYS2 MinGW-w64 UCRT64 (same `Makefile`, gcc, pkg-config, SDL3/SDL3_ttf packages).
- Portable refactor first (no behaviour change on Linux/macOS): SDL string helpers, a small time
  compatibility header, portable path helpers (absolute paths, readability), tests without POSIX calls.
- Windows specifics: single instance with a named mutex; settings and statistics in
  `%APPDATA%\Ribalta`; assets next to the executable; `--help`/`--version` printed to the parent
  console; `update`/`uninstall` point to the release page / the installer.
- Notifications: sound (as today) + `SDL_FlashWindow`; no toast, no child processes.
- Digit-font menu: system fonts from the registry (`…\Windows NT\CurrentVersion\Fonts`), read with
  SDL_ttf only while the menu is open.
- Executable resources: icon, manifest (UTF-8 code page, DPI awareness), version info.
- Renderer order chosen by measuring RAM/CPU at rest on Windows; numbers in `docs/03-architecture.md`.
- Packages: portable zip (exe + DLLs + assets) and an Inno Setup installer (Start menu entry,
  uninstaller that keeps `%APPDATA%\Ribalta`), both attached to every release by `windows.yml`.

**Done when**: CI is green on Linux, macOS, Flatpak and Windows; on a real Windows PC every feature
works from both the zip and the installer, a second instance is refused, user data survives the
uninstaller; resource numbers are documented.

## Future ideas
- Feedback from real Macs and resource measurements on macOS.
- Publish the Flatpak on Flathub.
- Snap package.
- Colour themes (light, amber, terminal green) built on the digit colour.
- Choice of monitor in fullscreen.
- AUR package (`PKGBUILD`).
- Status bar integration (e.g. waybar) showing the timer's remaining time.
