/**
 * sfx_sdl.c — SFX playback via SDL_mixer
 *
 * Replaces sfx_macos.m (AVFoundation) with pure C SDL_mixer calls.
 * 64 slots matching the original g_soundBuffers[64] array.
 * WAV files are 16-bit mono 22050 Hz PCM (SOUND/SFX/ *.WAV).
 *
 * Replaces: IDirectSound, IDirectSoundBuffer COM calls.
 *
 * Limitation: SDL_mixer does not support per-channel playback rate
 * changes. The frequency modulation in PlaySoundWithParams is
 * approximated but not exact.
 */

#include <SDL.h>
#include <SDL_mixer.h>
#include <math.h>
#include <stdio.h>
#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "sonicr_functions.h"
#include "sonicr_paths.h"
#include "path_ci.h"
#include "replay_voice.h"

extern void SetAllSoundVolumes(void);        /* 0x4D0760 */

extern int   g_soundActive[64];    /* 0x006DA080 */

/* =====================================================================
 * State
 * ===================================================================== */

#define SFX_MAX_SLOTS 64

/* The replay commentary plays at full volume: PlaySoundSimple (0x4d02d4) sets
 * no volume, and SetAllSoundVolumes skips the announcer slot during
 * demo/replay (0x4d07a6), so it keeps its fresh-buffer DSBVOLUME_MAX default
 * and cuts through the music. The clip is split across consecutive slots from
 * REPLAY_VOICE_SLOT_FIRST, so the exemption covers the whole range or the line
 * would change level partway through. See sound/replay_voice.c. */

static Mix_Chunk *s_chunks[SFX_MAX_SLOTS];
static int s_ready = 0;

/* =====================================================================
 * Custom voice path — SDL_mixer has no per-channel playback-rate control,
 * so for pitched sounds (looping engine pitch + the double-jump one-shot,
 * mirroring dc/src/sfx_dc.c) we hijack the channel: play the chunk on it,
 * then a per-channel effect OVERWRITES the channel buffer with our own
 * resampled+gained output. The effect ignores what Mix put there and reads
 * chunk->abuf directly through a phase accumulator.
 *
 * Base rate is 22050 (the WAV's native rate = the DirectSound buffer's
 * default frequency), even though Mix_LoadWAV upsampled abuf to the 44100
 * stereo device format: step 1.0 in the 44100 frame space == normal pitch.
 * Pan was never used by the 1998 binary (only SetVolume 0x3c + SetFrequency
 * 0x44, no SetPan 0x40), so voices are centered — a single volume gain.
 * ===================================================================== */
typedef struct {
    const Sint16 *pcm;      /* chunk->abuf as S16, device fmt: stereo interleaved */
    Uint32        frames;   /* length in stereo frames */
    Uint64        phase;    /* 32.32 fixed-point frame cursor; audio thread only */
    int           loop;
    SDL_atomic_t  freqHz;   /* live target frequency (game thread writes) */
    SDL_atomic_t  gain;     /* live volume, 0..256 (256 = unity) */
    SDL_atomic_t  done;     /* set by the effect when a one-shot finishes */
    int           active;   /* slot holds a live voice (main-thread bookkeeping) */
} SfxVoice;
static SfxVoice s_voice[SFX_MAX_SLOTS];

#define SFX_BASE_RATE 22050

/* g_masterVolume -> linear 0..1.
 *
 * SetAllSoundVolumes produces g_volumeBase(-2500)..0, one step per SFX volume
 * setting, and the distance attenuation stays inside that span; normalising
 * across it gives an even 1/8 per step.
 * Mirrors ds_volume_to_linear in dc/src/sfx_dc.c. */
/* SDL_mixer scales samples linearly, where the DC path hands the fraction to
 * the AICA as attenuation; the same 1/8 step therefore sounds louder here.
 * The exponent tapers the middle of the slider to compensate — 1.0 is the raw
 * fraction, higher is quieter, and 8/8 stays full scale either way. */
#define SFX_CURVE_EXP 1.5f
static float sfx_ds_linear(int dsVolume)
{
    if (dsVolume <= g_volumeBase) {
        return 0.0f;
    }
    if (dsVolume >= 0) {
        return 1.0f;
    }
    float t = (float)(dsVolume - g_volumeBase) / (float)(-g_volumeBase);
    return powf(t, SFX_CURVE_EXP);
}

/* Per-channel effect: overwrite `stream` with the resampled voice. Runs on
 * the audio thread — reads live params via atomics, owns phase itself. */
static void SFX_VoiceEffect(int chan, void *stream, int len, void *udata)
{
    (void)chan;
    SfxVoice *v = (SfxVoice *)udata;
    Sint16 *out = (Sint16 *)stream;
    int outFrames = len >> 2;               /* stereo S16 => 4 bytes/frame */

    if (v->pcm == NULL || v->frames == 0) {
        SDL_memset(stream, 0, (size_t)len); return;
    }

    int freq = SDL_AtomicGet(&v->freqHz);
    if (freq <= 0) {
        freq = SFX_BASE_RATE;
    }
    int gain = SDL_AtomicGet(&v->gain);     /* 0..256 */
    Uint64 step = (Uint64)((double)freq / (double)SFX_BASE_RATE * 4294967296.0);
    Uint64 end = (Uint64)v->frames << 32;
    Uint64 phase = v->phase;
    int i = 0;
    for (; i < outFrames; i++) {
        if (phase >= end) {
            if (v->loop) {
                phase -= end;
                if (phase >= end) {
                    phase %= end;
                }
            }
            else {
                break;                     /* one-shot exhausted */
            }
        }
        Uint32 idx  = (Uint32)(phase >> 32);
        Uint32 frac = (Uint32)(phase & 0xFFFFFFFFu);
        Uint32 idx1 = idx + 1;
        if (idx1 >= v->frames) {
            idx1 = v->loop ? 0u : idx;
        }

        Sint32 l0 = v->pcm[2*idx];
        Sint32 l1 = v->pcm[2*idx1];
        Sint32 r0 = v->pcm[2*idx + 1];
        Sint32 r1 = v->pcm[2*idx1 + 1];
        Sint32 l = l0 + (Sint32)(((Sint64)(l1 - l0) * frac) >> 32);
        Sint32 r = r0 + (Sint32)(((Sint64)(r1 - r0) * frac) >> 32);
        l = (l * gain) >> 8;
        r = (r * gain) >> 8;
        out[2*i] = (Sint16)(l < -32768 ? -32768 : (l > 32767 ? 32767 : l));
        out[2*i + 1] = (Sint16)(r < -32768 ? -32768 : (r > 32767 ? 32767 : r));
        phase += step;
    }
    for (; i < outFrames; i++) {
        out[2*i] = 0;
        out[2*i + 1] = 0;
    }   /* zero-fill tail */

    v->phase = phase;
    if (!v->loop && phase >= end) {
        SDL_AtomicSet(&v->done, 1);
    }
}

/* Start (or restart) a pitched voice on `slot`. gain256 is 0..256. */
static void SFX_PlayVoice(int slot, int gain256, int freqHz, int loop)
{
    Mix_Chunk *c = s_chunks[slot];
    SfxVoice *v = &s_voice[slot];

    Mix_HaltChannel(slot);
    Mix_UnregisterAllEffects(slot);         /* drop any stale effect/panning */

    v->pcm = (const Sint16 *)c->abuf;
    v->frames = c->alen >> 2;               /* stereo S16 */
    v->phase = 0;
    v->loop = loop ? 1 : 0;
    SDL_AtomicSet(&v->freqHz, freqHz > 0 ? freqHz : SFX_BASE_RATE);
    SDL_AtomicSet(&v->gain, gain256);
    SDL_AtomicSet(&v->done, 0);
    v->active = 1;

    /* Loop the underlying channel forever so Mix never auto-halts it — WE own
     * the lifetime (the effect ends one-shots and SFX_Tick reaps them). Mix's
     * own volume is irrelevant since the effect overwrites the buffer. */
    Mix_Volume(slot, MIX_MAX_VOLUME);
    Mix_PlayChannel(slot, c, -1);
    Mix_RegisterEffect(slot, SFX_VoiceEffect, NULL, v);
}

/* Tear a voice off a slot (used when a plain/stock play reclaims it). */
static void SFX_ClearVoice(int slot)
{
    if (!s_voice[slot].active) {
        return;
    }
    Mix_UnregisterAllEffects(slot);
    s_voice[slot].active = 0;
    s_voice[slot].loop = 0;
    s_voice[slot].pcm = NULL;
}

/* Reap finished one-shot voices. Call once per frame (game_loop) and lazily
 * from SFX_Play. Halting must happen off the audio thread — hence not in the
 * effect itself (Mix's channel lock would re-enter). */
void SFX_Tick(void)
{
    for (int i = 0; i < SFX_MAX_SLOTS; i++) {
        if (s_voice[i].active && SDL_AtomicGet(&s_voice[i].done)) {
            Mix_HaltChannel(i);
            SFX_ClearVoice(i);
        }
    }
}

/* =====================================================================
 * Slot-to-filename mapping — extracted from FUN_004d07d4 in SONICR.EXE.
 * ===================================================================== */

static const struct { int slot; const char *filename; } s_sfxTable[] = {
    { 0x00, "PAUSE.WAV"    },
    { 0x01, "CHOOSE.WAV"   },
    { 0x02, "SELECT.WAV"   },
    { 0x03, "RUNLEFT.WAV"  },
    { 0x04, "RUNRIGHT.WAV" },
    { 0x05, "AMY.WAV"      },
    { 0x06, "JET.WAV"      },
    { 0x07, "JUMP.WAV"     },
    { 0x08, "SPIN.WAV"     },
    { 0x09, "SPINGO.WAV"   },
    { 0x0A, "SPINREV.WAV"  },
    { 0x0B, "TAILS.WAV"    },
    { 0x0D, "JUMP.WAV"     },
    { 0x0E, "FIRE.WAV"     },
    { 0x0F, "EXPLODE.WAV"  },
    { 0x10, "AMYSKID.WAV"  },
    { 0x11, "AMYWATER.WAV" },
    { 0x12, "WATERRUN.WAV" },
    { 0x13, "WATERRUN.WAV" },
    { 0x14, "BUBBLE.WAV"   },
    { 0x15, "SPLASH.WAV"   },
    { 0x16, "POP.WAV"      },
    { 0x18, "HITCHAR.WAV"  },
    { 0x1A, "BONUS.WAV"    },
    { 0x1B, "GETTOKEN.WAV" },
    { 0x1C, "GETCHAOS.WAV" },
    { 0x1D, "RING1.WAV"    },
    { 0x1E, "RING1.WAV"    },
    { 0x1F, "WARP.WAV"     },
    { 0x20, "SKID1.WAV"    },
    { 0x21, "DOOR.WAV"     },
    { 0x22, "RECORD.WAV"   },
    { 0x23, "GOTALL.WAV"   },
    { 0x24, "BONUS.WAV"    },
    { 0x27, "TAG.WAV"      },
    { 0x2D, "THUNDER.WAV"  },
    { 0x32, "SPRING.WAV"   },
    { 0x33, "BUMPER1.WAV"  },
    { 0x34, "BUMPER2.WAV"  },
    { 0x35, "READY.WAV"    },
    { 0x36, "SET.WAV"      },
    { 0x37, "GO.WAV"       },
    { -1,   NULL           }
};

/* =====================================================================
 * Load a WAV file for the given slot.
 * ===================================================================== */
static int LoadWAVIntoSlot(int slot, const char *filename)
{
    if (slot < 0 || slot >= SFX_MAX_SLOTS) {
        return 0;
    }

    /* Release whatever the slot already holds, or reloading it once per replay
     * leaks a chunk each time. Halt before freeing: a live voice on this slot
     * reads chunk->abuf from the audio thread, so the channel has to be silent
     * and its effect unregistered before the memory goes away. */
    if (s_chunks[slot] != NULL) {
        Mix_HaltChannel(slot);
        SFX_ClearVoice(slot);
        Mix_FreeChunk(s_chunks[slot]);
        s_chunks[slot] = NULL;
        g_soundBuffers[slot] = NULL;
        g_soundActive[slot] = 0;
    }

    char path[512];
    snprintf(path, sizeof(path), DATA_DIR "/SOUND/SFX/%s", filename);

    /* SDL_mixer re-opens the path internally, so resolve the real on-disk
     * casing on case-sensitive filesystems before handing it over. */
    char *realPath = sr_resolve_case(path);
    const char *loadPath = realPath ? realPath : path;
    Mix_Chunk *chunk = Mix_LoadWAV(loadPath);
    free(realPath);
    if (chunk == NULL) {
        DebugLog("SFX: failed to load slot 0x%02X (%s): %s\n", slot, filename, Mix_GetError());
        return 0;
    }

    s_chunks[slot] = chunk;

    /* Mark as active in the game's global array */
    g_soundBuffers[slot] = (void *)(intptr_t)1;
    g_soundActive[slot] = 1;

    return 1;
}

/* =====================================================================
 * InitDirectSound — replacement for DirectSoundCreate.
 * Loads all SFX WAV files.
 * ===================================================================== */
void InitDirectSound(void)
{
    DebugLog("InitDirectSound (SDL_mixer)\n");

    /* Allocate enough mixer channels for all SFX slots */
    Mix_AllocateChannels(SFX_MAX_SLOTS);

    s_ready = 1;
    g_lpDirectSound = (void *)(intptr_t)1;
    g_initFeatureB = 1;                                /* 0x6D9AE8 */

    for (int i = 0; s_sfxTable[i].slot >= 0; i++) {
        LoadWAVIntoSlot(s_sfxTable[i].slot, s_sfxTable[i].filename);
    }

    /* Compute g_masterVolume from g_optSfxVolume and apply to all channels */
    SetAllSoundVolumes();
}

/* =====================================================================
 * SFX playback API
 * ===================================================================== */

void SFX_Play(int slot, int loop, int freq)
{
    if (!s_ready) {
        return;
    }
    if (slot < 0 || slot >= SFX_MAX_SLOTS) {
        return;
    }
    if (s_chunks[slot] == NULL) {
        return;
    }

    SFX_Tick();   /* lazily reap finished one-shot voices */

    float vol = sfx_ds_linear(g_masterVolume);
    if (IS_REPLAY_VOICE_SLOT(slot)) {
        vol = 1.0f;   /* binary exempts the announcer slot from attenuation */
    }
    int gain256 = (int)(vol * 256.0f);
    int mixVol  = (int)(vol * MIX_MAX_VOLUME);

    if (loop) {
        /* Start the loop once; thereafter update pitch LIVE in place — never
         * restart, or a continuously-varying frequency machine-guns the sample
         * from 0 each frame. Mirrors dc/src/sfx_dc.c. Volume is owned by
         * SFX_SetVolume (called per-frame with the distance-attenuated value),
         * so the loop live-update touches freq only. */
        if (!s_voice[slot].active || !s_voice[slot].loop) {
            SFX_PlayVoice(slot, gain256, freq, 1);
            return;
        }
        if (freq && freq != SDL_AtomicGet(&s_voice[slot].freqHz)) {
            SDL_AtomicSet(&s_voice[slot].freqHz, freq);
        }
        return;
    }

    /* One-shot. The double-jump (slot 0x0D) is pitched up to 22050+5512 here,
     * mirroring the DC special case — it arrives via the simple path with
     * freq 0, and we inject the pitch by slot. */
    if (slot == 0xD) {
        freq = 22050 + 5512;
    }

    if (freq) {
        SFX_PlayVoice(slot, gain256, freq, 0);   /* pitched one-shot via resampler */
        return;
    }

    /* Plain one-shot — stock mixer path (unchanged behavior). */
    SFX_ClearVoice(slot);
    Mix_Volume(slot, mixVol);
    Mix_PlayChannel(slot, s_chunks[slot], 0);
}

void SFX_Stop(int slot)
{
    if (!s_ready) {
        return;
    }
    if (slot < 0 || slot >= SFX_MAX_SLOTS) {
        return;
    }
    if (s_chunks[slot] == NULL) {
        return;
    }
    Mix_HaltChannel(slot);
    SFX_ClearVoice(slot);
}

void SFX_StopAll(void)
{
    if (!s_ready) {
        return;
    }
    Mix_HaltChannel(-1);  /* -1 halts all channels */
}

/* Per-slot mix trim, 256 = unity. DELIBERATE DIVERGENCE from the 1998 mix.
 *
 * The aggregate loops play as PlaySoundEffect(0x1000B, minDist, pitch), and
 * 0x4D0396 clamps any distance below 0x40 straight to 0xFF — so a loop that
 * belongs to the player themselves runs at full SFX volume for the entire
 * race with only its pitch moving. Faithful, and fatiguing on slot 0x0B.
 *
 * Applied to the linear gain rather than the DirectSound value: the latter is
 * an attenuation across g_volumeBase..0, and scaling it would bend the curve
 * instead of the level. Slots not listed stay at unity.
 *
 * Only reaches SFX_SetVolume, which owns the volume of looping voices — the
 * case this exists for. Plain one-shots take their gain from SFX_Play and are
 * NOT trimmed; adding an entry for one would silently do nothing. */
#define SFX_TRIM_UNITY 256
static const short s_sfxSlotTrim[SFX_MAX_SLOTS] = {
    [0x0B] = 154,   /* TAILS.WAV — rotor loop, 60% */
};

static float sfx_slot_trim(int slot)
{
    int t = s_sfxSlotTrim[slot];
    if (t <= 0) {
        return 1.0f;
    }
    return (float)t / (float)SFX_TRIM_UNITY;
}

void SFX_SetVolume(int slot, int dsVolume)
{
    if (slot < 0 || slot >= SFX_MAX_SLOTS) {
        return;
    }
    if (s_chunks[slot] == NULL) {
        return;
    }

    float linear = sfx_ds_linear(dsVolume) * sfx_slot_trim(slot);

    if (s_voice[slot].active) {
        /* Live voice: update the in-effect gain — Mix_Volume can't touch the
         * buffer our effect overwrites. */
        SDL_AtomicSet(&s_voice[slot].gain, (int)(linear * 256.0f));
    }
    else {
        Mix_Volume(slot, (int)(linear * MIX_MAX_VOLUME));
    }
}

void SFX_SetPan(int slot, int dsPan)
{
    if (slot < 0 || slot >= SFX_MAX_SLOTS) {
        return;
    }
    if (s_chunks[slot] == NULL) {
        return;
    }
    if (s_voice[slot].active) {
        return;   /* voices are centered; the 1998 binary never panned SFX */
    }

    /* DirectSound pan: -10000..10000 → SDL_mixer panning: left/right 0..255 */
    float pan = (float)dsPan / 10000.0f;
    if (pan < -1.0f) {
        pan = -1.0f;
    }
    if (pan > 1.0f) {
        pan = 1.0f;
    }

    /* Convert -1..1 to left/right channels (0..255) */
    Uint8 left  = (Uint8)(255.0f * (1.0f - pan) / 2.0f);
    Uint8 right = (Uint8)(255.0f * (1.0f + pan) / 2.0f);
    Mix_SetPanning(slot, left, right);
}

void SFX_SetPosition(int slot, int pos)
{
    (void)pos;
    if (slot < 0 || slot >= SFX_MAX_SLOTS) {
        return;
    }
    if (s_chunks[slot] == NULL) {
        return;
    }
    /* Rewind not directly supported in SDL_mixer for chunks.
     * Re-play from beginning if needed. */
}

/* =====================================================================
 * CloseDirectSound — tear down all chunks.
 * ===================================================================== */
void CloseDirectSound(void)
{
    s_ready = 0;

    for (int i = 0; i < SFX_MAX_SLOTS; i++) {
        if (s_chunks[i] != NULL) {
            Mix_HaltChannel(i);
            Mix_UnregisterAllEffects(i);
            s_voice[i].active = 0;
            s_voice[i].pcm = NULL;   /* about to free the backing abuf */
            Mix_FreeChunk(s_chunks[i]);
            s_chunks[i] = NULL;
        }
        g_soundBuffers[i] = NULL;
        g_soundActive[i] = 0;
    }

    g_lpDirectSound = NULL;
    DebugLog("SFX: closed\n");
}

/* =====================================================================
 * FUN_004d02d4 — 92 bytes
 * Simple one-shot play: rewind + play, no volume/pan.
 * EAX = slot index.
 * ===================================================================== */
static int PlaySoundSimple(int slot)                     /* 0x4d02d4 */
{
    if (slot < 0 || slot >= SFX_MAX_SLOTS) {
        return 0;
    }
    if (g_soundActive[slot] == 0) {
        return 0;
    }
    if (g_optSfxVolume == 0) {
        return 1;                     /* 0x4d02e7: sound disabled */
    }

    SFX_Play(slot, 0, 0);
    return 1;
}

/* =====================================================================
 * FUN_004d0330 — 221 bytes
 * Play sound with volume/pan control (looping).
 * EAX = slot, EDX = distance, EBX = frequency offset.
 * ===================================================================== */
static int PlaySoundWithParams(int slot, int distance, int freqParam)
                                                         /* 0x4d0330 */
{
    if (slot < 0 || slot >= SFX_MAX_SLOTS) {
        return 0;
    }
    if (g_soundActive[slot] == 0) {
        return 0;
    }
    if (g_optSfxVolume == 0) {
        return 1;                     /* 0x4d034d: sound disabled */
    }

    /* Volume attenuation from distance                           0x4d0389 */
    if (distance != 0x100) {
        int d = distance;
        if (d < 0x40) {
            d = 0xFF;                         /* 0x4d0396 */
        }
        else {
            d = 0xFF - d;                               /* 0x4d03a2 */
        }

        int volDiff = g_masterVolume - g_volumeBase;     /* 0x4d03a6: [0x6da29c] - [0x5041a8] */
        if (volDiff < 0) {
            volDiff = -volDiff;             /* 0x4d03b1: cdq;xor;sub = abs */
        }

        int divisor = (g_demoMode == 2) ? 0x12c : 0xff; /* 0x4d03bb */

        int attenVol = (d * volDiff) / divisor + g_volumeBase;  /* 0x4d03d7 */

        SFX_SetVolume(slot, attenVol);
    }

    /* Frequency param -> absolute Hz (binary SetFrequency, 0x4d035a).
     * Same mapping as dc/src/sfx_dc.c; drives the resampler voice. */
    if (freqParam != 0) {
        freqParam = (freqParam * 99900) / 255 + 100;
    }

    SFX_Play(slot, 1, freqParam);  /* looping — binary: Play(buf, 0, 0, 1) 0x4d03ed */
    return 1;
}

static int g_ringAlternate;                              /* 0x00901CBC */

/* =====================================================================
 * PlaySound — 0x00482280 — 111 bytes
 * Sound effect dispatcher. Faithful translation.
 * ===================================================================== */
void PlaySoundEffect(int soundCmd, int distance, int freqParam)
{                                                        /* 0x482280 */
    int slot = soundCmd & 0xFFFF;

    if (soundCmd & 0xFFFF0000) {
        PlaySoundWithParams(slot, distance, freqParam);  /* 0x4822a0 */
    }
    else {
        PlaySoundSimple(slot);                           /* 0x4822a7 */
    }

    if (g_demoMode == DEMO_REPLAY) {
        return;
    }

    /* Ring sound alternation: alternate between 0x1D and 0x1E */
    if ((soundCmd & 0xFFFF) == 0x1D) {
        slot = 0x1D + g_ringAlternate;
        g_ringAlternate = (g_ringAlternate + 1) & 1;
    }

    /* FUN_00496a44 — just ret in the binary */
}

/* =====================================================================
 * LoadSoundEffect — 0x004D064C — LoadWAV(EAX=filename, EDX=slot)
 * Loads an arbitrary WAV file into the given slot at runtime. The binary
 * does the DirectSound buffer create/load here; we delegate to the SDL_mixer
 * slot loader. Callers pass a basename; LoadWAVIntoSlot prepends SOUND/SFX/.
 * ===================================================================== */
void LoadSoundEffect(const char *filename, int slot)
{
    LoadWAVIntoSlot(slot, filename);
}

/* Length of the loaded chunk in ms. Mix_Chunk::alen is the DECODED size in the
 * mixer's output format, so this is correct whatever the source file was. */
int SFX_ClipDurationMs(int slot)
{
    if (slot < 0 || slot >= SFX_MAX_SLOTS || s_chunks[slot] == NULL) {
        return 0;
    }

    int freq = 0;
    Uint16 fmt = 0;
    int channels = 0;
    if (!Mix_QuerySpec(&freq, &fmt, &channels) || freq <= 0 || channels <= 0) {
        return 0;
    }

    int frameBytes = channels * (SDL_AUDIO_BITSIZE(fmt) / 8);
    if (frameBytes <= 0) {
        return 0;
    }
    return (int)(((Uint64)s_chunks[slot]->alen * 1000u) / ((Uint64)frameBytes * (Uint64)freq));
}
