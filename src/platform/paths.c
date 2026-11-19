#include "platform/paths.h"
#include <SDL3/SDL.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define PATH_MAX_LEN 1024

/* <$env>/ribalta, oppure ~/<home_sub>/ribalta se la variabile manca o non è assoluta.
 * macOS: senza la variabile, configurazione e dati stanno insieme in
 * ~/Library/Application Support/Ribalta (le variabili XDG restano valide, es. per i test). */
static bool xdg_dir(char *out, size_t n, const char *env, const char *home_sub)
{
    const char *x = getenv(env);
    if (x && x[0] == '/')
        return snprintf(out, n, "%s/ribalta", x) < (int)n;
    const char *h = getenv("HOME");
    if (!h || !h[0])
        return false;
#ifdef __APPLE__
    (void)home_sub;
    return snprintf(out, n, "%s/Library/Application Support/Ribalta", h) < (int)n;
#else
    return snprintf(out, n, "%s/%s/ribalta", h, home_sub) < (int)n;
#endif
}

bool paths_config_dir(char *out, size_t out_size)
{
    return xdg_dir(out, out_size, "XDG_CONFIG_HOME", ".config");
}

bool paths_config_file(char *out, size_t out_size)
{
    char d[PATH_MAX_LEN];
    return paths_config_dir(d, sizeof d) && snprintf(out, out_size, "%s/config.ini", d) < (int)out_size;
}

bool paths_mkdir_p(const char *path)
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

bool paths_data_dir(char *out, size_t out_size)
{
    char d[PATH_MAX_LEN];
    if (!xdg_dir(d, sizeof d, "XDG_DATA_HOME", ".local/share") || strlen(d) >= out_size)
        return false;
    if (!paths_mkdir_p(d))
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

bool paths_exe_dir(char *out, size_t out_size)
{
    char buf[PATH_MAX_LEN];
    const char *base = SDL_GetBasePath(); /* di proprieta' di SDL, con '/' finale */
    if (base && base[0]) {
        if (snprintf(buf, sizeof buf, "%s", base) >= (int)sizeof buf)
            return false;
    } else {
#ifdef __linux__
        ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
        if (n <= 0)
            return false;
        buf[n] = '\0';
        char *slash = strrchr(buf, '/');
        if (!slash)
            return false;
        slash[1] = '\0';
#else
        return false;
#endif
    }
    size_t len = strlen(buf);
    while (len > 1 && buf[len - 1] == '/')
        buf[--len] = '\0';
    if (buf[0] != '/' || len >= out_size)
        return false;
    memcpy(out, buf, len + 1);
    return true;
}

bool paths_find_asset(const char *name, char *out, size_t out_size)
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
    /* installazione con prefisso (es. Flatpak: /app/bin/ribalta -> /app/share/ribalta) */
    if (base && snprintf(d, sizeof d, "%s/../share/ribalta", base) < (int)sizeof d
        && try_dir(d, name, out, out_size))
        return true;
    if (xdg_dir(d, sizeof d, "XDG_DATA_HOME", ".local/share") && try_dir(d, name, out, out_size))
        return true;
    return try_dir("/usr/share/ribalta", name, out, out_size);
}
