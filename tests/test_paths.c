#include "platform/paths.h"
#include "test_util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

/* unsetenv portabile (test_util.h ha solo test_setenv): su Windows _putenv_s con valore vuoto
 * toglie la variabile dall'ambiente del runtime C. */
static void unset_env(const char *name)
{
#ifdef _WIN32
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}

/* true se la stringa ottenuta da paths_*() coincide con "<a><SEP><b>" */
static bool is_join(const char *got, const char *a, const char *b)
{
    char exp[1200];
    snprintf(exp, sizeof exp, "%s" PATHS_SEP "%s", a, b);
    return !strcmp(got, exp);
}

static void test_is_absolute(void)
{
    CHECK(!paths_is_absolute(NULL));
    CHECK(!paths_is_absolute(""));
    CHECK(paths_is_absolute("/"));
    CHECK(paths_is_absolute("/home/u"));
    CHECK(!paths_is_absolute("rel/path"));
    CHECK(!paths_is_absolute("./x"));
    CHECK(!paths_is_absolute("~/x"));
#ifdef _WIN32
    CHECK(paths_is_absolute("C:\\x"));
    CHECK(paths_is_absolute("c:/x"));
    CHECK(paths_is_absolute("C:\\"));
    CHECK(paths_is_absolute("\\\\srv\\sh"));
    CHECK(paths_is_absolute("\\\\srv\\sh\\dir"));
    CHECK(!paths_is_absolute("C:x"));
    CHECK(!paths_is_absolute("C:"));
    CHECK(!paths_is_absolute("x\\y"));
    CHECK(!paths_is_absolute(".\\x"));
#else
    CHECK(!paths_is_absolute("C:\\x"));
    CHECK(!paths_is_absolute("\\\\srv\\sh"));
#endif
}

static void test_is_sep(void)
{
    CHECK(paths_is_sep('/'));
    CHECK(!paths_is_sep('a'));
    CHECK(!paths_is_sep('\0'));
#ifdef _WIN32
    CHECK(paths_is_sep('\\'));
#else
    CHECK(!paths_is_sep('\\'));
#endif
}

/* variabili XDG: hanno la precedenza su ogni sistema, solo se il percorso è assoluto */
static void test_xdg_override(const char *dir)
{
    char cfg[1100], data[1100], out[1200], exp[1200];
    snprintf(cfg, sizeof cfg, "%s/xdg-config", dir);
    snprintf(data, sizeof data, "%s/xdg-data", dir);
    test_setenv("XDG_CONFIG_HOME", cfg);
    test_setenv("XDG_DATA_HOME", data);

    CHECK(paths_config_dir(out, sizeof out));
    CHECK(is_join(out, cfg, "ribalta"));
    CHECK(!test_exists(out));   /* paths_config_dir non crea nulla */
    CHECK(paths_config_file(out, sizeof out));
    snprintf(exp, sizeof exp, "%s" PATHS_SEP "ribalta" PATHS_SEP "config.ini", cfg);
    CHECK(!strcmp(out, exp));

    CHECK(paths_data_dir(out, sizeof out));   /* crea la cartella e quelle intermedie */
    CHECK(is_join(out, data, "ribalta"));
    CHECK(test_exists(out));
    CHECK(paths_sessions_file(out, sizeof out));
    snprintf(exp, sizeof exp, "%s" PATHS_SEP "ribalta" PATHS_SEP "sessions.csv", data);
    CHECK(!strcmp(out, exp));
    CHECK(!test_exists(out));   /* il file non viene creato, solo la sua cartella */

    /* buffer troppo piccoli: false */
    char tiny[8];
    CHECK(!paths_config_dir(tiny, sizeof tiny));
    CHECK(!paths_config_file(tiny, sizeof tiny));
    CHECK(!paths_data_dir(tiny, sizeof tiny));
    CHECK(!paths_sessions_file(tiny, sizeof tiny));
    CHECK(!paths_config_file(out, 0));
}

/* senza XDG (o con XDG relativo) valgono le cartelle predefinite del sistema */
static void test_defaults(const char *dir)
{
    char home[1100], out[1200], exp[1200];
    snprintf(home, sizeof home, "%s/home", dir);
    SDL_CreateDirectory(home);
    unset_env("XDG_CONFIG_HOME");
    unset_env("XDG_DATA_HOME");

#ifdef _WIN32
    /* %APPDATA%\Ribalta */
    test_setenv("APPDATA", home);
    test_setenv("USERPROFILE", "C:\\non\\usato");
    CHECK(paths_config_dir(out, sizeof out));
    CHECK(is_join(out, home, "Ribalta"));
    CHECK(paths_config_file(out, sizeof out));
    snprintf(exp, sizeof exp, "%s\\Ribalta\\config.ini", home);
    CHECK(!strcmp(out, exp));
    /* configurazione e dati nella stessa cartella */
    CHECK(paths_data_dir(out, sizeof out));
    CHECK(is_join(out, home, "Ribalta"));
    CHECK(test_exists(out));
    CHECK(paths_sessions_file(out, sizeof out));
    snprintf(exp, sizeof exp, "%s\\Ribalta\\sessions.csv", home);
    CHECK(!strcmp(out, exp));
    /* un XDG relativo non vale: si usa APPDATA */
    test_setenv("XDG_CONFIG_HOME", "relativo");
    CHECK(paths_config_dir(out, sizeof out) && is_join(out, home, "Ribalta"));
    unset_env("XDG_CONFIG_HOME");

    /* senza APPDATA: %USERPROFILE%\AppData\Roaming\Ribalta */
    unset_env("APPDATA");
    test_setenv("USERPROFILE", home);
    CHECK(paths_config_dir(out, sizeof out));
    snprintf(exp, sizeof exp, "%s\\AppData\\Roaming\\Ribalta", home);
    CHECK(!strcmp(out, exp));
    CHECK(paths_data_dir(out, sizeof out) && !strcmp(out, exp));   /* crea anche AppData\Roaming */
    CHECK(test_exists(out));
    /* un APPDATA non assoluto si ignora */
    test_setenv("APPDATA", "relativo");
    CHECK(paths_config_dir(out, sizeof out) && !strcmp(out, exp));

    /* niente APPDATA né USERPROFILE */
    unset_env("APPDATA");
    unset_env("USERPROFILE");
    CHECK(!paths_config_dir(out, sizeof out));
    CHECK(!paths_config_file(out, sizeof out));
    CHECK(!paths_data_dir(out, sizeof out));
    CHECK(!paths_sessions_file(out, sizeof out));
#else
    test_setenv("HOME", home);
#ifdef __APPLE__
    /* ~/Library/Application Support/Ribalta, per configurazione e dati */
    snprintf(exp, sizeof exp, "%s/Library/Application Support/Ribalta", home);
    CHECK(paths_config_dir(out, sizeof out) && !strcmp(out, exp));
    CHECK(paths_data_dir(out, sizeof out) && !strcmp(out, exp));
#else
    /* ~/.config/ribalta e ~/.local/share/ribalta */
    snprintf(exp, sizeof exp, "%s/.config/ribalta", home);
    CHECK(paths_config_dir(out, sizeof out) && !strcmp(out, exp));
    snprintf(exp, sizeof exp, "%s/.local/share/ribalta", home);
    CHECK(paths_data_dir(out, sizeof out) && !strcmp(out, exp));
#endif
    CHECK(test_exists(out));
    CHECK(paths_config_file(out, sizeof out));
    CHECK(strlen(out) > strlen("/config.ini") && !strcmp(out + strlen(out) - strlen("/config.ini"), "/config.ini"));
    CHECK(paths_sessions_file(out, sizeof out));
    CHECK(strlen(out) > strlen("/sessions.csv") && !strcmp(out + strlen(out) - strlen("/sessions.csv"), "/sessions.csv"));
    /* un XDG relativo non vale */
    test_setenv("XDG_CONFIG_HOME", "relativo");
    char rel[1200];
    CHECK(paths_config_dir(rel, sizeof rel) && strstr(rel, "relativo") == NULL);
    unset_env("XDG_CONFIG_HOME");
    /* senza HOME non si sa dove stare */
    unset_env("HOME");
    CHECK(!paths_config_dir(out, sizeof out));
    CHECK(!paths_config_file(out, sizeof out));
    CHECK(!paths_data_dir(out, sizeof out));
    CHECK(!paths_sessions_file(out, sizeof out));
#endif
}

static void test_mkdir_p(const char *dir)
{
    char p[1200], q[1200];

    snprintf(p, sizeof p, "%s/m/a/b/c", dir);
    CHECK(paths_mkdir_p(p));
    CHECK(test_exists(p));
    SDL_PathInfo info;
    CHECK(SDL_GetPathInfo(p, &info) && info.type == SDL_PATHTYPE_DIRECTORY);
    CHECK(paths_mkdir_p(p));   /* già esistente */
    snprintf(q, sizeof q, "%s/m/a/b/c/", dir);
    CHECK(paths_mkdir_p(q));   /* separatore finale */
    snprintf(q, sizeof q, "%s/m/a", dir);
    CHECK(paths_mkdir_p(q));   /* un livello intermedio */

#ifdef _WIN32
    /* anche con '\' (e separatori misti) */
    snprintf(p, sizeof p, "%s\\w\\x/y\\z", dir);
    CHECK(paths_mkdir_p(p));
    CHECK(test_exists(p));
    /* solo la radice: niente da creare */
    CHECK(paths_mkdir_p("C:"));
    CHECK(paths_mkdir_p("C:\\"));
    CHECK(paths_mkdir_p("c:/"));
    /* percorso UNC fatto della sola radice (\\server\share): non si prova a crearlo */
    CHECK(paths_mkdir_p("\\\\ribalta-test-server\\share"));
    CHECK(paths_mkdir_p("\\\\ribalta-test-server\\share\\"));
#endif

    /* un file al posto di una cartella intermedia: errore */
    snprintf(p, sizeof p, "%s/m/file", dir);
    FILE *f = fopen(p, "wb");
    CHECK(f != NULL);
    if (f) fclose(f);
    snprintf(q, sizeof q, "%s/m/file/sotto", dir);
    CHECK(!paths_mkdir_p(q));

    /* percorso vuoto o troppo lungo */
    CHECK(!paths_mkdir_p(""));
    char big[1200];
    memset(big, 'a', sizeof big - 1);
    big[0] = '/';
    big[sizeof big - 1] = '\0';
    CHECK(!paths_mkdir_p(big));
}

static void test_exe_dir(void)
{
    char out[1200];
    CHECK(paths_exe_dir(out, sizeof out));
    CHECK(paths_is_absolute(out));
    size_t len = strlen(out);
    CHECK(len > 0);
    /* senza separatore finale, salvo la radice ("/" o, su Windows, "C:\") */
    bool root = len == 1;
#ifdef _WIN32
    root = root || (len == 3 && out[1] == ':');
#endif
    CHECK(root || !paths_is_sep(out[len - 1]));
    /* il test sta in build/tests: l'eseguibile deve essere davvero lì */
    char probe[1300];
    snprintf(probe, sizeof probe, "%s" PATHS_SEP "test_paths%s", out,
#ifdef _WIN32
             ".exe"
#else
             ""
#endif
    );
    CHECK(test_exists(probe));
    char tiny[4];
    CHECK(!paths_exe_dir(tiny, sizeof tiny));
}

static void test_find_asset(const char *dir)
{
    char adir[1100], file[1200], out[1200];
    snprintf(adir, sizeof adir, "%s/assets-test", dir);
    CHECK(SDL_CreateDirectory(adir));
    snprintf(file, sizeof file, "%s/ribalta-asset.bin", adir);
    FILE *f = fopen(file, "wb");
    CHECK(f != NULL);
    if (f) { fputs("x", f); fclose(f); }

    unset_env("RIBALTA_DATA");
    CHECK(!paths_find_asset("ribalta-asset.bin", out, sizeof out));
    test_setenv("RIBALTA_DATA", adir);
    CHECK(paths_find_asset("ribalta-asset.bin", out, sizeof out));
    CHECK(is_join(out, adir, "ribalta-asset.bin"));
    CHECK(!paths_find_asset("ribalta-manca.bin", out, sizeof out));
    char tiny[8];
    CHECK(!paths_find_asset("ribalta-asset.bin", tiny, sizeof tiny));
    unset_env("RIBALTA_DATA");
}

int main(void)
{
    char dir[512];
    if (!test_mkdtemp(dir, sizeof dir)) { fprintf(stderr, "test_mkdtemp fallita\n"); return 1; }

    test_is_sep();
    test_is_absolute();
    test_xdg_override(dir);
    test_defaults(dir);
    test_mkdir_p(dir);
    test_exe_dir();
    test_find_asset(dir);

    test_rmtree(dir);

    if (fails) { fprintf(stderr, "%d failure(s)\n", fails); return 1; }
    puts("ok");
    return 0;
}
