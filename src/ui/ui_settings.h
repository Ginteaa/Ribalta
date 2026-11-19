#ifndef RIBALTA_UI_SETTINGS_H
#define RIBALTA_UI_SETTINGS_H
#include <stdbool.h>
#include <SDL3/SDL.h>
#include "core/settings.h"

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

/* Richiesta di aprire il selettore colore (ui_color), e per quale impostazione. */
typedef enum {
    SETTINGS_COLOR_NONE,     /* nessuna richiesta */
    SETTINGS_COLOR_DIGITS,   /* riga "Colore cifre" -> Settings.digit_color */
    SETTINGS_COLOR_BREAK     /* riga "Pomodoro: colore riposo" -> Settings.pomodoro_break_color */
} SettingsColorRequest;

/* Riga "Colore cifre": Invio chiede di aprire il selettore colore. ←/→ sulla riga scorrono la
 * palette (color_palette_step) modificando s->digit_color e ui_settings_key ritorna true.
 * Riga "Pomodoro: colore riposo": ←/→ ciclano s->pomodoro_break_mode
 * (DIGITS -> AUTO -> CUSTOM -> DIGITS, ← al contrario) e ui_settings_key ritorna true; Invio/Spazio
 * chiedono il selettore per il colore del riposo. Il valore mostrato è "come le cifre",
 * "blu automatico" oppure il campione di s->pomodoro_break_color + "#rrggbb".
 * Ritorna la richiesta UNA volta (poi SETTINGS_COLOR_NONE); main allora apre ui_color. */
SettingsColorRequest ui_settings_take_color_request(SettingsUI *ui);

/* Riga "Font cifre" (subito dopo "Colore cifre"): mostra s->font_family o FONT_BUNDLED_NAME se
 * vuoto. Invio chiede di aprire il menu dei font: ritorna true UNA volta (main apre ui_font).
 * ←/→ sulla riga passano al font successivo/precedente (fonts_step) scrivendo s->font_family e
 * s->font_file; ui_settings_key ritorna true (main allora chiama render_set_font). */
bool ui_settings_take_font_request(SettingsUI *ui);

/* Disegna overlay + pannello sopra il fotogramma corrente. Non chiama Present. */
void ui_settings_draw(SettingsUI *ui, const Settings *s);

#endif
