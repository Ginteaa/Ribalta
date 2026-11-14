#include "notify.h"
#include <SDL3/SDL.h>
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

extern char **environ;

#define MAX_CHILDREN 8
#define POLL_MS 100

static char        g_wav_path[1024];
static bool        g_have_wav;
static pid_t       g_children[MAX_CHILDREN];
static int         g_nchildren;

static bool              g_audio_on;   /* sottosistema audio inizializzato */
static SDL_AudioStream  *g_stream;
static Uint8            *g_wav_buf;
static bool              g_drained;    /* coda vuota: un ultimo giro di grazia */
static Uint64            g_deadline;   /* ms (SDL_GetTicks) oltre cui chiudere comunque */

void notify_init(const char *wav_path) {
    g_have_wav = false;
    g_nchildren = 0;
    if (wav_path && wav_path[0] && SDL_strlen(wav_path) < sizeof g_wav_path) {
        SDL_strlcpy(g_wav_path, wav_path, sizeof g_wav_path);
        g_have_wav = true;
    }
}

void notify_desktop(const char *title, const char *body) {
    if (g_nchildren >= MAX_CHILDREN) return;
    char *argv[] = { "notify-send", "-a", "Ribalta", "-i", "alarm-clock", "-u", "normal",
                     (char *)(title ? title : ""), (char *)(body ? body : ""), NULL };
    pid_t pid;
    int rc = posix_spawnp(&pid, "notify-send", NULL, NULL, argv, environ);
    if (rc != 0) {
        SDL_Log("notify: posix_spawnp fallita (%d)", rc);
        return;
    }
    g_children[g_nchildren++] = pid;
}

static void sound_close(void) {
    if (g_stream) { SDL_DestroyAudioStream(g_stream); g_stream = NULL; }
    if (g_wav_buf) { SDL_free(g_wav_buf); g_wav_buf = NULL; }
    if (g_audio_on) { SDL_QuitSubSystem(SDL_INIT_AUDIO); g_audio_on = false; }
    g_drained = false;
}

void notify_sound(void) {
    if (!g_have_wav || g_stream) return;
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        SDL_Log("notify: audio init: %s", SDL_GetError());
        return;
    }
    g_audio_on = true;
    SDL_AudioSpec spec;
    Uint32 len = 0;
    if (!SDL_LoadWAV(g_wav_path, &spec, &g_wav_buf, &len)) {
        SDL_Log("notify: LoadWAV: %s", SDL_GetError());
        g_wav_buf = NULL;
        sound_close();
        return;
    }
    g_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (!g_stream) {
        SDL_Log("notify: OpenAudioDeviceStream: %s", SDL_GetError());
        sound_close();
        return;
    }
    if (!SDL_PutAudioStreamData(g_stream, g_wav_buf, (int)len) ||
        !SDL_FlushAudioStream(g_stream) ||
        !SDL_ResumeAudioStreamDevice(g_stream)) {
        SDL_Log("notify: playback: %s", SDL_GetError());
        sound_close();
        return;
    }
    /* durata del WAV + margine, come rete di sicurezza se il device si blocca */
    int bps = SDL_AUDIO_BYTESIZE(spec.format) * (spec.channels > 0 ? spec.channels : 1);
    Uint64 ms = (spec.freq > 0 && bps > 0) ? (Uint64)len * 1000 / ((Uint64)bps * spec.freq) : 1000;
    g_deadline = SDL_GetTicks() + ms + 3000;
    g_drained = false;
}

bool notify_poll(void) {
    for (int i = 0; i < g_nchildren; ) {
        pid_t r = waitpid(g_children[i], NULL, WNOHANG);
        if (r == 0) { i++; continue; }
        if (r < 0 && errno == EINTR) continue;
        g_children[i] = g_children[--g_nchildren]; /* terminato o errore: rimuovi */
    }
    if (g_stream) {
        if (SDL_GetTicks() >= g_deadline) {
            sound_close();
        } else if (SDL_GetAudioStreamQueued(g_stream) <= 0) {
            /* coda vuota: aspetta un altro giro perche' il buffer del device finisca */
            if (g_drained) sound_close(); else g_drained = true;
        }
    }
    return g_nchildren > 0 || g_stream != NULL;
}

uint32_t notify_ms_to_next_poll(void) {
    return (g_nchildren > 0 || g_stream != NULL) ? POLL_MS : UINT32_MAX;
}

void notify_shutdown(void) {
    sound_close();
    for (int i = 0; i < g_nchildren; i++) waitpid(g_children[i], NULL, WNOHANG);
    g_nchildren = 0;
}
