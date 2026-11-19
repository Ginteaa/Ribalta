#include "core/settings.h"
#include "core/color.h"
#include "platform/paths.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#define PATH_MAX_LEN 1024

void settings_defaults(Settings *s)
{
    s->show_seconds = false;
    s->clock_24h = true;
    s->timer_default_minutes = 60;
    s->timer_sound = true;
    s->timer_notify = true;
    s->scale = 90;
    s->brightness = 100;
    s->start_fullscreen = false;
    s->animations = true;
    s->pomodoro_work = 25;
    s->pomodoro_break = 5;
    s->pomodoro_long_break = 15;
    s->pomodoro_cycles = 4;
    s->pomodoro_auto_continue = true;
    s->daily_goal_min = 0;
    s->weekly_goal_h = 0;
    s->pomodoro_break_mode = BREAK_COLOR_DIGITS;
    s->pomodoro_break_color = COLOR_BREAK;
    s->digit_color = 0xb7b7b7;
    s->recent_count = 0;
    for (int i = 0; i < RECENT_COLORS_MAX; i++)
        s->recent_colors[i] = 0;
    s->font_family[0] = '\0';
    s->font_file[0] = '\0';
    s->subject_count = 0;
    for (int i = 0; i < SUBJECTS_MAX; i++)
        s->subjects[i][0] = '\0';
    s->subject[0] = '\0';
}

/* ---- parsing ---- */

static char *trim(char *s)
{
    while (isspace((unsigned char)*s))
        s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1]))
        *--e = '\0';
    return s;
}

static void parse_bool(const char *v, bool *dst)
{
    if (!strcasecmp(v, "true") || !strcmp(v, "1") || !strcasecmp(v, "yes"))
        *dst = true;
    else if (!strcasecmp(v, "false") || !strcmp(v, "0") || !strcasecmp(v, "no"))
        *dst = false;
}

/* valore non valido -> *dst invariato (default); fuori range -> clamp */
static void parse_int(const char *v, int *dst, int lo, int hi)
{
    char *end;
    errno = 0;
    long x = strtol(v, &end, 10);
    if (end == v || *end != '\0' || errno)
        return;
    if (x < lo)
        x = lo;
    if (x > hi)
        x = hi;
    *dst = (int)x;
}

static void parse_recent(Settings *s, char *v)
{
    s->recent_count = 0;
    for (char *tok = strtok(v, ","); tok && s->recent_count < RECENT_COLORS_MAX; tok = strtok(NULL, ",")) {
        uint32_t c;
        if (color_parse_hex(tok, &c))
            s->recent_colors[s->recent_count++] = c;
    }
}

/* Stringa fino a fine riga (spazi interni conservati); troppo lunga -> ignorata. */
static void parse_str(const char *v, char *dst, size_t cap)
{
    if (strlen(v) >= cap)
        return;
    memcpy(dst, v, strlen(v) + 1);
}

static void parse_subjects(Settings *s, char *v)
{
    s->subject_count = 0;
    char *save = NULL;
    for (char *tok = strtok_r(v, ";", &save); tok; tok = strtok_r(NULL, ";", &save))
        settings_subject_add(s, tok);   /* scarta non validi, duplicati, eccedenti */
}

/* digits|auto|#rrggbb (parole senza distinzione di maiuscole); valore non valido: false, nulla cambia */
static bool parse_break_color(Settings *s, const char *v)
{
    uint32_t c;
    if (!strcasecmp(v, "digits")) s->pomodoro_break_mode = BREAK_COLOR_DIGITS;
    else if (!strcasecmp(v, "auto")) s->pomodoro_break_mode = BREAK_COLOR_AUTO;
    else if (color_parse_hex(v, &c)) {
        s->pomodoro_break_mode = BREAK_COLOR_CUSTOM;
        s->pomodoro_break_color = c;
    } else
        return false;
    return true;
}

static void apply_kv(Settings *s, const char *k, char *v)
{
    if (!strcmp(k, "show_seconds")) parse_bool(v, &s->show_seconds);
    else if (!strcmp(k, "clock_24h")) parse_bool(v, &s->clock_24h);
    else if (!strcmp(k, "timer_default_minutes")) parse_int(v, &s->timer_default_minutes, 1, 1439);
    else if (!strcmp(k, "timer_sound")) parse_bool(v, &s->timer_sound);
    else if (!strcmp(k, "timer_notify")) parse_bool(v, &s->timer_notify);
    else if (!strcmp(k, "scale")) parse_int(v, &s->scale, 50, 100);
    else if (!strcmp(k, "brightness")) parse_int(v, &s->brightness, 20, 100);
    else if (!strcmp(k, "start_fullscreen")) parse_bool(v, &s->start_fullscreen);
    else if (!strcmp(k, "animations")) parse_bool(v, &s->animations);
    else if (!strcmp(k, "pomodoro_work")) parse_int(v, &s->pomodoro_work, 1, 180);
    else if (!strcmp(k, "pomodoro_break")) parse_int(v, &s->pomodoro_break, 1, 60);
    else if (!strcmp(k, "pomodoro_long_break")) parse_int(v, &s->pomodoro_long_break, 1, 120);
    else if (!strcmp(k, "daily_goal_min")) parse_int(v, &s->daily_goal_min, 0, 720);
    else if (!strcmp(k, "weekly_goal_h")) parse_int(v, &s->weekly_goal_h, 0, 100);
    else if (!strcmp(k, "pomodoro_cycles")) parse_int(v, &s->pomodoro_cycles, 1, 12);
    else if (!strcmp(k, "pomodoro_auto_continue")) parse_bool(v, &s->pomodoro_auto_continue);
    else if (!strcmp(k, "digit_color")) {
        uint32_t c;
        if (color_parse_hex(v, &c))
            s->digit_color = c;
    }
    else if (!strcmp(k, "recent_colors")) parse_recent(s, v);
    else if (!strcmp(k, "font_family")) parse_str(v, s->font_family, sizeof s->font_family);
    else if (!strcmp(k, "font_file")) parse_str(v, s->font_file, sizeof s->font_file);
    else if (!strcmp(k, "subjects")) parse_subjects(s, v);
    else if (!strcmp(k, "subject")) parse_str(v, s->subject, sizeof s->subject);
}

bool settings_load(Settings *s)
{
    settings_defaults(s);
    char path[PATH_MAX_LEN];
    if (!paths_config_file(path, sizeof path))
        return false;
    FILE *f = fopen(path, "r");
    if (!f)
        return false;
    char line[1024];
    bool seen_break_color = false, legacy_blue = false;
    while (fgets(line, sizeof line, f)) {
        char *p = trim(line);
        if (*p == '#' || *p == '\0')
            continue;
        char *eq = strchr(p, '=');
        if (!eq)
            continue;
        *eq = '\0';
        char *k = trim(p), *v = trim(eq + 1);
        if (!strcmp(k, "pomodoro_break_color")) {
            if (parse_break_color(s, v))
                seen_break_color = true;
        } else if (!strcmp(k, "pomodoro_break_blue")) {
            bool b = false;   /* chiave vecchia: vale solo "true" */
            parse_bool(v, &b);
            if (b)
                legacy_blue = true;
        } else
            apply_kv(s, k, v);
    }
    fclose(f);
    /* compatibilità: pomodoro_break_blue=true senza pomodoro_break_color valido vale auto */
    if (legacy_blue && !seen_break_color)
        s->pomodoro_break_mode = BREAK_COLOR_AUTO;
    /* la materia attiva vale solo se in elenco (la chiave può precedere subjects) */
    if (settings_subject_find(s, s->subject) < 0)
        s->subject[0] = '\0';
    return true;
}

/* ---- salvataggio ---- */

bool settings_save(const Settings *s)
{
    char dir[PATH_MAX_LEN], path[PATH_MAX_LEN];
    if (!paths_config_dir(dir, sizeof dir) || snprintf(path, sizeof path, "%s/config.ini", dir) >= (int)sizeof path)
        return false;
    if (!paths_mkdir_p(dir))
        return false;
    FILE *f = fopen(path, "w");
    if (!f)
        return false;
#define B(x) ((x) ? "true" : "false")
    fprintf(f, "# ribalta config\n");
    fprintf(f, "show_seconds=%s\n", B(s->show_seconds));
    fprintf(f, "clock_24h=%s\n", B(s->clock_24h));
    fprintf(f, "timer_default_minutes=%d\n", s->timer_default_minutes);
    fprintf(f, "timer_sound=%s\n", B(s->timer_sound));
    fprintf(f, "timer_notify=%s\n", B(s->timer_notify));
    fprintf(f, "scale=%d\n", s->scale);
    fprintf(f, "brightness=%d\n", s->brightness);
    fprintf(f, "start_fullscreen=%s\n", B(s->start_fullscreen));
    fprintf(f, "animations=%s\n", B(s->animations));
    fprintf(f, "pomodoro_work=%d\n", s->pomodoro_work);
    fprintf(f, "pomodoro_break=%d\n", s->pomodoro_break);
    fprintf(f, "pomodoro_long_break=%d\n", s->pomodoro_long_break);
    fprintf(f, "pomodoro_cycles=%d\n", s->pomodoro_cycles);
    fprintf(f, "daily_goal_min=%d\n", s->daily_goal_min);
    fprintf(f, "weekly_goal_h=%d\n", s->weekly_goal_h);
    fprintf(f, "pomodoro_auto_continue=%s\n", B(s->pomodoro_auto_continue));
#undef B
    char hex[8];
    if (s->pomodoro_break_mode == BREAK_COLOR_CUSTOM) {
        color_format_hex(s->pomodoro_break_color, hex);
        fprintf(f, "pomodoro_break_color=%s\n", hex);
    } else
        fprintf(f, "pomodoro_break_color=%s\n", s->pomodoro_break_mode == BREAK_COLOR_AUTO ? "auto" : "digits");
    color_format_hex(s->digit_color, hex);
    fprintf(f, "digit_color=%s\n", hex);
    fprintf(f, "font_family=%s\n", s->font_family);
    fprintf(f, "font_file=%s\n", s->font_file);
    fprintf(f, "subjects=");
    for (int i = 0; i < s->subject_count && i < SUBJECTS_MAX; i++)
        fprintf(f, "%s%s", i ? ";" : "", s->subjects[i]);
    fprintf(f, "\nsubject=%s\n", s->subject);
    if (s->recent_count > 0) {
        fprintf(f, "recent_colors=");
        for (int i = 0; i < s->recent_count && i < RECENT_COLORS_MAX; i++) {
            color_format_hex(s->recent_colors[i], hex);
            fprintf(f, "%s%s", i ? "," : "", hex);
        }
        fprintf(f, "\n");
    }
    return fclose(f) == 0;
}

void settings_push_recent(Settings *s, uint32_t rgb)
{
    uint32_t out[RECENT_COLORS_MAX];
    int n = 0;
    out[n++] = rgb;
    for (int i = 0; i < s->recent_count && i < RECENT_COLORS_MAX && n < RECENT_COLORS_MAX; i++)
        if (s->recent_colors[i] != rgb)
            out[n++] = s->recent_colors[i];
    for (int i = 0; i < n; i++)
        s->recent_colors[i] = out[i];
    s->recent_count = n;
}

/* ---- materie ---- */

bool settings_subject_valid(const char *name)
{
    if (!name || !*name)
        return false;
    size_t n = strlen(name);
    if (n >= SUBJECT_LEN)
        return false;
    if (isspace((unsigned char)name[0]) || isspace((unsigned char)name[n - 1]))
        return false;
    for (const unsigned char *c = (const unsigned char *)name; *c; c++)
        if (*c < 0x20 || *c == 0x7f || *c == ',' || *c == ';' || *c == '"')
            return false;
    return true;
}

bool settings_subject_add(Settings *s, const char *name)
{
    if (!name || s->subject_count >= SUBJECTS_MAX)
        return false;
    char buf[SUBJECT_LEN + 64];
    if (strlen(name) >= sizeof buf)
        return false;
    memcpy(buf, name, strlen(name) + 1);
    const char *t = trim(buf);
    if (!settings_subject_valid(t))
        return false;
    for (int i = 0; i < s->subject_count; i++)
        if (!strcasecmp(s->subjects[i], t))
            return false;
    memcpy(s->subjects[s->subject_count++], t, strlen(t) + 1);
    return true;
}

void settings_subject_remove(Settings *s, int index)
{
    if (index < 0 || index >= s->subject_count)
        return;
    if (!strcmp(s->subject, s->subjects[index]))
        s->subject[0] = '\0';
    for (int i = index; i < s->subject_count - 1; i++)
        memcpy(s->subjects[i], s->subjects[i + 1], SUBJECT_LEN);
    s->subject_count--;
    s->subjects[s->subject_count][0] = '\0';
}

int settings_subject_find(const Settings *s, const char *name)
{
    if (!name || !*name)
        return -1;
    for (int i = 0; i < s->subject_count; i++)
        if (!strcmp(s->subjects[i], name))
            return i;
    return -1;
}
