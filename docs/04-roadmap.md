# 04 — Roadmap

## Fase 1 — MVP orologio

- Scheletro progetto, `Makefile`, font OFL (IBM Plex Sans Condensed Bold) in `assets/`.
- Finestra SDL3 su Wayland, app-id `ribalta`, HiDPI, ridimensionabile.
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
- File `ribalta.desktop` e target `make install`.

**Fatto quando**: un timer da 2 minuti termina con notifica e suono; il countdown non accumula
errore (±1 s su 1 h); le impostazioni sopravvivono a un riavvio.

## Fase 3 — Aggiunte

### 3.1 Modalità Pomodoro (tasto `P`)
- `P` alterna la vista Pomodoro (come `T` per il timer; `T`/`P` da una vista all'altra passano
  direttamente). Timer e Pomodoro sono indipendenti e possono girare entrambi in background.
- Ciclo: STUDIO (`pomodoro_work`, default 25 min) → RIPOSO (`pomodoro_break`, 5) → … ; dopo
  `pomodoro_cycles` (default 4) sessioni di studio il riposo è RIPOSO LUNGO (`pomodoro_long_break`, 15),
  poi il ciclo ricomincia da 1.
- Tasti nella vista: `Spazio`/`Invio` avvia/pausa, `N` salta alla fase successiva, `R` azzera tutto
  (fase STUDIO 1/N, fermo).
- Etichetta: `STUDIO 2/4`, `RIPOSO`, `RIPOSO LUNGO`; in pausa si aggiunge ` · IN PAUSA` e le tessere
  vanno al 50%. Durante RIPOSO/RIPOSO LUNGO le cifre diventano blu tenue `#6fa8dc`.
- A fine fase: notifica ("Fine studio — riposo di 5 min" / "Riposo finito — si torna a studiare")
  + suono; se `pomodoro_auto_continue=true` (default) la fase successiva parte da sola, altrimenti
  resta ferma pronta a partire. La vista non cambia (niente lampeggio, resta sul Pomodoro).
- Nuove chiavi config (anche nel pannello impostazioni): `pomodoro_work` (1..180),
  `pomodoro_break` (1..60), `pomodoro_long_break` (1..120), `pomodoro_cycles` (1..12),
  `pomodoro_auto_continue` (bool).

### 3.2 Statistiche di studio (tasto `I`)
- File `$XDG_DATA_HOME/ribalta/sessions.csv` (fallback `~/.local/share/ribalta/`), una riga
  per sessione: `inizio_iso8601_locale,durata_secondi,tipo,completata` con tipo `timer|pomodoro`.
  Prima riga di intestazione se il file è nuovo.
- Cosa si registra (solo tempo di studio, mai i riposi), se la durata studiata è ≥ 60 s:
  - timer: a fine timer (completata=1); su `R` o all'uscita dall'app con timer avviato (completata=0);
  - pomodoro: a fine fase STUDIO (1); su `N`/`R`/uscita durante uno STUDIO avviato (0).
  - Durata = tempo effettivamente trascorso, pause escluse.
- `I` apre/chiude un pannello in overlay (stile pannello impostazioni): **Oggi**, **Ultimi 7 giorni**
  (barre orizzontali per giorno, etichetta giorno abbreviato in italiano e `1h 25m`), **Questo mese**,
  **Giorni consecutivi** (streak: giorni consecutivi fino a oggi, o fino a ieri se oggi ancora 0),
  **Sessioni completate** totali. `Esc`/`I` chiude.
- Il file viene letto solo all'apertura del pannello; font e texture solo mentre è aperto.

### 3.3 Modalità screensaver
- `ribalta --screensaver`: schermo intero, cursore nascosto subito; esce con qualsiasi tasto,
  clic o movimento del mouse (ignorando gli eventi del primo secondo). Non salva nulla.
- `ribalta --help` mostra le opzioni.
- Avvio automatico dopo inattività con `swayidle` (README con esempio per niri:
  `spawn-at-startup "swayidle" "-w" "timeout" "300" "ribalta --screensaver"`).
  Se ribalta è già aperto, l'istanza screensaver esce subito (istanza singola).
- **Blocco dello spegnimento schermo**: mentre un timer o un Pomodoro è IN CORSO l'app chiama
  `SDL_DisableScreenSaver()` (idle-inhibit su Wayland); quando nessuno è in corso
  `SDL_EnableScreenSaver()`.

## Idee future
- Temi colore (chiaro, ambra, verde terminale).
- Scelta del monitor in schermo intero.
- Pacchetto AUR (`PKGBUILD`).
- Integrazione con la barra di stato (es. waybar) che mostra il tempo rimanente del timer.
