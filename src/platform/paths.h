#ifndef RIBALTA_PATHS_H
#define RIBALTA_PATHS_H
#include <stdbool.h>
#include <stddef.h>

/* Percorsi dell'app (XDG) e ricerca degli asset. */

/* $XDG_CONFIG_HOME/ribalta (fallback ~/.config/ribalta). Non crea la cartella. */
bool paths_config_dir(char *out, size_t out_size);
/* .../ribalta/config.ini */
bool paths_config_file(char *out, size_t out_size);

/* Directory dati: $XDG_DATA_HOME/ribalta (fallback ~/.local/share/ribalta), creata se manca.
 * Ritorna false su errore. */
bool paths_data_dir(char *out, size_t out_size);

/* mkdir -p: crea path e le cartelle intermedie (0755). */
bool paths_mkdir_p(const char *path);

/* Cerca un file asset (es. "font.ttf") in ordine:
 * $RIBALTA_DATA, ./assets, <dir dell'eseguibile>/assets,
 * $XDG_DATA_HOME/ribalta (fallback ~/.local/share/ribalta), /usr/share/ribalta.
 * Scrive il percorso trovato in out. Ritorna false se non trovato. */
bool paths_find_asset(const char *name, char *out, size_t out_size);

#endif
