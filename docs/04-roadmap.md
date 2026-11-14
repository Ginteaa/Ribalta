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

## Future ideas
- Colour themes (light, amber, terminal green).
- Choice of monitor in fullscreen.
- AUR package (`PKGBUILD`).
- Status bar integration (e.g. waybar) showing the timer's remaining time.
