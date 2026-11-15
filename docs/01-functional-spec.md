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
- States: `SETUP` → `RUNNING` ⇄ `PAUSED` → `FINISHED` (on screen: `IMPOSTAZIONE`, `IN CORSO`,
  `IN PAUSA`, `FINITO`).
- **SETUP**: the tiles show the chosen duration (`HH:MM`); the selected tile (hours or minutes)
  is at full brightness, the other is dimmed to 40% (no blinking: zero CPU wake-ups).
  ←/→ select hours or minutes; ↑/↓ change the selected part by ±1
  (on minutes, Shift = ±10). The duration is always clamped to 0h01–23h59.
- **Keyboard entry (microwave style)**: in SETUP, the digit keys `0`–`9` (also keypad) enter from
  the right, up to 4 digits: `1`,`3`,`0` → `01:30`. Raw values are shown while typing (`90` →
  `00:90`) and normalised on confirm (→ 1h30, clamped to 0h01–23h59). Both tiles are at full
  brightness and flip on each digit. `Backspace` deletes a digit. `Enter`/`Space` confirm and start
  (an empty or zero entry just cancels); arrows, `T`, `P`, `S`, `I` confirm without starting; `R`
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
  - desktop notification "Study session completed (2h30)" via `notify-send`;
  - optional short sound (`timer_sound=true`), played with `SDL_PlayAudio` from a bundled WAV;
  - the app switches to the Timer view, the tiles show `00:00` and blink 3 times (≈ 1.8 s),
    then the app returns to **Clock** and the timer goes back to SETUP with the last used duration.
- The countdown shows the time rounded up to the visible unit (just after starting a 2h30 timer
  it shows `02:30`).
- **The timer keeps running in the background**: `T` only toggles the Clock/Timer view; the
  countdown continues and the end is notified even while you are looking at the clock.
- The timer uses a monotonic clock: it is not affected by system time changes or NTP.
- If the app is closed while the timer is running, the timer is lost (but since Phase 3 the partial
  session is still recorded in the statistics).

### 1.3 Settings panel

A semi-transparent overlay drawn with SDL on top of the clock (no external GUI library).
Keyboard navigation: ↑/↓ choose an entry, ←/→ or Enter change it, `Esc`/`S` close.
Every change is applied immediately and saved when the panel is closed.

| Key | Type | Default | Description |
|---|---|---|---|
| `show_seconds` | bool | `false` | Show seconds |
| `clock_24h` | bool | `true` | 24-hour format (false = 12-hour with AM/PM) |
| `timer_default_minutes` | int (1–1439) | `60` | Duration proposed when entering Timer mode |
| `timer_sound` | bool | `true` | Sound when the timer ends |
| `timer_notify` | bool | `true` | Desktop notification when the timer ends |
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

**Subjects** (optional). `M` opens the subjects menu (overlay, same style as the font menu): first
entry "Nessuna materia", then the subjects; the active one is marked. `↑`/`↓` move, `Enter` makes the
selected entry active and closes, `N` starts typing a new name (text input: accents allowed; `Enter`
adds it, `Esc` cancels; an invalid or duplicate name shows the reason in red), `Canc`/`Delete`
removes the selected subject from the list (past sessions keep it in the CSV; if it was active, the
active subject becomes none), `Esc`/`M` close. Mouse: wheel scrolls, click selects, double click
chooses, click outside closes. The list and the active subject are saved when the menu closes.
Studying without a subject is always possible.

A session is recorded with the subject that was active when it **started**; changing subject during
a session affects only the next one. In Timer and Pomodoro study views the label shows the subject:
`TIMER · Analisi`, `PAUSA · Analisi`, `STUDIO 2/4 · Analisi`.

The "Digit colour" row shows a swatch and the HEX value; ←/→ step through the palette and Enter
opens a **colour picker** overlay (saturation/value square, hue bar, HEX field with read-only
RGB/HSV, palette grid, recent colours; mouse and keyboard). The digits preview the colour live;
Enter applies, saves and adds it to the recents, Esc restores the previous colour. In Pomodoro
breaks the digits are blue `#6fa8dc` (amber `#ffb347` if the chosen colour is bluish).

The "Digit font" row (right after "Digit colour", 16 rows in total) shows `font_family`, or the bundled
font name when empty (ellipsised if too wide). ←/→ move to the previous/next font of the ring
[bundled font, installed fonts…] and apply it immediately. Enter opens the **font menu** (fontconfig
is queried only while it is open): a search field filtering the list (case-insensitive substring),
a scrolling list of the font families that contain the digits 0–9 (first entry: the bundled font),
a "12:34" preview drawn in the selected font and a key hint. Keys: ↑/↓ entry, PgUp/PgDn page,
Home/End first/last, letters, digits, space and `-` edit the filter, Backspace deletes, mouse wheel
scrolls, click selects, double click or Enter applies and saves, Esc clears the filter, then
cancels and restores the font the menu was opened with. The tiles preview the selected font once the
selection rests for ~150 ms (no polling: the pause is a wait-timeout deadline).

Startup: if `font_file` is non-empty and readable (`access(R_OK)`, no fontconfig) it is used for the
digits; otherwise a warning `ribalta: font non trovato: <path>, uso il font incluso` goes to stderr, the
bundled font is used and the setting is left untouched. Panels, labels and the font menu itself always
use the bundled font.

The Pomodoro keys (`pomodoro_work`, `pomodoro_break`, `pomodoro_long_break`, `pomodoro_cycles`,
`pomodoro_auto_continue`) are described in [04-roadmap.md](04-roadmap.md), section 3.1.

File: `~/.config/ribalta/config.ini` (honours `$XDG_CONFIG_HOME`; on macOS
`~/Library/Application Support/Ribalta/config.ini`, where `sessions.csv` lives too). `key=value` format,
lines starting with `#` are ignored, unknown keys are ignored, invalid values fall back to the default.
If the file does not exist it is created on the first save.

## 2. Controls

| Key | Action |
|---|---|
| `F` / `F11` / double click | Toggle fullscreen |
| `T` | Toggle Clock / Timer view (a running timer keeps going in the background) |
| `P` | Toggle Clock / Pomodoro view (switching between Timer and Pomodoro is direct) |
| `Space` / `Enter` | Timer or Pomodoro: start / pause / resume |
| `R` | Timer: reset to the set duration. Pomodoro: reset everything (STUDIO 1/N, stopped) |
| `N` | Pomodoro: skip to the next phase |
| `I` | Open/close the study statistics panel (`←`/`→` inside it: change the subject filter) |
| `M` | Open/close the subjects menu |
| `0`–`9` / keypad, `Backspace` | Timer in setup: type the duration (microwave style), delete a digit |
| `↑ ↓ ← →` | Timer in setup: adjust duration. Panels: navigate |
| `S` | Open/close the settings panel |
| `Esc` | Cancel timer entry; close the font menu, the subjects menu or the colour picker, or the panel (settings or statistics); otherwise leave fullscreen; otherwise nothing |
| `Q` / `Ctrl+Q` | Quit |

The mouse cursor hides after 2 s of inactivity in fullscreen.

## 3. Window

- Title `ribalta`, Wayland app-id `ribalta` (for compositor rules, e.g. niri `window-rule`).
- Resizable; the layout adapts while keeping proportions and staying centred.
- Initial size 960×400 (logical), HiDPI support (`SDL_WINDOW_HIGH_PIXEL_DENSITY`).

## 4. Non-functional requirements

| Requirement | Target |
|---|---|
| RAM (RSS) at steady state | < 20 MB (ideal goal < 12 MB) |
| CPU at rest (between animations) | ~0% — the process sleeps waiting for events |
| CPU during a flip | short spike, animation ≤ 400 ms |
| Startup time | < 200 ms |
| Runtime dependencies | SDL3 + SDL3_ttf + fontconfig (+ optional `notify-send`) |
| Accuracy | the minute change happens within 50 ms of the real minute |
| Platforms | Linux Wayland (primary), X11 |
