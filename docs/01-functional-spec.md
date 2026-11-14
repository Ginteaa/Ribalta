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

The Pomodoro keys (`pomodoro_work`, `pomodoro_break`, `pomodoro_long_break`, `pomodoro_cycles`,
`pomodoro_auto_continue`) are described in [04-roadmap.md](04-roadmap.md), section 3.1.

File: `~/.config/ribalta/config.ini` (honours `$XDG_CONFIG_HOME`). `key=value` format,
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
| `I` | Open/close the study statistics panel |
| `↑ ↓ ← →` | Timer in setup: adjust duration. Panels: navigate |
| `S` | Open/close the settings panel |
| `Esc` | Close the panel (settings or statistics); otherwise leave fullscreen; otherwise nothing |
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
| Runtime dependencies | SDL3 + SDL3_ttf (+ optional `notify-send`) |
| Accuracy | the minute change happens within 50 ms of the real minute |
| Platforms | Linux Wayland (primary), X11 |
