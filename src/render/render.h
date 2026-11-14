#ifndef RIBALTA_RENDER_H
#define RIBALTA_RENDER_H
#include <stdbool.h>
#include <SDL3/SDL.h>

/* Modulo di disegno: tessere, cifre pre-renderizzate, animazione flip.
 * Vedi docs/02-design-grafico.md e docs/03-architettura-tecnica.md §2. */
typedef struct Render Render;

/* font_path: TTF usato per le cifre e per le etichette (AM/PM).
 * Non crea ancora texture: serve una chiamata a render_resize. NULL su errore. */
Render *render_create(SDL_Renderer *ren, const char *font_path);
void    render_destroy(Render *r);

/* Dimensione in PIXEL dell'area di disegno (SDL_GetCurrentRenderOutputSize).
 * Ricalcola il layout e rigenera le texture delle cifre/tessere a quella misura. */
void render_resize(Render *r, int pixel_w, int pixel_h);

/* scale_pct 50..100, brightness_pct 20..100. Se cambiano, rigenera le texture. */
void render_set_style(Render *r, int scale_pct, int brightness_pct);

/* Imposta i valori mostrati (count 2 o 3, ogni valore 0..99).
 * hide_leading_zero: la tessera 0 mostra una sola cifra se < 10.
 * animate=true: le tessere il cui valore cambia avviano il flip (400 ms) da now_ns.
 * Se count cambia rispetto a prima, il layout si aggiorna senza animazione.
 * Valori uguali ai correnti: nessun effetto. */
void render_set_tiles(Render *r, const int *values, int count, bool hide_leading_zero,
                      bool animate, Uint64 now_ns);

/* 0 = nascosto, 1 = AM, 2 = PM */
void render_set_ampm(Render *r, int ampm);

/* --- Fase 2 --- */

/* Etichetta piccola in alto a sinistra sopra la prima tessera (50% opacità). */
typedef enum { LABEL_NONE, LABEL_TIMER, LABEL_PAUSA } RenderLabel;
void render_set_label(Render *r, RenderLabel label);

/* Barra di avanzamento sotto le tessere (2 px logici, colore cifra al 30%).
 * p in 0..1; p < 0 la nasconde. */
void render_set_progress(Render *r, float p);

/* Opacità di una tessera (0..1; tile = -1 per tutte). Usata per: pausa (0.5),
 * selezione in impostazione timer (0.4 sulla non selezionata), lampeggio a fine timer. */
void render_set_tile_alpha(Render *r, int tile, float alpha);

/* --- Fase 3 --- */

/* Etichetta con testo libero (es. "STUDIO 2/4 · IN PAUSA"); NULL o "" la nasconde.
 * Ha la precedenza su render_set_label. La texture si ricrea solo se il testo cambia. */
void render_set_label_text(Render *r, const char *text);

/* Colore base delle cifre (prima della luminosità). Default 0xb7b7b7; pausa Pomodoro 0x6fa8dc.
 * ECONOMICO: i glifi sono bianchi e il colore si applica con SDL_SetTextureColorMod; un cambio
 * ricompone solo le facce delle tessere (nessuna riapertura del font, nessuna animazione).
 * Pensato per essere chiamato anche molte volte al secondo (anteprima del selettore colore). */
void render_set_digit_color(Render *r, Uint8 red, Uint8 green, Uint8 blue);

/* Colore di barra di avanzamento ed etichette (TIMER, STUDIO 2/4, AM/PM, testo libero).
 * Default 0xb7b7b7. Le etichette restano semitrasparenti (50%/70%) e la luminosità moltiplica.
 * Economico come render_set_digit_color (nessuna texture ricreata). */
void render_set_accent(Render *r, Uint8 red, Uint8 green, Uint8 blue);

/* --- Font delle cifre --- */

/* Cambia il TTF di cifre ed etichette e rigenera glifi e facce alla misura corrente
 * (nessuna animazione; colore, stile e valori mostrati restano). Costoso: va chiamata solo quando
 * il font cambia (anteprima del menu font, conferma, ←/→ nelle impostazioni).
 * Se il file non si apre ritorna false e lascia il font precedente intatto. Stesso percorso del
 * font attuale: nessun effetto, ritorna true. */
bool render_set_font(Render *r, const char *font_path);

/* true se almeno una tessera sta ancora animando a now_ns */
bool render_is_animating(const Render *r, Uint64 now_ns);

/* Disegna l'intero fotogramma (clear + tessere). NON chiama SDL_RenderPresent. */
void render_draw(Render *r, Uint64 now_ns);

#endif
