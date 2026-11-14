# Ribalta — documentazione

**Ribalta** è un orologio a tessere ribaltabili ("flip clock") per Linux, ispirato allo stile di Fliqlo
ma con grafica e codice originali. Mostra l'ora a schermo intero o in finestra, con l'animazione
delle cifre che si ribaltano, e ha un timer per sessioni di studio di durata scelta dall'utente.
Deve occupare pochissima RAM e restare praticamente a 0% di CPU quando non anima.
Scritto in C con SDL3, gira nativamente su Wayland (testato su niri) e su X11.

## Indice

| File | Contenuto |
|---|---|
| [01-specifiche-funzionali.md](01-specifiche-funzionali.md) | Cosa fa l'app: modalità orologio, timer, impostazioni, controlli, requisiti non funzionali |
| [02-design-grafico.md](02-design-grafico.md) | Palette, proporzioni delle tessere, animazione flip, mockup dei layout |
| [03-architettura-tecnica.md](03-architettura-tecnica.md) | Stack (C17 + SDL3), strategia per risparmiare RAM/CPU, moduli, Makefile, flusso di sviluppo con agenti |
| [04-roadmap.md](04-roadmap.md) | Fasi di sviluppo: MVP, timer, Pomodoro, statistiche, screensaver |

## Nota sul nome

Il progetto si chiama `ribalta` e non usa asset, font, icone o marchio di Fliqlo:
l'aspetto "flip clock" è uno stile generico, ricreato da zero.
