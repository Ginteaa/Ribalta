#ifndef RIBALTA_UI_SUBJECT_H
#define RIBALTA_UI_SUBJECT_H
#include <stdbool.h>
#include <SDL3/SDL.h>
#include "core/settings.h"

/* Menu delle materie in overlay (tasto M) — docs/plans/materie.md. Stile del menu font: overlay
 * leggero, pannello a destra, tessere visibili. Contenuto: titolo "MATERIE", lista (prima voce
 * "Nessuna materia", poi Settings.subjects nell'ordine; la materia attiva ha un segno), riga di
 * inserimento quando si scrive un nome nuovo, suggerimento tasti in basso
 * ("Invio sceglie · N nuova · Canc elimina · Esc chiude"; in inserimento "Invio aggiunge · Esc annulla").
 * Risorse: il font del pannello si apre in ui_subject_open e si chiude in ui_subject_close; texture
 * ricreate solo quando sporche. Nessun timer, nessuna animazione.
 *
 * Il menu modifica DIRETTAMENTE il Settings passato a ui_subject_open (che deve restare valido finché
 * il menu è aperto): elenco (settings_subject_add / settings_subject_remove) e materia attiva
 * (Settings.subject). Il salvataggio su disco lo fa il chiamante (alla chiusura). */
typedef struct SubjectUI SubjectUI;

typedef enum {
    UISUBJ_NONE,   /* nulla da fare per il chiamante (eventuale ridisegno: ui_subject_dirty) */
    UISUBJ_CLOSE   /* il menu va chiuso (Invio su una voce dopo averla resa attiva, Esc, M) */
} UiSubjectResult;

SubjectUI *ui_subject_create(SDL_Renderer *ren, const char *font_path);
void       ui_subject_destroy(SubjectUI *ui);

/* Apre il menu con la selezione sulla materia attiva (o su "Nessuna materia"). */
void ui_subject_open(SubjectUI *ui, int pixel_w, int pixel_h, Settings *cfg);
void ui_subject_close(SubjectUI *ui);
bool ui_subject_is_open(const SubjectUI *ui);
void ui_subject_resize(SubjectUI *ui, int pixel_w, int pixel_h);

/* Tastiera, fuori dall'inserimento: ↑/↓ voce (Home/Fine prima/ultima); Invio = rende attiva la voce
 * selezionata ("Nessuna materia" -> "") e CLOSE; N = inizia l'inserimento di un nome (se l'elenco
 * non è pieno); Canc (SDLK_DELETE) = elimina la materia selezionata (non "Nessuna materia"), la
 * selezione resta sulla stessa posizione o sulla precedente; Esc o M = CLOSE senza cambiare la
 * materia attiva.
 * In inserimento: Backspace cancella l'ultimo carattere UTF-8; Invio = settings_subject_add; se
 * riesce l'inserimento termina e la selezione va sulla nuova voce, se fallisce (nome non valido o
 * duplicato) resta in inserimento e mostra il motivo in rosso sotto la riga; Esc = annulla
 * l'inserimento (NONE). Il testo si riceve da ui_subject_text. */
UiSubjectResult ui_subject_key(SubjectUI *ui, SDL_Keycode key, SDL_Keymod mod);

/* Testo UTF-8 da SDL_EVENT_TEXT_INPUT (solo in inserimento, altrimenti ignorato). Si aggiunge al
 * nome finché resta < SUBJECT_LEN byte (un carattere che non ci sta viene scartato intero);
 * i caratteri , ; " sono scartati. */
void ui_subject_text(SubjectUI *ui, const char *utf8);

/* Mouse (coordinate già convertite nel renderer): rotella scorre; clic su una riga la seleziona;
 * doppio clic = come Invio (CLOSE); clic fuori dal pannello = CLOSE. */
UiSubjectResult ui_subject_mouse(SubjectUI *ui, const SDL_Event *ev);

/* true mentre si scrive un nome: il chiamante tiene attivo SDL_StartTextInput solo in questo stato. */
bool ui_subject_editing(const SubjectUI *ui);

bool ui_subject_dirty(const SubjectUI *ui);   /* serve un ridisegno */

/* Overlay + pannello. Non chiama Present. */
void ui_subject_draw(SubjectUI *ui);

#endif
