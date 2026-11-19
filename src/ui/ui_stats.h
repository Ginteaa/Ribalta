#ifndef RIBALTA_UI_STATS_H
#define RIBALTA_UI_STATS_H
#include <stdbool.h>
#include <SDL3/SDL.h>
#include "core/settings.h"

/* Pannello statistiche in overlay (tasto I) — docs/04-roadmap.md §3.2.
 * Stesso stile e stesse regole di risorse di ui_settings: font aperto solo mentre è aperto,
 * texture ricreate solo quando sporche (apertura, resize). */
typedef struct StatsUI StatsUI;

StatsUI *ui_stats_create(SDL_Renderer *ren, const char *font_path);
void     ui_stats_destroy(StatsUI *ui);

/* Filtri (←/→, ad anello): "Tutte" (stats_summary con NULL) -> ogni materia di subjects, nell'ordine
 * -> "Senza materia" (""). Si apre su "Tutte". Nella vista "Tutte" il pannello mostra anche le ore
 * della settimana per materia (stats_subject_totals, al massimo 4 righe; "" -> "Senza materia").
 * subjects/count vengono copiati all'apertura. */
/* Obiettivi (docs/plans/obiettivi.md): se daily_goal_min > 0 o weekly_goal_h > 0 il pannello mostra
 * anche il blocco "Obiettivi" (stats_goals, sempre sul totale qualunque sia il filtro): barra
 * "Oggi 1h 40m / 3h" (se daily_goal_min > 0), barra "Settimana 8h / 15h" (se weekly_goal_h > 0),
 * "Obiettivo raggiunto N giorni di fila" (se daily_goal_min > 0). Con entrambi a 0 nessun blocco.
 * Il blocco si calcola all'apertura, non cambia con il filtro. */
void ui_stats_open(StatsUI *ui, int pixel_w, int pixel_h,
                   const char subjects[][SUBJECT_LEN], int subject_count,
                   int daily_goal_min, int weekly_goal_h);
void ui_stats_close(StatsUI *ui);
bool ui_stats_is_open(const StatsUI *ui);
void ui_stats_resize(StatsUI *ui, int pixel_w, int pixel_h);
void ui_stats_draw(StatsUI *ui);   /* overlay + pannello; non chiama Present */

/* ←/→ cambiano filtro (rilegge il CSV, ridisegno necessario): ritorna true se il tasto è usato. */
bool ui_stats_key(StatsUI *ui, SDL_Keycode key);

#endif
