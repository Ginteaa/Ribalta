#ifndef RIBALTA_UI_FONT_H
#define RIBALTA_UI_FONT_H
#include <stdbool.h>
#include <SDL3/SDL.h>

/* Menu dei font delle cifre in overlay (stesso stile del selettore colore: overlay leggero,
 * pannello a destra, le tessere restano visibili per l'anteprima dal vivo).
 * Contenuto: titolo, campo di ricerca, lista scorrevole delle famiglie (prima voce: il font
 * incluso, FONT_BUNDLED_NAME), anteprima "12:34" disegnata nel font selezionato, suggerimento tasti.
 * Risorse: la lista (fonts_list) e i font esistono SOLO mentre il menu è aperto; ui_font_close
 * libera tutto (fonts_free -> FcFini). Le texture del testo si ricreano solo quando sporche; si
 * renderizzano solo le righe visibili. Il font dell'anteprima si riapre solo se cambia la voce. */
typedef struct FontUI FontUI;

typedef enum {
    UIFONT_NONE,     /* nulla di rilevante (eventuale ridisegno: ui_font_dirty) */
    UIFONT_CHANGED,  /* la voce selezionata è cambiata: main programma l'anteprima dopo ~150 ms */
    UIFONT_APPLY,    /* Invio / doppio clic: conferma la voce selezionata; il menu si chiude */
    UIFONT_CANCEL    /* Esc: ripristina il font dell'apertura; il menu si chiude */
} UiFontResult;

/* ui_font_path: font del testo del pannello (il font incluso).
 * bundled_path: file del font incluso, usato per la prima voce e la sua anteprima. */
FontUI *ui_font_create(SDL_Renderer *ren, const char *ui_font_path, const char *bundled_path);
void    ui_font_destroy(FontUI *ui);

/* Carica la lista (fonts_list) e seleziona la voce di current_file ("" = font incluso; un file
 * non in lista seleziona il font incluso). Il filtro parte vuoto. Se fontconfig fallisce il menu
 * si apre comunque con la sola voce del font incluso. */
void ui_font_open(FontUI *ui, int pixel_w, int pixel_h, const char *current_file);
void ui_font_close(FontUI *ui);
bool ui_font_is_open(const FontUI *ui);
void ui_font_resize(FontUI *ui, int pixel_w, int pixel_h);

/* Tastiera: ↑/↓ voce, PgSu/PgGiù pagina, Home/Fine prima/ultima voce (della lista filtrata);
 * a-z, 0-9, spazio, '-' scrivono nel filtro (case-insensitive, sottostringa del nome),
 * Backspace cancella; dopo un cambio del filtro la selezione va sulla prima voce filtrata
 * (CHANGED se la voce cambia). Invio = APPLY (se la lista filtrata è vuota: NONE),
 * Esc = se il filtro non è vuoto lo svuota (NONE), altrimenti CANCEL. */
UiFontResult ui_font_key(FontUI *ui, SDL_Keycode key, SDL_Keymod mod);

/* Mouse: ev è SDL_EVENT_MOUSE_BUTTON_DOWN/UP, SDL_EVENT_MOUSE_MOTION o SDL_EVENT_MOUSE_WHEEL
 * con coordinate GIÀ convertite nel renderer (SDL_ConvertEventToRenderCoordinates).
 * Rotella = scorre la lista (non cambia la selezione); clic su una riga = la seleziona (CHANGED);
 * doppio clic (ev->button.clicks >= 2) = APPLY; clic fuori dal pannello = NONE. */
UiFontResult ui_font_mouse(FontUI *ui, const SDL_Event *ev);

/* Voce selezionata: per il font incluso family = "" e file = "". Mai NULL. */
const char *ui_font_family(const FontUI *ui);
const char *ui_font_file(const FontUI *ui);
/* Font all'apertura (per Esc). Mai NULL. */
const char *ui_font_original_file(const FontUI *ui);
const char *ui_font_original_family(const FontUI *ui);

bool ui_font_dirty(const FontUI *ui);   /* serve un ridisegno */

/* Overlay + pannello. Non chiama Present. */
void ui_font_draw(FontUI *ui);

#endif
