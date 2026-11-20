# 01 — Functional specification

## 1. Modes

The app has one active mode at a time. It always starts in **Clock** mode.

### 1.1 Clock (default)

- Shows the current local time as `HH:MM` (no seconds).
- 24-hour format by default; with `clock_24h=false` it uses 12-hour format and shows a small
  `AM`/`PM` indicator in the top-left corner of the hours tile.
- With `show_seconds=true` it shows `HH:MM:SS` (a third pair of tiles).
- On every digit change the flip animation plays **only on the tiles that change**.
- In 24-hour format hours always have two digits (`09:05`); in 12-hour format the leading `0`
  is hidden (` 9:05`).

### 1.2 Study timer

- The user sets a duration in **hours and minutes** (from 0h01 to 23h59), e.g. 2h30 to study for
  two and a half hours.
- States: `SETUP` → `RUNNING` ⇄ `PAUSED` → `FINISHED`. The caption above the tiles is `TIMER`, or
  `PAUSA` while paused; `TIMER · <materia>` / `PAUSA · <materia>` when a subject is active; drawn at
  50% opacity. SETUP, RUNNING and FINISHED all show `TIMER`.
- **SETUP**: the tiles show the chosen duration (`HH:MM`); the selected tile (hours or minutes)
  is at full brightness, the other is dimmed to 40% (no blinking: zero CPU wake-ups).
  ←/→ select hours or minutes; ↑/↓ change the selected part by ±1
  (on minutes, Shift = ±10). The duration is always clamped to 0h01–23h59.
- **Keyboard entry (microwave style)**: in SETUP, the digit keys `0`–`9` (also keypad) enter from
  the right, up to 4 digits: `1`,`3`,`0` → `01:30`. Raw values are shown while typing (`90` →
  `00:90`) and normalised on confirm (→ 1h30, clamped to 0h01–23h59). Both tiles are at full
  brightness and flip on each digit. `Backspace` deletes a digit. `Enter`/`Space` confirm and start
  (an empty or zero entry just cancels); arrows, `T`, `P`, `S`, `I` confirm without starting; other keys (`M`, `H`, `F`) leave the typed digits
  pending; `R`
  or `Esc` cancel and keep the previous duration (`Esc` does not leave fullscreen in that case).
  Digits are ignored while the timer is running or paused.
- **RUNNING**: countdown using the same flip tiles.
  - If the remaining time is ≥ 1 h → shows `HH:MM` (minutes update on every whole minute).
  - If < 1 h → shows `MM:SS`.
  - If `show_seconds=true` → always shows `HH:MM:SS`.
  - A thin progress bar below the tiles (≥ 3 px, digit colour at 60% on a track at 20%)
    shows the elapsed time.
- **PAUSED**: the digits dim (50% opacity) and time does not advance.
- **FINISHED**:
  - desktop notification titled `Ribalta`: `Sessione di studio completata (2h 30min)` via
    `notify-send` (`osascript` on macOS), if `timer_notify`;
  - optional short sound (`timer_sound=true`), played through an SDL audio stream from the bundled
    `ding.wav` (the audio subsystem is opened only while it plays);
  - the app switches to the Timer view, the tiles show `00:00` and blink 3 times (≈ 1.8 s),
    then the app returns to **Clock** and the timer goes back to SETUP with the last used duration.
- The countdown shows the time rounded up to the visible unit (just after starting a 2h30 timer
  it shows `02:30`).
- **The timer keeps running in the background**: `T` only toggles the Clock/Timer view; the
  countdown continues and the end is notified even while you are looking at the clock.
- The timer uses a monotonic clock: it is not affected by system time changes or NTP.
- If the app is closed while the timer is running, the timer is lost, but the partial session is
  still recorded in the statistics (see §1.4).

### 1.3 Settings panel

A semi-transparent overlay drawn with SDL on top of the clock (no external GUI library).
Keyboard navigation: ↑/↓ choose an entry; `←`/`→`, `Enter` or `Space` change it (on a number
`Enter`/`Space` step it up, clamped; on the colour, font and break-colour rows `Enter`/`Space` open the
picker or the font menu, while `←`/`→` step through the choices); `Esc`/`S` close.
Every change is applied immediately and saved when the panel is closed.

| Key | Type | Default | Description |
|---|---|---|---|
| `show_seconds` | bool | `false` | Show seconds |
| `clock_24h` | bool | `true` | 24-hour format (false = 12-hour with AM/PM) |
| `timer_default_minutes` | int (1–1439) | `60` | Duration proposed when entering Timer mode |
| `timer_sound` | bool | `true` | Sound when a timer or Pomodoro phase ends (and when a goal is reached on a session stopped early) |
| `timer_notify` | bool | `true` | Desktop notification when a timer or Pomodoro phase ends or a goal is reached |
| `scale` | int (50–100) | `90` | Width taken by the tiles, in % of the window |
| `brightness` | int (20–100) | `100` | Digit brightness (useful at night) |
| `start_fullscreen` | bool | `false` | Start fullscreen |
| `animations` | bool | `true` | If false, digits change without animation (minimum power use) |
| `digit_color` | colour | `#b7b7b7` | Digit colour (`#rrggbb`); also the accent of progress bar and labels |
| `recent_colors` | list | empty | Up to 5 recent colours, comma separated, most recent first |
| `font_family` | string | empty | Digit font family shown in the panel; empty = bundled font ("Ribalta (IBM Plex Sans Condensed)") |
| `font_file` | string | empty | Digit font file; empty = bundled font. Both values run to the end of the line (inner spaces kept, CR/trailing blanks trimmed); a value that is too long (family > 127, file > 511 bytes) is ignored. The keys are always written |
| `subjects` | list | empty | Study subjects, `;` separated, in order (max 8; each 1–24 bytes UTF-8, no leading/trailing spaces, no `,` `;` `"` or control characters; invalid entries and case-insensitive duplicates are dropped) |
| `subject` | string | empty | Active subject; empty = no subject. Ignored if it is not in `subjects`. Both keys are always written |
| `daily_goal_min` | int (0–720) | `0` | Daily study goal in minutes; `0` = off. Steps of 15 min in the panel (Shift: 60 min) |
| `weekly_goal_h` | int (0–100) | `0` | Weekly study goal in hours; `0` = off. Steps of 1 h in the panel (Shift: 5 h) |
| `pomodoro_work` | int (1–180) | `25` | Study minutes of a STUDIO phase |
| `pomodoro_break` | int (1–60) | `5` | Short break (RIPOSO) minutes |
| `pomodoro_long_break` | int (1–120) | `15` | Long break (RIPOSO LUNGO) minutes |
| `pomodoro_cycles` | int (1–12) | `4` | Study sessions before the long break |
| `pomodoro_auto_continue` | bool | `true` | Start the next phase by itself; `false` = stop and wait |
| `pomodoro_break_color` | `digits` / `auto` / `custom` | `digits` | Break colour mode: `digits` keeps the digit colour; `auto` soft blue `#6fa8dc` (amber `#ffb347` if the digit colour is already bluish); `custom` the colour in `pomodoro_break_custom`. A `#rrggbb` written by hand selects `custom`; an old `pomodoro_break_blue=true` without a valid value is read as `auto` |
| `pomodoro_break_custom` | colour | `#6fa8dc` | Custom break colour `#rrggbb`; kept while the mode is `digits` or `auto`, so `custom` restores it |

**Steps.** `←`/`→` (and `Enter`/`Space`, which step up) change numbers by: `timer_default_minutes` 5
min (Shift 1 h); `scale` 5 (Shift 10); `brightness` 10 (Shift 20); `pomodoro_work`, `pomodoro_break`,
`pomodoro_long_break` 1 (Shift 5); `pomodoro_cycles` 1; `daily_goal_min` 15 min (Shift 60 min);
`weekly_goal_h` 1 h (Shift 5 h).

**Description line.** Above the footer, a dim line explains the selected entry: what it does, its
range and, for numbers, the step taken with `Shift`. It takes up to two lines and follows the
selection.

**Subjects** (optional). `M` opens the subjects menu (overlay, same style as the font menu): first
entry "Nessuna materia", then the subjects; the active one is marked. `↑`/`↓` move, `Home`/`End` first/last, `Enter` makes the
selected entry active and closes, `N` starts typing a new name (text input: accents allowed; `Enter`
adds it and selects it without making it active, `Esc` cancels; an invalid or duplicate name, or a
name when the list is full, shows the reason in red: `Nome non valido`, `Materia già presente`,
`Elenco pieno (max 8)`; `Q` is typed while editing, `Ctrl+Q` quits), `Canc`/`Delete`
removes the selected subject from the list (past sessions keep it in the CSV; if it was active, the
active subject becomes none), `Esc`/`M` close. Mouse: wheel scrolls, click selects, double click
chooses, click outside closes. The list and the active subject are saved when the menu closes.
Studying without a subject is always possible.

Changing subject **during** a session applies at once. While a timer is started, or a
Pomodoro STUDIO phase is started (running or paused), choosing another subject with `M` splits the
session: the part studied so far is saved as its own row with `completata=0` (pauses excluded) and
the new part starts now; when the session ends only the last part is saved as completed, so the
"Completed sessions" count does not grow. A part shorter than 60 s is not saved and its time goes to
the new subject. During Pomodoro breaks there is nothing to split: the next STUDIO phase takes the
active subject. With both a timer and a Pomodoro started, the change applies to both. Removing the
active subject from the list means switching to "no subject". In Timer and Pomodoro study views the
label shows the subject:
`TIMER · Analisi`, `PAUSA · Analisi`, `STUDIO 2/4 · Analisi`.

The "Colore cifre" row shows a swatch and the HEX value; ←/→ step through the palette and Enter
opens a **colour picker** overlay (saturation/value square, hue bar, HEX field with read-only
RGB/HSV, palette grid, recent colours; mouse and keyboard). The digits preview the colour live;
Enter applies, saves and adds it to the recents, Esc restores the previous colour. With the keyboard, `Tab`/`Shift+Tab` change zone: square, hue bar, palette (palette grid and recent
colours together), HEX field. In the square the arrows move 1% (Shift 10%); in the hue bar 1° (Shift
10°); in the palette they move between swatches; in the HEX field type `0`–`9`, `a`–`f` and
`Backspace`. In Pomodoro
breaks the digits keep the chosen colour (`pomodoro_break_color=digits`, the default). The row
"Pomodoro: colore riposo" (after the Pomodoro rows) sets the break colour mode: ←/→ cycle "come le
cifre" (`digits`) → "blu automatico" (`auto`, `#6fa8dc`, or amber `#ffb347` if the digit colour is
already bluish) → "colore scelto" (`custom`, swatch and HEX). The custom colour is stored in
`pomodoro_break_custom` (default `#6fa8dc`) and is kept while the mode is `digits` or `auto`, so
cycling back to "colore scelto" restores it. A hand-written `#rrggbb` in `pomodoro_break_color`
selects `custom` and sets that colour. Enter opens the colour picker on the custom break colour: the
tiles preview the candidate colour live, Enter confirms (switches to `custom`, added to the
recents), Esc restores. The accent (progress bar and labels) always stays the digit colour. A config
with the old `pomodoro_break_blue=true` and no valid `pomodoro_break_color` is read as `auto`; on
save the new keys are written.

The "Font cifre" row (right after "Colore cifre", 19 rows in total) shows `font_family`, or the bundled
font name when empty (ellipsised if too wide). ←/→ move to the previous/next font of the ring
[bundled font, installed fonts…] and apply it immediately. Enter opens the **font menu** (fontconfig
is queried only while it is open): a search field filtering the list (case-insensitive substring),
a scrolling list of the font families that contain the digits 0–9 (first entry: the bundled font),
a "12:34" preview drawn in the selected font and a key hint. Keys: ↑/↓ entry, PgUp/PgDn page,
Home/End first/last, letters, digits, space and `-` edit the filter (`Q` is typed too: it does not quit
here, `Ctrl+Q` does), Backspace deletes, mouse wheel
scrolls, click selects, double click or Enter applies and saves, Esc clears the filter, then
cancels and restores the font the menu was opened with. The tiles preview the selected font once the
selection rests for ~150 ms (no polling: the pause is a wait-timeout deadline).

**Goals** (optional). The last two rows, "Obiettivo giornaliero" and "Obiettivo settimanale", show
`no` when the goal is off, otherwise a value such as `2h 30m` or `15h`; ←/→ change it. Goals always
count the total study time, with or without subject, whatever filter the statistics panel uses.

Startup: if `font_file` is non-empty and readable (`access(R_OK)`, no fontconfig) it is used for the
digits; otherwise a warning `ribalta: font non trovato: <path>, uso il font incluso` goes to stderr, the
bundled font is used and the setting is left untouched. Panels, labels and the font menu itself always
use the bundled font.

The Pomodoro settings are in the table above; the Pomodoro mode itself is described in §1.5.

File: `~/.config/ribalta/config.ini` (honours `$XDG_CONFIG_HOME`; on macOS
`~/Library/Application Support/Ribalta/config.ini`, where `sessions.csv` lives too). `key=value` format,
lines starting with `#` are ignored, unknown keys are ignored, invalid values fall back to the default;
out-of-range numbers are clamped to the allowed range.
If the file does not exist it is created on the first save.

### 1.4 Statistics panel, goals and sessions file

**Sessions file.** Study sessions are appended to `~/.local/share/ribalta/sessions.csv` (honours
`$XDG_DATA_HOME`; on macOS it is in `~/Library/Application Support/Ribalta/`; in the Flatpak sandbox
it is `~/.var/app/io.github.ginteaa.Ribalta/data/ribalta/sessions.csv`). One row per session; the
header line is written when the file is created:

```csv
inizio,durata_s,tipo,completata,materia
2026-11-14T09:30:00,1500,pomodoro,1,Analisi
2026-11-14T11:00:00,3600,timer,0,
```

- `inizio`: local start time, ISO 8601.
- `durata_s`: studied seconds, pauses excluded.
- `tipo`: `timer` or `pomodoro`.
- `completata`: `1` if the session ran to its end, `0` if it was stopped early (or is a part of a
  session split by a subject change, see §1.3).
- `materia`: the subject; empty = no subject.

What is recorded: timer sessions and Pomodoro study phases, never breaks, and only if at least 60 s
were studied. A timer is recorded when it ends (`completata=1`), or when `R` resets it or the app
quits while it is started (`0`). A Pomodoro STUDIO phase is recorded when it ends (`1`), or when `N`,
`R` or quitting stops it while started (`0`).

Files written before subjects existed have 4 columns (no `materia`): their rows are still read and
count as "no subject", and the file is never rewritten. The file is read when the statistics panel opens and,
only if a goal is set, after each recorded session to check the goals.

**Statistics panel.** `I` opens it. Title `STATISTICHE`; the filter row `‹ Tutte ›` cycles with `←`/`→`
through `Tutte` (all study), each subject and `Senza materia`; the filter resets to `Tutte` at every
opening. Four rows follow the filter: `Oggi`, `Questo mese`, `Giorni consecutivi` (consecutive days
with study, counted back from today if today has study, otherwise from yesterday) and `Sessioni
completate`. Then the section `Ultimi 7 giorni` (seven rows: day name, duration, bar) and, only in the
`Tutte` view, `Per materia (7 giorni)` (at most four rows, longest first). The footer reads
`←/→ materia · Esc chiudi`.

The **Obiettivi** block is shown only if at least one goal is set:

- bars "Oggi 1h 40m / 3h" and "Settimana 8h / 15h"; a bar is full and highlighted when its goal is
  reached. The week is the calendar week, starting on Monday;
- "Obiettivo raggiunto N giorni di fila" (only if the daily goal is set; it counts daily-goal days
  only): consecutive days with the daily goal reached, up to today if it is already reached, otherwise
  up to yesterday.

The block is computed when the panel opens and ignores the subject filter. Each session counts for the
day it started.

**Goal notification**: when a recorded session makes the total cross a goal, a desktop notification
(if `timer_notify`) fires, plus the chime (if `timer_sound`) when the session was stopped early; a
completed session already plays its own end chime: "Obiettivo di oggi raggiunto (3h)" or
"Obiettivo della settimana raggiunto (15h)". The check runs only when a session is recorded, so it
fires once, even after a restart; it never fires when the app quits and there are no periodic wake-ups.

### 1.5 Pomodoro

- `P` opens the Pomodoro view; `T` and `P` switch directly between the Timer and the Pomodoro view.
- Cycle: STUDIO (`pomodoro_work`, 25 min) → RIPOSO (`pomodoro_break`, 5 min) → … ; after
  `pomodoro_cycles` (4) study sessions the break is RIPOSO LUNGO (`pomodoro_long_break`, 15 min), then
  the cycle restarts from STUDIO 1/N.
- Labels: `STUDIO 2/4`, `RIPOSO`, `RIPOSO LUNGO`; when paused ` · IN PAUSA` is appended and the tiles dim
  to 50%.
- Keys: `Space`/`Enter` start, pause or resume; `N` skips to the next phase (it starts running only if the
  current phase was running, otherwise the next phase waits stopped); `R` resets everything
  (STUDIO 1/N, stopped).
- During breaks the digits keep your colour, or the break colour set with `pomodoro_break_color` (§1.3).
- At the end of a phase: a notification ("Fine studio — riposo di 5 min", after the last study session
  of the cycle "Fine studio — riposo lungo di 15 min", or "Riposo finito — si torna a studiare"; only
  if `timer_notify`) and the chime (if `timer_sound`). With `pomodoro_auto_continue=true` (default) the next phase starts by
  itself; otherwise it stays stopped, ready to start. The view does not change.
- The timer and the Pomodoro are independent and can both run in the background.

### 1.6 Legend and hint

- `H` or `F1` opens the **legend**, an overlay titled `LEGENDA` with every key and the place of your
  files. `Esc`, `H` or `F1` close it; `Q` and `Ctrl+Q` quit. It opens from the clock and the
  Timer/Pomodoro views, and from the settings and statistics panels, which close when it opens.
  It is not available in the screensaver, nor in the colour picker, the font menu and the subjects
  menu, where letters are typed: there `H` and `F1` are ignored.
- Sections: **Generale** (`S` impostazioni, `I` statistiche, `M` materie, `T` timer, `P` Pomodoro,
  `F`/`F11`/double click schermo intero, `H`/`F1` legenda, `Esc`, `Q`/`Ctrl+Q`), **Timer**,
  **Pomodoro**, **Pannelli** (↑↓, ←→, `Invio`, statistics `←`/`→`, subjects `N`/`Canc`) and **File**:
  the real paths of `config.ini` and `sessions.csv`, with `~` for the home directory (in the Flatpak
  the sandbox paths), and `ribalta --help` for the command line.
- Layout: two columns when the window is wide, one column otherwise. The font shrinks until everything
  fits (minimum 8 px); there is no scrolling. The legend's font is loaded only while the legend is open.
- **Hint**: a small, faint `H · legenda` in the bottom-right corner. It is shown at startup and on
  every mouse movement, and hidden after 3 s (`HINT_NS`). It is hidden while a panel or the legend is
  open and in screensaver mode. It is event-driven: its deadline is part of the wait timeout, so it
  causes no wake-ups without mouse movement.

### 1.7 Screensaver

`ribalta --screensaver` shows the clock fullscreen with the cursor hidden. Any key, click or mouse
movement exits (mouse movement counts only after about 8 px of real movement); events during the first
second are ignored. It saves nothing. If Ribalta is already
running (single instance), the screensaver instance exits immediately.

To start it after 5 minutes of inactivity, with niri (`~/.config/niri/config.kdl`):

```kdl
spawn-at-startup "swayidle" "-w" "timeout" "300" "ribalta --screensaver"
```

or from any compositor or session script:

```sh
swayidle -w timeout 300 'ribalta --screensaver'
```

## 2. Controls

| Key | Action |
|---|---|
| `F` / `F11` / double click | Toggle fullscreen |
| `T` | Toggle Clock / Timer view (a running timer keeps going in the background) |
| `P` | Toggle Clock / Pomodoro view (switching between Timer and Pomodoro is direct) |
| `Space` / `Enter` | Timer or Pomodoro: start / pause / resume |
| `R` | Timer: reset to the set duration. Pomodoro: reset everything (STUDIO 1/N, stopped) |
| `N` | Pomodoro: skip to the next phase |
| `I` | Open/close the study statistics panel (`←`/`→` inside it: change the subject filter; from the settings panel: switches to statistics) |
| `M` | Open/close the subjects menu |
| `0`–`9` / keypad, `Backspace` | Timer in setup: type the duration (microwave style), delete a digit |
| `↑ ↓ ← →` | Timer in setup: adjust duration. Panels: navigate |
| `S` | Open/close the settings panel (from the statistics panel: switches to settings) |
| `H` / `F1` | Open/close the legend (every key and file paths, §1.6); from the settings or statistics panel it closes that panel and opens the legend |
| `Esc` | Cancel timer entry; close the legend, the font menu, the subjects menu or the colour picker, or the panel (settings or statistics); otherwise leave fullscreen; otherwise nothing |
| `Q` / `Ctrl+Q` | Quit. While typing in the font-menu search or a subject name `Q` is typed instead; `Ctrl+Q` always quits |

While a settings or statistics panel is open, `F`, `T`, `P`, `M`, `Space`, `R` are ignored; double click
still toggles fullscreen.

The mouse cursor hides after 2 s of inactivity in fullscreen.

## 3. Window

- Title `Ribalta`, Wayland app-id `ribalta` (for compositor rules, e.g. niri `window-rule`).
- While the timer or the Pomodoro is RUNNING, the system screen blanking is inhibited.
- Resizable; the layout adapts while keeping proportions and staying centred.
- Initial size 960×400 (logical), HiDPI support (`SDL_WINDOW_HIGH_PIXEL_DENSITY`).

## 4. Non-functional requirements

| Requirement | Target |
|---|---|
| RAM at steady state | PSS about 18 MB native (RSS about 27 MB including shared libraries), about 25 MB PSS as Flatpak; target PSS < 20 MB |
| CPU at rest (between animations) | ~0% — the process sleeps waiting for events |
| CPU during a flip | short spike, animation ≤ 400 ms |
| Startup time | < 200 ms |
| Runtime dependencies | SDL3 + SDL3_ttf (+ optional `notify-send`); fontconfig is optional, loaded only while the font list is needed |
| Accuracy | the minute change happens within 50 ms of the real minute |
| Platforms | Linux Wayland (primary), X11; macOS (experimental build) |

## 5. Command line

| Invocation | Behaviour | Exit code |
|---|---|---|
| `ribalta` | Starts the app | — |
| `ribalta --screensaver` | Screensaver mode, see §1.7 | — |
| `ribalta --version`, `-V` | Prints `ribalta X.Y.Z` (from `VERSION`) and exits | 0 |
| `ribalta --help`, `-h` | Lists the options | 0 |
| any other option | `ribalta: opzione sconosciuta: X` and the usage on stderr | 2 |
| `ribalta update` | Shows the latest release notes, asks `Update now? [Y/n]` (`-y` skips), builds and installs it in the same prefix; settings and statistics are untouched; says to restart a running instance | 0 done, already up to date or cancelled; 1 error |
| `ribalta update --check` | Reports current and latest version and the release notes, changes nothing | 0 up to date, 10 update available, 1 error |
| `ribalta uninstall` | Asks `[y/N]` (`-y` skips) and removes exactly what `make install` installed; settings and statistics are kept | 0 removed or cancelled; 1 error, or no terminal without `-y` (nothing done) |

If Ribalta is already running, a new instance prints `ribalta: già in esecuzione` and exits 0 (single-instance
lock `ribalta.lock` in `$XDG_RUNTIME_DIR`, falling back to `$TMPDIR` or `/tmp`).

`update` and `uninstall` need an installation made with `make install`. `update` downloads the source
of the latest release and builds it with the same tools as `make install`, plus `curl` or `wget` and
`tar`. The update path depends on how Ribalta was installed:

- Native (`make install`): `ribalta update`.
- Flatpak: the app is installed from the `ribalta.flatpak` bundle, so `flatpak update` does **not**
  update it. Download the new `ribalta.flatpak` from the latest GitHub release and run
  `flatpak install --user ribalta.flatpak` again; settings and statistics are kept. Remove it with
  `flatpak uninstall io.github.ginteaa.Ribalta`.
- macOS app: download the new `Ribalta.app` zip from the releases page.
- Binary run from the source tree: `git pull && make install`.

In the last three cases `update` and `uninstall` print a message pointing to the right command and
exit with 1. Installs older than 0.7.0 have no `update` command: update them once with
`git pull && make install`. If the prefix is not writable, `update` explains it and stops; it never
calls `sudo`.
