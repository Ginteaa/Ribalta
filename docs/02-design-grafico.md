# 02 — Design grafico

## 1. Palette

| Elemento | Colore | Note |
|---|---|---|
| Sfondo | `#000000` | nero pieno (ideale per OLED) |
| Tessera | `#1a1a1a` | grigio molto scuro |
| Cifra | `#b7b7b7` | grigio chiaro; moltiplicato per `brightness` |
| Linea di taglio | `#000000` | separa la metà superiore e inferiore della tessera |
| Ombra del flip | nero con alpha 0–60% | scurisce la metà in rotazione |
| AM/PM, etichette | `#b7b7b7` al 70% | testo piccolo |
| Pausa Pomodoro (Fase 3) | cifra `#6fa8dc` | blu tenue per distinguere la pausa |

## 2. Geometria

Tutte le misure sono relative alla larghezza `W` disponibile (`W = larghezza finestra × scale/100`).

- **Coppie di tessere**: 2 (`HH:MM`) o 3 (`HH:MM:SS`). Ogni tessera contiene **due cifre**.
- Larghezza tessera `Tw`: con 2 tessere `Tw = W × 0.46`; con 3 tessere `Tw = W × 0.31`.
- Spazio tra tessere `G = W × 0.04` (con 3 tessere `G = W × 0.035`).
- Altezza tessera `Th = Tw × 0.92` (leggermente rettangolare, `TILE_H_RATIO` in render.c), limitata all'85% dell'altezza finestra
  (se supera, si scala tutto di conseguenza).
- Raggio angoli `R = Tw × 0.05`.
- Linea di taglio orizzontale al centro: spessore `max(2px, Th × 0.012)`.
- Cifre: IBM Plex Sans Condensed Bold, altezza glifo fino a `Th × 0.72`, limitata in larghezza:
  cella = larghezza d'inchiostro della cifra più larga × 1.10, due celle ≤ `Tw × 0.86`
  Se la larghezza riduce l'altezza, le cifre vengono renderizzate alla piena altezza (fino a 1.25×)
  e compresse in orizzontale al disegno: risultato ~0.71 × `Th`, glifi alti e nitidi. Centrate nella tessera.
- Indicatore AM/PM: altezza `Th × 0.08`, in alto a sinistra, margine `Tw × 0.06`.
- Niente due punti `:` tra le tessere (stile minimale), lo spazio `G` basta.

```
 ┌──────────────────────┐    ┌──────────────────────┐
 │                      │    │                      │
 │        ██  ██        │    │        ██  ██        │
 │ ────── 1 ── 2 ────── │    │ ────── 3 ── 4 ────── │   ← linea di taglio
 │        ██  ██        │    │        ██  ██        │
 │                      │    │                      │
 └──────────────────────┘    └──────────────────────┘
          ore                        minuti
```

## 3. Animazione flip

Ogni tessera che cambia valore (es. `12` → `13`) è composta da 4 metà:

- `top_old`, `bottom_old` — metà superiore e inferiore del valore vecchio
- `top_new`, `bottom_new` — metà superiore e inferiore del valore nuovo

Sequenza (durata totale `D = 400 ms`, `t ∈ [0,1]`):

1. **Sfondo fisso**: disegna `top_new` sopra e `bottom_old` sotto.
2. **Fase A (`t` 0 → 0.5)**: la metà `top_old` "cade" verso il centro: viene disegnata con altezza
   `h = Th/2 × cos(θ)`, `θ = t/0.5 × 90°`, ancorata alla linea di taglio. Easing **ease-in** (quadratico).
   Ombra su `top_old` che cresce da 0 a 60%.
3. **Fase B (`t` 0.5 → 1)**: la metà `bottom_new` si "apre" dal centro verso il basso:
   altezza `h = Th/2 × sin(θ)`, `θ = (t−0.5)/0.5 × 90°`, ancorata alla linea di taglio.
   Easing **ease-out** con leggero rimbalzo finale (overshoot ≤ 3%). Ombra da 60% a 0.
   Contemporaneamente `bottom_old` (sotto) si scurisce leggermente (0 → 30%) e poi viene coperto.
4. A fine animazione la tessera mostra solo `top_new` + `bottom_new`.

È una prospettiva **simulata con scala verticale** (nessuna proiezione 3D): basta
`SDL_RenderTexture` con rettangolo di destinazione schiacciato. Le metà sono sottorettangoli della
texture pre-renderizzata della tessera (vedi architettura).

Quando cambiano più tessere nello stesso istante (es. `12:59` → `13:00`), partono insieme;
con `animations=false` il cambio è istantaneo.

## 4. Layout (mockup)

### Orologio `HH:MM`
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

### Orologio `HH:MM:SS`
```
┌────────────────────────────────────────────────────────┐
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐  │
│  │      1 2     │  │      3 4     │  │      5 6     │  │
│  │──────────────│  │──────────────│  │──────────────│  │
│  └──────────────┘  └──────────────┘  └──────────────┘  │
└────────────────────────────────────────────────────────┘
```

### Timer (in corso)
```
┌────────────────────────────────────────────────────────┐
│   TIMER                                                │
│   ┌──────────────────────┐   ┌──────────────────────┐  │
│   │         0 2          │   │         2 9          │  │
│   │──────────────────────│   │──────────────────────│  │
│   └──────────────────────┘   └──────────────────────┘  │
│   ██████████████░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░   │ ← barra avanzamento
└────────────────────────────────────────────────────────┘
```
L'etichetta `TIMER` (o `PAUSA`) è piccola, in alto a sinistra, al 50% di opacità.

### Pannello impostazioni (overlay)
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
Sfondo overlay `#000000` al 75%, pannello `#1a1a1a`, testo `#b7b7b7`, voce selezionata in bianco.

## 5. Font

Font sans-serif bold con licenza **OFL** incluso in `assets/` (scelta in fase di implementazione,
es. una famiglia tipo grotesk/neo-grotesk con cifre tabulari). Il file di licenza va copiato
accanto al font. Nessun font proprietario.
