/**
 * music_sdl.c — MP3/WAV music playback via SDL_mixer
 *
 * Replaces music_SDL.m (AVFoundation) with pure C SDL_mixer calls.
 * WAV files live in MUSIC/ directory, named track2.wav through track21.wav
 * matching the original CD track numbering.
 */

#include <SDL.h>
#include <SDL_mixer.h>
#include <stdio.h>
#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "sonicr_functions.h"
#include "sonicr_paths.h"
#include "path_ci.h"
#include "music_rwops.h"

static Mix_Music *s_musicTrack = NULL;
static int s_currentTrack = 0;
static int s_musicReady = 0;
static int s_currentTrackIsFanfare = 0;  /* don't auto-replay finished fanfares */

/* Per-track durations in ms — ROM table at 0x5041AC, indexed by CD track.
 * GetLogicalCDTrack times against these rather than asking whether audio is
 * still playing, so a ripped track whose padding differs from the redbook
 * original doesn't shift medley timing. */
static const int s_trackDurationMs[23] = {
         0,      0,  43210,   6993,  10170,  55580, 305305, 283904,
    270474, 237737, 295113, 241929, 240949, 164269, 208959, 210448,
    202700, 210360, 238142,   3921,   6451,   5910, 5448657
};

static int   s_logicalTrack = 0;   /* 0x6DA2A4 */
static DWORD s_trackStartMs = 0;   /* 0x6DA2A8 */

/* Music level, full scale to match dc/src/music_dc.c now that both platforms
 * put the SFX slider on the same even 1/8 steps. DUCKED is what the replay
 * commentary drops it to (see SFX_DuckMusic in sound.c). PlayCD reads
 * s_musicDucked so a track starting mid-duck comes in at the ducked level
 * instead of overriding it. */
#define MUSIC_VOL_NORMAL MIX_MAX_VOLUME
#define MUSIC_VOL_DUCKED (MUSIC_VOL_NORMAL / 2)
static int s_musicDucked = 0;

/* Music Volume slider level, 0-8 (mirrors g_optMusicVolume). Held locally so
 * the mixer level can be recomputed without either caller below knowing the
 * other's state. */
static int s_musicLevel = 8;

/* Slider and duck compose — the duck halves whatever the slider is set to. */
static int Music_EffectiveVolume(void)
{
    int base = s_musicDucked ? MUSIC_VOL_DUCKED : MUSIC_VOL_NORMAL;
    return base * s_musicLevel / 8;
}

void Music_SetVolume(int level)
{
    if (level < 0) {
        level = 0;
    }
    if (level > 8) {
        level = 8;
    }
    s_musicLevel = level;
    Mix_VolumeMusic(Music_EffectiveVolume());
}

void Music_SetDucked(int ducked)
{
    s_musicDucked = ducked ? 1 : 0;
    Mix_VolumeMusic(Music_EffectiveVolume());
}

void StopCD(void);

/**
 * OpenCDDevice — replaces MCI cdaudio open.
 * Just marks the music system as ready.
 */
int OpenCDDevice(void)
{
    DebugLog("OpenCDDevice (SDL_mixer mode)\n");
    s_musicReady = 1;
    g_mciDeviceId = 1;  /* non-zero = device ready */
    return 1;
}

/**
 * CloseCDDevice — replaces MCI close.
 */
void CloseCDDevice(void)
{
    if (s_musicTrack != NULL) {
        Mix_HaltMusic();
        Mix_FreeMusic(s_musicTrack);
        s_musicTrack = NULL;
    }
    s_currentTrack = 0;
    s_musicReady = 0;
    g_mciDeviceId = 0;
}

/**
 * load_track — find and load the music file for a CD track number.
 *
 * Probes MUSIC/track<N>.<ext> in preference order, first existing file wins:
 *   .son  raw headerless PCM (44100/16/stereo) — streamed via a music_rwops shim
 *   .adx  CRI ADX ADPCM                         — decoded on demand via the shim
 *   .ogg/.mp3/.flac/.wav                        — handed straight to SDL_mixer
 * The .son/.adx shims stream through SDL_mixer's WAV backend, so no whole-track
 * decode is held in RAM. freesrc=1 hands ownership of the RWops to SDL_mixer.
 */
static Mix_Music *load_track(int trackNum)
{
    char path[512];
    SDL_RWops *rw;

    snprintf(path, sizeof(path), DATA_DIR "/MUSIC/track%d.son", trackNum);
    rw = MusicRW_OpenSon(path, 44100, 2, 16);
    if (rw) {
        return Mix_LoadMUS_RW(rw, 1);
    }

    snprintf(path, sizeof(path), DATA_DIR "/MUSIC/track%d.adx", trackNum);
    rw = MusicRW_OpenAdx(path);
    if (rw) {
        return Mix_LoadMUS_RW(rw, 1);
    }

    snprintf(path, sizeof(path), DATA_DIR "/MUSIC/track%d.adp", trackNum);
    rw = MusicRW_OpenAdp(path, 44100, 2);
    if (rw) {
        return Mix_LoadMUS_RW(rw, 1);
    }

    static const char *const exts[] = { "ogg", "mp3", "flac", "wav" };
    for (int i = 0; i < (int)(sizeof(exts) / sizeof(exts[0])); i++) {
        snprintf(path, sizeof(path), DATA_DIR "/MUSIC/track%d.%s", trackNum, exts[i]);
        /* Case-sensitive filesystems: resolve the real on-disk casing before
         * handing the path to SDL_mixer (which re-opens it internally). */
        char *real = sr_resolve_case(path);
        if (real) {
            Mix_Music *mm = Mix_LoadMUS(real);
            free(real);
            if (mm) return mm;
        }
    }
    return NULL;
}

/**
 * PlayCD — replaces MCI play.
 * trackNum = CD track number from binary (2-21).
 * WAV files ripped from CD use matching track numbers: MUSIC/track2.wav, etc.
 */
void PlayCD(int trackNum)
{
    if (trackNum < 2 || trackNum > 21) {
        return;
    }
    if (g_musicEnabled == 0) {
        StopCD();
        return;
    }

    /* Already playing this track — don't restart */
    if (s_currentTrack == trackNum && s_musicTrack != NULL) {
        if (Mix_PlayingMusic()) {
            /* Re-baseline the medley clock. A looping track outlives its table
             * entry; the original re-asserted the redbook track at that point,
             * we let the mixer keep looping seamlessly and just restart the
             * timer so the logical position stays on this track. */
            s_logicalTrack = trackNum;
            s_trackStartMs = timeGetTime();
            return;             /* still in flight */
        }
        if (s_currentTrackIsFanfare) {
            return;        /* one-shot finished — don't replay */
        }
    }

    //DebugLog("PlayCD(%i) track%d.wav\n", trackNum, trackNum);

    /* Stop current music */
    if (s_musicTrack != NULL) {
        Mix_HaltMusic();
        Mix_FreeMusic(s_musicTrack);
        s_musicTrack = NULL;
    }
    s_currentTrack = 0;

    /* Find and load the track: .son/.adx stream via RWops shims, native
     * formats go straight to SDL_mixer. First existing MUSIC/track<N>.* wins. */
    s_musicTrack = load_track(trackNum);
    if (s_musicTrack == NULL) {
        DebugLog("PlayCD(%i) — no loadable MUSIC/track%d.* found: %s\n",
                 trackNum, trackNum, Mix_GetError());
        return;
    }

    int isFanfare = (trackNum == 2 || trackNum == 3 || trackNum == 4 ||
                     trackNum == 0x13 || trackNum == 0x14 || trackNum == 0x15);
    int loops = isFanfare ? 0 : -1;  /* 0 = play once, -1 = loop forever */

    Mix_VolumeMusic(Music_EffectiveVolume());
    Mix_PlayMusic(s_musicTrack, loops);
    s_currentTrack = trackNum;
    s_currentTrackIsFanfare = isFanfare;
    s_logicalTrack = trackNum;              /* 0x4D01AC sets [0x6DA2A4] */
    s_trackStartMs = timeGetTime();         /* and [0x6DA2A8] */
}

/**
 * StopCD - 0x004D0264 — 42 bytes
 * Stops CD audio playback (MCI_STOP = 0x808).
 * Called between races and on shutdown.
 */
void StopCD(void)
{
    if (s_musicTrack != NULL) {
        Mix_HaltMusic();
        Mix_FreeMusic(s_musicTrack);
        s_musicTrack = NULL;
    }
    s_currentTrack = 0;
    s_currentTrackIsFanfare = 0;
    s_logicalTrack = 0;
    s_trackStartMs = 0;
    /* Every path that abandons a replay stops the music, so clearing the duck
     * here covers the exits the race loop's per-frame tick can't reach. */
    SFX_DuckStop();
}

/**
 * GetLogicalCDTrack — 0x004D0100
 * The binary computes "which track of the medley is playing now" from elapsed
 * time since PlayCD against a per-track duration table (0x5041ac). Redbook CD
 * had no reliable "is this track still playing" query, so it pre-baked track
 * durations to know when a non-looping track ended and the medley advanced.
 * We stream per track and have Mix_PlayingMusic() — the exact signal that table
 * was faking. So return the current track while it plays, or the next logical
 * track (current+1) once a non-looping track has finished. Callers use
 * `if (GetLogicalCDTrack() != N) PlayCD(N)` to keep track N asserted.
 */
int GetLogicalCDTrack(void)
{
    if (!s_musicReady || s_logicalTrack == 0) {
        return 0;
    }

    int elapsed = (int)(timeGetTime() - s_trackStartMs);   /* 0x4D0109 */
    if (elapsed < 0) {
        elapsed = -elapsed;                                /* 0x4D010F: cdq/xor/sub */
    }

    if (s_logicalTrack > 0 && s_logicalTrack < 23 &&
        elapsed > s_trackDurationMs[s_logicalTrack]) {
        s_logicalTrack++;                                  /* 0x4D0123 */
    }

    return s_logicalTrack;                                 /* 0x4D012C */
}

/**
 * UpdateCDPlayback — 0x004D01AC
 * In the original, this IS PlayCD — same function, same address.
 */
void UpdateCDPlayback(int trackNum)
{
    /* Binary 0x4d01ba: mov ecx,[0x8fd4a0]; test ecx,ecx; je (return). When
     * music is disabled (Music Volume = off), UpdateCDPlayback plays nothing —
     * this gate keeps music off across screen/track transitions. Dropped in
     * translation, so "Music Volume off" had no effect. */
    if (g_musicEnabled == 0) {
        return;
    }

    PlayCD(trackNum);
}
