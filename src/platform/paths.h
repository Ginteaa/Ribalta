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

/* mkdir -p: crea path e le cartelle intermedie (0755; su Windows senza permessi).
 * Accetta '/' e, su Windows, anche '\' come separatori; non prova a creare "C:". */
bool paths_mkdir_p(const char *path);

/* true se c è un separatore di percorso: '/' ovunque, anche '\' su Windows. */
bool paths_is_sep(char c);

/* true se p è un percorso assoluto: "/..." su POSIX; su Windows anche "C:\...", "C:/..." e
 * "\\server\..." (un "/" iniziale conta come assoluto anche lì). NULL o "" -> false. */
bool paths_is_absolute(const char *p);

/* true se il file p esiste ed è leggibile (access R_OK su POSIX, _access su Windows). */
bool paths_readable(const char *p);

/* Cartella dell'eseguibile in uso (assoluta, senza '/' finale), es. "/home/u/.local/bin".
 * Non richiede SDL_Init: si può chiamare prima di inizializzare SDL (sottocomandi di main).
 * Ritorna false se non determinabile o se out è troppo piccolo. */
bool paths_exe_dir(char *out, size_t out_size);

/* Cerca un file asset (es. "font.ttf") in ordine:
 * $RIBALTA_DATA, ./assets, <dir dell'eseguibile>/assets, <dir dell'eseguibile>/../share/ribalta,
 * $XDG_DATA_HOME/ribalta (fallback ~/.local/share/ribalta), /usr/share/ribalta.
 * Scrive il percorso trovato in out. Ritorna false se non trovato. */
bool paths_find_asset(const char *name, char *out, size_t out_size);

#endif
