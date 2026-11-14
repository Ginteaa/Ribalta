#ifndef RIBALTA_UI_COLOR_H
#define RIBALTA_UI_COLOR_H
#include <stdbool.h>
#include <stdint.h>
#include <SDL3/SDL.h>

/* Selettore colore in overlay (stile DankMaterialShell): quadrato S/V, barra delle tonalità,
 * campione + campo HEX (+ RGB/HSV in sola lettura), griglia della palette, colori recenti.
 * Stesse regole di risorse degli altri pannelli: font aperto solo mentre è aperto, texture del
 * testo ricreate solo quando sporche; gradienti con SDL_RenderGeometry (nessuna texture). */
typedef struct ColorUI ColorUI;

typedef enum {
    UICOLOR_NONE,      /* nulla di rilevante (eventualmente da ridisegnare: vedi ui_color_dirty) */
    UICOLOR_CHANGED,   /* il colore corrente è cambiato: anteprima dal vivo */
    UICOLOR_APPLY,     /* Invio / pulsante: conferma ui_color_value(); il pannello si chiude */
    UICOLOR_CANCEL     /* Esc: ripristina ui_color_original(); il pannello si chiude */
} UiColorResult;

ColorUI *ui_color_create(SDL_Renderer *ren, const char *font_path);
void     ui_color_destroy(ColorUI *ui);

void ui_color_open(ColorUI *ui, int pixel_w, int pixel_h, uint32_t current,
                   const uint32_t *recent, int recent_count);
void ui_color_close(ColorUI *ui);
bool ui_color_is_open(const ColorUI *ui);
void ui_color_resize(ColorUI *ui, int pixel_w, int pixel_h);

/* Tastiera: Tab/Shift+Tab cambiano zona (quadrato, tonalità, palette, HEX); frecce muovono
 * (Shift = passo grande); nella zona HEX 0-9/a-f scrivono e Backspace cancella;
 * Invio = APPLY, Esc = CANCEL. */
UiColorResult ui_color_key(ColorUI *ui, SDL_Keycode key, SDL_Keymod mod);

/* Mouse: ev è SDL_EVENT_MOUSE_BUTTON_DOWN/UP o SDL_EVENT_MOUSE_MOTION GIÀ convertito in coordinate
 * del renderer (pixel) da main con SDL_ConvertEventToRenderCoordinates. Clic/trascinamento su
 * quadrato e barra, clic su un colore della palette/recenti, clic fuori dal pannello = nulla. */
UiColorResult ui_color_mouse(ColorUI *ui, const SDL_Event *ev);

uint32_t ui_color_value(const ColorUI *ui);     /* colore corrente 0xRRGGBB */
uint32_t ui_color_original(const ColorUI *ui);  /* colore all'apertura */
bool     ui_color_dirty(const ColorUI *ui);     /* serve un ridisegno (es. cambio zona) */

/* Overlay leggero (oscura poco: le cifre restano visibili per l'anteprima) + pannello a destra.
 * Non chiama Present. */
void ui_color_draw(ColorUI *ui);

#endif
