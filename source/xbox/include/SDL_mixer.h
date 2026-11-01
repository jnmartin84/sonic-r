/*
 * SDL_mixer.h - minimal SDL_mixer replacement for the original Xbox (nxdk).
 *
 * nxdk's SDL2 port does not ship SDL_mixer. Sonic R only uses a small corner
 * of the API (channels, per-channel effects, one streamed music source), so
 * xbox_mixer.c implements just that subset on top of SDL's audio callback.
 */
#ifndef XBOX_SDL_MIXER_H
#define XBOX_SDL_MIXER_H

#include <SDL.h>

#define MIX_MAX_VOLUME      128
#define MIX_DEFAULT_FORMAT  AUDIO_S16LSB

typedef struct Mix_Chunk {
    int     allocated;
    Uint8  *abuf;
    Uint32  alen;
    Uint8   volume;
    /* Xbox only: S16 mono sources are kept as loaded (abuf = S16 mono at
     * native_freq) and resampled while mixing. Converting them to S16 stereo at
     * the device rate would quadruple their size, and RAM is tight. */
    int     native_mono;
    int     native_freq;
} Mix_Chunk;

typedef struct _Mix_Music Mix_Music;

typedef void (*Mix_EffectFunc_t)(int chan, void *stream, int len, void *udata);
typedef void (*Mix_EffectDone_t)(int chan, void *udata);

int         Mix_OpenAudio(int frequency, Uint16 format, int channels, int chunksize);
void        Mix_CloseAudio(void);
int         Mix_AllocateChannels(int numchans);
int         Mix_QuerySpec(int *frequency, Uint16 *format, int *channels);
const char *Mix_GetError(void);

Mix_Chunk  *Mix_LoadWAV_RW(SDL_RWops *src, int freesrc);
#define Mix_LoadWAV(file) Mix_LoadWAV_RW(SDL_RWFromFile((file), "rb"), 1)
void        Mix_FreeChunk(Mix_Chunk *chunk);

Mix_Music  *Mix_LoadMUS(const char *file);
Mix_Music  *Mix_LoadMUS_RW(SDL_RWops *src, int freesrc);
void        Mix_FreeMusic(Mix_Music *music);

int         Mix_PlayChannelTimed(int channel, Mix_Chunk *chunk, int loops, int ticks);
#define Mix_PlayChannel(channel, chunk, loops) Mix_PlayChannelTimed((channel), (chunk), (loops), -1)
int         Mix_PlayMusic(Mix_Music *music, int loops);

int         Mix_HaltChannel(int channel);
int         Mix_HaltMusic(void);
int         Mix_PlayingMusic(void);

int         Mix_Volume(int channel, int volume);
int         Mix_VolumeMusic(int volume);
int         Mix_SetPanning(int channel, Uint8 left, Uint8 right);

int         Mix_RegisterEffect(int chan, Mix_EffectFunc_t f, Mix_EffectDone_t d, void *arg);
int         Mix_UnregisterAllEffects(int channel);

#endif /* XBOX_SDL_MIXER_H */
