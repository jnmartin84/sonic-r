/*
 * xbox_mixer.c - minimal SDL_mixer subset for the original Xbox (nxdk).
 *
 * Implements exactly what Sonic R calls: N sample channels with volume, pan and
 * one optional effect callback each, plus a single streamed music source read
 * from an SDL_RWops that presents a canonical 44-byte-header PCM WAV
 * (see sound/music_rwops.c). Output is always signed 16-bit stereo.
 *
 * Untested on hardware: written against the SDL_mixer semantics the game's
 * sfx_sdl.c / music_sdl.c rely on.
 */
#include "SDL_mixer.h"

#include <stdlib.h>
#include <string.h>

#define MAX_CHANNELS 32

typedef struct {
    Mix_Chunk       *chunk;
    Uint32           pos;        /* byte cursor into chunk->abuf */
    int              loops;      /* -1 = forever, else remaining repeats */
    int              playing;
    int              volume;     /* 0..128 */
    int              panL, panR; /* 0..255 */
    Mix_EffectFunc_t effect;
    Mix_EffectDone_t effectDone;
    void            *effectArg;
} Channel;

struct _Mix_Music {
    SDL_RWops *rw;
    int        ownsRw;
    Sint64     dataStart;        /* 44 for the canonical header */
};

static SDL_AudioDeviceID s_dev;
static SDL_AudioSpec     s_spec;
static Channel           s_chan[MAX_CHANNELS];
static int               s_numChan = 8;

static Mix_Music *s_music;
static int        s_musicPlaying;
static int        s_musicLoops;
static int        s_musicVolume = MIX_MAX_VOLUME;

static char s_err[128] = "";

static void set_err(const char *m)
{
    strncpy(s_err, m, sizeof(s_err) - 1);
    s_err[sizeof(s_err) - 1] = '\0';
}

const char *Mix_GetError(void) { return s_err; }

/* ------------------------------------------------------------------ */
/* Mixing                                                              */
/* ------------------------------------------------------------------ */

static void mix_chunk_channel(int idx, Uint8 *out, int len)
{
    Channel *c = &s_chan[idx];
    static Uint8 tmp[16384];
    Uint8 *buf = tmp;

    if (len > (int)sizeof(tmp)) {
        len = (int)sizeof(tmp);
    }
    memset(buf, 0, (size_t)len);

    /* Fill from the chunk, honouring loops. */
    int filled = 0;
    while (filled < len && c->playing) {
        Uint32 remain = c->chunk->alen - c->pos;
        if (remain == 0) {
            if (c->loops == -1 || c->loops > 0) {
                if (c->loops > 0) {
                    c->loops--;
                }
                c->pos = 0;
                continue;
            }
            c->playing = 0;
            break;
        }
        int n = len - filled;
        if ((Uint32)n > remain) {
            n = (int)remain;
        }
        memcpy(buf + filled, c->chunk->abuf + c->pos, (size_t)n);
        c->pos += (Uint32)n;
        filled += n;
    }

    if (c->effect) {
        c->effect(idx, buf, len, c->effectArg);
    }

    /* Pan on S16 stereo. */
    if (c->panL != 255 || c->panR != 255) {
        Sint16 *s = (Sint16 *)buf;
        int frames = len / 4;
        for (int i = 0; i < frames; i++) {
            s[i * 2]     = (Sint16)((s[i * 2]     * c->panL) / 255);
            s[i * 2 + 1] = (Sint16)((s[i * 2 + 1] * c->panR) / 255);
        }
    }

    SDL_MixAudioFormat(out, buf, AUDIO_S16LSB, (Uint32)len, c->volume);
}

static void mix_music(Uint8 *out, int len)
{
    static Uint8 tmp[16384];
    if (len > (int)sizeof(tmp)) {
        len = (int)sizeof(tmp);
    }
    int filled = 0;
    while (filled < len && s_musicPlaying) {
        size_t got = SDL_RWread(s_music->rw, tmp + filled, 1, (size_t)(len - filled));
        if (got == 0) {
            if (s_musicLoops == -1 || s_musicLoops > 0) {
                if (s_musicLoops > 0) {
                    s_musicLoops--;
                }
                SDL_RWseek(s_music->rw, s_music->dataStart, RW_SEEK_SET);
                continue;
            }
            s_musicPlaying = 0;
            break;
        }
        filled += (int)got;
    }
    if (filled > 0) {
        SDL_MixAudioFormat(out, tmp, AUDIO_S16LSB, (Uint32)filled, s_musicVolume);
    }
}

static void SDLCALL audio_cb(void *ud, Uint8 *stream, int len)
{
    (void)ud;
    SDL_memset(stream, 0, (size_t)len);
    if (s_musicPlaying && s_music) {
        mix_music(stream, len);
    }
    for (int i = 0; i < s_numChan; i++) {
        if (s_chan[i].playing && s_chan[i].chunk) {
            mix_chunk_channel(i, stream, len);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Device                                                              */
/* ------------------------------------------------------------------ */

int Mix_OpenAudio(int frequency, Uint16 format, int channels, int chunksize)
{
    SDL_AudioSpec want;
    (void)format; (void)channels;

    SDL_zero(want);
    want.freq     = frequency;
    want.format   = AUDIO_S16LSB;
    want.channels = 2;
    want.samples  = (Uint16)chunksize;
    want.callback = audio_cb;

    for (int i = 0; i < MAX_CHANNELS; i++) {
        s_chan[i].volume = MIX_MAX_VOLUME;
        s_chan[i].panL = s_chan[i].panR = 255;
    }

    s_dev = SDL_OpenAudioDevice(NULL, 0, &want, &s_spec, 0);
    if (s_dev == 0) {
        set_err(SDL_GetError());
        return -1;
    }
    SDL_PauseAudioDevice(s_dev, 0);
    return 0;
}

void Mix_CloseAudio(void)
{
    if (s_dev) {
        SDL_CloseAudioDevice(s_dev);
        s_dev = 0;
    }
}

int Mix_AllocateChannels(int numchans)
{
    if (numchans > 0) {
        s_numChan = numchans > MAX_CHANNELS ? MAX_CHANNELS : numchans;
    }
    return s_numChan;
}

int Mix_QuerySpec(int *frequency, Uint16 *format, int *channels)
{
    if (!s_dev) {
        return 0;
    }
    if (frequency) *frequency = s_spec.freq;
    if (format)    *format    = s_spec.format;
    if (channels)  *channels  = s_spec.channels;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Chunks                                                              */
/* ------------------------------------------------------------------ */

Mix_Chunk *Mix_LoadWAV_RW(SDL_RWops *src, int freesrc)
{
    SDL_AudioSpec spec;
    Uint8 *buf = NULL;
    Uint32 len = 0;
    SDL_AudioCVT cvt;

    if (!src) {
        set_err("null RWops");
        return NULL;
    }
    if (!SDL_LoadWAV_RW(src, freesrc, &spec, &buf, &len)) {
        set_err(SDL_GetError());
        return NULL;
    }
    /* SDL_mixer converts to the device format at load time; sfx_sdl.c depends
     * on abuf being S16 stereo at the device rate. */
    if (SDL_BuildAudioCVT(&cvt, spec.format, spec.channels, spec.freq,
                          s_spec.format, s_spec.channels, s_spec.freq) < 0) {
        SDL_FreeWAV(buf);
        set_err(SDL_GetError());
        return NULL;
    }

    Mix_Chunk *c = (Mix_Chunk *)calloc(1, sizeof(*c));
    if (!c) {
        SDL_FreeWAV(buf);
        return NULL;
    }
    if (cvt.needed) {
        cvt.len = (int)len;
        cvt.buf = (Uint8 *)malloc((size_t)len * (size_t)cvt.len_mult);
        if (!cvt.buf) {
            SDL_FreeWAV(buf);
            free(c);
            return NULL;
        }
        memcpy(cvt.buf, buf, len);
        SDL_FreeWAV(buf);
        if (SDL_ConvertAudio(&cvt) < 0) {
            free(cvt.buf);
            free(c);
            set_err(SDL_GetError());
            return NULL;
        }
        c->abuf = cvt.buf;
        c->alen = (Uint32)cvt.len_cvt;
    }
    else {
        c->abuf = (Uint8 *)malloc(len);
        if (!c->abuf) {
            SDL_FreeWAV(buf);
            free(c);
            return NULL;
        }
        memcpy(c->abuf, buf, len);
        SDL_FreeWAV(buf);
        c->alen = len;
    }
    c->allocated = 1;
    c->volume = MIX_MAX_VOLUME;
    return c;
}

void Mix_FreeChunk(Mix_Chunk *chunk)
{
    if (!chunk) {
        return;
    }
    SDL_LockAudioDevice(s_dev);
    for (int i = 0; i < MAX_CHANNELS; i++) {
        if (s_chan[i].chunk == chunk) {
            s_chan[i].playing = 0;
            s_chan[i].chunk = NULL;
        }
    }
    SDL_UnlockAudioDevice(s_dev);
    if (chunk->allocated) {
        free(chunk->abuf);
    }
    free(chunk);
}

int Mix_PlayChannelTimed(int channel, Mix_Chunk *chunk, int loops, int ticks)
{
    (void)ticks;
    if (!chunk || channel < 0 || channel >= s_numChan) {
        return -1;
    }
    SDL_LockAudioDevice(s_dev);
    Channel *c = &s_chan[channel];
    c->chunk = chunk;
    c->pos = 0;
    c->loops = loops;
    c->playing = 1;
    SDL_UnlockAudioDevice(s_dev);
    return channel;
}

int Mix_HaltChannel(int channel)
{
    SDL_LockAudioDevice(s_dev);
    if (channel < 0) {
        for (int i = 0; i < s_numChan; i++) {
            s_chan[i].playing = 0;
        }
    }
    else if (channel < s_numChan) {
        s_chan[channel].playing = 0;
    }
    SDL_UnlockAudioDevice(s_dev);
    return 0;
}

int Mix_Volume(int channel, int volume)
{
    int prev = MIX_MAX_VOLUME;
    if (volume > MIX_MAX_VOLUME) volume = MIX_MAX_VOLUME;
    SDL_LockAudioDevice(s_dev);
    for (int i = 0; i < s_numChan; i++) {
        if (channel < 0 || channel == i) {
            prev = s_chan[i].volume;
            if (volume >= 0) {
                s_chan[i].volume = volume;
            }
        }
    }
    SDL_UnlockAudioDevice(s_dev);
    return prev;
}

int Mix_SetPanning(int channel, Uint8 left, Uint8 right)
{
    if (channel < 0 || channel >= s_numChan) {
        return 0;
    }
    SDL_LockAudioDevice(s_dev);
    s_chan[channel].panL = left;
    s_chan[channel].panR = right;
    SDL_UnlockAudioDevice(s_dev);
    return 1;
}

int Mix_RegisterEffect(int chan, Mix_EffectFunc_t f, Mix_EffectDone_t d, void *arg)
{
    if (chan < 0 || chan >= s_numChan) {
        return 0;
    }
    SDL_LockAudioDevice(s_dev);
    s_chan[chan].effect = f;
    s_chan[chan].effectDone = d;
    s_chan[chan].effectArg = arg;
    SDL_UnlockAudioDevice(s_dev);
    return 1;
}

int Mix_UnregisterAllEffects(int channel)
{
    if (channel < 0 || channel >= s_numChan) {
        return 0;
    }
    SDL_LockAudioDevice(s_dev);
    s_chan[channel].effect = NULL;
    s_chan[channel].effectDone = NULL;
    s_chan[channel].effectArg = NULL;
    SDL_UnlockAudioDevice(s_dev);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Music                                                               */
/* ------------------------------------------------------------------ */

Mix_Music *Mix_LoadMUS_RW(SDL_RWops *src, int freesrc)
{
    if (!src) {
        set_err("null RWops");
        return NULL;
    }
    Mix_Music *m = (Mix_Music *)calloc(1, sizeof(*m));
    if (!m) {
        return NULL;
    }
    m->rw = src;
    m->ownsRw = freesrc;
    m->dataStart = 44;               /* canonical PCM WAV header */
    SDL_RWseek(src, m->dataStart, RW_SEEK_SET);
    return m;
}

Mix_Music *Mix_LoadMUS(const char *file)
{
    SDL_RWops *rw = SDL_RWFromFile(file, "rb");
    if (!rw) {
        set_err(SDL_GetError());
        return NULL;
    }
    return Mix_LoadMUS_RW(rw, 1);
}

void Mix_FreeMusic(Mix_Music *music)
{
    if (!music) {
        return;
    }
    SDL_LockAudioDevice(s_dev);
    if (s_music == music) {
        s_musicPlaying = 0;
        s_music = NULL;
    }
    SDL_UnlockAudioDevice(s_dev);
    if (music->ownsRw) {
        SDL_RWclose(music->rw);
    }
    free(music);
}

int Mix_PlayMusic(Mix_Music *music, int loops)
{
    if (!music) {
        return -1;
    }
    SDL_LockAudioDevice(s_dev);
    s_music = music;
    s_musicLoops = loops;
    SDL_RWseek(music->rw, music->dataStart, RW_SEEK_SET);
    s_musicPlaying = 1;
    SDL_UnlockAudioDevice(s_dev);
    return 0;
}

int Mix_HaltMusic(void)
{
    SDL_LockAudioDevice(s_dev);
    s_musicPlaying = 0;
    SDL_UnlockAudioDevice(s_dev);
    return 0;
}

int Mix_PlayingMusic(void)
{
    return s_musicPlaying;
}

int Mix_VolumeMusic(int volume)
{
    int prev = s_musicVolume;
    if (volume >= 0) {
        s_musicVolume = volume > MIX_MAX_VOLUME ? MIX_MAX_VOLUME : volume;
    }
    return prev;
}
