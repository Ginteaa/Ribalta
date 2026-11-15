# 04 — Roadmap

## Phase 1 — Clock MVP ✅

- Project skeleton, `Makefile`, OFL font (IBM Plex Sans Condensed Bold) in `assets/`.
- SDL3 window on Wayland, app-id `ribalta`, HiDPI, resizable.
- Pre-rendered tiles and digits, `HH:MM` and `HH:MM:SS` layouts.
- Flip animation (section 3 of [02-visual-design.md](02-visual-design.md)).
- Event-driven loop waking at the next minute/second.
- `config.ini`: `show_seconds`, `clock_24h`, `scale`, `brightness`, `start_fullscreen`, `animations`.
- Fullscreen (`F`/`F11`/double click), `Q` to quit, hidden cursor.

**Done when**: the time is correct and flips every minute; RSS < 20 MB; CPU at rest ~0%
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
  go to 50%. During RIPOSO/RIPOSO LUNGO the digits turn soft blue `#6fa8dc`.
- At the end of a phase: notification ("Fine studio — riposo di 5 min" / "Riposo finito — si torna a
  studiare", i.e. study over — 5 min break / break over — back to studying) + sound; if
  `pomodoro_auto_continue=true` (default) the next phase starts by itself, otherwise it stays
  stopped, ready to start. The view does not change (no blinking, it stays on Pomodoro).
- New config keys (also in the settings panel): `pomodoro_work` (1..180),
  `pomodoro_break` (1..60), `pomodoro_long_break` (1..120), `pomodoro_cycles` (1..12),
  `pomodoro_auto_continue` (bool).

### 3.2 Study statistics (`I` key)
- File `$XDG_DATA_HOME/ribalta/sessions.csv` (fallback `~/.local/share/ribalta/`), one row
  per session: `start_iso8601_local,duration_seconds,type,completed` with type `timer|pomodoro`.
  A header row is written if the file is new (the header uses the Italian column names
  `inizio,durata_s,tipo,completata`).
- What is recorded (study time only, never breaks), if the studied duration is ≥ 60 s:
  - timer: when the timer ends (completed=1); on `R` or on quitting the app with a started timer (completed=0);
  - pomodoro: at the end of a STUDIO phase (1); on `N`/`R`/quit during a started STUDIO (0).
  - Duration = time actually elapsed, pauses excluded.
- `I` opens/closes an overlay panel (same style as the settings panel): **Today**, **Last 7 days**
  (horizontal bars per day, abbreviated Italian day label and `1h 25m`), **This month**,
  **Streak** (consecutive days up to today, or up to yesterday if today is still 0),
  total **Completed sessions**. `Esc`/`I` closes it.
- The file is read only when the panel opens; font and textures exist only while it is open.

### 3.3 Screensaver mode
- `ribalta --screensaver`: fullscreen, cursor hidden immediately; exits on any key, click or
  mouse movement (ignoring events during the first second). Saves nothing.
- `ribalta --help` shows the options.
- Automatic start after inactivity with `swayidle` (README with a niri example:
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
- Pomodoro breaks keep blue digits (amber if the chosen colour is blue).
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
- Still to do: feedback from real Macs, resource measurements on macOS.

## Phase 8 — Study subjects (planned)

- Optional subject per session (`M` key overlay); CSV gains a `materia` column, old rows stay
  valid and count as "no subject".
- Statistics filter: all study, each subject, no subject; weekly hours per subject.

## Phase 9 — Daily goal (planned)

- `daily_goal_min` setting; progress bar and "goal reached N days in a row" in the statistics;
  one notification when the goal is reached.

## Future ideas
- Publish the Flatpak on Flathub.
- Snap package.
- Colour themes (light, amber, terminal green) built on the digit colour.
- Choice of monitor in fullscreen.
- AUR package (`PKGBUILD`).
- Status bar integration (e.g. waybar) showing the timer's remaining time.
