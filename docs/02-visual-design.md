# 02 — Visual design

## 1. Palette

| Element | Colour | Notes |
|---|---|---|
| Background | `#000000` | pure black (ideal for OLED) |
| Tile | `#1a1a1a` | very dark grey |
| Digit | `#b7b7b7` | light grey; multiplied by `brightness` |
| Cut line | `#000000` | separates the upper and lower halves of the tile |
| Flip shadow | black with alpha 0–60% | darkens the rotating half |
| AM/PM, labels | `#b7b7b7` at 70% | small text |
| Pomodoro break (Phase 3) | digit `#6fa8dc` | soft blue to tell breaks apart |

## 2. Geometry

All measurements are relative to the available width `W` (`W = window width × scale/100`).

- **Tile pairs**: 2 (`HH:MM`) or 3 (`HH:MM:SS`). Each tile holds **two digits**.
- Tile width `Tw`: with 2 tiles `Tw = W × 0.46`; with 3 tiles `Tw = W × 0.31`.
- Gap between tiles `G = W × 0.04` (with 3 tiles `G = W × 0.035`).
- Tile height `Th = Tw × 0.92` (slightly rectangular, `TILE_H_RATIO` in render.c), capped at 85% of
  the window height (if it exceeds that, everything is scaled accordingly).
- Corner radius `R = Tw × 0.05`.
- Horizontal cut line at the centre: thickness `max(2px, Th × 0.012)`.
- Digits: IBM Plex Sans Condensed Bold, glyph height up to `Th × 0.72`, limited in width:
  cell = ink width of the widest digit × 1.10, two cells ≤ `Tw × 0.86`.
  If the width limit would reduce the height, digits are rendered at full height (up to 1.25×)
  and compressed horizontally when drawn: the result is ~0.71 × `Th`, tall and crisp glyphs.
  Centred in the tile.
- AM/PM indicator: height `Th × 0.08`, top-left, margin `Tw × 0.06`.
- No colon `:` between tiles (minimal style); the gap `G` is enough.

```
 ┌──────────────────────┐    ┌──────────────────────┐
 │                      │    │                      │
 │        ██  ██        │    │        ██  ██        │
 │ ────── 1 ── 2 ────── │    │ ────── 3 ── 4 ────── │   ← cut line
 │        ██  ██        │    │        ██  ██        │
 │                      │    │                      │
 └──────────────────────┘    └──────────────────────┘
         hours                       minutes
```

## 3. Flip animation

Every tile whose value changes (e.g. `12` → `13`) is made of 4 halves:

- `top_old`, `bottom_old` — upper and lower half of the old value
- `top_new`, `bottom_new` — upper and lower half of the new value

Sequence (total duration `D = 400 ms`, drawn at 30 fps, `t ∈ [0,1]`):

1. **Static background**: draw `top_new` on top and `bottom_old` below.
2. **Phase A (`t` 0 → 0.5)**: the `top_old` half "falls" towards the centre: it is drawn with height
   `h = Th/2 × cos(θ)`, `θ = t/0.5 × 90°`, anchored at the cut line. **Ease-in** (quadratic).
   A shadow on `top_old` grows from 0 to 60%.
3. **Phase B (`t` 0.5 → 1)**: the `bottom_new` half "opens" from the centre downwards:
   height `h = Th/2 × sin(θ)`, `θ = (t−0.5)/0.5 × 90°`, anchored at the cut line.
   **Ease-out** with a slight final bounce (overshoot ≤ 3%). Shadow from 60% to 0.
   At the same time `bottom_old` (below) darkens slightly (0 → 30%) and is then covered.
4. At the end of the animation the tile shows only `top_new` + `bottom_new`.

The perspective is **simulated with vertical scaling** (no 3D projection): a plain
`SDL_RenderTexture` with a squashed destination rectangle is enough. The halves are sub-rectangles
of the tile's pre-rendered texture (see the architecture document).

When several tiles change at the same instant (e.g. `12:59` → `13:00`) they start together;
with `animations=false` the change is instantaneous.

## 4. Layouts (mockups)

### Clock `HH:MM`
```
┌────────────────────────────────────────────────────────┐
│                                                        │
│   ┌──────────────────────┐   ┌──────────────────────┐  │
│   │PM                    │   │                      │  │
│   │         1 2          │   │         3 4          │  │
│   │──────────────────────│   │──────────────────────│  │
│   │                      │   │                      │  │
│   └──────────────────────┘   └──────────────────────┘  │
│                                                        │
└────────────────────────────────────────────────────────┘
```

### Clock `HH:MM:SS`
```
┌────────────────────────────────────────────────────────┐
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐  │
│  │      1 2     │  │      3 4     │  │      5 6     │  │
│  │──────────────│  │──────────────│  │──────────────│  │
│  └──────────────┘  └──────────────┘  └──────────────┘  │
└────────────────────────────────────────────────────────┘
```

### Timer (running)
```
┌────────────────────────────────────────────────────────┐
│   TIMER                                                │
│   ┌──────────────────────┐   ┌──────────────────────┐  │
│   │         0 2          │   │         2 9          │  │
│   │──────────────────────│   │──────────────────────│  │
│   └──────────────────────┘   └──────────────────────┘  │
│   ██████████████░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░   │ ← progress bar
└────────────────────────────────────────────────────────┘
```
The `TIMER` label (or `PAUSA` when paused) is small, top-left, at 50% opacity. With an active subject
it becomes `TIMER · Analisi` (Pomodoro study: `STUDIO 2/4 · Analisi`).

### Settings panel (overlay)
```
┌────────────────────────────────────────────────────────┐
│        ┌──────────────────────────────────┐            │
│        │  IMPOSTAZIONI                    │            │
│        │  ▸ Mostra secondi        [ no ]  │            │
│        │    Formato 24h           [ sì ]  │            │
│        │    Timer predefinito     [60 m]  │            │
│        │    Suono fine timer      [ sì ]  │            │
│        │    Notifica              [ sì ]  │            │
│        │    Dimensione            [ 90%]  │            │
│        │    Luminosità            [100%]  │            │
│        │    Schermo intero avvio  [ no ]  │            │
│        │    Animazioni            [ sì ]  │            │
│        │  Esc per chiudere                │            │
│        └──────────────────────────────────┘            │
└────────────────────────────────────────────────────────┘
```
(The labels in the mockup are the app's current Italian UI strings: "Show seconds", "24h format",
"Default timer", "Timer sound", "Notification", "Size", "Brightness", "Start fullscreen",
"Animations", "Esc to close".)

Overlay background `#000000` at 75%, panel `#1a1a1a`, text `#b7b7b7`, selected entry in white.
The panel has 16 rows; "Digit font" ("Font cifre") follows "Digit colour" and its value is ellipsised
with `...` when it does not fit the value column.

### Font menu (overlay)

Same style as the colour picker: light overlay, panel on the right, tiles left visible for the live preview.
```
┌──────────────────────────────┐
│ FONT CIFRE                   │
│ ┌──────────────────────────┐ │
│ │ cerca: dej█              │ │ ← search field
│ └──────────────────────────┘ │
│ ▸ DejaVu Sans                │ ← selected row (white, highlighted)
│   DejaVu Sans Mono           │
│   DejaVu Serif               │ ← scrolling list, visible rows only
│                              │
│        12:34                 │ ← preview in the selected font
│ ↑↓ scegli · Invio ok · Esc   │ ← hint
└──────────────────────────────┘
```
The first entry is always the bundled font ("Ribalta (IBM Plex Sans Condensed)"). Panel text uses the
bundled font; only the "12:34" preview uses the selected one.

### Subjects menu (overlay, `M`)

Same style as the font menu: light overlay, panel on the right, tiles visible.
```
┌──────────────────────────────┐
│ MATERIE                      │
│   Nessuna materia            │
│ ▸ ● Analisi                  │ ← selected row; ● = active subject
│   Fisica                     │
│   Diritto privato            │
│   Nome della materia|        │ ← input row (after N), static caret
│   Materia già presente       │ ← error in red
│ Invio sceglie · N nuova ·    │
│ Canc elimina · Esc chiude    │ ← hint
└──────────────────────────────┘
```

### Statistics panel: subject filter

Under the title, `‹ Tutte ›` / `‹ Analisi ›` / `‹ Senza materia ›` shows the filter (`←`/`→`). In the
"Tutte" view a section "Per materia (7 giorni)" lists up to 4 subjects with their hours.

## 5. Font

A bold sans-serif font under the **OFL** licence, bundled in `assets/` (IBM Plex Sans Condensed
Bold, chosen during implementation: a grotesque-style family with tabular figures). The licence
file is kept next to the font. No proprietary fonts.
