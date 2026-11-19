#ifndef RIBALTA_COLOR_H
#define RIBALTA_COLOR_H
#include <stdbool.h>
#include <stdint.h>

/* Utilità colore. Colori come 0xRRGGBB. Nessuna dipendenza da SDL (testabile). */

typedef struct {
    float h;   /* tonalità 0..360 */
    float s;   /* saturazione 0..1 */
    float v;   /* valore/luminosità 0..1 */
} ColorHSV;

uint32_t color_from_hsv(ColorHSV hsv);
ColorHSV color_to_hsv(uint32_t rgb);

/* Accetta "#rrggbb" o "rrggbb" (maiuscole/minuscole), spazi ai lati ignorati. */
bool color_parse_hex(const char *s, uint32_t *out);
/* Scrive "#rrggbb" (minuscolo) in out (almeno 8 byte). */
void color_format_hex(uint32_t rgb, char out[8]);

/* Palette predefinita in stile Material: COLOR_PALETTE_ROWS righe x COLOR_PALETTE_COLS colonne
 * (tonalità per colonna, sfumature per riga), poi COLOR_PALETTE_NEUTRALS neutri in coda
 * (grigio scuro, bianco, grigio di default 0xb7b7b7: da default → va al rosso, ← al bianco). */
#define COLOR_PALETTE_COLS     14
#define COLOR_PALETTE_ROWS     3
#define COLOR_PALETTE_NEUTRALS 3
#define COLOR_PALETTE_COUNT    (COLOR_PALETTE_COLS * COLOR_PALETTE_ROWS + COLOR_PALETTE_NEUTRALS)
extern const uint32_t color_palette[COLOR_PALETTE_COUNT];

/* Indice nella palette, -1 se il colore non è un preset. */
int color_palette_index(uint32_t rgb);
/* Preset successivo (dir = +1) o precedente (-1) rispetto a rgb, ciclico. Se rgb non è un preset
 * parte dal preset più vicino. Usato da ←/→ sulla riga "Colore cifre". */
uint32_t color_palette_step(uint32_t rgb, int dir);

/* Colori del riposo Pomodoro in modalità BREAK_COLOR_AUTO (core/settings.h). */
#define COLOR_BREAK     0x6fa8dcu
#define COLOR_BREAK_ALT 0xffb347u /* riposo se il colore delle cifre è già blu */

/* Colore delle cifre durante un riposo Pomodoro: mode = BREAK_COLOR_* (core/settings.h).
 * DIGITS -> digit; AUTO -> COLOR_BREAK, o COLOR_BREAK_ALT se color_is_bluish(digit);
 * CUSTOM -> custom. Una modalità sconosciuta vale DIGITS. */
uint32_t color_break(int mode, uint32_t custom, uint32_t digit);

/* true se il colore è "blu" (tonalità 180..260 con saturazione >= 0.25): in quel caso la pausa
 * del Pomodoro usa l'ambra invece del blu. */
bool color_is_bluish(uint32_t rgb);

#endif
