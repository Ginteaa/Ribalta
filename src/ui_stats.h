#ifndef FLIPCLOCK_UI_STATS_H
#define FLIPCLOCK_UI_STATS_H
#include <stdbool.h>
#include <SDL3/SDL.h>

/* Pannello statistiche in overlay (tasto I) — docs/04-roadmap.md §3.2.
 * Stesso stile e stesse regole di risorse di ui_settings: font aperto solo mentre è aperto,
 * texture ricreate solo quando sporche (apertura, resize). */
typedef struct StatsUI StatsUI;

StatsUI *ui_stats_create(SDL_Renderer *ren, const char *font_path);
void     ui_stats_destroy(StatsUI *ui);

void ui_stats_open(StatsUI *ui, int pixel_w, int pixel_h); /* legge stats_summary(now) */
void ui_stats_close(StatsUI *ui);
bool ui_stats_is_open(const StatsUI *ui);
void ui_stats_resize(StatsUI *ui, int pixel_w, int pixel_h);
void ui_stats_draw(StatsUI *ui);   /* overlay + pannello; non chiama Present */

#endif
