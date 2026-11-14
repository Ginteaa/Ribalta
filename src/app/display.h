#ifndef RIBALTA_APP_DISPLAY_H
#define RIBALTA_APP_DISPLAY_H

#include <SDL3/SDL.h>
#include "app/app.h"

/* Colore base delle cifre (anteprima del selettore se attiva). */
uint32_t display_base_color(const App *a);
/* Aggiorna lo stato desiderato e lo applica al render se cambia qualcosa di visibile. */
void display_update(App *a, Uint64 now);
/* Forza la riapplicazione del volto al prossimo display_update, senza animazione. */
void display_invalidate(App *a);

#endif
