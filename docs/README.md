# Ribalta — documentation

**Ribalta** is a flip clock for Linux, inspired by the classic flip-clock look but with original
artwork and code. It shows the time fullscreen or in a window, with digits that flip over, and
includes a study timer, a Pomodoro mode, study statistics with goals and an in-app legend. It must use very little RAM and stay at
practically 0% CPU when nothing is animating. Written in C with SDL3, it runs natively on
Wayland (tested on niri) and on X11.

## Index

| File | Contents |
|---|---|
| [01-functional-spec.md](01-functional-spec.md) | What the app does: clock, timer, settings (keys, defaults, ranges, steps), subjects, statistics panel and file, goals, Pomodoro, legend and hint, screensaver, controls, window, non-functional requirements, command line |
| [02-visual-design.md](02-visual-design.md) | Palette, tile proportions, flip animation, layout mockups (clock, timer, settings, font menu, colour picker, subjects menu, legend, hint, statistics) |
| [03-architecture.md](03-architecture.md) | Stack (C17 + SDL3), RAM/CPU strategy and measurements, repository layout and main entry points, Flatpak and macOS packaging, Makefile, installer, update and uninstall scripts, CI, agent-based development workflow |
| [04-roadmap.md](04-roadmap.md) | Development phases 1–14 (MVP, timer, Pomodoro, statistics, screensaver, colours and fonts, Flatpak, macOS, subjects, goals, terminal commands, legend, installer fix) and future ideas |

## A note on the name

The project is called `ribalta` and uses no assets, fonts, icons or trademarks from Fliqlo:
the "flip clock" look is a generic style, recreated from scratch.

Note: the app's user interface is currently in Italian, so UI labels quoted in these documents
(`TIMER`, `RIPOSO`, `IMPOSTAZIONI`, ...) are the literal on-screen strings.
