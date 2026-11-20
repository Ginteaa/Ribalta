# 02 — Visual design

## 1. Palette

| Element | Colour | Notes |
|---|---|---|
| Background | `#000000` | pure black (ideal for OLED) |
| Tile | `#1a1a1a` | very dark grey |
| Digit | `#b7b7b7` | light grey; multiplied by `brightness` |
| Cut line | `#000000` | separates the upper and lower halves of the tile |
| Flip shadow | black with alpha 0–60% | darkens the rotating half |
| AM/PM | accent colour at 70% | small text; the accent is the digit colour × `brightness` (default `#b7b7b7`) |
| `TIMER` / `PAUSA` / Pomodoro and subject labels | accent colour at 50% | small text |
| Pomodoro break (Phase 3, 9) | digit colour (default), `#6fa8dc` with `pomodoro_break_color=auto`, or a custom colour (`pomodoro_break_color=custom`, `pomodoro_break_custom`) | optional colour to tell breaks apart |
| Legend (`H`) | keys `#ffffff`, descriptions `#b7b7b7`, section headers `#808080`, title as the other panels | overlay text |
| Hint `H · legenda` | digit colour (accent) at about 35% alpha | small, bottom-right, see §4 |

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
   **Ease-out** with a very slight final bounce (overshoot < 1%: peak ≈ 1.0035). Shadow from 60% to 0.
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
│        ┌────────────────────────────────────┐          │
│        │  IMPOSTAZIONI                      │          │
│        │  ▸ Mostra secondi          [ no ]  │          │
│        │    Formato 24 ore          [ sì ]  │          │
│        │    Timer predefinito       [60 m]  │          │
│        │    Suono fine timer        [ sì ]  │          │
│        │    Notifica fine timer     [ sì ]  │          │
│        │    Dimensione              [ 90%]  │          │
│        │    Luminosità              [100%]  │          │
│        │    Colore cifre         [■] #b7b7b7│          │
│        │    Font cifre              [ ... ] │          │
│        │    Schermo intero all'avvio [ no ] │          │
│        │    Animazioni              [ sì ]  │          │
│        │    …  (Pomodoro and goal rows)     │          │
│        │  Durata proposta quando apri il    │          │
│        │  timer (1 min – 23h59), Shift ±1 h │          │
│        │  ↑↓ scegli · ←→ cambia · Esc chiudi│          │
│        └────────────────────────────────────┘          │
└────────────────────────────────────────────────────────┘
```
(The labels in the mockup are the app's literal Italian strings.) The mockup shows the row "Timer
predefinito" selected, so the description line is its real text. The title `IMPOSTAZIONI` is `#808080`;
"Colore cifre" shows a colour swatch plus the hex value (`#b7b7b7`).

Overlay background `#000000` at 75%, panel `#1a1a1a`, text `#b7b7b7`, selected entry in white.
The panel has 19 rows, in this order: Mostra secondi, Formato 24 ore, Timer predefinito, Suono fine
timer, Notifica fine timer, Dimensione, Luminosità, Colore cifre, Font cifre, Schermo intero
all'avvio, Animazioni, Pomodoro: studio, Pomodoro: riposo, Pomodoro: riposo lungo, Pomodoro: cicli,
Pomodoro: avvio automatico, Pomodoro: colore riposo, Obiettivo giornaliero, Obiettivo settimanale.
The value of "Font cifre" is ellipsised with `...` when it does not fit the value column.

The description line sits above the footer: dim grey `#808080` like the footer, at 0.9× the panel
text, that explains the selected entry: what it does, its range and, for numbers, the step with
`Shift`. It has at most two lines and changes with the selection; the panel grows by about two rows
to hold it. The footer is `↑↓ scegli · ←→ cambia · Esc chiudi`.

### Font menu (overlay)

Light overlay (`#000000` at 35%, alpha 89), panel `#1a1a1a` on the right, tiles left visible for the
live preview.
```
┌──────────────────────────────┐
│ Font cifre                   │ ← title (white, mixed case)
│ ┌──────────────────────────┐ │
│ │ Cerca…                   │ │ ← search field (placeholder; the typed filter + caret otherwise)
│ └──────────────────────────┘ │
│▌▸ DejaVu Sans                │ ← selected row: #6fa8dc at alpha 70, #6fa8dc marker, white text
│   DejaVu Sans Mono           │
│   DejaVu Serif               │ ← scrolling list, visible rows only
│                              │
│ ┌──────────────────────────┐ │
│ │          12:34           │ │ ← preview in the selected font ("Anteprima non disponibile")
│ └──────────────────────────┘ │
│ Invio applica · Esc annulla  │ ← hint (#808080)
└──────────────────────────────┘
```
The first entry is always the bundled font ("Ribalta (IBM Plex Sans Condensed)"). Panel text uses the
bundled font; only the "12:34" preview uses the selected one. If the selected font cannot be rendered
the preview shows "Anteprima non disponibile".

### Colour picker (overlay)

Opened with `Invio` on "Colore cifre" or "Pomodoro: colore riposo". Light overlay (alpha 89), panel on
the right (width `min(42% of W, 560 × density)`, `density = max(1, H/1080)`), tiles left visible.
Stacked from the top:
```
┌──────────────────────────────────────┐
│ ┌──────────────────────────────────┐ │
│ │                                  │ │ ← saturation / value square
│ └──────────────────────────────────┘ │
│ ┌──────────────────────────────────┐ │ ← hue bar
│ └──────────────────────────────────┘ │
│ [■] [ #b7b7b7  ]  RGB  183, 183, 183 │ ← swatch, editable HEX field,
│                   HSV  0, 0, 72      │   read-only RGB / HSV text
│ ■■■■■■■■■■■■■■                       │
│ ■■■■■■■■■■■■■■                       │ ← palette grid (14 columns × 3 rows + neutrals row)
│ ■■■■■■■■■■■■■■                       │
│ ■■■■■■■■■■■■■■                       │
│ Recenti                              │
│ ■ ■ ■ ■ ■                            │ ← up to 5 recent colours
│ Tab zona · frecce · Invio applica ·  │
│ Esc annulla                          │ ← footer (shrinks to fit the width)
└──────────────────────────────────────┘
```

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
While typing a new name the hint becomes `Invio aggiunge · Esc annulla`. The error line (red) can be
"Nome non valido", "Materia già presente" or "Elenco pieno (max 8)".

### Legend (overlay, `H` / `F1`)

Overlay `#000000` at 75% (alpha 191), panel `#1a1a1a`, title `LEGENDA` in `#808080`. Five sections
(headers drawn as written, `#808080`): "Generale", "Timer", "Pomodoro", "Pannelli", "File". Keys are
white, descriptions `#b7b7b7`. The footer `H · F1 · Esc chiudi` (`#808080`) is centred.

Layout: two columns (Generale + File on the left; Timer, Pomodoro, Pannelli on the right) when the
window is at least 1.25× wider than tall and the text fits at a font size not smaller than in a single
column; otherwise one column. The font starts at window height/34 and shrinks (minimum 8 px) until the
panel fits in 90% of the width and 94% of the height. The "File" rows show the real paths (with `~`
for the home directory, shortened from the left with `…` if needed) and `ribalta --help`.
The mockup shows the wide layout.
```
┌────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┐
│ LEGENDA                                                                                                                                │
│                                                                                                                                        │
│ Generale                                                                    Timer                                                      │
│ S                      Impostazioni                                         Spazio · Invio  Avvia, pausa, riprendi                     │
│ I                      Statistiche                                          0–9             Scrivi la durata (1 3 0 → 01:30)           │
│ M                      Materie                                              Backspace       Cancella l'ultima cifra                    │
│ T                      Timer / orologio                                     ← →             Ore o minuti                               │
│ P                      Pomodoro / orologio                                  ↑ ↓             Regola (Shift ±10 min)                     │
│ F · F11 · doppio clic  Schermo intero                                       R               Azzera                                     │
│ H · F1                 Questa legenda                                                                                                  │
│ Esc                    Chiude il pannello o esce dallo schermo intero       Pomodoro                                                   │
│ Q · Ctrl+Q             Esci                                                 Spazio · Invio  Avvia, pausa, riprendi                     │
│                                                                             N               Fase successiva                            │
│ File                                                                        R               Ricomincia da capo                         │
│ Impostazioni  ~/.config/ribalta/config.ini                                                                                             │
│ Statistiche   ~/.local/share/ribalta/sessions.csv                           Pannelli                                                   │
│ Terminale     ribalta --help                                                ↑ ↓             Scegli la voce                             │
│                                                                             ← →             Cambia il valore (Shift: passo più grande) │
│                                                                             Invio           Selettore colore o menu dei font           │
│                                                                             ← → in Statistiche  Filtra per materia                     │
│                                                                             N · Canc in Materie  Nuova materia, elimina                │
│                                                                                                                                        │
│                                                          H · F1 · Esc chiudi                                                           │
└────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┘
```
(Schematic: spacing is approximate; all descriptions are on one line in the app.)

### Hint (`H · legenda`)

A small, faint text in the bottom-right corner of the window, shown at startup and on every mouse
movement, for 3 s. Its colour is the accent (the digit colour) at about 35% alpha; the capital-letter
height is 1.7% of the window height (minimum 8 px), i.e. a text line of about `max(11 px, 2.2%)`, with
a margin from the right and bottom edges of 1.2× the text height. It is drawn over the tiles and is
not part of the layout: the tiles do not move.
```
┌──────────────────────────────────────────────────────────────────┐
│                                                                  │
│                                                                  │
│                  ┌──────────┐    ┌──────────┐                    │
│                  │    1 2   │    │    3 4   │                    │
│                  └──────────┘    └──────────┘                    │
│                                                                  │
│                                              H · legenda  ← 3 s  │
└──────────────────────────────────────────────────────────────────┘
```

### Statistics panel (overlay, `I`)

Centred panel `#1a1a1a`; title `STATISTICHE` and section headers `#808080`, filter white, rows
`#b7b7b7` (today and the current day in white). Under the title the filter `‹ Tutte ›` /
`‹ Analisi ›` / `‹ Senza materia ›` (`←`/`→`). The "Per materia (7 giorni)" section appears only in
the "Tutte" view and lists up to 4 subjects with their hours; the goals block (below) only if a goal
is set.
```
┌──────────────────────────────────────┐
│ STATISTICHE                          │
│            ‹  Tutte  ›               │
│ Oggi                          1h 40m │
│ Questo mese                  32h 10m │
│ Giorni consecutivi                 5 │
│ Sessioni completate               48 │
│ Ultimi 7 giorni                      │
│ lun  ██████░░░░░░░░░░░░░░░░    1h 05m │
│ mar  ████████████░░░░░░░░░░    2h 10m │
│ mer  ░░░░░░░░░░░░░░░░░░░░░░       0m │
│ gio  ████████░░░░░░░░░░░░░░    1h 30m │
│ ven  ██████████░░░░░░░░░░░░    1h 50m │
│ sab  ████░░░░░░░░░░░░░░░░░░      40m │
│ dom  █████████░░░░░░░░░░░░░    1h 40m │ ← today, white
│ Per materia (7 giorni)               │
│ Analisi                        5h 10m │
│ Fisica                         3h 00m │
│ Obiettivi  (goals block, see below)  │
│ ←/→ materia · Esc chiudi             │
└──────────────────────────────────────┘
```

### Statistics panel: goals block

Shown only if a goal is set, below the summary. Bars grow with the progress and are full and
highlighted once reached.
```
Obiettivi
Oggi       [███████████░░░░░]  1h 40m / 3h
Settimana  [████████░░░░░░░░]  8h / 15h
Obiettivo raggiunto 2 giorni di fila
```

## 5. Font

A bold sans-serif font under the **OFL** licence, bundled in `assets/` (IBM Plex Sans Condensed
Bold, chosen during implementation: a grotesque-style family with tabular figures). The licence
file is kept next to the font. No proprietary fonts.
