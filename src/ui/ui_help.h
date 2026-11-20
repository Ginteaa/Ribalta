#ifndef RIBALTA_UI_HELP_H
#define RIBALTA_UI_HELP_H
#include <stdbool.h>
#include <SDL3/SDL.h>

/* Legenda in overlay (tasti H / F1) — docs/01-functional-spec.md §1.6, docs/plans/legenda.md.
 * Stesso stile e stesse regole di risorse di ui_stats / ui_settings: il font si apre solo mentre la
 * legenda è aperta e si chiude alla chiusura; le texture si creano all'apertura e al resize, mai a
 * ogni fotogramma. Contenuto fisso (sezioni Generale, Timer, Pomodoro, Pannelli, File) in italiano,
 * su 2 colonne se la finestra è abbastanza larga, altrimenti 1; il corpo del testo si riduce finché
 * tutto entra (niente scorrimento). */
typedef struct HelpUI HelpUI;

HelpUI *ui_help_create(SDL_Renderer *ren, const char *font_path);
void    ui_help_destroy(HelpUI *ui);

/* config_path / stats_path: percorsi completi di config.ini e sessions.csv (possono essere NULL o
 * "": la riga mostra allora solo il nome del file). Vengono copiati; se iniziano con $HOME la
 * legenda mostra "~" al posto della home. */
void ui_help_open(HelpUI *ui, int pixel_w, int pixel_h, const char *config_path, const char *stats_path);
void ui_help_close(HelpUI *ui);   /* libera font e texture */
bool ui_help_is_open(const HelpUI *ui);
void ui_help_resize(HelpUI *ui, int pixel_w, int pixel_h);
void ui_help_draw(HelpUI *ui);    /* overlay + pannello; non chiama Present */

#endif
