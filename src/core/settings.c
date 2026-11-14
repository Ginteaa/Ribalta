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
    s->digit_color = 0xb7b7b7;
    s->recent_count = 0;
    for (int i = 0; i < RECENT_COLORS_MAX; i++)
        s->recent_colors[i] = 0;
    s->font_family[0] = '\0';
    s->font_file[0] = '\0';
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
    while (fgets(line, sizeof line, f)) {
        char *p = trim(line);
        if (*p == '#' || *p == '\0')
            continue;
        char *eq = strchr(p, '=');
        if (!eq)
            continue;
        *eq = '\0';
        apply_kv(s, trim(p), trim(eq + 1));
    }
    fclose(f);
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
    fprintf(f, "pomodoro_auto_continue=%s\n", B(s->pomodoro_auto_continue));
#undef B
    char hex[8];
    color_format_hex(s->digit_color, hex);
    fprintf(f, "digit_color=%s\n", hex);
    fprintf(f, "font_family=%s\n", s->font_family);
    fprintf(f, "font_file=%s\n", s->font_file);
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
