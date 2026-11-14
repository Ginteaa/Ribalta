# 01 — Specifiche funzionali

## 1. Modalità

L'app ha una modalità attiva alla volta. All'avvio parte sempre in **Orologio**.

### 1.1 Orologio (default)

- Mostra l'ora locale corrente come `HH:MM` (senza secondi).
- Formato 24h di default; con `clock_24h=false` usa 12h e mostra un piccolo indicatore `AM`/`PM`
  nell'angolo in alto a sinistra della tessera delle ore.
- Con `show_seconds=true` mostra `HH:MM:SS` (terza coppia di tessere).
- A ogni cambio di cifra parte l'animazione flip **solo sulle tessere che cambiano**.
- Le ore nel formato 24h hanno sempre due cifre (`09:05`); nel formato 12h la cifra iniziale `0`
  è nascosta (` 9:05`).

### 1.2 Timer di studio

- L'utente imposta una durata in **ore e minuti** (da 0h01 a 23h59), es. 2h30 per studiare 2 ore e mezza.
- Stati: `IMPOSTAZIONE` → `IN CORSO` ⇄ `IN PAUSA` → `FINITO`.
- **IMPOSTAZIONE**: le tessere mostrano la durata scelta (`HH:MM`), la cifra selezionata lampeggia
  leggermente (opacità). Frecce ↑/↓ cambiano il valore (±1 min, Shift = ±10 min,
  Ctrl = ±1 h), ←/→ spostano la selezione tra ore e minuti.
- **IN CORSO**: countdown con le stesse tessere flip.
  - Se la durata rimanente è ≥ 1 h → mostra `HH:MM` (i minuti si aggiornano a ogni minuto intero).
  - Se < 1 h → mostra `MM:SS`.
  - Se `show_seconds=true` → mostra sempre `HH:MM:SS`.
  - Una sottile barra di avanzamento in basso (2 px, colore della cifra al 30%) indica il tempo trascorso.
- **IN PAUSA**: le cifre si attenuano (opacità 50%), il tempo non scorre.
- **FINITO**:
  - notifica desktop "Sessione di studio completata (2h30)" tramite `notify-send`;
  - suono breve opzionale (`timer_sound=true`), riprodotto con `SDL_PlayAudio` da un WAV incluso;
  - le tessere mostrano `00:00` e lampeggiano 3 volte, poi l'app torna in **Orologio**.
- Il timer usa un orologio monotono: non è influenzato da cambi di ora di sistema o NTP.
- Se si chiude l'app con il timer in corso, il timer è perso (in Fase 3 la sessione parziale viene
  comunque registrata nelle statistiche).

### 1.3 Pannello impostazioni

Overlay semitrasparente disegnato con SDL sopra l'orologio (nessuna libreria GUI esterna).
Navigazione da tastiera: ↑/↓ per scegliere la voce, ←/→ o Invio per cambiarla, `Esc`/`S` per chiudere.
Ogni modifica è applicata subito e salvata alla chiusura del pannello.

| Chiave | Tipo | Default | Descrizione |
|---|---|---|---|
| `show_seconds` | bool | `false` | Mostra i secondi |
| `clock_24h` | bool | `true` | Formato 24h (false = 12h con AM/PM) |
| `timer_default_minutes` | int | `60` | Durata proposta quando si entra in modalità Timer |
| `timer_sound` | bool | `true` | Suono a fine timer |
| `timer_notify` | bool | `true` | Notifica desktop a fine timer |
| `scale` | int (50–100) | `90` | Larghezza occupata dalle tessere, in % della finestra |
| `brightness` | int (20–100) | `100` | Luminosità delle cifre (utile di notte) |
| `start_fullscreen` | bool | `false` | Avvio a schermo intero |
| `animations` | bool | `true` | Se false, le cifre cambiano senza animazione (consumo minimo) |

File: `~/.config/flipclock/config.ini` (rispetta `$XDG_CONFIG_HOME`). Formato `chiave=valore`,
righe con `#` ignorate, chiavi sconosciute ignorate, valori non validi → default.
Se il file non esiste viene creato al primo salvataggio.

## 2. Controlli

| Tasto | Azione |
|---|---|
| `F` / `F11` / doppio clic | Schermo intero on/off |
| `T` | Entra/esce dalla modalità Timer (uscendo con timer attivo chiede conferma premendo di nuovo `T`) |
| `Spazio` / `Invio` | Timer: avvia / pausa / riprendi |
| `R` | Timer: reset alla durata impostata |
| `↑ ↓ ← →` | Timer in impostazione: regola durata. Pannello: naviga |
| `S` | Apre/chiude il pannello impostazioni |
| `Esc` | Chiude il pannello; altrimenti esce dallo schermo intero; altrimenti nulla |
| `Q` / `Ctrl+Q` | Esci |

Il cursore del mouse si nasconde dopo 2 s di inattività in schermo intero.

## 3. Finestra

- Titolo `flipclock`, app-id Wayland `flipclock` (per regole del compositor, es. niri `window-rule`).
- Ridimensionabile; il layout si adatta mantenendo le proporzioni e restando centrato.
- Dimensione iniziale 960×400 (logici), supporto HiDPI (`SDL_WINDOW_HIGH_PIXEL_DENSITY`).

## 4. Requisiti non funzionali

| Requisito | Obiettivo |
|---|---|
| RAM (RSS) a regime | < 20 MB (obiettivo ideale < 12 MB) |
| CPU a riposo (tra un'animazione e l'altra) | ~0% — il processo dorme in attesa di eventi |
| CPU durante il flip | breve picco, animazione ≤ 400 ms |
| Tempo di avvio | < 200 ms |
| Dipendenze runtime | SDL3 + SDL3_ttf (+ `notify-send` opzionale) |
| Precisione | il cambio di minuto avviene entro 50 ms dal minuto reale |
| Piattaforme | Linux Wayland (prioritario), X11 |
