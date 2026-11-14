#ifndef RIBALTA_NOTIFY_H
#define RIBALTA_NOTIFY_H
#include <stdbool.h>
#include <stdint.h>

/* Notifiche di fine timer — docs/03-architettura-tecnica.md.
 * Nessun costo a riposo: niente thread, il sottosistema audio è aperto solo mentre suona. */

/* wav_path: percorso di ding.wav, o NULL se assente (il suono viene saltato). */
void notify_init(const char *wav_path);

/* Lancia `notify-send -a ribalta -i alarm-clock <title> <body>` senza bloccare
 * (posix_spawnp). Se notify-send manca, fallisce in silenzio. */
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
