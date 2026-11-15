#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <SDL3/SDL.h>
#include "app/display.h"

/* Colore base delle cifre: anteprima del selettore se attiva, altrimenti quello salvato. */
uint32_t display_base_color(const App *a)
{
    return a->preview_active ? a->preview_color : a->cfg.digit_color;
}

static bool face_equal(const ClockFace *x, const ClockFace *y)
{
    if (x->count != y->count || x->ampm != y->ampm || x->hide_leading_zero != y->hide_leading_zero)
        return false;
    for (int i = 0; i < x->count; i++)
        if (x->values[i] != y->values[i])
            return false;
    return true;
}

/* Materia da mostrare: quella fissata all'inizio della sessione in corso, altrimenti l'attiva. */
static const char *shown_subject(const App *a, time_t start, const char *session_subject)
{
    return start ? session_subject : a->cfg.subject;
}

/* Aggiunge " · <materia>" all'etichetta (base = "TIMER", "STUDIO 1/4", …). */
static void label_with_subject(char *out, size_t n, const char *base, const char *subject)
{
    if (subject && subject[0])
        snprintf(out, n, "%.31s \u00b7 %.24s", base, subject); /* 31 + 4 + 24 < 64 */
    else
        snprintf(out, n, "%s", base);
}

static void build_timer_display(const App *a, Uint64 now, Display *d)
{
    const Timer *t = &a->timer;
    timer_face(t, &a->cfg, now, d->face.values, &d->face.count);
    d->label = t->state == TIMER_PAUSED ? LABEL_PAUSA : LABEL_TIMER;
    d->has_progress = t->state != TIMER_SETUP;
    d->alpha[0] = d->alpha[1] = t->state == TIMER_PAUSED ? 0.5f : 1.0f;
    if (a->entry_active && t->state == TIMER_SETUP) {
        timer_entry_face(&a->entry, d->face.values);
        d->face.count = 2;
        d->label = LABEL_TIMER;
        d->has_progress = false;
        d->alpha[0] = d->alpha[1] = 1.0f;
    } else if (t->state == TIMER_SETUP) {
        d->alpha[0] = a->timer_sel == 0 ? 1.0f : 0.4f;
        d->alpha[1] = a->timer_sel == 1 ? 1.0f : 0.4f;
    }
    const char *subj = shown_subject(a, a->timer_start, a->timer_subject);
    if (subj[0]) /* il testo libero ha la precedenza sull'etichetta TIMER/PAUSA */
        label_with_subject(d->text, sizeof d->text, d->label == LABEL_PAUSA ? "PAUSA" : "TIMER", subj);
}

static void build_pomo_display(const App *a, Uint64 now, Display *d)
{
    const Pomodoro *p = &a->pomo;
    timer_face(&p->timer, &a->cfg, now, d->face.values, &d->face.count);
    pomo_label(p, &a->cfg, d->text, sizeof d->text);
    if (p->phase == POMO_WORK) {
        char base[32];
        snprintf(base, sizeof base, "%.31s", d->text);
        label_with_subject(d->text, sizeof d->text, base, shown_subject(a, a->pomo_start, a->pomo_subject));
    }
    d->has_progress = p->timer.state != TIMER_SETUP;
    d->alpha[0] = d->alpha[1] = p->timer.state == TIMER_PAUSED ? 0.5f : 1.0f;
    uint32_t base = display_base_color(a);
    if (p->phase == POMO_WORK)
        d->color = base;
    else
        d->color = color_is_bluish(base) ? COLOR_BREAK_ALT : COLOR_BREAK;
}

static void build_blink_display(const App *a, Uint64 now, Display *d)
{
    unsigned phase = (unsigned)((now - a->blink_start) / BLINK_PHASE_NS);
    d->face.values[0] = d->face.values[1] = 0;
    d->face.count = 2;
    d->label = LABEL_TIMER;
    d->has_progress = false;
    d->alpha[0] = d->alpha[1] = (phase % 2 == 0) ? 0.15f : 1.0f;
}

static void build_display(const App *a, Uint64 now, Display *d)
{
    memset(d, 0, sizeof *d);
    d->view = a->view;
    d->alpha[0] = d->alpha[1] = 1.0f;
    d->label = LABEL_NONE;
    d->color = display_base_color(a);
    d->accent = display_base_color(a);
    if (a->view == VIEW_CLOCK) {
        clock_get(&a->cfg, &d->face);
    } else if (a->view == VIEW_POMODORO) {
        build_pomo_display(a, now, d);
    } else if (a->blink_active) {
        build_blink_display(a, now, d);
    } else {
        build_timer_display(a, now, d);
    }
}

static bool display_equal(const Display *x, const Display *y)
{
    return x->view == y->view && x->label == y->label && x->has_progress == y->has_progress &&
           x->alpha[0] == y->alpha[0] && x->alpha[1] == y->alpha[1] &&
           x->color == y->color && x->accent == y->accent && strcmp(x->text, y->text) == 0 && face_equal(&x->face, &y->face);
}

static void apply_alpha(App *a, const Display *d)
{
    if (d->alpha[0] == d->alpha[1]) {
        render_set_tile_alpha(a->render, -1, d->alpha[0]);
    } else {
        render_set_tile_alpha(a->render, 0, d->alpha[0]);
        render_set_tile_alpha(a->render, 1, d->alpha[1]);
    }
}

static void apply_display(App *a, const Display *d, Uint64 now)
{
    bool animate = a->cfg.animations && a->has_shown && a->shown.view == d->view;
    const ClockFace *f = &d->face;
    if (d->color != a->applied_color) {
        render_set_digit_color(a->render, (Uint8)(d->color >> 16), (Uint8)(d->color >> 8), (Uint8)d->color);
        a->applied_color = d->color;
    }
    if (d->accent != a->applied_accent) {
        render_set_accent(a->render, (Uint8)(d->accent >> 16), (Uint8)(d->accent >> 8), (Uint8)d->accent);
        a->applied_accent = d->accent;
    }
    render_set_tiles(a->render, f->values, f->count, f->hide_leading_zero, animate, now);
    render_set_ampm(a->render, f->ampm);
    render_set_label(a->render, d->label);
    render_set_label_text(a->render, d->text[0] ? d->text : NULL);
    apply_alpha(a, d);
    float p = -1.0f;
    if (d->has_progress)
        p = timer_progress(d->view == VIEW_POMODORO ? &a->pomo.timer : &a->timer, now);
    render_set_progress(a->render, p);
    a->shown = *d;
    a->has_shown = true;
}

/* Il progresso si aggiorna solo quando cambia qualcosa di visibile (nessun risveglio extra). */
void display_update(App *a, Uint64 now)
{
    Display d;
    build_display(a, now, &d);
    if (!a->has_shown || !display_equal(&d, &a->shown)) {
        apply_display(a, &d, now);
        a->dirty = true;
    }
}

void display_invalidate(App *a)
{
    a->has_shown = false; /* riapplica il volto (24h/12h, secondi) senza animazione */
}
