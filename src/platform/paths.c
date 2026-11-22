#include "platform/paths.h"
#include <SDL3/SDL.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#define PATH_MAX_LEN 1024

/* snprintf è riuscita e il risultato entra (con il '\0') in n byte. */
static bool fits(int r, size_t n)
{
    return r > 0 && (size_t)r < n;
}

bool paths_is_sep(char c)
{
#ifdef _WIN32
    return c == '/' || c == '\\';
#else
    return c == '/';
#endif
}

#ifdef _WIN32
/* p comincia con una lettera di unità seguita da ':' ("C:"). */
static bool has_drive(const char *p)
{
    return ((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')) && p[1] == ':';
}
#endif

bool paths_is_absolute(const char *p)
{
    if (!p || !p[0])
        return false;
    if (p[0] == '/')
        return true;
#ifdef _WIN32
    if (p[0] == '\\' && p[1] == '\\')  /* UNC: \\server\... */
        return true;
    if (has_drive(p) && paths_is_sep(p[2]))
        return true;  /* C:\... o C:/... */
#endif
    return false;
}

bool paths_readable(const char *p)
{
    if (!p || !p[0])
        return false;
#ifdef _WIN32
    return _access(p, 4) == 0;
#else
    return access(p, R_OK) == 0;
#endif
}

/* Cartella dell'app:
 *   <$env>/ribalta se la variabile è un percorso assoluto (su ogni sistema: serve ai test);
 *   altrimenti Linux ~/<home_sub>/ribalta; macOS ~/Library/Application Support/Ribalta
 *   (configurazione e dati insieme); Windows %APPDATA%\Ribalta, o in sua mancanza
 *   %USERPROFILE%\AppData\Roaming\Ribalta (idem: configurazione e dati insieme). */
static bool app_dir(char *out, size_t n, const char *env, const char *home_sub)
{
    const char *x = getenv(env);
    if (x && paths_is_absolute(x))
        return fits(snprintf(out, n, "%s" PATHS_SEP "ribalta", x), n);
#ifdef _WIN32
    (void)home_sub;
    const char *appdata = getenv("APPDATA");
    if (appdata && paths_is_absolute(appdata))
        return fits(snprintf(out, n, "%s" PATHS_SEP "Ribalta", appdata), n);
    const char *profile = getenv("USERPROFILE");
    if (!profile || !profile[0])
        return false;
    return fits(snprintf(out, n, "%s" PATHS_SEP "AppData" PATHS_SEP "Roaming" PATHS_SEP "Ribalta", profile), n);
#else
    const char *h = getenv("HOME");
    if (!h || !h[0])
        return false;
#ifdef __APPLE__
    (void)home_sub;
    return fits(snprintf(out, n, "%s/Library/Application Support/Ribalta", h), n);
#else
    return fits(snprintf(out, n, "%s/%s/ribalta", h, home_sub), n);
#endif
#endif
}

bool paths_config_dir(char *out, size_t out_size)
{
    return app_dir(out, out_size, "XDG_CONFIG_HOME", ".config");
}

bool paths_config_file(char *out, size_t out_size)
{
    char d[PATH_MAX_LEN];
    return paths_config_dir(d, sizeof d) && fits(snprintf(out, out_size, "%s" PATHS_SEP "config.ini", d), out_size);
}

/* Crea una sola cartella: 0 se creata, -1 con errno impostato (come mkdir). */
static int make_dir(const char *p)
{
#ifdef _WIN32
    return _mkdir(p);
#else
    return mkdir(p, 0755);
#endif
}

/* Lunghezza del prefisso di p che non si può (né serve) creare: su Windows la radice del
 * disco ("C:" o "C:\") o, per un percorso UNC, "\\server\share" con l'eventuale separatore
 * finale. 0 su POSIX e quando il percorso non comincia con una radice. */
static size_t root_len(const char *p)
{
#ifdef _WIN32
    if (has_drive(p))
        return paths_is_sep(p[2]) ? 3 : 2;
    if (paths_is_sep(p[0]) && paths_is_sep(p[1]) && p[2] && !paths_is_sep(p[2])) {
        size_t i = 2;
        while (p[i] && !paths_is_sep(p[i]))  /* server */
            i++;
        if (p[i])
            i++;
        while (p[i] && !paths_is_sep(p[i]))  /* condivisione */
            i++;
        if (p[i])
            i++;
        return i;
    }
#else
    (void)p;
#endif
    return 0;
}

bool paths_mkdir_p(const char *path)
{
    char tmp[PATH_MAX_LEN];
    size_t len = strlen(path);
    if (len == 0 || len >= sizeof tmp)
        return false;
    memcpy(tmp, path, len + 1);
    size_t root = root_len(tmp);
    if (len <= root)  /* solo la radice ("C:", "C:\", "\\server\share"): niente da creare */
        return true;
    /* si parte dal secondo carattere (un '/' iniziale non va troncato), oppure dopo la radice */
    for (char *p = tmp + (root > 1 ? root : 1); *p; p++) {
        if (!paths_is_sep(*p))
            continue;
        char sep = *p;
        *p = '\0';
        if (make_dir(tmp) != 0 && errno != EEXIST)
            return false;
        *p = sep;
    }
    return make_dir(tmp) == 0 || errno == EEXIST;
}

bool paths_data_dir(char *out, size_t out_size)
{
    char d[PATH_MAX_LEN];
    if (!app_dir(d, sizeof d, "XDG_DATA_HOME", ".local/share") || strlen(d) >= out_size)
        return false;
    if (!paths_mkdir_p(d))
        return false;
    memcpy(out, d, strlen(d) + 1);
    return true;
}

bool paths_sessions_file(char *out, size_t out_size)
{
    char d[PATH_MAX_LEN];
    return paths_data_dir(d, sizeof d) && fits(snprintf(out, out_size, "%s" PATHS_SEP "sessions.csv", d), out_size);
}

/* ---- asset ---- */

static bool try_dir(const char *dir, const char *name, char *out, size_t n)
{
    if (!dir || !dir[0])
        return false;
    char p[PATH_MAX_LEN];
    if (snprintf(p, sizeof p, "%s" PATHS_SEP "%s", dir, name) >= (int)sizeof p)
        return false;
    if (!paths_readable(p))
        return false;
    if (strlen(p) >= n)
        return false;
    memcpy(out, p, strlen(p) + 1);
    return true;
}

bool paths_exe_dir(char *out, size_t out_size)
{
    char buf[PATH_MAX_LEN];
    const char *base = SDL_GetBasePath(); /* di proprieta' di SDL, con separatore finale */
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
    /* toglie i separatori finali, ma la radice resta: "/" e (Windows) "C:\" */
    size_t len = strlen(buf), keep = 1;
#ifdef _WIN32
    if (has_drive(buf))
        keep = 3;
#endif
    while (len > keep && paths_is_sep(buf[len - 1]))
        buf[--len] = '\0';
    if (!paths_is_absolute(buf) || len >= out_size)
        return false;
    memcpy(out, buf, len + 1);
    return true;
}

bool paths_find_asset(const char *name, char *out, size_t out_size)
{
    char d[PATH_MAX_LEN];
    if (try_dir(getenv("RIBALTA_DATA"), name, out, out_size))
        return true;
    if (try_dir("." PATHS_SEP "assets", name, out, out_size))
        return true;
#ifdef _WIN32
    /* zip e installer mettono gli asset accanto all'eseguibile */
    char exe[PATH_MAX_LEN];
    return paths_exe_dir(exe, sizeof exe) && snprintf(d, sizeof d, "%s" PATHS_SEP "assets", exe) < (int)sizeof d
           && try_dir(d, name, out, out_size);
#else
    const char *base = SDL_GetBasePath(); /* di proprieta' di SDL */
    if (base && snprintf(d, sizeof d, "%s/assets", base) < (int)sizeof d
        && try_dir(d, name, out, out_size))
        return true;
    /* installazione con prefisso (es. Flatpak: /app/bin/ribalta -> /app/share/ribalta) */
    if (base && snprintf(d, sizeof d, "%s/../share/ribalta", base) < (int)sizeof d
        && try_dir(d, name, out, out_size))
        return true;
    if (app_dir(d, sizeof d, "XDG_DATA_HOME", ".local/share") && try_dir(d, name, out, out_size))
        return true;
    return try_dir("/usr/share/ribalta", name, out, out_size);
#endif
}
