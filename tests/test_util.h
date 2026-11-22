#ifndef RIBALTA_TEST_UTIL_H
#define RIBALTA_TEST_UTIL_H
/* Utilità portabili per i test (Linux, macOS, Windows): cartella temporanea, variabili
 * d'ambiente, rimozione di file e cartelle. Solo header, usa SDL (già collegata ai test). */
#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

/* Crea una cartella temporanea nuova "<tmp>/ribalta-test-XXXXXX" e ne scrive il percorso in out.
 * <tmp> = $TMPDIR, poi %TEMP% (Windows), poi /tmp. Ritorna false su errore. */
static inline bool test_mkdtemp(char *out, size_t n)
{
    const char *base = SDL_getenv("TMPDIR");
    if (!base || !*base) base = SDL_getenv("TEMP");
    if (!base || !*base) base = "/tmp";
    Uint64 seed = SDL_GetPerformanceCounter();
    for (int i = 0; i < 100; i++) {
        unsigned r = (unsigned)SDL_rand_bits_r(&seed);
        if (SDL_snprintf(out, n, "%s/ribalta-test-%08x", base, r) >= (int)n) return false;
        SDL_PathInfo info;
        if (SDL_GetPathInfo(out, &info)) continue; /* esiste già */
        if (SDL_CreateDirectory(out)) return true;
    }
    return false;
}

/* setenv(name, value, 1) portabile, nell'ambiente del runtime C (quello letto da getenv in
 * paths.c): su Windows SDL_setenv_unsafe non aggiorna la copia del CRT. */
static inline void test_setenv(const char *name, const char *value)
{
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

/* true se il percorso esiste (file o cartella). */
static inline bool test_exists(const char *path)
{
    SDL_PathInfo info;
    return SDL_GetPathInfo(path, &info);
}

/* Rimuove un file o una cartella vuota; nessun errore se manca. */
static inline void test_remove(const char *path)
{
    SDL_RemovePath(path);
}

static inline SDL_EnumerationResult SDLCALL test_rmtree_cb(void *ud, const char *dir, const char *name);

/* rm -rf path (ricorsivo, senza shell). */
static inline void test_rmtree(const char *path)
{
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(path, &info)) return;
    if (info.type == SDL_PATHTYPE_DIRECTORY)
        SDL_EnumerateDirectory(path, test_rmtree_cb, NULL);
    SDL_RemovePath(path);
}

static inline SDL_EnumerationResult SDLCALL test_rmtree_cb(void *ud, const char *dir, const char *name)
{
    (void)ud;
    char p[1024];
    /* SDL passa dir con il separatore finale */
    if (SDL_snprintf(p, sizeof p, "%s%s", dir, name) < (int)sizeof p)
        test_rmtree(p);
    return SDL_ENUM_CONTINUE;
}

#endif
