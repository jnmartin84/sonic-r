/**
 * camera_per_track.c — Per-track camera follow and shake functions
 *
 * Each track has animated scenery (water, doors, gears) and dynamic
 * effects (object bobbing, light cycling). These are dispatched from
 * UpdateTrackWorld and AnimateTrackObjects in init.c.
 *
 * Per-track world updates and object animation.
 */

#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "sonicr_functions.h"
#include "player_struct.h"

/* FORWARD DECL AND EXTERN FUNCTIONS */

int IsOnTrackSurface(float x, float z);                                /* 0x0047B794 */
static void UpdateSlotMachineReels(void);                              /* 0x0047BF90 — forward decl */
void TickAnimation(int animIndex);                                     /* 0x0047DE0C */
void ApplyAnimationOffsets(void);                                      /* 0x0047DE8C */
void AnimateTrackGeometry(int groupIndex, int *statePtr);              /* 0x0047EF88 */
void CheckCollectiblesAllPlayers(int *table);                          /* 0x0047F76C */

/* Forward-declare the rendering functions */
void RenderBalloonModelForRace(int posX, int posY, int posZ, int pitchAngle,
                         int yawAngle, int rollAngle, int lightX, int lightY, int lightZ);


/* EXTERN VARS */

/* g_animStateC8/D0/300/304/330/338/33C and g_factoryDoorState sit inside the
 * 0x92528C state block — aliased in sonicr_globals.h, so no extern here. */
/* g_animState48C..4D8 (the two sign-physics blocks) are aliases into the
 * 0x92528C state block too — see sonicr_globals.h. */

extern int g_raceOrder[];   /* 0x902070 — [3]=doorTrigger, [4]=debrisGate, [5]=debrisPlayerIdx */

extern int g_p2CollectionCount;

extern void *g_triggeredObjectPtr;

extern int *g_collectVertexPtrs[];

/* Track geometry animation control structs — shared by RegisterAnimatedGeometry and
 * AnimateTrackGeometry.  Binary base 0x680FF0, stride 20 bytes (5 ints).
 * Current C definition at 0x681000 (track_anim_init.c) is offset-matched:
 * AnimateTrackGeometry group N reads g_geometryAnimCtrl[N*5 .. N*5+3]. */
extern int g_geometryAnimCtrl[];      /* 0x00680FF0 (binary) — canonical in track_anim_init.c */

extern int g_factoryObjAngle[];   /* 0x00681644 — per-object heading angle */


/* STATIC VARS */

/* 0x4FF558: UV delta pairs for Factory silo-door animation — 4 phases × {deltaA, deltaB} */
static const int s_factoryDoorUVTable[8] = {
    0x00000000, 0x00800000,   /* phase 0 */
    0x00000000, 0x00C00000,   /* phase 1 */
    0x00400000, 0x00800000,   /* phase 2 */
    0x00400000, 0x00C00000,   /* phase 3 */
};

/* Polygon offsets within each Factory door panel: 8 quads, skipping every 3rd */
static const int s_factoryDoorPolyIndices[8] = { 0, 1, 3, 4, 6, 7, 9, 10 };

/* ROM offset tables extracted from binary
 * Pass 1 (cyclic): 0x4FF598 — indexed by (g_animFrameCounter / 4), phase 0 or 1
 * Pass 2 (opening): 0x4FF578 — indexed by (*statePtr + 1), range 1..3 */
static const int s_animOffPass1[][2] = {  /* 0x4FF598 */
    { 0x00900000, 0x00C00000 },  /* phase 0 */
    { 0x00900000, 0x00D80000 },  /* phase 1 */
};

static const int s_animOffPass2[][2] = {  /* 0x4FF578 */
    { 0x00000000, 0x00000000 },  /* [0] unused */
    { 0x00C00000, 0x00400000 },  /* [1] *statePtr==0 */
    { 0x00C00000, 0x00800000 },  /* [2] *statePtr==1 */
    { 0x00C00000, 0x00C00000 },  /* [3] *statePtr==2 */
};

/* Per-track animation descriptor tables from ROM (DGROUP).
 * Each entry: {objectIndex, 0x80, type, groupId}, 4 ints = 16 bytes. */
static const int s_animDescEmerald[] = { /* 0x4FFD20 — 10 entries + sentinel */
    830,0x80,1,4, 879,0x80,0,7, 880,0x80,0,7, 881,0x80,0,7, 882,0x80,0,7,
    883,0x80,0,7, 884,0x80,0,7, 885,0x80,0,7, 886,0x80,0,7, 888,0x80,0,7,
    -1,0,0,0,
};

static const int s_animDescFactory[] = { /* 0x4FFDE8 — 20 entries + sentinel */
    780,0x80,0,1, 981,0x80,0,2, 982,0x80,0,3, 784,0x80,1,4, 783,0x80,0,5,
    987,0x80,0,5, 988,0x80,0,5, 989,0x80,0,5, 990,0x80,0,5, 807,0x80,0,6,
    808,0x80,0,8, 972,0x80,0,7, 973,0x80,0,7, 974,0x80,0,7, 975,0x80,0,7,
    976,0x80,0,7, 977,0x80,0,7, 978,0x80,0,7, 979,0x80,0,7, 980,0x80,0,7,
    -1,0,0,0,
};

static const int s_animDescRuin[] = { /* 0x4FFF68 — 20 entries + sentinel */
    778,0x80,0,1, 979,0x80,0,2, 980,0x80,0,3, 780,0x80,1,4, 779,0x80,0,5,
    981,0x80,0,5, 982,0x80,0,5, 983,0x80,0,5, 984,0x80,0,5, 777,0x80,0,8,
    781,0x80,0,6, 838,0x80,0,7, 839,0x80,0,7, 840,0x80,0,7, 841,0x80,0,7,
    842,0x80,0,7, 843,0x80,0,7, 844,0x80,0,7, 845,0x80,0,7, 846,0x80,0,7,
    -1,0,0,0,
};

static const int s_animDescCity[] = { /* 0x500164 — 20 entries + sentinel */
    992,0x80,0,1, 991,0x80,0,2, 707,0x80,0,3, 899,0x80,1,4,1015,0x80,0,5,
   1014,0x80,0,5,1013,0x80,0,5,1012,0x80,0,5, 898,0x80,0,5, 896,0x80,0,6,
    897,0x80,0,8, 981,0x80,0,7, 982,0x80,0,7, 983,0x80,0,7, 984,0x80,0,7,
    985,0x80,0,7, 986,0x80,0,7, 987,0x80,0,7, 988,0x80,0,7, 989,0x80,0,7,
    -1,0,0,0,
};

static const int s_animDescIsland[] = { /* 0x500608 — 18 entries + sentinel */
    601,0x80,0,1, 500,0x80,0,2, 503,0x80,1,4, 606,0x80,0,5, 607,0x80,0,5,
    502,0x80,0,5, 604,0x80,0,5, 605,0x80,0,5, 498,0x80,0,6, 547,0x80,0,7,
    543,0x80,0,7, 541,0x80,0,7, 542,0x80,0,7, 545,0x80,0,7, 549,0x80,0,7,
    548,0x80,0,7, 544,0x80,0,7, 546,0x80,0,7,
    -1,0,0,0,
};

/* 0x5000AC: 46 object indices used by Factory object animation */
static const int s_factoryParticleObjIndices[46] = {
    880,881,882,883,884,885,886,887,888,889,890,891,892,893,894,895,
    896,897,898,899,900,901,902,903,904,905,906,907,908,909,910,911,
    912,913,914,915,916,917,918,919,920,921,922,923,924,925,
};



/* Reel state — contiguous block at 0x6816FC-0x681733.
 * The 5 non-static symbols below are written by InitAnimCity in
 * track_anim_init.c as part of the binary's init block 0x0047bc46..0x0047bcf3
 * (reel-face poly pointer registration + pos/velocity/counter/state reset). Keeping
 * them file-local had made that init silently no-op and UpdateSlotMachineReels
 * saw NULL poly pointers, so the slot-machine reels never spun.
 * s_reelTarget stays static — nothing outside this file writes it. */
int *s_reelPoly[3];                                       /* 0x6816FC */
int  s_reelPos[3];                                        /* 0x681708 */
int  s_reelTarget[3];                                     /* 0x681714 */
int  s_reelVelocity[3];                                   /* 0x681720 */
int  s_reelCounter;                                       /* 0x68172C */
int  s_reelState;                                         /* 0x681730 */

#define REEL_UV_MAX    0xE00000
#define REEL_UV_SPAN   0x1FFFFF
#define REEL_ACCEL     0x8000
#define REEL_MAX_VEL   0x60000

/* Per-reel parameters: stop frame, minimum active frame, done bit */
static const int s_reelStopFrame[3] = { 0x96, 0xB4, 0xD2 };  /* 150, 180, 210 */
static const int s_reelMinFrame[3]   = { 0, 0x1E, 0x3C };      /* 0, 30, 60 */


/* StepModelRotation -- 0x0047F824 -- 22 bytes
 * Increment g_modelRotation by 0x100, then mask to lower 16 bits.
 * Original: add dword [0x92528C], 0x100; then zero word at [0x92528E]
 * which clears the upper 16 bits of the dword.
 * Net effect: g_modelRotation = (g_modelRotation + 0x100) & 0xFFFF */
void StepModelRotation(void)
{
    g_modelRotation = (g_modelRotation + 0x100) & 0xFFFF;  /* [0x0092528C] */
}

static void SpawnOneParticle(int *src, unsigned char **stream, int *slotIdx)
{
    unsigned char *s = *stream;
    int idx = *slotIdx;
    CollectEffect *p = &g_collectEffectBuf[idx];

    /* Random position offset from source object (+0x20/+0x24/+0x28 = world XYZ) */
    int rx = (int)(s[0] & 0x7F);                                /* 0-127 */
    p->posX = ((rx + src[0x20 / 4]) << 8) - 0x3F00;            /* X */

    int ry = (int)(signed short)(s[2] & 0x1F);                  /* 0-31 */
    p->posY = -(((ry + src[0x24 / 4]) << 8) - 0x0F00);         /* Y (negated) */

    int rz = (int)(signed short)(s[4] & 0x7F);                  /* 0-127 */
    p->posZ = ((rz + src[0x28 / 4]) << 8) - 0x3F00;            /* Z */

    /* Zero velocity */
    p->velX = 0;
    p->velY = 0;
    p->velZ = 0;
    p->accelY = 0;

    /* Particle billboard properties */
    p->halfW = 0x20;
    p->lifetime = 0x1F;
    p->spriteId = 0x210;
    p->type = 1;
    p->timer = 0;
    p->animEnd = 0x40;
    p->animFrameW = 8;
    p->animFrameH = 8;
    p->billboardSize = 9;
    p->animDiv = 0x10;
    p->uvBaseX = 0;
    p->uvBaseY = 0x60;
    p->tpage = (unsigned char)g_tpageCharBase;
    p->uvSpan = 0x10;

    *stream = s + 6;                            /* advance random stream */
    idx++;
    if (idx >= 60) {
        idx = 0;                     /* wrap at 60 slots */
    }
    *slotIdx = idx;
}

/* =====================================================================
 * SpawnEffectParticles — FUN_0047E0EC — 1087 bytes
 * Spawns billboard particles (birds, butterflies, etc.) into
 * g_collectEffectBuf at random offsets from 3 source object positions.
 * Each source is gated by its own step counter (g_animStep1/2/3).
 * Called per-frame from CameraFollow for Island, City, Ruin, Factory.
 *
 * Original Watcom: EAX = srcA, EDX = srcB, EBX = srcC
 * ===================================================================== */
void SpawnEffectParticles(int *srcA, int *srcB, int *srcC)
{
    unsigned char *stream = (unsigned char *)g_ringSpawnReadPtr;
    int slotIdx = g_raceCounterA0;

    /* Block 1: source A, step counter g_animStep1 (0x925294) */
    if (g_particleSpawnStep1 > 0x40) {
        g_particleSpawnStep1 -= 2;
        SpawnOneParticle(srcA, &stream, &slotIdx);
    }

    /* Block 2: source B, step counter g_animStep2 (0x92529C) */
    if (g_particleSpawnStep2 > 0x40) {
        g_particleSpawnStep2 -= 2;
        SpawnOneParticle(srcB, &stream, &slotIdx);
    }

    /* Block 3: source C, step counter g_animStep3 (0x9252A4) */
    if (g_particleSpawnStep3 > 0x40) {
        g_particleSpawnStep3 -= 2;
        SpawnOneParticle(srcC, &stream, &slotIdx);
    }

    g_raceCounterA0 = slotIdx;
    g_ringSpawnReadPtr = (unsigned short *)stream;
}

/**
 * UpdateTrackWorld_Island — 0x0047DF3C — 205 bytes
 * Resort Island: waterfall, collectible triggers.
 */
void UpdateTrackWorld_Island(void)
{
    char *obj;
    /* Surface UV animation — 2-frame swap every 8 frames */
    int *polyPtr = (int *)((char *)g_polygonArrayBase +
                   (*(unsigned short *)((char *)g_objectStructArray + 0x85CC) + 0x47) * 0x30);

    if ((g_totalFrames & 0xF) < 8) {
        polyPtr[3] = 0;
        polyPtr[7] = 0x1FFFFF;
    } else {
        polyPtr[3] = 0x200000;
        polyPtr[7] = 0x3FFFFF;
    }
    polyPtr[1] = polyPtr[3];
    polyPtr[5] = polyPtr[7];

    /* Binary 0x47DFA0: collectible triggers */
    CheckCollectiblesAllPlayers((int *)s_animDescIsland);

    /* Binary 0x47DFB2: spawn bird/effect particles */
    obj = (char *)g_objectStructArray;
    SpawnEffectParticles((int *)(obj + 0x9FA4),   /* EAX = srcA */
                         (int *)(obj + 0x84D0),   /* EDX = srcB */
                         (int *)(obj + 0x9FA4));  /* EBX = srcC */

    /* Binary 0x47DFCB: animate waterfall, birds, scenery — 3 anim slots */
    TickAnimation(0);
    TickAnimation(1);
    TickAnimation(2);

    ApplyAnimationOffsets();

    AnimateTrackGeometry(0, &g_animStateC8);   /* 0x47DFF0: EAX=0, EDX=0x9252C8 */
    AnimateTrackGeometry(1, &g_animStateE4);   /* 0x47DFFC: EAX=1, EDX=0x9252E4 */
}

/**
 * UpdateTrackWorld_City — 0x0047C3B8 — 1539 bytes
 * Radical City: unknown animated object UV swap, barrier physics (2 objects),
 * collectible checks, effect particles, slot-machine reels, and all animation ticks.
 *
 * EAX = first-frame flag (non-zero on first call after track load).
 *
 * Two "barrier" objects (A=obj 1005, B=obj 892) can be knocked over by players.
 * When a player touches a barrier, the barrier inherits player velocity and falls
 * with gravity, bouncing up to 3 times with 2/3 restitution. Each bounce
 * adds random rotation scatter. When Y drops below -512, the barrier is hidden.
 *
 * Player detection loop iterates 5 player slots (stride 0x71C) via
 * g_trailSrcA (0x8FD4E4, set to g_playerBase at init).
 * Player field +0xD4>>16: barrier type (2 = barrier A, other = barrier B).
 * Player field +0xD6: non-zero when player is near a barrier.
 */
void UpdateTrackWorld_City(void)
{
    char *obj = (char *)g_objectStructArray;       /* 0x712D44 */
    int *polyBase = (int *)g_polygonArrayBase;     /* 0x712D4C */

    /* Binary receives EAX (first-frame flag) but UpdateTrackWorld always
     * passes 1, so the guarded calls are effectively unconditional. */

    /* ---- unknown animated object UV swap (0x47C3BF-0x47C41C) ----
     * Object 899 polygon (polyStartIdx + 71): toggle UVs every 8 frames.
     * Low frames (0-7): dark UVs.  High frames (8-15): lit UVs. */
    unsigned short polyStart = *(unsigned short *)(obj + 0xEEFC);  /* obj[899]+0x30 */
    int *poly = (int *)((char *)polyBase + (polyStart + 0x47) * 0x30);  /* 0x47C3E5: EAX */
    int phase = g_totalFrames & 0xF;                                /* 0x47C3ED */
    if (phase < 8) {
        poly[3] = 0;                                                /* +0x0C = 0 */
        poly[7] = 0x1FFFFF;                                         /* +0x1C = 0x1FFFFF */
    } else {
        poly[3] = 0x200000;                                         /* +0x0C = 0x200000 */
        poly[7] = 0x3FFFFF;                                         /* +0x1C = 0x3FFFFF */
    }
    poly[1] = poly[3];                                             /* +0x04 = +0x0C */
    poly[5] = poly[7];                                             /* +0x14 = +0x1C */
    
    /* ---- CheckCollectiblesAllPlayers (0x47C41F-0x47C42D) ---- */
    CheckCollectiblesAllPlayers((int *)s_animDescCity);                /* 0x47C428 */

    /* ---- Player barrier activation loop (0x47C42D-0x47C586) ----
     * Iterates 5 player slots. If a player is near a barrier (+0xD6 != 0),
     * initializes the corresponding barrier's physics state from player velocity. */
    for (int pi = 0; pi < 5; pi++) {                                  /* 0x47C4C9 */
        Player *pl = &g_trailSrcA[pi];                                       /* 0x47C4DA */

        if (pl->_unk_0xD6 == 0) {
            continue;                         /* 0x47C4DC: skip inactive */
        }

        int barrierType = pl->_unk_0xD6;                              /* 0x47C4EC — P_INT(0xD4)>>16 = _unk_0xD6 */
        pl->sfxTrigger = 0x18;                                    /* 0x47C4EF: set timer */

        /* barrier B activation (type != 2): object 892 (0x47C43E-0x47C4BE) */
        if (barrierType != 2) {            
            g_animState4B4 = 1;                                    /* 0x47C43E */
            g_animState4B8 = pl->velX;                             /* 0x47C44B: velX from player */
            g_animState4C0 = pl->velZ;                             /* 0x47C456: velZ from player */
            g_animState4BC = 0;                                    /* 0x47C45B: gravity = 0 */
            g_animState4C4 = Random() / 128 - 128;                /* 0x47C46F: scatter X */
            g_animState4C8 = Random() / 128 - 128;                /* 0x47C482: scatter Y */
            g_animState4CC = Random() / 128 - 128;                /* 0x47C495: scatter Z */
            /* Initial position from object 892 pivot (0x47C49A-0x47C4BE) */
            int *obj892 = (int *)(obj + 0xECF0);
            g_animState4D0 = obj892[0x20/4] << 12;                /* 0x47C4B8: pivotX << 12 */
            g_animState4D4 = obj892[0x28/4] << 12;                /* 0x47C4B1: pivotZ << 12 */
            g_animState4D8 = 0;                                    /* 0x47C4BE: bounce count */
        }
        /* barrier A activation (type == 2): object 1005 (0x47C501-0x47C586) */
        else {
            g_animState48C = 1;                                    /* 0x47C501 */
            g_animState490 = pl->velX;                             /* 0x47C50E */
            g_animState498 = pl->velZ;                             /* 0x47C519 */
            g_animState494 = 0;                                    /* 0x47C51E */
            g_animState49C = Random() / 128 - 128;                /* 0x47C532 */
            g_animState4A0 = Random() / 128 - 128;                /* 0x47C545 */
            g_animState4A4 = Random() / 128 - 128;                /* 0x47C558 */
            int *obj1005 = (int *)(obj + 0x10AF4);
            g_animState4A8 = obj1005[0x20/4] << 12;               /* 0x47C571 */
            g_animState4AC = obj1005[0x28/4] << 12;               /* 0x47C574 */
            g_animState4B0 = 0;                                    /* 0x47C586 */
        }
    }

    /* ---- barrier A physics: object 1005 (0x47C58B-0x47C75F) ---- */
    int *s = (int *)(obj + 0x10AF4);                               /* ebx = &obj[1005] */
    int visA = *(short *)(obj + 0x10B20);                          /* 0x47C597: obj[1005]+0x2A */
    if (visA != -1 && g_animState48C == 1) {
        /* Position update (0x47C5B6-0x47C60E) */
        g_animState4A8 += g_animState490;                          /* accumX += velX */
        s[0x20/4] = g_animState4A8 >> 12;                         /* pivotX = accumX >> 12 */
        s[0x24/4] += g_animState494;                               /* pivotY += gravity */
        if (s[0x24/4] < -512) {                                    /* 0x47C5DD: fell too far */
            *(short *)((char *)s + 0x2C) = (short)0xFFFF;         /* hide object */
            goto barrier_a_done;
        }
        g_animState4AC += g_animState498;                          /* accumZ += velZ */
        s[0x28/4] = g_animState4AC >> 12;                         /* pivotZ = accumZ >> 12 */

        /* Velocity damping: vel = vel * 49/50 (0x47C611-0x47C64E) */
        g_animState490 = (g_animState490 + g_animState490 * 48) / 50;
        g_animState498 = (g_animState498 + g_animState498 * 48) / 50;

        /* Bounce check (0x47C64B-0x47C700) */
        if (s[0x24/4] < 10 && g_animState4B0 < 3) {
            int absGrav = g_animState494;
            if (absGrav < 0) {
                absGrav = -absGrav;
            }
            int scatter = absGrav / 10;
            g_animState49C = (Random() / 1024 - 16) * scatter;    /* 0x47C696 */
            g_animState4A0 = (Random() / 1024 - 16) * scatter;    /* 0x47C6B3 */
            g_animState4A4 = (Random() / 1024 - 16) * scatter;    /* 0x47C6D0 */
            g_animState494 = (-g_animState494 * 2) / 3;           /* 0x47C6EC: bounce */
            s[0x24/4] = 10;                                        /* reset Y to ground */
            g_animState4B0++;                                      /* 0x47C700 */
        }

        /* Rotation scatter (0x47C705-0x47C73D) */
        unsigned short *rot = (unsigned short *)((char *)s + 0x18);
        rot[0] = (rot[0] + (short)g_animState49C) & 0x0FFF;
        rot[1] = (rot[1] + (short)g_animState4A0) & 0x0FFF;
        rot[2] = (rot[2] + (short)g_animState4A4) & 0x0FFF;

        /* Copy pivot to world position; decrement gravity (0x47C741-0x47C75C) */
        s[0] = s[0x20/4];
        s[1] = s[0x24/4];
        s[2] = s[0x28/4];
        g_animState494--;
    }
barrier_a_done:

    /* ---- barrier B physics: object 892 (0x47C75F-0x47C933) ---- */
    s = (int *)(obj + 0xECF0);                                /* ebx = &obj[892] */
    int visB = *(short *)(obj + 0xED1C);                           /* 0x47C770: obj[892]+0x2A */
    if (visB != -1 && g_animState4B4 == 1) {
        /* Position update (0x47C78F-0x47C7E2) */
        g_animState4D0 += g_animState4B8;
        s[0x20/4] = g_animState4D0 >> 12;
        s[0x24/4] += g_animState4BC;
        if (s[0x24/4] < -512) {
            *(short *)((char *)s + 0x2C) = (short)0xFFFF;
            goto barrier_b_done;
        }
        g_animState4D4 += g_animState4C0;
        s[0x28/4] = g_animState4D4 >> 12;

        /* Velocity damping (0x47C7E5-0x47C822) */
        g_animState4B8 = (g_animState4B8 + g_animState4B8 * 48) / 50;
        g_animState4C0 = (g_animState4C0 + g_animState4C0 * 48) / 50;

        /* Bounce check (0x47C81F-0x47C8D4) */
        if (s[0x24/4] < 10 && g_animState4D8 < 3) {
            int absGrav = g_animState4BC;
            if (absGrav < 0) {
                absGrav = -absGrav;
            }
            int scatter = absGrav / 10;
            g_animState4C4 = (Random() / 1024 - 16) * scatter;
            g_animState4C8 = (Random() / 1024 - 16) * scatter;
            g_animState4CC = (Random() / 1024 - 16) * scatter;
            g_animState4BC = (-g_animState4BC * 2) / 3;
            s[0x24/4] = 10;
            g_animState4D8++;
        }

        /* Rotation scatter (0x47C8D9-0x47C911) */
        unsigned short *rot = (unsigned short *)((char *)s + 0x18);
        rot[0] = (rot[0] + (short)g_animState4C4) & 0x0FFF;
        rot[1] = (rot[1] + (short)g_animState4C8) & 0x0FFF;
        rot[2] = (rot[2] + (short)g_animState4CC) & 0x0FFF;

        /* Copy pivot to world position; decrement gravity (0x47C915-0x47C930) */
        s[0] = s[0x20/4];
        s[1] = s[0x24/4];
        s[2] = s[0x28/4];
        g_animState4BC--;
    }
barrier_b_done:

    /* ---- SpawnEffectParticles (0x47C933-0x47C952) ---- */
    SpawnEffectParticles((int *)(obj + 0x10780),                       /* EAX = &obj[992] */
                         (int *)(obj + 0x1073C),                       /* EDX = &obj[991] */
                         (int *)(obj + 0xBBCC));                       /* EBX = &obj[707] */

    /* ---- Animation ticks (0x47C952-0x47C9BA) ---- */
    UpdateSlotMachineReels();                                               /* 0x47C952 */
    TickAnimation(0);                                                  /* 0x47C959 */
    TickAnimation(3);                                                  /* 0x47C963 */
    TickAnimation(4);                                                  /* 0x47C96D */
    TickAnimation(5);                                                  /* 0x47C977 */
    TickAnimation(6);                                                  /* 0x47C986 */
    ApplyAnimationOffsets();                                           /* 0x47C98B */

    /* Binary doesn't explicitly set EDX for group 0; value is stale from
     * ApplyAnimationOffsets. Pass &g_animStateE0 (last EDX set = 0x9252E0). */
    AnimateTrackGeometry(0, &g_animStateE0);                           /* 0x47C992 */
    AnimateTrackGeometry(1, &g_animStateD0);                           /* 0x47C9A1 */
    AnimateTrackGeometry(2, &g_menuAnimY);                             /* 0x47C9B0 */
}

/* =====================================================================
 * Radical City slot-machine reels
 *
 * The giant slot machine's three spinning faces (reels), driven by UV scroll.
 * Each reel accelerates, picks a random stop face at a staggered frame,
 * scrolls toward it, and snaps/locks when reached.  After all 3 lock,
 * a brief pause precedes the next spin.
 *
 * UV space is 20.12 fixed point, wrapping at 0xE00000 (14.0).
 * ===================================================================== */

/**
 * UpdateSlotMachineReels — 0x0047BF90 — 1009 bytes
 *
 * Drives Radical City's giant slot machine — 3 spinning reel faces via UV scroll.
 * Each reel: accelerate → pick random stop face → scroll to it → snap/lock.
 * All 3 locked → pause 90 frames → restart the spin.
 * Final section writes UV coords to each reel's polygon quad corners.
 */
static void UpdateSlotMachineReels(void)                        /* 0x47bf90 */
{
    s_reelCounter++;                                      /* 0x47bf9b */

    if (s_reelState == 8) {                                 /* 0x47bfa8 */
        goto end_check;
    }

    for (int ch = 0; ch < 3; ch++) {
        int doneBit = 1 << ch;

        if (s_reelState & doneBit) {
            continue;              /* test byte, N */
        }

        if (s_reelCounter <= s_reelMinFrame[ch]) {         /* 0x47bfc0/c0ca/c1d1 */
            continue;
        }

        /* At start frame: pick random target, keep moving */
        if (s_reelCounter == s_reelStopFrame[ch]) {     /* 0x47bfc6/c0d0/c1d7 */
            s_reelTarget[ch] = ((Random() / 0x124A) + 1) << 21;  /* 0x47bfce */
            s_reelPos[ch] += s_reelVelocity[ch];         /* 0x47bff3 */
            if (s_reelPos[ch] > REEL_UV_MAX) {             /* 0x47bffb */
                s_reelPos[ch] -= REEL_UV_MAX;
            }
        }
        /* After start: move toward target, snap when crossed */
        else if (s_reelCounter > s_reelStopFrame[ch]) {
            if (s_reelPos[ch] <= s_reelTarget[ch] &&     /* 0x47c025 */
                s_reelPos[ch] + s_reelVelocity[ch] >= s_reelTarget[ch]) {
                /* Crossed target — snap and mark done */   /* 0x47c033 */
                s_reelPos[ch] = s_reelTarget[ch];
                s_reelState |= doneBit;                   /* 0x47c039 */
            }
            else {
                /* Keep scrolling */                        /* 0x47c045 */
                s_reelPos[ch] += s_reelVelocity[ch];
                if (s_reelPos[ch] > REEL_UV_MAX) {
                    s_reelPos[ch] -= REEL_UV_MAX;
                }
            }
        }
        /* Before start: accelerate and move */         /* 0x47c06d */
        else {
            s_reelVelocity[ch] += REEL_ACCEL;            /* 0x47c072 */
            if (s_reelVelocity[ch] > REEL_MAX_VEL) {       /* 0x47c07c */
                s_reelVelocity[ch] = REEL_MAX_VEL;
            }
            s_reelPos[ch] += s_reelVelocity[ch];         /* 0x47c098 */
            if (s_reelPos[ch] > REEL_UV_MAX) {
                s_reelPos[ch] -= REEL_UV_MAX;
            }
        }
    }

end_check:
    /* All 3 done → enter reset phase */
    if (s_reelState == 7) {                               /* 0x47c2c2 */
        s_reelState = 8;                                  /* 0x47c2d2 */
        s_reelCounter = 0;                                /* 0x47c2d7 */
    }

    /* Reset phase: after 90 frames, restart cycle */
    if (s_reelState == 8 && s_reelCounter > 0x5A) {      /* 0x47c2dd */
        s_reelCounter = 0;                                /* 0x47c2f1 */
        s_reelVelocity[2] = 0;                            /* 0x47c2f7 */
        s_reelVelocity[1] = 0;                            /* 0x47c2fd */
        s_reelVelocity[0] = 0;                            /* 0x47c303 */
        s_reelState = 0;                                  /* 0x47c309 */
    }

    /* Apply UV coordinates to polygon quads */             /* 0x47c30f */
    for (int ch = 0; ch < 3; ch++) {
        int uv = REEL_UV_MAX - s_reelPos[ch];
        int *p = s_reelPoly[ch];
        if (p == NULL) {
            continue;
        }
        if (ch < 2) {
            /* Reels 0,1: low UV at [+0x1C],[+0x14] */      /* 0x47c31f */
            p[7] = uv;                                    /* +0x1C */
            p[5] = uv;                                    /* +0x14 */
            p[3] = uv + REEL_UV_SPAN;                    /* +0x0C */
            p[1] = uv + REEL_UV_SPAN;                    /* +0x04 */
        } else {
            /* Reel 2: swapped UV direction */              /* 0x47c362 */
            p[3] = uv;                                    /* +0x0C */
            p[1] = uv;                                    /* +0x04 */
            p[7] = uv + REEL_UV_SPAN;                    /* +0x1C */
            p[5] = uv + REEL_UV_SPAN;                    /* +0x14 */
        }
    }
}

/**
 * UpdateCityAnimations — 0x0047C384 — 51 bytes
 *
 * Radical City animation tick.  Updates the slot-machine reels, then ticks
 * animation channels 3-6 (data-driven scenery animations)
 * and applies the geometry offsets.
 */
void UpdateCityAnimations(void)                            /* 0x47c384 */
{
    UpdateSlotMachineReels();                                   /* 0x47bf90 */
    TickAnimation(3);                                      /* 0x47c389 */
    TickAnimation(4);                                      /* 0x47c393 */
    TickAnimation(5);                                      /* 0x47c39d */
    TickAnimation(6);                                      /* 0x47c3a7 */
    ApplyAnimationOffsets();                                /* 0x47c3b1 */
}

/**
 * UpdateTrackWorld_Ruin — 0x00479B90 — 946 bytes
 * Regal Ruin (trackId 4, TRACK_REGAL_RUIN): pillar opening animations
 * (2 pillars), UV swap, particle spawning, animated geometry.
 *
 * NOTE: binary trackId 3/4 are swapped in ROM tables vs runtime.
 * This function is at 0x479B90 and serves Regal Ruin.
 *
 * Two pillar sequences each have 3 phases:
 *   0→1:    Triggered when g_itemStateTable flag == 0
 *   0x32→0x78: UV scrolling (pillar rotation) + vertex displacement after 0x55
 *   == 0x78:   Mark object polys hidden (word = 0xFFFF)
 *   PlaySoundEffect(0x24, 0, 0) when counter reaches 0x32
 */
void UpdateTrackWorld_Ruin(void)
{
    char *obj = (char *)g_objectStructArray;

    /* UV swap (same pattern as Island/Emerald) */
    int *polyPtr = (int *)((char *)g_polygonArrayBase +
                   (*(unsigned short *)(obj + 0xD070) + 0x47) * 0x30);
    if ((g_totalFrames & 0xF) < 8) {
        polyPtr[3] = 0;
        polyPtr[7] = 0x1FFFFF;
    } else {
        polyPtr[3] = 0x200000;
        polyPtr[7] = 0x3FFFFF;
    }
    polyPtr[1] = polyPtr[3];
    polyPtr[5] = polyPtr[7];

    /* Collectibles + particle spawning */
    CheckCollectiblesAllPlayers((int *)s_animDescFactory);  /* 0x479BFB: eax=0x4FFDE8 */
    SpawnEffectParticles((int *)(obj + 0xCF30),             /* 0x479C16: EAX */
                         (int *)(obj + 0x10494),            /* EDX */
                         (int *)(obj + 0x104D8));           /* EBX */

    /* Increment model rotation counter */
    StepModelRotation();                                         /* 0x479C20: g_modelRotation += 0x100 */

    /* Pillar 1 animation (g_animState300 @ 0x925300) */

    /* Trigger: when item-state flag is clear and counter is still 0 */
    int flag1 = *(short *)((char *)g_itemStateTable + 8); /* 0x479C25 */
    if (flag1 == 0 && g_animState300 == 0) {
        g_animState300 = 1; /* 0x479C3D */
    }

    /* Advance counter while 0 < counter < 0x96 */
    if (g_animState300 > 0 && g_animState300 < 0x96) {
        g_animState300++;
        if (g_animState300 == 0x32) {
            PlaySoundEffect(0x24, 0, 0); /* 0x479C67 */
        }
    }

    /* UV rotation: counter in (0x32, 0x78) */
    if (g_animState300 > 0x32 && g_animState300 < 0x78) {
        unsigned short *p;
        unsigned short v;

        p = (unsigned short *)(obj + 0xD0E0);
        v = *p;
        v = (v - 0xC) & 0x0FFF;
        *p = v; /* 0x479C94 */

        p = (unsigned short *)(obj + 0x10758);
        v = *p;
        v = (v + 0xC) & 0x0FFF;
        *p = v; /* 0x479CA8 */

        p = (unsigned short *)(obj + 0x107E0);
        v = *p;
        v = (v - 0xC) & 0x0FFF;
        *p = v; /* 0x479CBC */

        p = (unsigned short *)(obj + 0x10820);
        v = *p;
        v = (v + 0xC) & 0x0FFF;
        *p = v; /* 0x479CD0 */

        /* Vertex displacement after frame 0x55 */
        if (g_animState300 > 0x55) {
            int delta = g_animState300 - 0x55; /* 0x479CEF */
            int half = delta >> 1;             /* 0x479D10 */

            *(int *)(obj + 0xD0EC) += delta;  /* 0x479D0A */
            *(int *)(obj + 0xD0F0) -= half;   /* 0x479D1A */
            *(int *)(obj + 0x10760) += delta; /* 0x479D08 */
            *(int *)(obj + 0x1075C) -= half;  /* 0x479D28 */
            *(int *)(obj + 0x107E4) += half;  /* 0x479D36 */
            *(int *)(obj + 0x107E8) += delta; /* 0x479D42 */
            *(int *)(obj + 0x1082C) += delta; /* 0x479D52 */
            *(int *)(obj + 0x10830) += half;  /* 0x479D5E */
        }
    }

    /* At frame 0x78: hide the pillar objects */
    if (g_animState300 == 0x78) { /* 0x479D66 */
        *(unsigned short *)(obj + 0xD0F4) = 0xFFFF;
        *(unsigned short *)(obj + 0x10768) = 0xFFFF;
        *(unsigned short *)(obj + 0x107F0) = 0xFFFF;
        *(unsigned short *)(obj + 0x10834) = 0xFFFF;
    }

    /* Pillar 2 animation (g_animState304 @ 0x925304) */

    /* Trigger: item-state slot 1 flag */
    int flag2 = *(short *)((char *)g_itemStateTable + 0x14); /* 0x479D98 */
    if (flag2 == 0 && g_animState304 == 0) {
        g_animState304 = 1; /* 0x479DB0 */
    }

    if (g_animState304 > 0 && g_animState304 < 0x96) {
        g_animState304++;
        if (g_animState304 == 0x32) {
            PlaySoundEffect(0x24, 0, 0); /* 0x479DDA */
        }
    }

    if (g_animState304 > 0x32 && g_animState304 < 0x78) {
        unsigned short *p;
        unsigned short v;

        p = (unsigned short *)(obj + 0xD1AC);
        v = *p;
        v = (v - 0x12) & 0x0FFF;
        *p = v; /* 0x479E07 */

        p = (unsigned short *)(obj + 0x10868);
        v = *p;
        v = (v + 0x12) & 0x0FFF;
        *p = v; /* 0x479E1B */

        p = (unsigned short *)(obj + 0x108AC);
        v = *p;
        v = (v - 0x12) & 0x0FFF;
        *p = v; /* 0x479E2F */

        p = (unsigned short *)(obj + 0x108EC);
        v = *p;
        v = (v + 0x12) & 0x0FFF;
        *p = v; /* 0x479E43 */

        if (g_animState304 > 0x55) {
            int delta = g_animState304 - 0x55; /* 0x479E62 */
            int half = delta >> 1;             /* 0x479E83 */

            *(int *)(obj + 0xD1B8) += delta;
            *(int *)(obj + 0xD1BC) -= half;
            *(int *)(obj + 0x10870) += delta;
            *(int *)(obj + 0x1086C) -= half;
            *(int *)(obj + 0x108B0) += half;
            *(int *)(obj + 0x108B4) += delta;
            *(int *)(obj + 0x108F8) += delta;
            *(int *)(obj + 0x108FC) += half;
        }
    }

    if (g_animState304 == 0x78) { /* 0x479ED9 */
        *(unsigned short *)(obj + 0xD1C0) = 0xFFFF;
        *(unsigned short *)(obj + 0x10878) = 0xFFFF;
        *(unsigned short *)(obj + 0x108BC) = 0xFFFF;
        *(unsigned short *)(obj + 0x10900) = 0xFFFF;
    }

    /* Tick animations + animated geometry */
    TickAnimation(0);
    TickAnimation(7);
    ApplyAnimationOffsets();
    AnimateTrackGeometry(0, &g_menuAnimY);     /* 0x479F28: EAX=0, EDX=0x9252C0 */
    AnimateTrackGeometry(1, &g_animStateD0);   /* 0x479F37: EAX=1, EDX=0x9252D0 */
}

/* Reactive Factory: 46 animated objects */

/**
 * ApplyFactoryDoorUVDeltas — helper for Factory silo-door UV scrolling.
 * Applies deltaA to U and deltaB to V for 4 vertices × 8 selected polygons.
 */
static void ApplyFactoryDoorUVDeltas(int *polyBase, int deltaA, int deltaB)
{
    for (int i = 0; i < 8; i++) {
        int *p = polyBase + s_factoryDoorPolyIndices[i] * (0x30 / 4);
        for (int j = 0; j < 8; j += 2) {
            p[j]   = (p[j]   & 0x3FFFFF) + deltaA;
            p[j+1] = (p[j+1] & 0x3FFFFF) + deltaB;
        }
    }
}

/**
 * FUN_0047a310 — Factory object animation update — 741 bytes
 * Iterates 46 animated objects, updating position, velocity, UV scrolling,
 * and heading angle relative to camera. Called from UpdateTrackWorld_Factory.
 */
static void UpdateFactoryParticles(void)         /* FUN_0047a310 */
{
    char *obj = (char *)g_objectStructArray;

    for (int idx = 0; idx < 46; idx++) {
        int objIndex = s_factoryParticleObjIndices[idx];
        char *fobj= obj + objIndex * 0x44;                /* 0x47a518 */
        int *si = (int *)fobj;

        if (*(short *)(fobj +0x18) == 0) {                /* 0x47a526: skip inactive */
            continue;
        }

        /* Compute velocity damping — X direction */
        int velX = si[3];                                 /* sand+0x0C */
        int absVX = velX < 0 ? -velX : velX;
        int esi_flag = 0;

        if (absVX > 0x100) {                              /* 0x47a536 */
            int damped = (velX * 31) / 32;                /* 0x47a540: *0x1F/0x20 */
            si[3] = damped;                               /* 0x47a552 */
            si[0] += damped;                              /* 0x47a557: posX += velX */
            esi_flag = 1;
        }

        /* Velocity damping — Z direction */
        int velZ = si[5];                                 /* sand+0x14 */
        int absVZ = velZ < 0 ? -velZ : velZ;

        if (absVZ > 0x100) {                              /* 0x47a566 */
            int damped = (velZ * 31) / 32;                /* 0x47a570 */
            si[5] = damped;                               /* 0x47a583 */
            si[2] += damped;                              /* 0x47a586 */
            esi_flag++;
        }

        /* Y position: gravity/sink */
        si[4] -= 0x100;                                   /* 0x47a58f: velY -= 0x100 */
        int posY = si[1];                                 /* sand+0x04 */

        if (posY != 0x2000 || si[4] != (int)0xFFFFFF00) {
            /* Not at rest — continue sinking */
            goto uv_scroll;                               /* 0x47a5a0/0x47a5ab → 0x47a375 */
        }

        /* At rest (posY==0x2000, velY==-0x100) — adjust height based on world Y */

        int worldY = *(short *)(fobj + 0x18); /* 0x47a5b7: object height */
        *(short *)(fobj + 0x18) -= 0x800;     /* 0x47a5b1: sub 0x800 */

        if (worldY > 0) {
            if (worldY >= 0x340) {                                    /* 0x47a5ca */
                *(short *)(fobj + 0x18) -= 0x32; /* 0x47a320: sub 0x32 */
                worldY = *(short *)(fobj + 0x18);
                if (worldY < 0x340) {
                    *(short *)(fobj + 0x18) = 0x340; /* 0x47a332 */
                }
            }
            else {
                *(short *)(fobj + 0x18) += 0x32; /* 0x47a5d0: add 0x32 */
                worldY = *(short *)(fobj + 0x18);
                if (worldY > 0x340) {
                    *(short *)(fobj + 0x18) = 0x340; /* 0x47a5e6 */
                }
            }
        }
        else if (worldY < (int)0xFFFFFCC0) {                                    /* -0x340 */
            *(short *)(fobj + 0x18) += 0x32; /* 0x47a341 */
            worldY = *(short *)(fobj + 0x18);
            if (worldY > (int)0xFFFFFCC0) {
                *(short *)(fobj + 0x18) = (short)0xFCC0; /* 0x47a369 */
            }
        }
        else if (worldY > (int)0xFFFFFCC0) {                                    /* 0x47a355 */
            *(short *)(fobj + 0x18) -= 0x32; /* 0x47a357 */
            worldY = *(short *)(fobj + 0x18);
            if (worldY < (int)0xFFFFFCC0) {
                *(short *)(fobj + 0x18) = (short)0xFCC0;
            }
        }

        /* 0x47a36f: advance UV counter (high byte of short at +0x18) */
        short val = *(short *)(fobj + 0x18);
        int hi = (val >> 8) & 0xFF;
        hi = (hi + 8) & 0xFF;
        *(short *)(fobj + 0x18) = (short)((hi << 8) | (val & 0xFF));

    uv_scroll:
        if (esi_flag == 0) {
            /* No horizontal velocity — camera-relative angle tracking */
            si[4] = 0;                                     /* 0x47a37e: velY = 0 */

            if (!(g_factoryObjState[idx] & 1)) {             /* 0x47a387: first time? */
                g_factoryObjState[idx] |= 1;                 /* 0x47a391: mark visited */
                /* Compute on-track flag via atan2 of object world pos */
                int onTrack = IsOnTrackSurface(             /* 0x47a3a9 */
                    (float)si[0x20/4], (float)si[0x28/4]);
                g_factoryObjAngle[idx] = 0x800;               /* 0x47a3bd */
                g_factoryObjState[idx] |= (onTrack << 1);     /* 0x47a3bb */
            }

            if ((g_factoryObjState[idx] & 2) || g_weatherType == WEATHER_SNOW) {
                continue; // goto obj_done;                              /* 0x47a509 */
            }

            /* Advance angle and look up cosine for Y displacement */
            int angle = g_factoryObjAngle[idx];           /* 0x47a3e8 */
            int cosVal = g_cosTable[angle];              /* 0x47a3ee */
            si[0x24/4] = (cosVal >> 10) + 0x30;         /* 0x47a3f7: worldY = cos/1024+48 */
            angle = (angle + 0x64) & 0xFFF;             /* 0x47a400/0x47a403 */
            g_factoryObjAngle[idx] = angle;

            continue;
        }

        /* Has horizontal velocity — update UV + position */
        int vel = si[4]; /* 0x47a416: velY (modified) */
        si[1] += vel;    /* 0x47a41b: posY += velY */

        if (si[1] < 0x2000 && 0 > vel) { /* 0x47a424/0x47a42a */
            /* Bounce: damp velocity, snap to ground */
            int absV = vel < 0 ? -vel : vel;
            si[1] = 0x2000;   /* 0x47a441 */
            si[4] = absV / 2; /* 0x47a448 */

            if (si[4] >= 0x300) { /* 0x47a44b */
                /* Read random UV deltas from stream */
                unsigned char *s = (unsigned char *)g_ringSpawnReadPtr;
                int r1 = (int)(signed short)(s[0] & 0xFF) - 0x80; /* 0x47a462 */
                int uvDx = (r1 * si[4]) / 4096;                   /* 0x47a468-0x47a47d: *vel/4096 */
                *(short *)(fobj + 0x3C) = (short)uvDx;            /* 0x47a480 */

                int r2 = (int)(signed short)(s[2] & 0xFF) - 0x80; /* 0x47a496 */
                int uvDz = (r2 * si[4]) / 4096;
                *(short *)(fobj + 0x3E) = (short)uvDz; /* 0x47a4b2 */

                g_ringSpawnReadPtr = (unsigned short *)(s + 4); /* 0x47a46e+0x47a491 */
            }
            else {
                si[4] = 0; /* 0x47a4b8: stop bouncing */
                goto update_world;
            }
        }

        /* Apply UV scroll (fobj+0x3C/0x3E are UV velocity) */
        short uvVelX = *(short *)(fobj + 0x3C); /* 0x47a4bd */
        short curU = *(short *)(fobj + 0x18);
        curU = (short)((curU + uvVelX) & 0x0FFF); /* 0x47a4ca: UV wrap */
        *(short *)(fobj + 0x18) = curU;

        short uvVelZ = *(short *)(fobj + 0x3E); /* 0x47a4ce */
        short curV = *(short *)(fobj + 0x1A);
        curV = (short)((curV + uvVelZ) & 0x0FFF); /* 0x47a4db */
        *(short *)(fobj + 0x1A) = curV;

        /* If object height field is 0, set U to 1 (visible) */
        if (*(short *)(fobj + 0x18) == 0) { /* 0x47a4e2-0x47a4e7 */
            *(short *)(fobj + 0x18) = 1;  /* 0x47a4e9 */
        }

    update_world:
        /* Update world XYZ from fixed-point positions */
        si[0x20/4] = si[0] >> 8;                           /* 0x47a4f1 */
        si[0x24/4] = si[1] >> 8;                           /* 0x47a4fa */
        si[0x28/4] = si[2] >> 8;                           /* 0x47a506 */
    }
}

/**
 * UpdateTrackWorld_Factory — 0x0047A628 — 4459 bytes
 * Reactive Factory (trackId 3, TRACK_REACTIVE_FACTORY): per-frame world update.
 * Binary calls: CheckCollectiblesAllPlayers, SpawnEffectParticles,
 * StepModelRotation, FUN_0047a310, PlaySound×2, TickAnimation(0,8,9,10,11),
 * ApplyAnimationOffsets, AnimateTrackGeometry(0,1,2).
 *
 * NOTE: binary trackId 3/4 are swapped in ROM tables vs runtime.
 * This function is at 0x47A628 and serves Reactive Factory.
 */
void UpdateTrackWorld_Factory(void)
{
    char *obj = (char *)g_objectStructArray;

    /* UV swap (same pattern as all tracks) — 0x47a628 */
    int *polyPtr = (int *)((char *)g_polygonArrayBase +
                    (*(unsigned short *)(obj + 0xCF60) + 0x47) * 0x30);
    if ((g_totalFrames & 0xF) < 8) {                   /* 0x47a663 */
        polyPtr[3] = 0;
        polyPtr[7] = 0x1FFFFF;
    } else {
        polyPtr[3] = 0x200000;
        polyPtr[7] = 0x3FFFFF;
    }
    polyPtr[1] = polyPtr[3];                            /* 0x47a689 */
    polyPtr[5] = polyPtr[7];

    /* Collectibles + particle spawning — 0x47a695 */
    CheckCollectiblesAllPlayers((int *)s_animDescRuin);     /* 0x47a69e */

    SpawnEffectParticles((int *)(obj + 0xCEA8),             /* EAX = srcA */
                         (int *)(obj + 0x1040C),            /* EDX = srcB */
                         (int *)(obj + 0x10450));           /* EBX = srcC */

    /* Increment model rotation counter — 0x47a6be */
    StepModelRotation();

    /* Per-player object proximity — 0x47a6c3 */
    Player *player = g_playerBase;                      /* 0x8FD4F4 */
    for (int p = 0; p < g_numPlayers; p++, player++) {
        if (player->_unk_0xD6 == 0) {                     /* 0x47a80b: skip if inactive */
            continue;
        }

        player->sfxTrigger = 0x18;                      /* 0x47a81a */

        /* Find closest object at ground level */
        int bestDist = 0x7FFFFFFF;                      /* 0x47a815 */
        int bestIdx = 0;
        for (int s = 0; s < 0x2E; s++) {                    /* 0x47a6e4 */
            char *nearObj = obj + s_factoryParticleObjIndices[s] * 0x44;
            if (*(short *)(nearObj + 0x18) != 0) {         /* 0x47a6fa: skip if not at ground */
                continue;
            }

            int playerX = player->posX >> 12;           /* 0x47a706 */
            int dx = *(int *)(nearObj + 0x20) - playerX;
            int playerZ = player->posZ >> 12;           /* 0x47a716 */
            int dz = *(int *)(nearObj + 0x28) - playerZ;
            int dist = dx * dx + dz * dz;               /* 0x47a729 */
            if (dist < bestDist) {
                bestIdx = s;
                bestDist = dist;
            }
        }

        /* Setup closest object for this player */
        
        char *best = obj + s_factoryParticleObjIndices[bestIdx] * 0x44;
        int *bi = (int *)best;

        /* Set fixed-point position from world coords */
        bi[0] = bi[0x20/4] << 8;                    /* 0x47a750 */
        bi[1] = bi[0x24/4] << 8;                    /* 0x47a758 */
        bi[2] = bi[0x28/4] << 8;                    /* 0x47a761 */

        /* Read random stream for velocity */
        unsigned char *stream = (unsigned char *)g_ringSpawnReadPtr;

        short raw1 = *(short *)stream;
        raw1 &= 0x0FFF;                              /* 0x47a770: and bh, 0xf */
        stream += 2;
        int r1 = (int)(signed short)raw1 - 0x800;    /* 0x47a77f */
        int playerVelX = player->velX >> 3;          /* 0x47a785 */
        bi[0x10/4] = 0x1900;                         /* 0x47a788: Y velocity */
        bi[0x0C/4] = playerVelX + r1;                /* 0x47a797: X velocity */

        short raw2 = *(short *)stream;
        raw2 &= 0x0FFF;
        int r2 = (int)(signed short)raw2 - 0x800;    /* 0x47a7a6 */
        int playerVelZ = player->velZ >> 3;
        bi[0x14/4] = playerVelZ + r2;                /* 0x47a7b1: Z velocity */

        /* UV coordinates from stream */
        short uv1 = *(short *)(stream + 2);
        stream += 2;
        uv1 &= 0x00FF;                               /* 0x47a7bb: xor bh, bh */
        g_ringSpawnReadPtr = (unsigned short *)stream;
        *(short *)(best + 0x3C) = (short)(uv1 - 0x80); /* 0x47a7cc */
        stream += 2;
        g_ringSpawnReadPtr = (unsigned short *)stream;

        short uv2 = *(short *)stream;
        stream += 2;
        uv2 &= 0x00FF;
        *(short *)(best + 0x18) = 1;                 /* 0x47a7de: active flag */
        *(short *)(best + 0x3E) = (short)(uv2 - 0x80); /* 0x47a7f0 */
        g_ringSpawnReadPtr = (unsigned short *)stream;
    }

    /* Factory object animation update — 0x47a832 */
    UpdateFactoryParticles();                               /* call 0x47a310 */

    /* this may be one of the two emeralds that "launch" */

    /* First triggered sequence (g_animState330) — 0x47a837 */
    int flag1 = *(short *)((char *)g_itemStateTable + 0x14);/* 0x47a83c */
    if (flag1 == 0 && g_animState330 == 0) {                /* 0x47a844/0x47a846 */
        g_factoryEmeraldBounceTimer2 = 0x28;                              /* 0x47a859 */
        g_animState330 = 1;                                 /* 0x47a85f */
    }

    if (g_animState330 > 0 && g_animState330 < 0x32) {      /* 0x47a865-0x47a872 */
        g_animState330++;
        if (g_animState330 == 0x32) {                       /* 0x47a87c */
                PlaySoundEffect(0x0E, 0, 0);                      /* 0x47a88c */
        }
    }

    /* First sequence UV animation — when counter == 0x31 (49) — 0x47a891 */
    if (g_animState330 == 0x31) {
        int *polyBase = (int *)((char *)g_polygonArrayBase +
                        (*(unsigned short *)(obj + 0xD02C) + 0x63) * 0x30);
        /* Process 4 polygons: indices 0, 1, 4, 5 from base */
        static const int triggerPolys[] = { 0, 1, 4, 5 };
        for (int k = 0; k < 4; k++) {
            char *pp = (char *)polyBase + triggerPolys[k] * 0x30;
            *(unsigned char *)(pp + 0x28) = 2;              /* 0x47a8ce: tpage index — retarget sign polys to tpage 2 */
            *(short *)(pp + 0x02) += 0x40;                  /* vertex 0 U high */
            *(short *)(pp + 0x0A) += 0x40;                  /* vertex 1 U high */
            *(short *)(pp + 0x12) += 0x40;                  /* vertex 2 U high */
            *(short *)(pp + 0x1A) += 0x40;                  /* vertex 3 U high */
        }
    }

    /* First sequence bounce physics — when counter == 0x32 — 0x47a9c7 */
    if (g_animState330 == 0x32) {
        int height = *(int *)(obj + 0xCE88);                 /* 0x47a9db */
        int xpos   = *(int *)(obj + 0xCE84);                 /* 0x47a9e1 */
        height += g_factoryEmeraldBounceTimer2;                            /* 0x47a9e7 */
        g_factoryEmeraldBounceTimer2--;                                    /* 0x47a9e9 */
        *(int *)(obj + 0xCE88) = height;                     /* 0x47a9ea */
        xpos -= 5;                                           /* 0x47a9f0 */
        *(int *)(obj + 0xCE84) = xpos;                       /* 0x47a9f8 */

        if (g_factoryEmeraldBounceTimer2 < 0 && height < 0x9A) {           /* 0x47a9fe/0x47aa02 */
            int absV = g_factoryEmeraldBounceTimer2 < 0 ? -g_factoryEmeraldBounceTimer2 : g_factoryEmeraldBounceTimer2;
            g_factoryEmeraldBounceTimer2 = absV / 2;                       /* 0x47aa19 */
            if (g_factoryEmeraldBounceTimer2 < 2) {                        /* 0x47aa1e */
                g_animState330 = 0x33;                       /* 0x47aa23: settled */
                *(int *)(obj + 0xCE88) = 0x9A;               /* 0x47aa2d */
            }
        }
    }

    /* First sequence debris particles — 0x47aa37 */
    if (g_animState330 > 0x31 && g_raceOrder[4] == 0) {      /* 0x902080 */
        unsigned char *stream = (unsigned char *)g_ringSpawnReadPtr;
        int slot = g_raceCounterA0;
        CollectEffect *p = &g_collectEffectBuf[slot];

        int rx = (int)(stream[0] & 0x7F);                    /* 0x47aa5a-0x47aa61 */
        stream += 2;
        g_ringSpawnReadPtr = (unsigned short *)stream;

        int posX = (rx << 8) + 0x22C00;                      /* 0x47aa7f */
        p->posX = posX;                                      /* 0x47aa88 */
        p->posY = (int)0xFFFE9800;                           /* 0x47aa95: Y */
        p->velX = 0;
        p->velY = 0;
        p->velZ = 0;
        p->accelY = (int)0xFFFFFF80;                         /* 0x47aaaa: -128 */

        int rz = (int)(stream[0] & 0x7F);                    /* 0x47aaa6 */
        stream += 2;
        g_ringSpawnReadPtr = (unsigned short *)stream;
        int posZ = (rz << 8) + 0x163C00;                     /* 0x47ab12 */
        p->posZ = posZ;                                      /* 0x47ab1a */

        p->halfW = 0x10;                                     /* 0x47ab0a */
        p->lifetime = 0x28;                                  /* 0x47ab2f */
        p->spriteId = 0x210;                                 /* 0x47aab3 */
        p->timer = 0;                                        /* 0x47ab21 */
        p->animEnd = 0x40;                                   /* 0x47ab4b */
        p->animFrameW = 5;                                   /* 0x47aadc */
        p->animFrameH = 5;                                   /* 0x47aae7 */
        p->billboardSize = 0x48;                             /* 0x47ab65 */
        p->animDiv = 0x10;                                   /* 0x47aaf4 */
        p->uvBaseX = 0;                                      /* 0x47ab3d */
        p->uvBaseY = 0x50;                                   /* 0x47ab53 */
        p->tpage = (unsigned char)g_tpageCharBase;           /* 0x47ab73 */
        p->uvSpan = 0x10;                                    /* 0x47aaff */

        g_raceCounterA0++;
        if (g_raceCounterA0 >= 0x3C) {                       /* 0x47ab7d */
            g_raceCounterA0 = 0;
        }
    }

    /* this may be one of the two emeralds that "launch" */

    /* Second triggered sequence (g_animState338) — 0x47ab86 */
    int flag2 = *(short *)((char *)g_itemStateTable + 8); /* 0x47ab8b */
    if (flag2 == 0 && g_animState338 == 0) {              /* 0x47ab93/0x47ab95 */
        g_animState33C = 0x28;                            /* 0x47aba8 */
        g_animState338 = 1;                               /* 0x47abae */
    }
    if (g_animState338 > 0 && g_animState338 < 0x32) {    /* 0x47abb4-0x47abc1 */
        g_animState338++;
        if (g_animState338 == 0x32) {                     /* 0x47abcc */
            PlaySoundEffect(0x0E, 0, 0);                        /* 0x47abdc */
        }
    }

    /* Second sequence bounce — when counter == 0x32 — 0x47abe1 */
    if (g_animState338 == 0x32) {
        int height = *(int *)(obj + 0xCF98);                /* 0x47abf5 */
        int xpos   = *(int *)(obj + 0xCF94);                /* 0x47abfb */
        height += g_animState33C;                           /* 0x47ac01 */
        g_animState33C--;                                   /* 0x47ac03 */
        *(int *)(obj + 0xCF98) = height;
        xpos += 8;                                          /* 0x47ac0a: +8 (not -5) */
        *(int *)(obj + 0xCF94) = xpos;

        if (g_animState33C < 0 && height < 0) {             /* 0x47ac18/0x47ac1c */
            int absV = g_animState33C < 0 ? -g_animState33C : g_animState33C;
            g_animState33C = absV / 2;                      /* 0x47ac30 */
            if (g_animState33C < 2) {                       /* 0x47ac35 */
                g_animState338 = 0x33;                      /* 0x47ac3a */
                *(int *)(obj + 0xCF98) = 0;                 /* 0x47ac44 */
            }
        }
    }

    /* Second sequence debris particles — proximity check — 0x47ac4e */
    if (g_raceType != RACE_TIMEATTACK && g_raceOrder[5] < g_numPlayers) { /* 0x902084 */
        int pidx = g_raceOrder[5];
        /* Compute player struct offset: pidx * 0x71C */
        Player *player = &((Player *)g_playerBase)[pidx];    /* 0x47ac82 */
        int px = player->posX >> 12;                         /* 0x47ac8d */
        int py = player->posY >> 12;                         /* 0x47ac9a */
        int pz = player->posZ >> 12;                         /* 0x47acb1 */
        int dx = 0x1C8D - px;
        int dy = (int)0xFFFFFC50 - py;                       /* -0x3B0 */
        int dz = (int)0xFFFFF10B - pz;                       /* -0xEF5 */
        int dist = dx*dx + dy*dy + dz*dz;

        if (dist < 0x6C000) {                                /* 0x47acbf */
            /* Spawn ring-chase debris particle */
            int slot = g_particleIdx;
            int *part = &g_ringChaseArray[slot * 8];          /* 0x907B20 + slot*32 */
            part[0] = 0x1C8D00;                              /* X */
            part[1] = (int)0xFFFC5000;                       /* Y */
            part[2] = (int)0xFFF0E800;                       /* Z */
            part[3] = 0;                                     /* unused */
            part[4] = (int)0xFFFFF000;                       /* velX */
            part[5] = 0x1000;                                /* velY */
            part[6] = 0x12C;                                 /* velZ */
            /* Binary 0x47ad29: stores -player ptr in [ebx+0x1c] (path A —
             * UpdateRaceRings will skip the target+0x80 validation). On
             * 64-bit we shadow the real ptr in g_ringChaseTarget[] and
             * use part[7] as a path flag only. */
            g_ringChaseTarget[slot] = player;
            part[7] = -1;                                    /* path A flag */

            g_particleIdx++;
            if (g_particleIdx >= 0x20) {                     /* 0x47ad2f */
                g_particleIdx = 0;
            }
        }
    }

    /* Tick animations — 0x47ad3b */
    TickAnimation(0);                                        /* 0x47ad3d */
    TickAnimation(8);                                        /* 0x47ad47 */
    TickAnimation(9);                                        /* 0x47ad51 */
    TickAnimation(10);                                       /* 0x47ad5b */
    TickAnimation(11);                                       /* 0x47ad65 */

    /* Factory UV animation — when g_raceOrder[3] == 1 — 0x47ad6a */
    if (g_raceOrder[3] == 1) {                       /* 0x90207C */
        /* Find a player near the trigger zone */
        int foundPlayer = 0;
        Player *nearPlayer = NULL;
        Player *pp = (Player *)g_playerBase;
        for (int np = 0; np < g_numPlayers; np++, pp++) {
            nearPlayer = pp;                                 /* 0x47adc6: mov esi, eax — unconditional, last-examined */
            int px = pp->posX >> 12;                         /* 0x47ada1 */
            int pz = pp->posZ >> 12;                         /* 0x47adba */
            int dx = (int)0xFFFFE426 - px;                   /* -0x1BDA */
            int dz = 0xDBA - pz;
            int dist = dx*dx + dz*dz;                        /* 0x47adc4 */
            if (dist < 0x120000) {                           /* 0x47adc8 */
                foundPlayer = 1;
                break;
            }
        }

        /* Update door proximity state (g_factoryDoorState, 0..3 hysteresis) */
        int doUV = 0;
        /* Player left trigger zone — decrement counter */
        if (!foundPlayer) {
            if (g_factoryDoorState > 0) {                  /* 0x47ade3 */
                if (g_factoryDoorState == 3) {
                    nearPlayer->sfxTrigger = 0x21;           /* 0x47adec: only at 3 */
                }
                g_factoryDoorState--;                      /* 0x47ae00 */
                doUV = 2;
            }
        }
        /* Player in trigger zone — increment counter up to 3 */
        else {
            if (g_factoryDoorState < 3) {                  /* 0x47ae11 */
                if (g_factoryDoorState == 0 && nearPlayer) {
                    nearPlayer->sfxTrigger = 0x21;           /* 0x47ae1a: at 0 */
                }
                g_factoryDoorState++;                      /* 0x47ae2e */
                doUV = 2;
            }
        }

        if (doUV == 2) {                                     /* 0x47ae38 */
            /* Apply UV deltas to door-panel polygons */
            int deltaA = s_factoryDoorUVTable[g_factoryDoorState * 2];
            int deltaB = s_factoryDoorUVTable[g_factoryDoorState * 2 + 1];

            /* Door panel 1: base from obj+0xD180, +0x12 offset */
            int idx1 = *(unsigned short *)(obj + 0xD180) + 0x12;
            int *polyBase1 = (int *)((char *)g_polygonArrayBase + idx1 * 0x30);
            ApplyFactoryDoorUVDeltas(polyBase1, deltaA, deltaB);

            /* Door panel 2: base from obj+0xD1C4, +0x1A offset */
            int idx2 = *(unsigned short *)(obj + 0xD1C4) + 0x1A;
            int *polyBase2 = (int *)((char *)g_polygonArrayBase + idx2 * 0x30);
            ApplyFactoryDoorUVDeltas(polyBase2, deltaA, deltaB);
        }
    }

    /* Apply animation offsets + track geometry — 0x47b75a */
    ApplyAnimationOffsets();
    AnimateTrackGeometry(0, &g_animStateD0);   /* 0x47B766: EAX=0, EDX=0x9252D0 */
    AnimateTrackGeometry(1, &g_menuAnimY);     /* 0x47B775: EAX=1, EDX=0x9252C0 */
    AnimateTrackGeometry(2, &g_animStateE0);   /* 0x47B784: EAX=2, EDX=0x9252E0 */
}

/**
 * UpdateTrackWorld_Emerald — 0x00479704 — 153 bytes
 * Radiant Emerald: simplest track — just collectibles.
 * Binary calls: CheckCollectiblesAllPlayers, TickAnimation(0),
 * ApplyAnimationOffsets, AnimateTrackGeometry(0), AnimateTrackGeometry(1).
 */
void UpdateTrackWorld_Emerald(void)
{
    /* UV animation (same pattern as Island) */
    int *polyPtr = (int *)((char *)g_polygonArrayBase +
                   (*(unsigned short *)((char *)g_objectStructArray + 0xDCA8) + 0x47) * 0x30);

    if ((g_totalFrames & 0xF) < 8) {
        polyPtr[3] = 0;
        polyPtr[7] = 0x1FFFFF;
    } else {
        polyPtr[3] = 0x200000;
        polyPtr[7] = 0x3FFFFF;
    }

    polyPtr[1] = polyPtr[3];
    polyPtr[5] = polyPtr[7];

    CheckCollectiblesAllPlayers((int *)s_animDescEmerald);
    TickAnimation(0);
    ApplyAnimationOffsets();
    
    AnimateTrackGeometry(0, &g_animStateD0);   /* 0x479786: EAX=0, EDX=0x9252D0 */
    AnimateTrackGeometry(1, &g_menuAnimY);     /* 0x479795: EAX=1, EDX=0x9252C0 */
}

/* =====================================================================
 * AnimateBalloons — 0x0046149C — 174 bytes
 * Per-frame: iterates 17 collectible objects, applies sine-table bobbing
 * and rotation, then renders each via RenderBalloonModelForRace + SubmitGroundShadow.
 * Object array starts at g_objectStructArray + 0x30, stride 0x2C (44 bytes).
 * Called from render pipeline (binary 0x4CD67D).
 * ===================================================================== */
void AnimateBalloons(void)
{
    int *items = (int *)g_balloonArray;

    /* Compute 3 sine phase indices from g_totalFrames.
     * Binary uses byte offsets (<<7/<<6/<<8); divide by 4 for int array index. */
    int phase1 = (g_totalFrames & 0x7F) << 5;                 /* 0x4614C2/D0: byte<<7 / 4 */
    int phase2 = (g_totalFrames & 0xFF) << 4;                 /* 0x4614C5/CD: byte<<6 / 4 */
    int phase3 = (g_totalFrames & 0x3F) << 6;                 /* 0x4614CA/D3: byte<<8 / 4 */

    for (int i = 0; i < 17; i++, items = (int *)((char *)items + 0x2C)) {
        if (items[6] <= -1) {
            continue;                                         /* 0x4614DF: skip inactive (+0x18) */
        }

        int worldX = items[7];                                /* +0x1C */
        int worldY = items[8];                                /* +0x20 */
        int worldZ = items[9];                                /* +0x24 */

        int rot1 = (g_sinTable[phase2] >> 6) & 0xFFF;         /* 0x4614F4-0x461502: rotation angle */
        int rot2 = (g_sinTable[phase1] >> 6) & 0xFFF;         /* 0x461511-0x461523: secondary rotation */
        int bob = g_sinTable[phase3] >> 10;                 /* 0x461517-0x461520: Y bob offset */

        int posX = items[0];                                  /* +0x00 */
        int posY = items[1] + bob;                            /* +0x04 + bob */
        int posZ = items[2];                                  /* +0x08 */

        RenderBalloonModelForRace(posX, posY, posZ, rot2,           /* 0x46152D */
                            rot1, 0, worldX, worldY, worldZ);

        /* Original stored pointer at items[10]; 64-bit uses side table */
        int *vtxBuf = g_collectVertexPtrs[i];
        if (vtxBuf != NULL) {
            SubmitGroundShadow((intptr_t)vtxBuf);          /* 0x461535 */
        }
    }
}

/**
 * AnimateTrackObjects_Island — 0x0047E00C — 223 bytes
 * Updates sine-driven rotation on Island's windmills and objects.
 */
void AnimateTrackObjects_Island(void)
{
    char *obj = (char *)g_objectStructArray;
    /* Two windmill objects: rotation from anim angles */
    *(unsigned short *)(obj + 0x9FBE) = (unsigned short)g_menuExtraY;        /* 0x47E01B */
    *(unsigned short *)(obj + 0x84EA) = (unsigned short)g_menuScrollTarget;  /* 0x47E028 */

    /* Sine-driven bobbing height + rotation from anim angle 6 */
    *(int *)(obj + 0x85C0) =                                                 /* 0x47E04B */
        g_animBobBaseY + (g_sinTable[g_menuCursor & 0xFFF] >> 10);
    int halfAngle = g_menuCursor;
    int s = halfAngle >> 31;                                                 /* sign */
    halfAngle = (halfAngle - s) >> 1;                                        /* abs/2 */
    *(unsigned short *)(obj + 0x85B6) =                                      /* 0x47E06E */
        (unsigned short)((g_sinTable[halfAngle] >> 6) & 0xFFF);

    /* 5 propeller/windmill rotations from g_animAngle3 — 0x47E075 */
    unsigned short angle3 = (unsigned short)g_animStateAC;
    *(unsigned short *)(obj + 0xA112) = angle3;                              /* 0x47E07B */
    *(unsigned short *)(obj + 0xA156) = angle3;                              /* 0x47E082 */
    *(unsigned short *)(obj + 0x8572) = angle3;                              /* 0x47E089 */
    *(unsigned short *)(obj + 0xA08A) = angle3;                              /* 0x47E090 */
    *(unsigned short *)(obj + 0xA0CE) = angle3;                              /* 0x47E097 */

    /* 1 rotation from g_animAngle2 — 0x47E09E */
    *(unsigned short *)(obj + 0x8462) = (unsigned short)g_animStateA8; /* 0x47E0A4 */

    /* Advance rotation counter (+0x333/frame, wrap at 0xFFF) — 0x47E0AB */
    g_animStateE0 += 0x333;                                                  /* 0x47E0B1 */
    if (g_animStateE0 > 0xFFE) {                                             /* 0x47E0BD */
        g_animStateE0 = 0;
    }
    /* 2 objects from rotation counter — 0x47E0CD */
    *(unsigned short *)(obj + 0x9F7A) = (unsigned short)g_animStateE0;       /* 0x47E0D9 */
    *(unsigned short *)(obj + 0x84A6) = (unsigned short)g_animStateE0;       /* 0x47E0E0 */
}

/**
 * AnimateTrackObjects_City — 0x0047C9BC — 181 bytes
 * Copies per-frame animation angles to object rotation fields (+0x1A = roll).
 * Animation angles are updated by TickAnimation; this function propagates
 * them to the actual object struct fields that RenderTrackD3D mode 2 reads.
 *
 * Also computes a sinusoidal bob for object 899's pivot Y and a sine-derived
 * roll angle for the same object.
 *
 * Object mapping (all field +0x1A = roll, except obj 899 which also gets +0x24):
 *   obj[992].roll = animAngle1 (0x925290)
 *   obj[991].roll = animAngle2 (0x925298)
 *   obj[707].roll = animAngle3 (0x9252A0)
 *   obj[899].pivY = animAngle5 base + sin(animAngle6) oscillation
 *   obj[899].roll = sin(animAngle6/2) derived
 *   obj[1015,1014,1013,1012,898].roll = animAngle4 (0x9252AC)
 *   obj[897,896].roll = animAngle5 (0x9252A8)
 */
void AnimateTrackObjects_City(void)
{
    char *obj = (char *)g_objectStructArray;                     /* 0x47C9BE: EBX */

    /* Propeller/fan rotations — copy anim angles to object roll fields */
    *(unsigned short *)(obj + 0x1079A) = (unsigned short)g_menuExtraY;        /* 0x47C9CA: obj[992]+0x1A */
    *(unsigned short *)(obj + 0x10756) = (unsigned short)g_menuScrollTarget;  /* 0x47C9D7: obj[991]+0x1A */
    *(unsigned short *)(obj + 0xBBE6)  = (unsigned short)g_animStateA0; /* 0x47C9E4: obj[707]+0x1A */

    /* unknown animated object bob: obj[899].pivotY = base + sin(angle6) >> 10 (0x47C9EB-0x47CA07) */
    int sinVal = g_sinTable[g_menuCursor & 0xFFF] >> 10;         /* 0x47C9F5-0x47CA02 */
    *(int *)(obj + 0xEEF0) = g_animBobBaseY + sinVal;            /* 0x47CA07: obj[899]+0x24 */

    /* unknown animated object roll: obj[899].roll = sin(angle6/2) >> 6, masked to 12 bits (0x47CA0D-0x47CA2A) */
    int halfAngle = g_menuCursor;                                /* 0x47CA0D */
    /* Signed divide by 2 (0x47CA12-0x47CA19) */
    int sign = halfAngle >> 31;
    halfAngle = (halfAngle - sign) >> 1;
    int rollVal = (g_sinTable[halfAngle] >> 6) & 0xFFF;          /* 0x47CA1B-0x47CA25 */
    *(unsigned short *)(obj + 0xEEE6) = (unsigned short)rollVal; /* 0x47CA2A: obj[899]+0x1A */

    /* Rotating signs group: 5 objects share animAngle4 (0x47CA31-0x47CA53) */
    unsigned short angle4 = (unsigned short)g_animStateAC;       /* 0x47CA31 */
    *(unsigned short *)(obj + 0x10DB6) = angle4;                 /* obj[1015]+0x1A */
    *(unsigned short *)(obj + 0x10D72) = angle4;                 /* obj[1014]+0x1A */
    *(unsigned short *)(obj + 0x10D2E) = angle4;                 /* obj[1013]+0x1A */
    *(unsigned short *)(obj + 0x10CEA) = angle4;                 /* obj[1012]+0x1A */
    *(unsigned short *)(obj + 0xEEA2)  = angle4;                 /* obj[898]+0x1A */

    /* Rotating signs group: 2 objects share animAngle5 (0x47CA5A-0x47CA67) */
    unsigned short angle5 = (unsigned short)g_animStateA8; /* 0x47CA5A */
    *(unsigned short *)(obj + 0xEE5E) = angle5;                  /* obj[897]+0x1A */
    *(unsigned short *)(obj + 0xEE1A) = angle5;                  /* obj[896]+0x1A */
}

/**
 * AnimateTrackObjects_Ruin — 0x00479F44 — 181 bytes
 * Copies animation angles to object rotation/position fields for Regal Ruin.
 * Binary dispatch 0x47FBE8 `cmp trackId,4` -> here: 4 = Ruin in binary
 * convention, TRACK_REGAL_RUIN (3) in ours — name and dispatch are correct.
 * Same pattern as AnimateTrackObjects_City/Factory: angle→roll, plus sine bob+roll.
 */
void AnimateTrackObjects_Ruin(void)
{
    char *obj = (char *)g_objectStructArray;                    /* 0x479F46: EBX */

    /* Individual rotations (0x479F4C-0x479F6C) */
    *(unsigned short *)(obj + 0xCF4A) = (unsigned short)g_menuExtraY;         /* obj[756]+0x1A */
    *(unsigned short *)(obj + 0x104AE) = (unsigned short)g_menuScrollTarget;  /* obj[951]+0x1A */
    *(unsigned short *)(obj + 0x104F2) = (unsigned short)g_animStateA0; /* obj[952]+0x1A */

    /* Group rotation from animAngle4 (0x479F73-0x479F95) */
    unsigned short angle4 = (unsigned short)g_animStateAC;
    *(unsigned short *)(obj + 0xD016) = angle4;                 /* obj[758]+0x1A */
    *(unsigned short *)(obj + 0x10646) = angle4;                /* obj[955]+0x1A */
    *(unsigned short *)(obj + 0x1068A) = angle4;                /* obj[956]+0x1A */
    *(unsigned short *)(obj + 0x106CE) = angle4;                /* obj[957]+0x1A */
    *(unsigned short *)(obj + 0x10712) = angle4;                /* obj[958]+0x1A */

    /* 2 objects from animAngle5 (0x479F9C-0x479FA9) */
    unsigned short angle5 = (unsigned short)g_animStateA8;
    *(unsigned short *)(obj + 0xD6BA) = angle5;                 /* obj[783]+0x1A */
    *(unsigned short *)(obj + 0xD676) = angle5;                 /* obj[782]+0x1A */

    /* Sine bob: obj pivotY = base + sin(angle6) >> 10 (0x479FB0-0x479FCC) */
    int sinVal = g_sinTable[g_menuCursor & 0xFFF] >> 10;
    *(int *)(obj + 0xD064) = g_animBobBaseY + sinVal;           /* obj[759]+0x24 */

    /* Sine roll: sin(angle6/2) >> 6 masked (0x479FD2-0x479FEF) */
    int halfAngle = g_menuCursor;
    int s = halfAngle >> 31;
    halfAngle = (halfAngle - s) >> 1;
    unsigned short rollVal = (unsigned short)((g_sinTable[halfAngle] >> 6) & 0xFFF);
    *(unsigned short *)(obj + 0xD05A) = rollVal;                /* obj[759]+0x1A */
}

/**
 * AnimateTrackObjects_Factory — 0x0047B9C8 — 181 bytes
 * Copies animation angles to object rotation/position fields for Reactive Factory.
 * Binary dispatch 0x47FBF4 `cmp trackId,3` -> here: 3 = Factory in binary
 * convention, TRACK_REACTIVE_FACTORY (4) in ours — name and dispatch are correct.
 * Same pattern as City/Ruin: angle→roll, plus sine bob+roll.
 */
void AnimateTrackObjects_Factory(void)
{
    char *obj = (char *)g_objectStructArray;                    /* 0x47B9CA: EBX */

    /* Individual rotations (0x47B9D0-0x47B9F0) */
    *(unsigned short *)(obj + 0xCEC2) = (unsigned short)g_menuExtraY;         /* obj[0x30A]+0x1A */
    *(unsigned short *)(obj + 0x10426) = (unsigned short)g_menuScrollTarget;  /* obj[0x3D3]+0x1A */
    *(unsigned short *)(obj + 0x1046A) = (unsigned short)g_animStateA0; /* obj[0x3D4]+0x1A */

    /* Sine bob: obj pivotY = base + sin(angle6) >> 10 (0x47B9F7-0x47BA13) */
    int sinVal = g_sinTable[g_menuCursor & 0xFFF] >> 10;
    *(int *)(obj + 0xCF54) = g_animBobBaseY + sinVal;           /* obj[0x30C]+0x24 */

    /* Sine roll: sin(angle6/2) >> 6 masked (0x47BA19-0x47BA36) */
    int halfAngle = g_menuCursor;
    int s = halfAngle >> 31;
    halfAngle = (halfAngle - s) >> 1;
    unsigned short rollVal = (unsigned short)((g_sinTable[halfAngle] >> 6) & 0xFFF);
    *(unsigned short *)(obj + 0xCF4A) = rollVal;                /* obj[0x30C]+0x1A */

    /* Group rotation from animAngle4 (0x47BA3D-0x47BA5F) */
    unsigned short angle4 = (unsigned short)g_animStateAC;
    *(unsigned short *)(obj + 0xCF06) = angle4;                 /* obj[0x30B]+0x1A */
    *(unsigned short *)(obj + 0x104AE) = angle4;                /* obj[0x3D5]+0x1A */
    *(unsigned short *)(obj + 0x104F2) = angle4;                /* obj[0x3D6]+0x1A */
    *(unsigned short *)(obj + 0x10536) = angle4;                /* obj[0x3D7]+0x1A */
    *(unsigned short *)(obj + 0x1057A) = angle4;                /* obj[0x3D8]+0x1A */

    /* 2 objects from animAngle5 (0x47BA66-0x47BA73) */
    unsigned short angle5 = (unsigned short)g_animStateA8;
    *(unsigned short *)(obj + 0xCF8E) = angle5;                 /* obj[0x30D]+0x1A */
    *(unsigned short *)(obj + 0xCE7E) = angle5;                 /* obj[0x309]+0x1A */
}

/**
 * AnimateTrackObjects_Emerald — 0x004797A0 — 111 bytes
 * Bobs and rolls the "GO!" box above the boost pad (object 830). Its resting height
 * is g_animBobBaseY, taken from the track data by InitAnimEmerald.
 */
void AnimateTrackObjects_Emerald(void)
{
    int *obj = (int *)g_objectStructArray;

    g_animState310++;                                            /* 0x4797A4-0x4797AB */
    if (g_animState310 == 100) {                                 /* 0x4797B1 */
        g_animState310 = 0;                                      /* 0x4797B8 */
    }

    /* Sine-driven bobbing around the resting height — 0x4797BE-0x4797E0 */
    *(int *)((char *)obj + 0xDC9C) =
        g_animBobBaseY + (g_sinTable[g_menuCursor & 0xFFF] >> 10);

    /* Rotation from half-speed sine */
    *(unsigned short *)((char *)obj + 0xDC92) =
        (unsigned short)((g_sinTable[(g_menuCursor / 2)] >> 6) & 0xFFF);
}

/* =====================================================================
 * Track animation helpers
 * ===================================================================== */

/**
 * AnimateTrackGeometry — 0x0047EF88 — 436 bytes
 * Two-pass vertex animation: cyclic offsets (pass 1) plus
 * one-shot opening offsets (pass 2).
 *
 * EAX = groupIndex — selects control struct from g_geometryAnimCtrl
 * EDX = statePtr   — pointer to int counter (0 on first frame)
 *
 * Control struct layout (5 ints, stride 20 bytes):
 *   [0] = pass1 start index in g_animRegTable
 *   [1] = pass1 count
 *   [2] = pass2 start index
 *   [3] = pass2 count
 *   [4] = reserved
 *
 * Each g_animRegTable entry (10 intptr_t, 40 bytes on 32-bit):
 *   [0]: pointer to target vertex data (4 XY pairs = 8 ints)
 *   [1]-[8]: base offsets added to vertex X/Y coordinates
 *   [9]: pointer to animDesc
 */
void AnimateTrackGeometry(int groupIndex, int *statePtr)        /* EAX, EDX */
{
    int *ctrlPtr = &g_geometryAnimCtrl[groupIndex * 5];

    /* Check if item animation has already changed — skip if so */
    /* Binary 0x47EFAB: g_itemStateTable + groupIndex*12 + 6, >> 16 */
    int flag = *(short *)((char *)g_itemStateTable + groupIndex * 12 + 8);
    if (flag != 0) {
        return;
    }

    /* First frame: play opening sounds */
    if (*statePtr == 0) {                                       /* 0x47EFC8 */
        PlaySoundEffect(0x21, 0, 0);                                  /* 0x47EFDA: EAX=0x21, EBX=0x32, ECX=0 */
        PlaySoundEffect(0x1A, 0, 0);                                  /* 0x47EFED: EAX=0x1A, EBX=0x32, ECX=0 */
    }

    /* Pass 1: cyclic vertex animation (every 4th frame) */
    if ((g_animFrameCounter & 3) == 0) {                        /* 0x47EFF2 */
        int phase = g_animFrameCounter / 4;
        int offsetX = s_animOffPass1[phase][0];                 /* 0x47F014: [eax*8 + 0x4FF598] */
        int offsetY = s_animOffPass1[phase][1];                 /* 0x47F01D: [eax*8 + 0x4FF59C] */
        int startIdx = ctrlPtr[0];
        int count = ctrlPtr[1];
        intptr_t *entry = g_animRegTable + startIdx * 10;

        for (int i = 0; i < count; i++) {                           /* 0x47F041..0x47F08E */
            int *target = (int *)entry[0];
            target[0] = (int)entry[1] + offsetX;
            target[1] = (int)entry[2] + offsetY;
            target[2] = (int)entry[3] + offsetX;
            target[3] = (int)entry[4] + offsetY;
            target[4] = (int)entry[5] + offsetX;
            target[5] = (int)entry[6] + offsetY;
            target[6] = (int)entry[7] + offsetX;
            target[7] = (int)entry[8] + offsetY;
            entry += 10;
        }
    }

    /* Pass 2: one-shot opening animation (even frames only, 3 steps) */
    if (g_raceOrder[2] != 0) {
        return;                                                 /* 0x47F090: [0x902078] = frame parity toggle, not the ghost flag (0x8FB960) */
    }
    if (*statePtr >= 3) {
        return;                                                 /* 0x47F0A2 */
    }
    int phaseIdx = *statePtr + 1;                               /* 0x47F0AB */
    int offsetX = s_animOffPass2[phaseIdx][0];                  /* [ebx*8 + 0x4FF578] */
    int offsetY = s_animOffPass2[phaseIdx][1];                  /* [ebx*8 + 0x4FF57C] */
    int startIdx = ctrlPtr[2];
    int count = ctrlPtr[3];
    intptr_t *entry = g_animRegTable + startIdx * 10;

    for (int i = 0; i < count; i++) {                           /* 0x47F0DF..0x47F12C */
        int *target = (int *)entry[0];
        target[0] = (int)entry[1] + offsetX;
        target[1] = (int)entry[2] + offsetY;
        target[2] = (int)entry[3] + offsetX;
        target[3] = (int)entry[4] + offsetY;
        target[4] = (int)entry[5] + offsetX;
        target[5] = (int)entry[6] + offsetY;
        target[6] = (int)entry[7] + offsetX;
        target[7] = (int)entry[8] + offsetY;
        entry += 10;
    }

    (*statePtr)++;                                              /* 0x47F131 */
}

/**
 * ProcessTrackTriggers — 0x0047F13C — 189 bytes
 * Walks a linked list of trigger objects and checks distance to player.
 * Returns trigger type if within radius, 0 if no trigger hit.
 *
 * Trigger list entry: [0]=objIndex, [1]=radius, [2]=2D/3D flag,
 *                     [3]=type, [4]=next link
 */
int ProcessTrackTriggers(int *triggerList, int playerX, int playerY, int playerZ)
{
    while (triggerList[0] != -1) {
        char *objPtr = (char *)g_objectStructArray + triggerList[0] * 0x44;

        /* Skip if already collected */
        if (*(short *)(objPtr + 0x2C) != -1) {
            int dx, dy, dz;
            unsigned int distSq;

            dx = *(int *)(objPtr + 0x20) - playerX;
            dy = *(int *)(objPtr + 0x24) - playerY;
            dz = *(int *)(objPtr + 0x28) - playerZ;

            /* 3D distance */
            if (triggerList[2] == 0) {
                distSq = (unsigned int)(dx * dx + dy * dy + dz * dz);
            }
            /* 2D distance (XZ only) */
            else {
                distSq = (unsigned int)(dx * dx + dz * dz);
            }

            if (distSq < (unsigned int)(triggerList[1] * triggerList[1])) {
                g_triggeredObjectPtr = objPtr;
                return triggerList[3];                          /* trigger type */
            }
        }

        triggerList += 4;                                       /* next entry (original uses [4] as link) */
    }
    return 0;
}

/**
 * ProcessTrackTriggerResponse — 0x0047F1FC — 1389 bytes
 * Checks triggers for one player, then processes the response:
 * ring/token collection, door opening, chaos emerald pickup.
 *
 * Binary fastcall: EAX=triggerTable, EDX=viewportIdx, EBX=modeFlag
 *   modeFlag: 0 = primary player, 1 = secondary local, 2 = AI/remote
 */
void ProcessTrackTriggerResponse(int *triggerTable, int vpIdx, int modeFlag)
{
    /* Player struct: base 0x8FD4F4, stride 0x71C (1820 bytes) */
    Player *player = &((Player *)g_playerBase)[vpIdx];           /* 0x47F20D-0x47F225 */
    int playerZ = player->posZ >> 12;                            /* 0x47F227 */
    short charId = player->charId;                               /* 0x47F22D */
    int charHeight = g_modelMeta[charId].charHeight;             /* 0x47F243 */
    int playerY = (charHeight - player->posY) >> 12;             /* 0x47F24B */
    int playerX = player->posX >> 12;                            /* 0x47F252 */

    int trigType = ProcessTrackTriggers(triggerTable, playerX, playerY, playerZ); /* 0x47F255 */

    /* Type 5: Ring/token collection (0x47F268-0x47F30A) */
    if (modeFlag < 2 && trigType == 5) {                        /* 0x47F25F,0x47F268 */
        char *obj = (char *)g_triggeredObjectPtr;
        if (*(short *)(obj + 0x2C) >= 0) {                      /* 0x47F276: not already collected */
            *(short *)(obj + 0x2C) = (short)0xFFFF;             /* mark invisible */

            if (modeFlag == 0) {                                /* 0x47F28A: player 1 */
                g_p1CollectionCount++;                          /* 0x47F294 */
                if (g_p1CollectionCount == trigType) {          /* 0x47F29B: count == 5 fanfare */
                    PlaySoundEffect(0x23, 0, 0);
                } else {
                    player->sfxTrigger = 0x1B;                  /* ring collect SFX */
                }
            } else {                                            /* player 2 */
                g_p2CollectionCount++;                          /* 0x47F2B6 */
                if (g_p2CollectionCount == trigType) {
                    PlaySoundEffect(0x23, 0, 0);
                } else {
                    player->sfxTrigger = 0x1B;
                }
            }

            /* Set collect effect position from triggered object */
            g_effectPosItemBurst[0] = *(int *)(obj + 0x20) << 8;    /* 0x47F2E3 */
            g_effectPosItemBurst[1] = -(*(int *)(obj + 0x24)) << 8; /* 0x47F2EF (neg then <<8) */
            g_effectPosItemBurst[2] = *(int *)(obj + 0x28) << 8;    /* 0x47F2FA */
            player->renderState = 0x30001;                      /* 0x47F2FA: visual state */
        }
    }

    /* Type 6: Chaos emerald (0x47F30F-0x47F373) */
    if (trigType == 6) {
        char *obj = (char *)g_triggeredObjectPtr;
        if (*(short *)(obj + 0x2C) >= 0) {
            player->sfxTrigger = 0x1C;                          /* emerald SFX */
            g_effectPosItemBurst[0] = *(int *)(obj + 0x20) << 8;
            int ey = *(int *)(obj + 0x24) + 0x32;               /* +50 height offset */
            *(short *)(obj + 0x2C) = (short)0xFFFF;
            g_effectPosItemBurst[2] = *(int *)(obj + 0x28) << 8;
            player->renderState = 0x30001;
            g_effectPosItemBurst[1] = (-ey) << 8;
            g_raceCheckpoint |= 1;                              /* 0x47F364 */
        }
    }

    /* Type 8: Special item (?) (0x47F373-0x47F3D7) */
    if (trigType == 8) {
        char *obj = (char *)g_triggeredObjectPtr;
        if (*(short *)(obj + 0x2C) >= 0) {
            player->sfxTrigger = 0x1C;
            g_effectPosItemBurst[0] = *(int *)(obj + 0x20) << 8;
            int ey = *(int *)(obj + 0x24) + 0x32;
            *(short *)(obj + 0x2C) = (short)0xFFFF;
            g_effectPosItemBurst[2] = *(int *)(obj + 0x28) << 8;
            player->renderState = 0x30001;
            g_effectPosItemBurst[1] = (-ey) << 8;
            g_raceCheckpoint |= 2;                              /* 0x47F3C9 */
        }
    }

    /* Type 7: balloons (0x47F3D7-0x47F47D) */
    if (trigType == 7 && g_raceSubMode == SUBMODE_BALLOON) {    /* 0x47F3D7,0x47F3E0 */
        char *obj = (char *)g_triggeredObjectPtr;
        if (*(short *)(obj + 0x2C) >= 0) {
            *(short *)(obj + 0x2C) = (short)0xFFFF;

            /* Increment per-player balloon count at player + 0x1F4 */
            player->collisionCount++;                           /* 0x47F41A-0x47F422 */
            if (player->collisionCount == 5) {
                PlaySoundEffect(0x23, 0, 0);                          /* fanfare at 5 baloons */
            } else {
                player->sfxTrigger = 0x16;                      /* balloon/token SFX */
            }

            g_effectPosItemBurst[0] = *(int *)(obj + 0x20) << 8;
            g_effectPosItemBurst[1] = -(*(int *)(obj + 0x24)) << 8;
            g_effectPosItemBurst[2] = *(int *)(obj + 0x28) << 8;
            player->renderState = 0x30001;
        }
    }

    /* Types 1-3: Animation triggers (0x47F47D-0x47F51B) */
    if (player->itemResponseTimer == 0) {                       /* 0x47F47D: P_INT(0x64)>>16 = itemResponseTimer */
        if (trigType == 1) {                                    /* 0x47F48B */
            g_menuScrollX = 0x100;                              /* 0x47F49E: bump step counter */
            player->itemResponseTimer = 0x5A;
            if (modeFlag < 2) {
                player->sfxTrigger = 0x1A;                      /* door SFX */
            }
            player->renderState = 0x30001;
        }
        if (trigType == 2) {                                    /* 0x47F4BB */
            g_menuMaxScroll = 0x100;                            /* 0x47F4CE */
            player->itemResponseTimer = 0x5A;
            if (modeFlag < 2) {
                player->sfxTrigger = 0x1A;
            }
            player->renderState = 0x30001;
        }
        if (trigType == 3) {                                    /* 0x47F4EB */
            g_animStateA4 = 0x100;                              /* 0x47F4FE */
            player->itemResponseTimer = 0x5A;
            if (modeFlag < 2) {
                player->sfxTrigger = 0x1A;
            }
            player->renderState = 0x30001;
        }
    }

    /* Type 4 - Gate activation (0x47F51B-0x47F5AC) */
    if (player->_unk_0x78 == 0) {                               /* 0x47F51B: P_INT(0x76)>>16 = _unk_0x78 */
        int speed = player->ringCount;                          /* P_INT(0x16)>>16 = ringCount */
        if (speed > 0 &&                                        /* must be moving */
            player->groundedFlag != 0 &&                        /* P_INT(0x70)>>16 = groundedFlag */
            !(g_trackId == TRACK_REACTIVE_FACTORY && player->collisionLayer != 0) && /* not on alternate collision layer */
            trigType == 4)                                      /* must be type 4 */
        {
            player->_unk_0x78 = 3;                              /* gate timer = 3 */
            if (modeFlag < 2)
                player->sfxTrigger = 0x1F;                      /* gate SFX */
            if (speed < 0x32) {                                 /* 0x47F579 */
                short accel = player->ringCount;
                player->_unk_0x7C = (short)(accel * 3);         /* gate target from accel */
                player->ringCount = 0;
            } else {
                player->_unk_0x7C = 0x96;                       /* max gate target */
                player->ringCount -= 0x32;
            }
            player->renderState = 0x30005;                      /* visual state */
        }
    }

    /* ===== Gate physics update (0x47F5AC-0x47F6A1) ===== */
    int gateState = player->_unk_0x78;                        /* P_INT(0x76)>>16 = _unk_0x78 */

    if (gateState != 0 && g_gateWaypointPtr != NULL) {        /* 0x47F5AC: gate active */
        player->velZ = 0;                                    /* 0x47F5BA */
        int *wp = g_gateWaypointPtr;
        player->_unk_0x74 = (short)0xFFFF;                  /* mark gate visual */
        player->velX = 0;

        /* Apply waypoint XZ offsets to player position */
        int wpX = wp[gateState - 3] << 12;                  /* 0x47F5D9: [ptr + state*4 - 0xC] */
        player->posX += wpX;
        int wpZ = wp[gateState - 2] << 12;                  /* 0x47F5EC: [ptr + state*4 - 8] */
        player->posZ += wpZ;

        /* Rotation from waypoint */
        int wpRot = wp[gateState - 1];                      /* 0x47F60B: [ptr + state*4 - 4] */
        int facing = (0xFFF - wpRot + 0x800) & 0xFFF;       /* 0x47F610-0x47F617 */
        player->angleYaw = facing;

        /* Advance gate timer and phase */
        short gTimer = player->_unk_0x78 + 3;               /* 0x47F620 */
        player->_unk_0x78 = gTimer;
        short gPhase = player->_unk_0x7A + 1;               /* 0x47F62B */
        player->_unk_0x7A = gPhase;
        if (player->_unk_0x7A > 0xF) {
            player->_unk_0x7A = 0xF;
        }

        /* Check if gate animation complete */
        if (player->_unk_0x7C < player->_unk_0x78) {        /* 0x47F641 */
            /* Done: set velocity from sin/cos of facing angle */
            player->velX = g_sinTable[facing] << 3;         /* 0x47F64E */
            player->velZ = g_cosTable[facing] << 3;         /* 0x47F663 */
            player->_unk_0x78 = 0;                          /* reset timer */
            if (player->_unk_0x7A < 3) {                    /* 0x47F678 */
                player->_unk_0x7A = 0;
            }
        }
    } else {
        /* Gate inactive: decay phase (0x47F67F) */
        short phase = player->_unk_0x7A;
        if (phase != 0) {
            player->_unk_0x7A = phase - 3;                  /* 0x47F68A */
        }
        if (player->_unk_0x7A < 0) {                        /* 0x47F694 */
            player->_unk_0x7A = 0;
        }
    }

    /* Position history ring buffer (0x47F6A1-0x47F761) */
    if (player->_unk_0x78 == 0 && player->_unk_0x7A == 0) {
        return;                                                 /* 0x47F6A1-0x47F6AD: no gate activity */
    }

    /* Compute visual phase */
    short visPhase;
    if (player->_unk_0x78 != 0) {                               /* 0x47F6B3 */
        visPhase = (short)(player->_unk_0x78 / 3 - 2);          /* 0x47F6CA: P_INT(0x76)>>16 = _unk_0x78 */
    } else {
        visPhase = player->_unk_0x7A;                           /* 0x47F6D1 */
    }
    player->_unk_0x1E6 = visPhase;                              /* 0x47F6D5 */
    if (player->_unk_0x1E6 > 0xF) {
        player->_unk_0x1E6 = 0xF;
    }

    /* Ring buffer: shift entries up, write current pos at entry[0].
     * Binary: eax starts at buf (entry[histPhase]), loop decrements eax
     * by 0x80 for histPhase iterations, so after loop eax = base (entry[0]).
     * Position write goes to [eax+0x14] = entry[0]. */
    int histPhase = player->_unk_0x1E6;
    char *base = g_gateHistoryBuf + vpIdx * 0x800;
    char *buf = base + histPhase * 0x80;

    if (player->_unk_0x1E6 != 0 && histPhase >= 1) {            /* 0x47F70E */
        int k;
        char *p = buf;
        for (k = histPhase; k >= 1; k--) {                      /* 0x47F71F-0x47F743 */
            p -= 0x80;
            *(int *)(p + 0x94) = *(int *)(p + 0x14);            /* copy X up */
            *(int *)(p + 0x98) = *(int *)(p + 0x18);            /* copy Y up */
            *(int *)(p + 0x9C) = *(int *)(p + 0x1C);            /* copy Z up */
        }
    }

    /* Write current player position at entry[0] (head) */
    *(int *)(base + 0x14) = player->posX >> 12;                 /*  X >> 12 */
    *(int *)(base + 0x18) = -(player->posY) >> 12;              /* -Y >> 12 */
    *(int *)(base + 0x1C) = player->posZ >> 12;                 /*  Z >> 12 */
}

/**
 * CheckCollectiblesAllPlayers — 0x0047F76C — 184 bytes
 * Iterates players/viewports and calls ProcessCollectibleForPlayer (0x47f1fc)
 * for each, passing the per-track descriptor table.
 */
void CheckCollectiblesAllPlayers(int *table)
{
    /* First call: player 0, mode 0 */                          /* 0x47F772-0x47F776 */
    ProcessTrackTriggerResponse(table, 0, 0);

    if (g_numHumans != 1) {
        goto multiHuman;                                        /* 0x47F781-0x47F784 */
    }

    if (g_netSessionActive != 0) {
        goto multiHuman;                                        /* 0x47F786-0x47F78D */
    }

    /* Single player, non-network — AI trigger processing */
    if (g_raceType > RACE_TIMEATTACK) {                         /* 0x47F795-0x47F798 */
        ProcessTrackTriggerResponse(table, g_numHumans, 2);     /* 0x47F79F: EDX=numHumans */
    }
    /* GP mode: AI players 1-4 */
    else if (g_raceType == RACE_GP) {                           /* 0x47F7A8 */
        ProcessTrackTriggerResponse(table, g_numHumans, 2);     /* 0x47F7B1 */
        ProcessTrackTriggerResponse(table, 2, 2);               /* 0x47F7C1 */
        ProcessTrackTriggerResponse(table, 3, 2);               /* 0x47F7D2 */
        ProcessTrackTriggerResponse(table, 4, 2);               /* 0x47F7E3 */
    }
    /* raceType 1 or 2: no additional calls */
    goto done;

multiHuman:
    /* Multi-human or network: other local players with mode 1 */
    for (int p = 1; p < g_numPlayers; p++) {                    /* 0x47F7EA-0x47F80E */
        ProcessTrackTriggerResponse(table, p, 1);
    }

done:
    if (g_raceSubMode == SUBMODE_BALLOON) {                     /* 0x47F810 */
        CollectiblePickupCheck();                               /* 0x47F819 */
    }
}
