#ifndef RIBALTA_NOTIFY_H
#define RIBALTA_NOTIFY_H
#include <stdbool.h>
#include <stdint.h>

/* Notifiche di fine timer — docs/03-architettura-tecnica.md.
 * Nessun costo a riposo: niente thread, il sottosistema audio è aperto solo mentre suona. */

/* wav_path: percorso di ding.wav, o NULL se assente (il suono viene saltato). */
/* icon_path: percorso dell'icona (ribalta.svg) per le notifiche, o NULL -> icona di tema "alarm-clock". */
void notify_init(const char *wav_path, const char *icon_path);

/* Finestra da far lampeggiare su Windows (NULL = nessuna). Va chiamata dopo SDL_CreateWindow;
 * notify_shutdown la dimentica (chiamarla prima di distruggere la finestra). Su Linux e macOS non ha effetto. */
typedef struct SDL_Window SDL_Window;
void notify_set_window(SDL_Window *w);

/* Linux: lancia `notify-send -a Ribalta -i <icona> <title> <body>` senza bloccare
 * (posix_spawnp); se notify-send manca, fallisce in silenzio. macOS: osascript.
 * Windows: fa lampeggiare la finestra nella barra delle applicazioni finché non riceve il fuoco
 * (SDL_FlashWindow, nessun processo figlio, nessun costo a riposo). */
void notify_desktop(const char *title, const char *body);

/* Avvia la riproduzione del WAV (SDL_InitSubSystem(SDL_INIT_AUDIO) al bisogno). */
void notify_sound(void);

/* Da chiamare a ogni giro del loop: raccoglie i processi figli terminati (waitpid WNOHANG)
 * e, a suono finito, chiude lo stream e SDL_QuitSubSystem(SDL_INIT_AUDIO).
 * Ritorna true se c'è ancora lavoro in sospeso (il loop deve risvegliarsi presto). */
bool notify_poll(void);

/* ms consigliati prima del prossimo notify_poll, UINT32_MAX se nulla in sospeso. */
uint32_t notify_ms_to_next_poll(void);

void notify_shutdown(void);

#endif
