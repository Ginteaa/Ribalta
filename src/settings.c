#include "settings.h"
#include <SDL3/SDL.h>
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
}

/* ---- percorsi ---- */

static bool xdg_dir(char *out, size_t n, const char *env, const char *home_sub)
{
    const char *x = getenv(env);
    if (x && x[0] == '/')
        return snprintf(out, n, "%s/ribalta", x) < (int)n;
    const char *h = getenv("HOME");
    if (!h || !h[0])
        return false;
    return snprintf(out, n, "%s/%s/ribalta", h, home_sub) < (int)n;
}

static bool config_path(char *out, size_t n, bool dir_only)
{
    char d[PATH_MAX_LEN];
    if (!xdg_dir(d, sizeof d, "XDG_CONFIG_HOME", ".config"))
        return false;
    if (dir_only)
        return snprintf(out, n, "%s", d) < (int)n;
    return snprintf(out, n, "%s/config.ini", d) < (int)n;
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

static void apply_kv(Settings *s, const char *k, const char *v)
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
}

bool settings_load(Settings *s)
{
    settings_defaults(s);
    char path[PATH_MAX_LEN];
    if (!config_path(path, sizeof path, false))
        return false;
    FILE *f = fopen(path, "r");
    if (!f)
        return false;
    char line[512];
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

static bool mkdir_p(const char *path)
{
    char tmp[PATH_MAX_LEN];
    size_t len = strlen(path);
    if (len == 0 || len >= sizeof tmp)
        return false;
    memcpy(tmp, path, len + 1);
    for (char *p = tmp + 1; *p; p++) {
        if (*p != '/')
            continue;
        *p = '\0';
        if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
            return false;
        *p = '/';
    }
    return mkdir(tmp, 0755) == 0 || errno == EEXIST;
}

bool settings_save(const Settings *s)
{
    char dir[PATH_MAX_LEN], path[PATH_MAX_LEN];
    if (!config_path(dir, sizeof dir, true) || !config_path(path, sizeof path, false))
        return false;
    if (!mkdir_p(dir))
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
    return fclose(f) == 0;
}

bool settings_data_dir(char *out, size_t out_size)
{
    char d[PATH_MAX_LEN];
    if (!xdg_dir(d, sizeof d, "XDG_DATA_HOME", ".local/share") || strlen(d) >= out_size)
        return false;
    if (!mkdir_p(d))
        return false;
    memcpy(out, d, strlen(d) + 1);
    return true;
}

/* ---- asset ---- */

static bool try_dir(const char *dir, const char *name, char *out, size_t n)
{
    if (!dir || !dir[0])
        return false;
    char p[PATH_MAX_LEN];
    if (snprintf(p, sizeof p, "%s/%s", dir, name) >= (int)sizeof p)
        return false;
    if (access(p, R_OK) != 0)
        return false;
    if (strlen(p) >= n)
        return false;
    memcpy(out, p, strlen(p) + 1);
    return true;
}

bool settings_find_asset(const char *name, char *out, size_t out_size)
{
    char d[PATH_MAX_LEN];
    if (try_dir(getenv("RIBALTA_DATA"), name, out, out_size))
        return true;
    if (try_dir("./assets", name, out, out_size))
        return true;
    const char *base = SDL_GetBasePath(); /* di proprieta' di SDL */
    if (base && snprintf(d, sizeof d, "%s/assets", base) < (int)sizeof d
        && try_dir(d, name, out, out_size))
        return true;
    if (xdg_dir(d, sizeof d, "XDG_DATA_HOME", ".local/share") && try_dir(d, name, out, out_size))
        return true;
    return try_dir("/usr/share/ribalta", name, out, out_size);
}
