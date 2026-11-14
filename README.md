# flipclock

Orologio flip leggero per Linux (Wayland/X11), scritto in C17 con SDL3 e SDL3_ttf.
Mostra l'ora a tessere animate e include un timer di studio (da 0h01 a 23h59) che
continua in background. Consumo a riposo ~0% CPU.

## Dipendenze

- `sdl3`, `sdl3_ttf` (build e runtime)
- `libnotify` (opzionale): fornisce `notify-send` per la notifica di fine timer

## Compilazione e installazione

```sh
make            # build di sviluppo
make release    # build ottimizzata (LTO, strip)
make install    # installa in ~/.local (PREFIX=/usr per altro percorso)
make uninstall
```

## Tasti

| Tasto | Azione |
|---|---|
| `F` / `F11` / doppio clic | Schermo intero on/off |
| `T` | Alterna la vista Orologio / Timer (il timer attivo continua in background) |
| `Spazio` / `Invio` | Timer: avvia / pausa / riprendi |
| `R` | Timer: reset alla durata impostata |
| `↑ ↓ ← →` | Timer in impostazione: regola durata. Pannello: naviga |
| `S` | Apre/chiude il pannello impostazioni |
| `Esc` | Chiude il pannello; altrimenti esce dallo schermo intero |
| `Q` / `Ctrl+Q` | Esci |

## Configurazione

File `~/.config/flipclock/config.ini` (rispetta `$XDG_CONFIG_HOME`), formato `chiave=valore`:

| Chiave | Default | Descrizione |
|---|---|---|
| `show_seconds` | `false` | Mostra i secondi |
| `clock_24h` | `true` | Formato 24h (false = 12h con AM/PM) |
| `timer_default_minutes` | `60` | Durata proposta del timer (1..1439) |
| `timer_sound` | `true` | Suono a fine timer |
| `timer_notify` | `true` | Notifica desktop a fine timer |
| `scale` | `90` | Larghezza delle tessere in % (50..100) |
| `brightness` | `100` | Luminosità delle cifre (20..100) |
| `start_fullscreen` | `false` | Avvio a schermo intero |
| `animations` | `true` | Animazione flip |

Le impostazioni si modificano anche dal pannello (`S`).

## Documentazione

Specifiche e architettura in [`docs/`](docs/).
