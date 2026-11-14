# 04 — Roadmap

## Fase 1 — MVP orologio

- Scheletro progetto, `Makefile`, font OFL (IBM Plex Sans Bold) in `assets/`.
- Finestra SDL3 su Wayland, app-id `flipclock`, HiDPI, ridimensionabile.
- Tessere + cifre pre-renderizzate, layout `HH:MM` e `HH:MM:SS`.
- Animazione flip (sezione 3 di `02-design-grafico.md`).
- Loop guidato dagli eventi con risveglio al prossimo minuto/secondo.
- `config.ini`: `show_seconds`, `clock_24h`, `scale`, `brightness`, `start_fullscreen`, `animations`.
- Schermo intero (`F`/`F11`/doppio clic), `Q` per uscire, cursore nascosto.

**Fatto quando**: l'ora è corretta e si ribalta a ogni minuto; RSS < 20 MB; CPU a riposo ~0%
(misurata con `pidstat` per 2 minuti); `make debug` senza errori ASan.

## Fase 2 — Timer di studio + impostazioni

- Modalità Timer completa (sezione 1.2 di `01-specifiche-funzionali.md`).
- Barra di avanzamento, etichette `TIMER`/`PAUSA`.
- Notifica `notify-send` + suono `ding.wav`.
- Pannello impostazioni in overlay con tutte le voci.
- File `flipclock.desktop` e target `make install`.

**Fatto quando**: un timer da 2 minuti termina con notifica e suono; il countdown non accumula
errore (±1 s su 1 h); le impostazioni sopravvivono a un riavvio.

## Fase 3 — Aggiunte

### 3.1 Modalità Pomodoro
- Nuova modalità (tasto `P`): cicli studio/pausa configurabili.
- Preset: 25/5 e 50/10, più personalizzato; pausa lunga (15–30 min) ogni N cicli (default 4).
- Durante la pausa le cifre diventano blu tenue (`#6fa8dc`); etichetta `STUDIO 2/4` / `PAUSA`.
- Notifica e suono a ogni passaggio studio ↔ pausa; avvio automatico della fase successiva
  configurabile (`pomodoro_auto_continue`).
- Nuove chiavi: `pomodoro_work`, `pomodoro_break`, `pomodoro_long_break`, `pomodoro_cycles`,
  `pomodoro_auto_continue`.

### 3.2 Statistiche di studio
- Ogni sessione di timer/Pomodoro (anche interrotta, se ≥ 1 min) viene aggiunta a
  `~/.local/share/flipclock/sessions.csv` (`$XDG_DATA_HOME`):
  `inizio_iso8601,durata_secondi,tipo(timer|pomodoro),completata(0|1)`.
- Schermata statistiche (tasto `I`): ore di studio oggi, ultimi 7 giorni (barre semplici
  disegnate con rettangoli), totale del mese, giorni consecutivi di studio (streak).
- Il file viene letto solo quando si apre la schermata (niente in memoria nel resto del tempo).

### 3.3 Modalità screensaver
- Opzione `--screensaver`: avvio diretto a schermo intero, esce con qualsiasi tasto o movimento del mouse.
- Avvio automatico dopo inattività tramite `swayidle` (o equivalente per niri), es.
  `swayidle timeout 300 'flipclock --screensaver' resume 'pkill -x flipclock'`
  — documentato nel README con un esempio di configurazione niri.
- Con timer o Pomodoro attivo l'app chiama `SDL_DisableScreenSaver()` (usa idle-inhibit su Wayland)
  così lo schermo non si spegne durante lo studio; ripristinato a fine sessione.
- Opzionale in seguito: supporto diretto a `ext-idle-notify-v1` senza swayidle.

## Idee future
- Temi colore (chiaro, ambra, verde terminale).
- Scelta del monitor in schermo intero.
- Pacchetto AUR (`PKGBUILD`).
- Integrazione con la barra di stato (es. waybar) che mostra il tempo rimanente del timer.
