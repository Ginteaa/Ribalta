#ifndef RIBALTA_APP_INPUT_H
#define RIBALTA_APP_INPUT_H

#include <SDL3/SDL.h>
#include "app/app.h"

void input_handle_event(App *a, const SDL_Event *ev);
/* Da chiamare su movimento mouse o ingresso in fullscreen. */
void input_cursor_activity(App *a);
void input_cursor_tick(App *a, Uint64 now);
bool input_panel_open(const App *a);
bool input_stats_open(const App *a);
bool input_color_open(const App *a);
bool input_font_open(const App *a);
bool input_subject_open(const App *a);
bool input_help_open(const App *a);
/* Suggerimento "H · legenda": alla scadenza di hint_deadline lo spegne (hint_visible = false,
 * hint_deadline = 0, ridisegno). Da chiamare a ogni giro del ciclo principale, come
 * input_cursor_tick. */
void input_hint_tick(App *a, Uint64 now);
/* Scaduta la pausa FONT_PREVIEW_NS: applica a Render il font selezionato nel menu (anteprima). */
void input_font_preview_apply(App *a);

#endif
