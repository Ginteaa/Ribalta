 AGENTS.md — guide for AI agents working on Ribalta

Read this file first. It tells you what the project is, where the documentation lives, how the code
is organised and how work is split between agents.

> **Claude Code:** it reads `CLAUDE.md`, not `AGENTS.md`. Keep a local, git-ignored `CLAUDE.md`
> containing the single line `@AGENTS.md` so this file is loaded automatically.

## What Ribalta is

A lightweight **flip clock and study timer for Linux** (Wayland and X11), written in **C17 + SDL3 +
SDL3_ttf + fontconfig**. Features: flip clock (HH:MM, optional seconds, 12/24h), study timer (also typed with the
digit keys, microwave style), Pomodoro, study statistics (CSV), settings overlay, colour picker for
the digits, digit-font menu (system fonts via fontconfig), screensaver mode, desktop notifications.

**The top requirement is resource usage:** ~18 MB of RAM and ~0% CPU when idle. Every change must
preserve that (see *Engineering rules* below).

## Documentation map

| File | What it represents | Read it when… |
|---|---|---|
| [`README.md`](README.md) | User-facing overview: features, install, keys, configuration, performance | you need the product view or change user-visible behaviour (keep it in sync) |
| [`docs/README.md`](docs/README.md) | Index of the design documents | you look for the right design doc |
| [`docs/01-functional-spec.md`](docs/01-functional-spec.md) | **What the app does**: modes, timer, settings keys, keyboard shortcuts, non-functional requirements | you add or change a feature or a key — this is the source of truth for behaviour |
| [`docs/02-visual-design.md`](docs/02-visual-design.md) | **How it looks**: palette, tile geometry, flip animation, layout mockups | you touch rendering or any overlay |
| [`docs/03-architecture.md`](docs/03-architecture.md) | **How it is built**: stack, resource strategy, repository layout, measurements, build | you add a module, change the main loop or anything performance-related |
| [`docs/04-roadmap.md`](docs/04-roadmap.md) | Phases done (✅) and future ideas | you plan new work |
| `docs/images/` | Screenshots and the flip GIF used by the README | you change something visible (update the screenshot) |
| `docs/plans/` | **Local, git-ignored**: open plans not yet implemented (one file each, with status and date) | **before planning new work** — continue an existing plan instead of starting over; may not exist on other machines |

If code and docs disagree, **the code is the truth** — fix the docs in the same change.

## Repository layout

```
src/
├── main.c        CLI, single-instance lock, init/close, event-driven main loop
├── app/          app.h (App state, View, Display) · input.c (keys, mouse, overlays)
│                 display.c (what the tiles show) · session.c (timer/Pomodoro end, stats, inhibit)
├── core/         pure logic, no window needed: clock, timer, pomodoro, stats, settings, color
├── platform/     notify.c (notify-send + chime) · paths.c (XDG dirs, asset lookup)
│                 fonts.c (system font list, fontconfig only while needed)
├── render/       render.c — tiles, pre-rendered glyphs, flip animation
└── ui/           overlays: ui_settings, ui_stats, ui_color (colour picker), ui_font (font menu)
assets/           font (IBM Plex Sans Condensed, OFL), chime, icon (svg + icons/NxN.png), .desktop
tests/            unit tests (plain C, no display): test_timer, test_color, test_settings, test_fonts
tools/            install.sh / uninstall.sh (pretty installer), icons.sh (PNG icons from the SVG)
docs/             design documents (see the map above)
.github/workflows/build.yml   CI: Arch container, -Werror build, tests, release, install/uninstall checks
```

Conventions: each `.h` lives next to its `.c`; includes are written from `src/`
(`#include "core/timer.h"`); public functions are prefixed with their module
(`timer_*`, `render_*`, `paths_*`, `input_*` …); everything else is `static`.

## Build and test

| Command | Purpose |
|---|---|
| `make` | development build (`make V=1` shows full commands) |
| `make CFLAGS="-std=c17 -Wall -Wextra -Wpedantic -O2 -Werror"` | what CI runs — must have **zero warnings** |
| `make test` | unit tests (no display needed) |
| `make debug` | ASan + UBSan build |
| `make release` | LTO + stripped binary |
| `make install PREFIX=/tmp/...` | installer test — **never** install into the user's real `~/.local` unless asked |
| `make icons` | regenerate `assets/icons/*.png` from `assets/ribalta.svg` |

Runtime knobs: `SDL_RENDER_DRIVER=<name>` (default order gpu, opengl, opengles2, software),
`RIBALTA_DEBUG=1` (prints the renderer), `RIBALTA_DATA=<dir>` (asset lookup).

## Multi-agent workflow (Opus orchestrator + Sonnet implementers)

Work on this project is split between an **orchestrator** and **implementers**:

| Role | Model | Responsibilities |
|---|---|---|
| **Orchestrator** | Claude **Opus** | Talks to the user, clarifies requirements, writes the plan and updates `docs/`; **designs the interfaces (writes the `.h` headers) before any code**; splits the work into tasks with **disjoint file ownership**; spawns the implementers; reviews their code and previews; integrates, builds, runs tests and end-to-end checks; measures RAM/CPU; commits, merges and pushes. Small fixes during integration are fine. |
| **Implementers** | Claude **Sonnet** subagents | One task each, usually one module (`render.c`, `ui_color.c`, `timer.c` + tests …). They implement against the headers written by the orchestrator, which they must **not change** (if impossible, explain in the report). They only edit the files they own, test in throwaway directories under `/tmp`, never commit or push, and end with a concise report: files changed, tests run, preview image paths, concerns. |

Typical cycle for a feature:

1. **Plan** — the orchestrator agrees the behaviour with the user, saves the plan as `docs/plans/<name>.md`
   (local, git-ignored) and updates the spec / roadmap. When the plan is done, mark it ✅ in the roadmap
   and delete the plan file.
2. **Interfaces** — the orchestrator writes or extends the headers so the project still compiles.
3. **Parallel implementation** — 2–4 Sonnet subagents, each owning different files, briefed with:
   the docs to read, the header contract, the files they may touch, acceptance criteria
   (`-Werror` compile command, tests, offscreen previews to look at).
4. **Integration** — clean sequential build (agents building in parallel can collide in `build/`),
   `make test`, `make debug`, end-to-end checks, resource measurements.
5. **Ship** — feature branch → commit → fast-forward merge into `main` → push → wait for green CI.

## Engineering rules

- **Event-driven loop.** When nothing animates the process sleeps in `SDL_WaitEventTimeout` until the
  next real deadline (clock tick, timer, blink, cursor, notification poll). Never add polling or
  periodic wake-ups.
- **Draw only when something changed**; flips are paced at 30 fps.
- **No allocations in the main loop or per frame.** Glyphs are pre-rendered once; colours are applied
  with `SDL_SetTextureColorMod` (changing colour must not re-open the font).
- **Load on demand.** Fonts for overlays, the fontconfig font list and the audio subsystem exist only
  while needed.
- **Measure** after any change that could affect resources: idle PSS (~18 MB) and CPU over 65 s
  (≈ tens of ms per minute). Document new numbers in `docs/03-architecture.md`.
- **User data is sacred:** never delete or overwrite `~/.config/ribalta/` or
  `~/.local/share/ribalta/sessions.csv` (the uninstaller keeps them too). Tests use temporary
  `XDG_CONFIG_HOME` / `XDG_DATA_HOME`.
- **GUI tests must not disturb the user.** Prefer offscreen rendering
  (`SDL_HINT_VIDEO_DRIVER=offscreen` or `SDL_CreateSoftwareRenderer`) for previews. When the real app
  must run, use a temporary config, a separate `XDG_RUNTIME_DIR` (the single-instance lock would
  otherwise stop it if the user has Ribalta open; then pass `WAYLAND_DISPLAY` as an absolute path,
  e.g. `$XDG_RUNTIME_DIR/$WAYLAND_DISPLAY` taken before the override, or no window appears), fake
  `notify-send` and `SDL_AUDIO_DRIVER=dummy`.
- **Language:** code comments and UI strings are in Italian; README, docs and installer output in English.

## Git conventions

- Never commit directly on `main`: use a short feature branch, then fast-forward merge.
- Commit messages: Italian, `type: summary` (`feat`, `fix`, `refactor`, `style`, `docs`, `ci`, `chore`)
  plus a short bullet body.
- Push only when the user agrees; after pushing, check that CI is green.
- License: GPL-3.0-or-later (code); the bundled font keeps SIL OFL 1.1.
