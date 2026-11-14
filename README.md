# flipclock

Orologio flip leggero per Linux (Wayland/X11), scritto in C17 con SDL3 e SDL3_ttf.
Mostra l'ora a tessere animate e include un timer di studio (da 0h01 a 23h59) che
continua in background, una modalità Pomodoro, le statistiche di studio e una modalità
screensaver. Consumo a riposo ~0% CPU.

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
| `P` | Alterna la vista Orologio / Pomodoro (`T`/`P` passano direttamente da una vista all'altra) |
| `Spazio` / `Invio` | Timer o Pomodoro: avvia / pausa / riprendi |
| `R` | Timer: reset alla durata impostata. Pomodoro: azzera tutto (STUDIO 1/N, fermo) |
| `N` | Pomodoro: salta alla fase successiva |
| `I` | Apre/chiude il pannello statistiche |
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
| `pomodoro_work` | `25` | Minuti di studio (1..180) |
| `pomodoro_break` | `5` | Minuti di riposo (1..60) |
| `pomodoro_long_break` | `15` | Minuti di riposo lungo (1..120) |
| `pomodoro_cycles` | `4` | Sessioni di studio prima del riposo lungo (1..12) |
| `pomodoro_auto_continue` | `true` | La fase successiva parte da sola |

Le impostazioni si modificano anche dal pannello (`S`).

## Pomodoro

`P` apre la vista Pomodoro: STUDIO (25 min) → RIPOSO (5 min) → ... e, dopo 4 sessioni di studio,
RIPOSO LUNGO (15 min). `Spazio`/`Invio` avvia o mette in pausa, `N` salta alla fase successiva,
`R` azzera tutto. Durante i riposi le cifre diventano blu. A fine fase arrivano notifica e suono
e, con `pomodoro_auto_continue=true`, parte la fase successiva. Timer e Pomodoro sono indipendenti
e possono girare entrambi in background.

## Statistiche

Le sessioni di studio (timer e fasi STUDIO del Pomodoro, mai i riposi, solo se durano almeno 60 s)
sono registrate in `$XDG_DATA_HOME/flipclock/sessions.csv` (default `~/.local/share/flipclock/sessions.csv`),
una riga per sessione: `inizio_iso8601_locale,durata_secondi,tipo,completata`.
`I` mostra oggi, ultimi 7 giorni, mese, giorni consecutivi e sessioni completate.

## Screensaver

```sh
flipclock --screensaver
```

Schermo intero con cursore nascosto; esce con qualsiasi tasto, clic o movimento del mouse
(gli eventi del primo secondo sono ignorati). Non salva nulla. Se flipclock è già aperto,
l'istanza screensaver esce subito. Esempio con `swayidle` in niri (`config.kdl`):

```kdl
spawn-at-startup "swayidle" "-w" "timeout" "300" "flipclock --screensaver"
```

Mentre un timer o un Pomodoro è in corso, l'app impedisce lo spegnimento dello schermo (idle-inhibit).

`flipclock --help` mostra le opzioni.

## Documentazione

Specifiche e architettura in [`docs/`](docs/).
