# 03 — Architettura tecnica

## 1. Stack

| Componente | Scelta | Motivo |
|---|---|---|
| Linguaggio | **C17** | nessun runtime, binario piccolo, controllo totale della memoria |
| Finestra / rendering / input / audio | **SDL3** (già installato, 3.4.x) | backend Wayland nativo, renderer GPU, un'unica dipendenza |
| Rasterizzazione font | **SDL3_ttf** (3.2.x) | API semplice; usato solo per generare le texture una volta all'avvio/resize. Il font viene chiuso dopo la generazione per liberare memoria |
| Notifiche | processo `notify-send` (`fork`+`execvp`) | evita di linkare libnotify/GLib (diversi MB di RAM) |
| Build | **Makefile** + `pkg-config` | semplice, nessun build system extra |

### Confronto (RAM indicativa a regime)

| Opzione | RAM | CPU a riposo | Note |
|---|---|---|---|
| **C + SDL3 (scelta)** | ~8–15 MB | ~0% | buon compromesso tra leggerezza e semplicità |
| C + Wayland + Cairo diretto | ~3–6 MB | ~0% | molto più codice (protocolli xdg-shell, buffer shm, input) |
| C + GTK4 | ~40–70 MB | bassa | GTK4 carica GL, CSS, GLib/GObject |
| Tauri / webview | ~80–150 MB | media | motore web completo |
| Electron | ~150–300 MB | media/alta | Chromium intero |

> **Misure Fase 1** (Wayland/niri, 65 s inclusi un flip, PSS da `/proc/<pid>/smaps_rollup`):
>
> | Renderer SDL | PSS | RSS | CPU in 65 s |
> |---|---|---|---|
> | `gpu` (SDL_GPU / Vulkan) — **default** | ~17 MB | ~27 MB | ~50 ms |
> | `opengl` | ~37 MB | ~103 MB | ~40 ms |
> | `software` | RSS ~113 MB (carica comunque EGL) | | |
>
> Ordine impostato: `gpu,opengl,opengles2,software`; `SDL_RENDER_DRIVER=<nome>` lo sovrascrive.
> Binario release: ~27 KB.

## 2. Strategia per consumare pochissime risorse

1. **Loop guidato dagli eventi, non a frame fissi.**
   Quando non c'è animazione il thread principale chiama
   `SDL_WaitEventTimeout(&ev, ms_al_prossimo_cambio)`: il processo dorme fino al prossimo
   minuto (o secondo, se visibili) o a un input. Niente polling a 60 fps.
2. **Render solo quando serve.** Si ridisegna solo se: parte/continua un'animazione, la finestra
   cambia dimensione/esposizione, si apre il pannello. Durante il flip si usa il vsync
   (`SDL_SetRenderVSync(renderer, 1)`), ~24 frame per 400 ms, poi si torna a dormire.
3. **Texture pre-renderizzate.** All'avvio (e a ogni resize) si generano le texture delle cifre
   `0–9` alla dimensione esatta con SDL3_ttf, e la texture della tessera vuota. Durante il flip
   si compongono solo rettangoli già in GPU: zero rasterizzazione per frame.
   Le texture si rigenerano solo al resize (con debounce di 150 ms).
4. **Tempo corretto senza drift.**
   - Orologio: `clock_gettime(CLOCK_REALTIME)` + `localtime_r`; il timeout di attesa è calcolato
     fino al prossimo confine di minuto/secondo, +5 ms di margine.
   - Timer: `SDL_GetTicksNS()` (monotono); si memorizza l'istante di fine, non si decrementa un contatore.
5. **Nessun thread aggiuntivo**, nessuna allocazione dinamica nel ciclo principale.
6. **Hint SDL**: `SDL_HINT_VIDEO_DRIVER="wayland,x11"`, `SDL_HINT_APP_ID="flipclock"`,
   `SDL_HINT_VIDEO_ALLOW_SCREENSAVER="1"` (tranne con timer attivo, Fase 3).
7. **Build release** con `-O2`, `-flto`, `-s` (strip) per un binario piccolo.

Verifica dei consumi: `ps -o rss,pcpu -C flipclock`, `pidstat -p <pid> 1`, `/usr/bin/time -v`.

## 3. Struttura del repository

```
Fliqlo_linux/
├── Makefile
├── README.md
├── docs/
├── assets/
│   ├── font.ttf          (IBM Plex Sans Condensed Bold, OFL) + OFL.txt
│   └── ding.wav          (suono fine timer, breve, libero da diritti)
└── src/
    ├── main.c            init SDL, loop eventi, dispatch tasti, scelta del timeout
    ├── app.h             struct App condivisa (stato globale, modalità)
    ├── clock.c/.h        lettura ora locale → valori delle tessere, prossimo confine
    ├── timer.c/.h        macchina a stati del timer (IMPOSTAZIONE/IN CORSO/IN PAUSA/FINITO)
    ├── render.c/.h       layout, texture cifre/tessere, animazione flip, barra avanzamento
    ├── font.c/.h         caricamento TTF, rasterizzazione glifi con SDL3_ttf → SDL_Texture
    ├── settings.c/.h     lettura/scrittura config.ini (XDG)
    ├── ui_settings.c/.h  pannello impostazioni (overlay)
    └── notify.c/.h       notify-send + suono SDL
```

I file degli asset vengono cercati, in ordine: `$FLIPCLOCK_DATA`, `./assets`,
`~/.local/share/flipclock`, `/usr/share/flipclock`.

### Interfacce principali (bozza)

```c
/* app.h */
typedef enum { MODE_CLOCK, MODE_TIMER } Mode;

typedef struct {
    int  value;          /* 0..99 mostrato sulla tessera, -1 = vuota */
    int  prev_value;
    Uint64 anim_start_ns;/* 0 = nessuna animazione */
} Tile;

typedef struct {
    SDL_Window   *win;
    SDL_Renderer *ren;
    Settings      cfg;
    Mode          mode;
    Timer         timer;
    Tile          tiles[3];
    int           tile_count;     /* 2 o 3 */
    bool          settings_open;
    bool          dirty;          /* serve un ridisegno */
} App;

/* render.h */
bool render_init(App *a);                /* crea texture */
void render_resize(App *a);              /* rigenera texture alla nuova dimensione */
bool render_frame(App *a, Uint64 now);   /* true se c'è ancora un'animazione in corso */
void tile_set(Tile *t, int v, Uint64 now, bool animate);

/* clock.h */
void   clock_values(const Settings *s, int out[3], bool *pm);
Uint32 clock_ms_to_next_change(const Settings *s);
```

## 4. Makefile

```make
CC      ?= cc
PKGS    := sdl3 sdl3-ttf
CFLAGS  ?= -std=c17 -Wall -Wextra -Wpedantic -O2
CFLAGS  += $(shell pkg-config --cflags $(PKGS))
LDLIBS  += $(shell pkg-config --libs $(PKGS)) -lm
SRC     := $(wildcard src/*.c)
OBJ     := $(SRC:src/%.c=build/%.o)
BIN     := flipclock
PREFIX  ?= $(HOME)/.local

all: $(BIN)
$(BIN): $(OBJ)            ; $(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)
build/%.o: src/%.c | build ; $(CC) $(CFLAGS) -MMD -c -o $@ $<
build:                    ; mkdir -p build

debug:   CFLAGS += -O0 -g -fsanitize=address,undefined
debug:   LDFLAGS += -fsanitize=address,undefined
debug:   clean $(BIN)
release: CFLAGS += -flto -DNDEBUG
release: LDFLAGS += -flto -s
release: clean $(BIN)

install: release
	install -Dm755 $(BIN) $(PREFIX)/bin/$(BIN)
	install -Dm644 -t $(PREFIX)/share/flipclock assets/*
	install -Dm644 flipclock.desktop $(PREFIX)/share/applications/flipclock.desktop
uninstall:
	rm -rf $(PREFIX)/bin/$(BIN) $(PREFIX)/share/flipclock $(PREFIX)/share/applications/flipclock.desktop
clean:
	rm -rf build $(BIN)

-include $(OBJ:.o=.d)
.PHONY: all debug release install uninstall clean
```

(Le ricette su una riga sono abbreviate per leggibilità: nel Makefile reale vanno su righe con TAB.)

## 5. Sviluppo con agenti

- **Orchestratore — Claude Opus 5.5** (sessione principale): mantiene le specifiche in `docs/`,
  divide ogni fase in task per modulo, definisce le interfacce (`*.h`) prima del codice, revisiona,
  integra, compila con `make` / `make debug` e misura RAM/CPU.
- **Implementazione — subagenti Claude Sonnet 5.5** (`model: "sonnet"`): uno per task/modulo
  (es. `settings.c`, `render.c` + `font.c`, `timer.c`). Ogni prompt indica i file `docs/` da leggere,
  l'header da rispettare e i criteri di accettazione (compila senza warning, comportamento atteso).
- Task indipendenti in parallelo; quelli che toccano gli stessi file in sequenza.
- Dopo ogni fase: `make debug` (ASan/UBSan), esecuzione manuale, misura consumi, aggiornamento docs.
