#ifndef FLIPCLOCK_UI_SETTINGS_H
#define FLIPCLOCK_UI_SETTINGS_H
#include <stdbool.h>
#include <SDL3/SDL.h>
#include "settings.h"

/* Pannello impostazioni in overlay — docs/01 §1.3 e docs/02 §4.
 * Il font TTF viene aperto solo mentre il pannello è aperto (RAM minima a riposo).
 * Le texture del testo si ricreano solo quando qualcosa cambia (tasto, apertura, resize),
 * non a ogni fotogramma. */
typedef struct SettingsUI SettingsUI;

SettingsUI *ui_settings_create(SDL_Renderer *ren, const char *font_path);
void        ui_settings_destroy(SettingsUI *ui);

void ui_settings_open(SettingsUI *ui, int pixel_w, int pixel_h);
void ui_settings_close(SettingsUI *ui);     /* libera font e texture */
bool ui_settings_is_open(const SettingsUI *ui);
void ui_settings_resize(SettingsUI *ui, int pixel_w, int pixel_h);

/* Gestisce ↑/↓ (voce), ←/→/Invio/Spazio (cambia valore; Shift = passo x10 sugli interi).
 * Modifica *s direttamente. Ritorna true se un valore di *s è cambiato.
 * Esc e S NON sono gestiti qui (li gestisce main per chiudere). */
bool ui_settings_key(SettingsUI *ui, SDL_Keycode key, SDL_Keymod mod, Settings *s);

/* Disegna overlay + pannello sopra il fotogramma corrente. Non chiama Present. */
void ui_settings_draw(SettingsUI *ui, const Settings *s);

#endif
