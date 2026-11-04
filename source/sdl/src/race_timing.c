/**
 * race_timing.c — Race timing, lap counting, and physics timer functions
 *
 * UpdateLapCounter @ 0x00481FFC — 644 bytes
 * CheckRaceCompletion @ 0x004CB620 — 788 bytes
 * InitWeather @ 0x004E0448 — 86 bytes
 * UpdateWeatherEffects @ 0x004E04A0 — 104 bytes
 */

#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "sonicr_functions.h"
#include "debug_flags.h"

extern void InitRainParticleSystem(void);  /* 0x4ded7c — rain */
extern void InitSnowParticleSystem(void);  /* 0x4dea50 — snow */
extern void PlaySoundEffect(int soundCmd, int distance, int freqParam);

extern int g_trackEventActive;
extern unsigned char g_randomRingBuffer[];
extern void UpdateRaceSFX(void);  /* FUN_00482674 — in sound.c */
extern unsigned short *g_randomStream;
extern int g_soundActive[];

/* Per-viewport precipitation particle ranges — 0x0094D924, five contiguous
 * ints. Viewport v owns g_precipVpRange[v] .. g_precipVpRange[v+1], so entry
 * [0] is always 0 and entry [4] is always PRECIP_MAX. Four viewports need
 * five boundaries; DrawSnowParticlesD3D (0x4DF4BA) and FUN_004DF168
 * (0x4DF1BD) both read the pair [v] and [v+1] off this one base.
 *
 * This was five separately named scalars, and the [4] terminator ended up
 * write-only — so the four-player pass read one past the end of a local copy
 * and looped on a garbage count. It stays ONE array for that reason. */
int g_precipVpRange[5];          /* 0x0094D924 */

/**
 * UpdatePlayerLapProgress — 0x00481F7C — 128 bytes
 * Advances the current lap's split timer by 2 for each of the 5 players.
 * The lap being timed is player.lapsCompleted (0/1/2 -> lap1/lap2/lap3Time,
 * three contiguous ints); players who have finished all 3 laps are skipped.
 * Only runs when the intro countdown is done and the race hasn't ended.
 */
static void UpdatePlayerLapProgress(void)
{
    if (g_introCountdown != 0 || g_postRaceCameraMode != 0) {
        return;
    }

    for (int i = 0; i < MAX_PLAYERS; i++) {
        Player *p = &g_playerBase[i];
        int lap = p->lapsCompleted;                 /* signed short, 0-3 */
        switch (lap) {                              /* lap >= 3: finished, skip */
            case 0:
                p->lap1Time += 2;
                break;
            case 1:
                p->lap2Time += 2;
                break;
            case 2:
                p->lap3Time += 2;
                break;
        }
    }
}

/**
 * CheckRaceSoundTrigger — FUN_00482B98 — 97 bytes
 * Checks if a DirectSound buffer should play. If active and no Tails
 * player is in flying animation, triggers playback.
 */
static void CheckRaceSoundTrigger(void)           /* FUN_00482b98 */
{
    if (g_soundActive[0x0B] == 0) {
        return; /* buffer not playing */
    }

    Player *pl = g_playerBase;
    for (int i = 0; i < (int)g_numPlayers; i++) {
        if (pl->charId == CHAR_TAILS) {
            int moveState = pl->animId;            /* short at +0x98 */
            if (moveState == 0x0C || moveState == 0x0D) {
                return;                            /* Tails flying — keep sound */
            }
        }
        pl++;
    }

    /* No Tails flying — stop the flight sound */
    SFX_Stop(0x0B); /* 0x482bf0: call SoundStop(0xB) */
}

/* =====================================================================
 * InitSplitScreenLayout — 0x004de9ac — 161 bytes
 * Sets viewport dimensions for split-screen based on player count.
 * Reads g_numHumans (0x6e9908), writes to 0x94d924-0x94d934.
 * Default: 0x80 per quadrant. 2P: halved width. 3P: 0x54/0x2a.
 * 4P: 0x40 width, 0x60 height, 0x20 depth.
 * Also clears the camera interpolation counter (0x94bcfc).
 * No parameters.
 * ===================================================================== */
void InitSplitScreenLayout(void)
{
    /* 0x4DE9BF-0x4DE9D7: [1]..[4] = 0x80, then [0] = 0. Entry [4] is never
     * touched again by any branch below — it is the table terminator. */
    g_precipVpRange[1] = 0x80;
    g_precipVpRange[2] = 0x80;
    g_precipVpRange[3] = 0x80;
    g_precipVpRange[4] = 0x80;
    g_precipVpRange[0] = 0;

    if (g_numHumans == 2) {
        g_precipVpRange[1] = 0x40;
    }
    else if (g_numHumans == 3) {
        g_precipVpRange[2] = 0x54;
        g_precipVpRange[1] = 0x2a;
        g_colorTintEnable = 0;
        return;
    }
    else if (g_numHumans == 4) {
        g_precipVpRange[2] = 0x40;
        g_precipVpRange[3] = 0x60;
        g_precipVpRange[1] = 0x20;
    }

    g_colorTintEnable = 0;
}

#define PRECIP_MAX 128
PrecipParticle g_precipParticles[PRECIP_MAX]; /* 0x0094BD24 — 128 × 56 bytes */

/**
 * UpdateRainParticles — FUN_004dee6c — 764 bytes
 * Per-frame rain particle update. Same structure as UpdateSnowParticles
 * but with steeper fall velocity (-0x1400), ground splash effect (flag 0x80),
 * and rain-specific UV animation.
 */
static void UpdateRainParticles(void)
{
    int vpIdx = 0;
    if (g_netSessionActive != 0) {
        vpIdx = (int)*(unsigned short *)&g_localPlayerIndex;  /* 0x68ACDC — 0x4dee82: mov di, word [0x68acdc] */
    }

    int *vp = g_viewportConfigArray + vpIdx * 50;

    int velScaleX = vp[9]  * 48;                                 /* [vp+0x24] * 0x30 */
    int velScaleY = vp[10] * 48;                                 /* [vp+0x28] * 0x30 */
    int velScaleZ = vp[11] * 48;                                 /* [vp+0x2C] * 0x30 */

    for (int i = 0; i < PRECIP_MAX; i++) {
        PrecipParticle *p = &g_precipParticles[i];
        /* Split-screen viewport boundary checks — 0x4DF006/0x4DF036/0x4DF066
         * test the three interior boundaries [1]..[3] in order. */
        if (g_netSessionActive == 0) {
            if (i == g_precipVpRange[1]) {
                velScaleX = vp[59] * 48;
                velScaleY = vp[60] * 48;
                velScaleZ = vp[61] * 48;
                vp += 50;
            }
            if (i == g_precipVpRange[2]) {
                velScaleX = vp[59] * 48;
                velScaleY = vp[60] * 48;
                velScaleZ = vp[61] * 48;
                vp += 50;
            }
            if (i == g_precipVpRange[3]) {
                velScaleX = vp[59] * 48;
                velScaleY = vp[60] * 48;
                velScaleZ = vp[61] * 48;
                vp += 50;
            }
        }

        int flagsHi = p->age;
        int stateHi = p->state;

        /* Three spawn triggers from binary (0x4DF09F, 0x4DF0AD, 0x4DF114):
         * 1. flagsHi == 0x80: fresh ground hit
         * 2. stateHi == 0: inactive particle
         * 3. flagsHi == 0x88: splash animation complete (8 frames after ground hit) */
        int needSpawn;
        if (flagsHi == 0x80 || flagsHi == 0x88) {
            needSpawn = 1;
        }
        else if (stateHi != 0) {
            needSpawn = 0;
        }
        else {
            needSpawn = 1;
        }

        if (needSpawn) {
            /* Spawn new rain particle */
            int cx = vp[0] >> 4;
            p->posX = velScaleX + Random() * 8 - 0x1FFFA + cx;

            int cy = vp[1] >> 4;
            p->posY = velScaleY + Random() * 8 - 0x1FFFA + cy;

            int cz = vp[2] >> 4;
            p->posZ = velScaleZ + Random() * 8 - 0x1FFFA + cz;

            p->velY = -0x1400;                                   /* steeper than snow */
            p->age = 0;
            p->flags = (short)0x8020;
            p->sizeW = 6;
            p->sizeH = 6;

            p->uvBaseX = 0x80;
            p->uvBaseY = 0xB0;
            p->frameW = 0x40;
            p->frameH = 0x40;
        }
        else {
            /* Update existing rain particle */
            int posY = p->posY + p->velY;
            p->posY = posY;

            if (posY < 0) {
                /* Hit ground — mark as splash */
                p->age = 0x80;
                p->posY = 0;
            }

            /* Binary re-reads flagsHi here (0x4df0cf) after ground-hit write */
            flagsHi = p->age;

            /* Check for splash state */
            if (flagsHi == 0x80) {
                /* Splash: freeze velocity, change to splash sprite */
                p->velY = 0;
                p->velZ = 0;
                p->sizeW = 0x0A;                                /* splash width */
                p->sizeH = 8;                                   /* splash height */
                p->frameW = 0x10;                                /* splash UV W */
                p->frameH = 0x0C;                                /* splash UV H */
                p->velX = 0;
            }

            /* Check for respawn-after-splash */
            if (flagsHi == 0x88) {
                /* Will respawn next frame (needSpawn catches 0x80 above,
                 * 0x88 catches here after splash animation completes) */
            }

            /* UV animation for active rain (flags > 0x7F) */
            if (flagsHi > 0x7F) {
                unsigned char ageVal = (unsigned char)p->age;
                unsigned char uvX = ((ageVal - 0x80) & 3) * 16 + 0x80;
                p->uvBaseX = uvX;

                /* UV Y depends on viewport count */
                int ageScaled = (flagsHi - 0x80);
                int s = ageScaled >> 31;
                int div4 = (int)(((ageScaled + s * -4) - (unsigned)((s << 1) < 0)) >> 2);
                p->uvBaseY = (unsigned char)(div4 * 12 + 0x60);
            }
        }

        /* Increment age */
        p->age += 1;
    }
}

/**
 * UpdateSnowParticles — FUN_004deb38 — 578 bytes
 *
 * Updates 128 snow particles. Each particle drifts toward a target position,
 * decelerating horizontally and falling vertically. Dead particles are
 * respawned at random positions around the current viewport's camera.
 *
 * Particle struct (56 = 0x38 bytes) at g_precipParticles[i]:
 *   +0x00: posX      +0x04: posY      +0x08: posZ
 *   +0x0C: targetX   +0x14: targetZ
 *   +0x18: velX      +0x1C: velY      +0x20: velZ
 *   +0x24: flags (high word)   +0x26: age (short, ++)
 *   +0x32: state (high word: 0=active)
 */
static void UpdateSnowParticles(void)
{
    /* Determine viewport index */
    int vpIdx = 0;
    if (g_netSessionActive != 0) {
        vpIdx = (int)*(unsigned short *)&g_localPlayerIndex;  /* 0x68ACDC */
    }

    /* Compute viewport struct pointer (stride = 0xC8 / 4 = 50 ints) */
    /* VALIDATED: (idx*4-idx)*8+idx = idx*25; *8 = idx*200 = idx*0xC8... wait */
    /* Binary: (idx*4-idx)<<3 + idx = idx*3*8 + idx = idx*25; <<3 = idx*200 */
    int *vp = g_viewportConfigArray + vpIdx * 50;             /* 0x6E9924 + vpIdx*0xC8 */

    /* Read initial velocity scale from viewport +0x24, +0x28, +0x2C */
    int velScaleX = vp[9] * 48;                              /* [vp+0x24] * 0x30 = VALIDATED: (v*4-v)<<4 = v*48 */
    int velScaleY = vp[10] * 48;                             /* [vp+0x28] * 0x30 */
    int velScaleZ = vp[11] * 48;                             /* [vp+0x2C] * 0x30 */

    for (int i = 0; i < PRECIP_MAX; i++) {
        PrecipParticle *p = &g_precipParticles[i];
        /* Check for viewport split boundaries — reload velocity params.
         * 0x4DEC09/0x4DEC34/0x4DEC5F: the three interior boundaries [1]..[3]. */
        if (g_netSessionActive == 0) {
            if (i == g_precipVpRange[1]) {
                velScaleX = vp[59] * 48;                     /* [vp+0xEC] * 0x30 */
                velScaleY = vp[60] * 48;                     /* [vp+0xF0] * 0x30 */
                velScaleZ = vp[61] * 48;                     /* [vp+0xF4] * 0x30 */
                vp += 50;                                     /* advance to next viewport (+0xC8) */
            }
            if (i == g_precipVpRange[2]) {
                velScaleX = vp[59] * 48;
                velScaleY = vp[60] * 48;
                velScaleZ = vp[61] * 48;
                vp += 50;
            }
            if (i == g_precipVpRange[3]) {
                velScaleX = vp[59] * 48;
                velScaleY = vp[60] * 48;
                velScaleZ = vp[61] * 48;
                vp += 50;
            }
        }

        /* Check if particle needs respawning.
         * VALIDATED: binary checks sequentially — posY<0 or age==0x80 → spawn;
         * state!=0 → skip spawn (just steer); else → spawn */
        int needSpawn;
        if (p->posY < 0 || p->age == 0x80) {
            needSpawn = 1;
        }
        else if (p->state != 0) {
            needSpawn = 0;                                   /* active particle, just steer */
        }
        else {
            needSpawn = 1;                                   /* default: respawn */
        }

        if (needSpawn) {
            /* Spawn new particle */
            p->velY = Random() / 16 - 0xC00;

            int cx = vp[0] >> 4;                             /* viewport posX / 16 */
            p->targetX = velScaleX + Random() * 8 - 0x1FFFA + cx;

            int cy = vp[1] >> 4;                             /* viewport posY / 16 */
            p->posY = velScaleY + Random() * 8 - 0x1FFFA + cy;

            int cz = vp[2] >> 4;                             /* viewport posZ / 16 */
            p->targetZ = velScaleZ + Random() * 8 - 0x1FFFA + cz;

            p->posX = p->targetX + Random() / 4 - 0x1000;
            p->posZ = p->targetZ + Random() / 4 - 0x1000;
            p->age = 0;
        }

        /* Steer toward target */
        if (p->targetX > p->posX) {
            p->velX += 0x100;
        }
        else {
            p->velX -= 0x100;
        }
        if (p->targetZ > p->posZ) {
            p->velZ += 0x100;
        }
        else {
            p->velZ -= 0x100;
        }

        /* Integrate velocity → position */
        p->posX += p->velX;
        p->posY += p->velY;
        p->posZ += p->velZ;

        /* Increment age */
        p->age += 1;
    }
}

/* Camera interp state — UNION with color tint / grid tint (different game states).
 * Binary 0x94BCFC-0x94BD14 is shared: camera interp during transitions,
 * color/grid tint during gameplay. Kept SEPARATE to prevent cross-contamination. */
static int g_camInterpTgtX;        /* 0x0094BD18 */
static int g_camInterpTgtY;        /* 0x0094BD1C */
static int g_camInterpTgtZ;        /* 0x0094BD20 */

/* =====================================================================
 * LightningStrike — FUN_004de830 — 253 bytes
 *
 * Rain lightning-strobe init (only caller is UpdateWeatherEffects @ 0x4E04EF,
 * on the WEATHER_RAIN + Random() < 0x200 branch). Plays the thunder SFX
 * (ID 0x2D), generates 3 random tint channel values (Random()/256 + 0x40),
 * stores as g_gridTintR/G/B and computes target = value/5,
 * scaled = value*8192+0x1000. Sets g_colorTintEnable to 5.
 * Also initializes per-player state flag at +0x204 to 0x30005 for
 * ALL players (indices 0..numPlayers-1) — binary writes player 0 too.
 *
 * No parameters (Watcom fastcall — no EAX input used).
 * ===================================================================== */
void LightningStrike(void)
{
#ifdef SONICR_XBOX
    {
        extern void xb_note(const char *what);
        xb_note("lightning");
    }
#endif
    /* Play thunder SFX for the lightning strobe */
    PlaySoundEffect(0x2D, 0, 0);                                   /* EAX=0x2D, ECX=0, EBX=0, EDX=0 */

    /* Generate random camera offsets: Random()/256 + 64 */
    int rx = Random() / 256 + 0x40;                        /* SDIV 256 + 0x40 */
    g_gridTintR = rx;
    g_camInterpTgtX = rx / 5;

    int ry = Random() / 256 + 0x40;
    g_gridTintG = ry;
    g_camInterpTgtY = ry / 5;

    int rz = Random() / 256 + 0x40;
    g_gridTintB = rz;
    g_camInterpTgtZ = rz / 5;

    /* Compute scaled versions: val * 8192 + 0x1000 */
    g_colorTintR = (g_gridTintR << 13) + 0x1000;
    g_colorTintG = (g_gridTintG << 13) + 0x1000;
    g_colorTintB = (g_gridTintB << 13) + 0x1000;

    /* Set interpolation counter (= g_colorTintEnable at 0x94BCFC) */
    g_colorTintEnable = 5;                              /* ebx = 5 at line 59 */

    /* Initialize per-player state flag at player[i].renderState.
     * Binary: base = &player[0].renderState - 0x71C, eax adds 0x71C before
     * each write, loops while eax <= numPlayers*0x71C — so the first write
     * lands on player[0].renderState and it writes players 0..numPlayers-1. */
    if (g_numPlayers > 0) {
        for (int i = 0; i < g_numPlayers; i++) {
            g_playerBase[i].renderState = 0x30005;
        }
    }
}

/* =====================================================================
 * StepColorTintEffect — 0x004de930 — 123 bytes
 * Computes camera position deltas (current - target), scales by <<13 + 0x1000,
 * stores results, decrements interpolation counter.
 * Operates on BSS block at 0x94bcfc-0x94bd20.
 * No parameters.
 * ===================================================================== */
void StepColorTintEffect(void)
{
    int dx = g_gridTintR - g_camInterpTgtX;
    int dy = g_gridTintG - g_camInterpTgtY;
    int dz = g_gridTintB - g_camInterpTgtZ;

    g_colorTintR = (dx << 13) + 0x1000;
    g_colorTintG = (dy << 13) + 0x1000;
    g_colorTintB = (dz << 13) + 0x1000;

    g_gridTintR = dx;
    g_gridTintG = dy;
    g_gridTintB = dz;

    g_colorTintEnable--;
}

/* DAT_ globals — resolved to canonical names from globals_extra.c */
int g_itemEffectAnimPhase;               /* 0x00901C68 — non-static for render_char_sprites.c */
#define g_sfxCycleCounter    g_raceOrder[3]   /* 0x90207C — cycles 0-2 */
#define g_stuckPlayerIdx     g_raceOrder[5]   /* 0x902084 — cycles 0-4, player index */
int g_sfxPhaseCounter;   /* 0x00902094 — cycles 0-31, used for SFX gating */
static int _DAT_0068975c;         /* unique to race_timing.c — no conflict */

/**
 * UpdateLapCounter — 0x00481FFC — 644 bytes
 * Per-frame: increments animation counters, random ring index,
 * calls per-player lap checking, decrements intro countdown.
 *
 * in_EAX = nonzero if game is active (we always pass 1).
 */
void UpdateLapCounter(int isGameActive)
{
    /* 0x481FFC — cycle g_raceOrder[1..9] counters.
     * Binary accesses these as a single int array at 0x902070.
     * Some entries were previously separate globals (g_trackEventActive,
     * g_animFrameCounter, etc.) that did NOT alias g_raceOrder[] in C,
     * so g_raceOrder[2] was never cycled — breaking ComputeRacePositions
     * for player counts that index into [2]. */
    g_raceOrder[1] = 0;                                /* 0x482011 */
    g_raceOrder[2] = (g_raceOrder[2] + 1) & 1;         /* 0x482028: cycles 0-1 */
    g_raceOrder[3] = g_raceOrder[3] + 1;                /* 0x482022 */
    if (g_raceOrder[3] > 2) {
        g_raceOrder[3] = 0;        /* 0x48202E: cycles 0-2 */
    }
    g_raceOrder[4] = (g_raceOrder[4] + 1) & 3;         /* 0x482058: cycles 0-3 */
    g_raceOrder[5] = g_raceOrder[5] + 1;                /* 0x482052 */
    if (g_raceOrder[5] > 4) {
        g_raceOrder[5] = 0;        /* 0x48205E: cycles 0-4 */
    }
    g_raceOrder[6] = g_raceOrder[6] + 1;                /* 0x482072 */
    if (g_raceOrder[6] > 5) {
        g_raceOrder[6] = 0;        /* 0x482078: cycles 0-5 */
    }
    g_raceOrder[7] = (g_raceOrder[7] + 1) & 7;         /* 0x4820B8: cycles 0-7 */
    g_raceOrder[8] = (g_raceOrder[8] + 1) & 0xF;       /* 0x4820BE: cycles 0-15 */
    g_raceOrder[9] = (g_raceOrder[9] + 1) & 0x1F;      /* 0x4820D0: cycles 0-31 */

    /* Keep legacy aliases in sync for code that reads them */
    g_trackEventActive = g_raceOrder[2];
    g_animFrameCounter = g_raceOrder[7];
    g_collectAnimTimer = g_raceOrder[8];
    g_sfxPhaseCounter  = g_raceOrder[9];

    g_randomRingIdx = g_randomRingIdx + 1;
    if (g_randomRingIdx > 0xFF) {
        g_randomRingIdx = 0;
    }

    if (isGameActive != 0) {
        UpdatePlayerLapProgress();
        if (g_trackId < 6) {
            UpdateRaceSFX();
            CheckRaceSoundTrigger();
        }
        if (g_sfxCooldownTimer > 0) {
            g_sfxCooldownTimer--;
        }

        /* Intro countdown management */
        if (g_introCountdown == 4) {
            g_introCountdown = 3;
            _DAT_0068975c = g_totalFrames2;
        }
        else if (g_introCountdown == 3 && g_netReadyFlag == 0) {
            if (g_totalFrames2 - _DAT_0068975c > 600) {
                g_netReadyFlag = 1;
            }
            if (g_netReadyFlag != 0) {
                g_introCountdown = 2;
            }
        }
        else if (g_introCountdown < 1) {
            if (g_introTimer != 0) {
                g_introTimer--;
            }
        }
        else {
            if (g_introCountdown == 1) {
                g_introTimer = 0x3C;
            }
            g_introCountdown--;
        }
    }

    g_ringAnimFrame = (g_ringAnimFrame + 1) & 0xF;
    g_emeraldAnimFrame1 = (g_emeraldAnimFrame1 + 1) & 7;
    g_itemEffectAnimPhase = (g_itemEffectAnimPhase + 1) & 7;
    if (g_weatherType != WEATHER_SNOW) {
        /* _DAT_008f7074 = (_DAT_008f7074 + 1) & 0xF; */
    }
    g_totalFrames++;
}

/**
 * CheckRaceCompletion — 0x004CB620 — 788 bytes
 * Per-frame: checks race finish conditions, manages post-race camera orbit,
 * resets state at intro countdown == 0x3C.
 */
void CheckRaceCompletion(void)
{
    int fadeState = g_fadeState;   /* ECX — accumulator */
    int fadeSpeed = g_fadeSpeed;   /* ESI — accumulator */

    /* Early exit: 0x4CB631-0x4CB641
     * If flag == 1 and no post-race camera active, nothing to do */
    if (g_netWaitFlag == 1 && g_postRaceCameraMode == 0) {
        goto epilogue;
    }

    /* player tagged everyone? (0x4CB647) */
    if (g_raceSubMode == SUBMODE_TAG && g_p1CollectionCount >= 4) {
        g_playerBase[0].lapsCompleted = 3;  /* player0 finish flag */
    }

    /* player got 5 balloons?
     * For each player: if field 0x1F4 == 5 (race type complete), set 0x5E = 3 */
    if (g_raceSubMode == SUBMODE_BALLOON && g_numViewports > 0) {
        for (int pi = 0; pi < g_numViewports; pi++) {
            Player *p = &g_playerBase[pi];
            if (p->collisionCount == 5) {
                p->lapsCompleted = 3;
            }
        }
    }

    /* Race finish detection (0x4CB6A8) */
    if (g_raceFinished != 0) {
        goto orbit_section;
    }

    /* Store current fade values before checks (binary: 0x4CB6BB) */
    g_fadeState = fadeState;
    g_fadeSpeed = fadeSpeed;

    if (g_postRaceCameraMode != 0) {
        goto save_replay_check;
    }

    /* Player lap completion loop (0x4CB6CF)
     * Check each viewport player for lap field (byte 0x5E) == 3 */
    if (g_numViewports > 0) {
#if DEBUG_GP_FAST_FIRST
        int finishLap = (g_raceType == RACE_GP) ? 1 : 3;
#else
        int finishLap = 3;
#endif
        for (int pi = 0; pi < g_numViewports; pi++) {
            Player *p = &g_playerBase[pi];
            int lapField = *(short *)((char *)p + 0x5E);
            if (lapField == finishLap) {
                g_raceFinished = 1;    /* 0x4CB702: mov [0x901c88], 1 */
                fadeSpeed = 0x10;
                fadeState = FADE_OUT;  /* ECX = 2 */
#if DEBUG_GP_FAST_FIRST
                if (g_raceType == RACE_GP && pi == 0) {
                    g_playerBase[0].racePosition = 1;
                }
#endif
#if DEBUG_FORCE_EMERALD_PICKUP
                g_raceCheckpoint |= 1;
#endif
#if DEBUG_FORCE_5_COINS
                if (g_raceType == RACE_GP && pi == 0)
                    g_p1CollectionCount = 5;
#endif
            }
        }
    }

    /* Special race (raceType 4) player1 check (0x4CB716) */
    if (g_raceType == 4) {
        int p1Lap = (int)g_playerBase[1].lapsCompleted;
        if (p1Lap == 3) {
            g_raceFinished = 1;
            fadeState = FADE_OUT;
            fadeSpeed = 0x10;
        }
    }

    /* Time limit check (0x4CB740)
     * Sum of 3 lap times (24-bit each) vs threshold */
    {
        Player *p0 = &g_playerBase[0];
        int lap1 = p0->lap1Time & 0xFFFFFF;
        int lap2 = p0->lap2Time & 0xFFFFFF;
        int lap3 = p0->lap3Time & 0xFFFFFF;
        int totalTime = lap1 + lap2 + lap3;

        /* Threshold = g_raceSpeedMult * 7080 (0x1BA8)
         * Binary: mult*16-mult=15; *4=60; -mult=59; *8=472; *16=7552; -472=7080 */
        int threshold = g_raceSpeedMult * 7080;
        if (totalTime > threshold) {
            g_raceFinished = 2;
            fadeSpeed = 0x10;
            fadeState = FADE_OUT;
        }
    }

    /* Lap progress counter check (0x4CB798) */
    if (g_finishOrderCounter == 4) {
        g_raceFinished = 2;   /* binary: mov [0x901c88], ecx where ecx=2 */
        fadeState = FADE_OUT;
        fadeSpeed = 0x10;
    }

    /* Abort check — any player unfinished OR escape pressed (0x4CB7B1) */
    if (g_playerFinishFlag[0] == -1 || g_playerFinishFlag[1] == -1 ||
        g_playerFinishFlag[2] == -1 || g_playerFinishFlag[3] == -1 ||
        (g_demoMode != DEMO_NONE && (g_inputBits & 8) != 0))
    {
        g_raceFinished = 3;
        fadeState = FADE_OUT;
        fadeSpeed = 0x10;
    }

    /* Write fade values + save replay check (0x4CB7FB) */
    g_fadeState = fadeState;
    g_fadeSpeed = fadeSpeed;

save_replay_check:
    if (g_demoMode == DEMO_NONE && g_raceFinished != 0 &&
        g_netSessionActive == 0 && g_isNetworkGame == 0 &&
        g_raceType < RACE_TIMEATTACK && g_raceSubMode == SUBMODE_NORMAL)
    {
        SaveReplayLog();
    }

    /* Post-race camera timer (0x4CB843)
     * Reload fade values from globals (binary reloads ECX/ESI at 0x4CB84E/0x4CB857) */
orbit_section:
    fadeSpeed = g_fadeSpeed;
    fadeState = g_fadeState;
    if (g_raceSpeedMult * 8 == g_postRaceCameraMode) {
        g_raceFinished = 4;
        fadeState = FADE_OUT;
        fadeSpeed = 0x10;
    }

    /* Orbit angle update (0x4CB875) */
    if (g_postRaceCameraMode != 0) {
        int step = 0x4000 / (g_raceSpeedMult * 35);
        g_orbitAngle = (g_orbitAngle + step) & 0xFFF;
        g_postRaceCameraMode++;
    }

    /* Intro countdown reset (0x4CB8C5) */
    if (g_introCountdown == 0x3C) {
        g_randomRingIdx = 0;
        /* Binary 0x4CB8E0-0x4CB910 zeroes [0x902074]..[0x902094] = g_raceOrder[1..9].
         * g_trackEventActive/g_animFrameCounter/g_collectAnimTimer/g_sfxPhaseCounter
         * are separate C variables synced FROM g_raceOrder[2]/[7]/[8]/[9] at the top
         * of this function, so both must be zeroed here or the reset is lost. */
        for (int i = 1; i <= 9; i++) {
            g_raceOrder[i] = 0;
        }
        g_trackEventActive = 0;
        g_animFrameCounter = 0;
        g_collectAnimTimer = 0;
        g_sfxPhaseCounter = 0;
        g_totalFrames = 0x96;
        g_ringSpawnReadPtr = (unsigned short *)g_randomRingBuffer;  /* 0x4CB91C: [0x901CD0] = 0x92498C */
    }

epilogue:
    g_fadeSpeed = fadeSpeed;
    g_fadeState = fadeState;
}

/**
 * InitWeather — 0x004E0448 — 86 bytes
 * Initializes weather particle system for the track.
 */
void InitWeather(void)
{
    DebugLog("InitWeather\n");
    InitSplitScreenLayout();
    if (g_trackId != TRACK_RADIANT_EMERALD && (g_raceType != RACE_TIMEATTACK || g_raceSubMode > SUBMODE_REVERSE)) {
        if (g_weatherType == WEATHER_RAIN) {
            InitRainParticleSystem();                           /* 0x4ded7c — rain */
            return;
        }
        if (g_weatherType == WEATHER_SNOW) {
            InitSnowParticleSystem();                           /* 0x4dea50 — snow */
        }
    }
}

/**
 * UpdateWeatherEffects — 0x004E04A0 — 104 bytes
 * Per-frame weather particle update.
 */
void UpdateWeatherEffects(void)
{
    if (g_trackId != TRACK_RADIANT_EMERALD && (g_raceType != RACE_TIMEATTACK || g_raceSubMode > SUBMODE_REVERSE)) {
        if (g_weatherType == WEATHER_RAIN) {
            UpdateRainParticles();
        }
        else if (g_weatherType == WEATHER_SNOW) {
            UpdateSnowParticles();
        }
        if (g_weatherType == WEATHER_RAIN) {
            int r = Random();
            if (r < 0x200) {
                LightningStrike();
                return;
            }
            if (g_colorTintEnable != 0) {
                StepColorTintEffect();
            }
        }
    }
}
