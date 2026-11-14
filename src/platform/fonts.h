#ifndef RIBALTA_FONTS_H
#define RIBALTA_FONTS_H
#include <stdbool.h>

/* Elenco dei font di sistema (fontconfig), usato SOLO mentre serve (menu font aperto o ←/→ sulla
 * riga "Font cifre"): all'avvio l'app apre direttamente Settings.font_file senza fontconfig.
 * Ogni chiamata a fonts_list va chiusa con fonts_free, che libera anche la cache (FcFini). */

#define FONT_FAMILY_MAX 128
#define FONT_FILE_MAX   512

/* Nome mostrato per il font incluso (Settings.font_file vuoto). */
#define FONT_BUNDLED_NAME "Ribalta (IBM Plex Sans Condensed)"

typedef struct {
    char family[FONT_FAMILY_MAX]; /* nome della famiglia (es. "DejaVu Sans") */
    char file[FONT_FILE_MAX];     /* percorso assoluto del file (variante Bold, altrimenti Regular) */
} FontEntry;

typedef struct {
    FontEntry *items;  /* ordinati per family (case-insensitive), senza duplicati */
    int        count;
} FontList;

/* Elenca le famiglie scalabili (TrueType/OpenType) che contengono tutte le cifre 0x30..0x39.
 * Per ogni famiglia sceglie la variante Bold (weight >= FC_WEIGHT_BOLD più vicino a Bold, non
 * corsivo); se manca, la Regular non corsiva; altrimenti la prima disponibile.
 * Scarta i file inesistenti. Ritorna false su errore di fontconfig (out vuota, va comunque
 * passata a fonts_free). Il font incluso NON fa parte della lista. */
bool fonts_list(FontList *out);

/* Libera la lista (out torna vuota) e chiama FcFini. Sicura su una lista vuota. */
void fonts_free(FontList *l);

/* Indice della voce con quel file, -1 se assente (file vuoto o NULL -> -1). */
int fonts_find(const FontList *l, const char *file);

/* Font successivo (dir = +1) o precedente (dir = -1) rispetto a current_file, nell'anello
 * [font incluso, lista[0], ..., lista[n-1]]. current_file vuoto/NULL = font incluso; un file non
 * in lista si comporta come il font incluso. Il font incluso si scrive come family = "" e file = "".
 * Chiama internamente fonts_list/fonts_free. Ritorna false se fontconfig fallisce (out invariato). */
bool fonts_step(const char *current_file, int dir, FontEntry *out);

#endif
