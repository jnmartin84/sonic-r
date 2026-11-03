/**
 * weather.c — Per-track weather particle functions
 *
 * TickIslandParticles, TickFactoryParticles, TickEmeraldParticles — per-track particle
 * spawning and ticking. See Weather_annotated.c for documentation.
 */

#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "sonicr_functions.h"
#include "r_types.h"
#include "r_state.h"
#include "r_draw.h"

#define MAX_PARTICLES       64

OtherParticle g_particleArray[MAX_PARTICLES]; /* 0x00673DB0 */
int g_nextParticleSlot;                                /* 0x00674EB4 — dual use: next slot index / static particle count */
/* g_islandStaticParticleCount aliases g_nextParticleSlot: same address, counts static rain particles */
#define g_islandStaticParticleCount g_nextParticleSlot
int g_particleSlotB;                                   /* 0x00674EB0 — TickFactoryParticles spawner slot */
int g_particleTableIdx;                                 /* 0x00674ED4 — current position table index */

/* Per-track static particle spawn positions (6 entries × 3 ints) — ROM at 0x4FCA4C */
/* Extracted from ROM at 0x4FCA4C */
static const int s_islandSpawnPos[6 * 3] = {
    -0x1B09, 0x34F, 0xAAB,
    -0x141,  0x26C, 0x1194,
     0x216E, 0x3AE, 0x21F1,
     0x173A, 0x244, -0x9D3,
     0xC2A,  0x346, 0x2C,
    -0x119B, 0x32D, -0xFEF,
};

static const unsigned char s_gameModeWeatherSet[5] = { 0, 1, 3, 2, 4 }; /* ROM 0x51E07C */

static const unsigned char s_weatherRGB[][27] = { /* ROM 0x4FF100 */
    {238,191,135, 68,117,254, 186,205,206, 226,154,185, 153,186,163, 34,58,43, 229,178,77, 122,150,180, 31,39,75},
    {218,137,82, 217,205,236, 45,79,77, 176,104,125, 146,152,184, 108,104,124, 203,128,103, 255,251,255, 6,8,83},
    {255,123,33, 199,224,243, 63,77,80, 185,113,67, 158,125,126, 121,105,117, 255,192,149, 229,203,195, 54,39,64},
    {255,197,111, 99,179,254, 19,46,11, 81,60,91, 234,228,204, 65,64,109, 154,121,153, 186,220,249, 48,46,22},
    {128,128,128, 0,84,10, 120,0,40, 6,80,0, 8,12,84, 0,20,8, 232,0,180, 7,96,0, 124,11,100},
};

static const short s_weatherPhase[][18] = { /* ROM 0x4FF170 */
    {2644,120, 1576,80, 3080,84, 2068,232, 1972,96, 2940,100, 281,208, 1031,121, 2962,59},
    {400,168, 928,100, 2924,76, 412,180, 882,73, 2928,60, 1738,101, 1804,83, 617,71},
    {1662,224, 1607,71, 1474,92, 1648,215, 744,72, 2026,90, 1614,231, 1655,95, 1681,107},
    {2104,204, 1664,80, 1648,116, 3113,199, 2188,104, 2036,112, 1244,152, 1960,47, 1103,107},
    {832,82, 0,0, 1,0, 2,0, 3,0, 4,0, 5,0, 6,0, 7,0},
};

/* =====================================================================
 * Weather particle spawning + per-track lighting
 * ===================================================================== */

extern int g_renderStateBlock[16];          /* 0x008F6F60 — canonical in globals_extra.c */
#define g_particleLifetime g_renderStateBlock
#define g_timeVariant1 g_weatherType        /* 0x0094BCF4 — same variable */
extern int g_lightingParam2;                /* 0x0094D93C */

/* Particle system state — 16 slots */
static int g_particleFrameIdx;              /* 0x008F6C5C — cycles 0-15 */
GroundParticle g_particleSlots[16];         /* 0x008F6C60 */
int g_particleAge[16];                      /* 0x008F6FA0 — per-slot age counter */
static int g_particleMaxAge[16];            /* 0x008F6FE0 — per-slot max age */
int g_particleOwner[16];                    /* 0x008F7020 — viewport that owns this slot */

extern unsigned short *g_randomStream;

/* ROM position+velocity table at 0x4FCA94 — 5 active entries × 6 ints (24 bytes).
 * Entries 5-29 are adjacent ROM data (not positions), yielding off-screen particles. */
static const int s_factoryParticleTable[30 * 6] = {
      -5572,     521,    -499,     -40,       0,       0,  /* entry  0 */
      -4576,     400,    1035,       0,       0,     -40,  /* entry  1 */
     -10037,     150,     102,       0,      40,       0,  /* entry  2: steam pipe */
      -5575,     521,     -69,     -40,       0,       0,  /* entry  3 */
      -4152,     400,    1035,       0,       0,     -40,  /* entry  4 */
    /* entries 5-29: adjacent ROM data, not valid positions — particles spawn off-screen */
          0, 672686336, -139033031, 16250758, 538443897, -134744271,
      26880, 1895852288, 1497442304, 10354688, 1227948190, 538447969,
    2629648, 1229571584, 1633771849,  530489, 824180870, 1761634625,
    -150978304, 408025087, 269488128, 3750209, 1627418880, 1362700288,
    -1642061577, 958439070,   36425, 956831793, 1769042200, -269488279,
    -150994697, -1361637129, 9369368, -136904704, 271065238, 2655752,
      20736, 1366419423, 2038004215, 270027097, 1499011080, 406417753,
    -1106216554, 270532705, 14153480, 13553358, -1502480600,   10430,
    823662616, 674312497, 2648344, 1235091614, -677511168, 4784359,
    671113687, -1764874200, -141164544, 1627440790, 539517184, 676959768,
    -139546962, 1362165881, -1509949335, -1912581698, 1085070, -675899904,
    141105201, -973078528, 1364283648, -415680199, 1501954161, -1639905256,
    1105167945,    6201, 1504634758, -676950447, -1241458729,   61184,
    -135839391, 1493212903, 9363200, 1224781518,   38432, 6382014,
    1628989830, -1912072143, 676921352,     198, 7436544, -417857423,
    1632216823, 12976185, -1094813242, -1107273266, 1495318206, 139046582,
      16798, 1237192720,   42496, -1105710743, -1375719071, 277805031,
    -1762138137, 1899561191, -146730802, -553592954, 1907243414, 1364326201,
    676921433, -1496428272, -1499027968,  539030, -1627920450, 10942208,
    13027014, -1101463410, 2659929, 141642174, -1375731655, -549371648,
    4826697, 1226354176, 822094071, -135293351, 1374642614, -2038004074,
    -148373273, -539020866, -2033788961, 1371418977, -2033795361, 1375205160,
    1239368033, 676984649, 14677776, -147230440, 541686206, 831924240,
    -686809023, -1768554391, -1241467145, -149940026, 1909364430, 7462656,
    1359497585, 539008670, -140089056, 2039402126, -537953850, 828442822,
};

extern int Random(void);                          /* 0x4E1342 — 0..0x7FFF */

/**
 * InitParticleDisplayParams — FUN_0046E250 — 102 bytes
 * Initializes display properties for all 64 particles: sprite size, UV,
 * alpha, tpage. Called from TitleScreen.
 *
 * Per particle (stride 0x11 ints = 68 bytes):
 *   +0x2C (short at puVar1+0xb): width = 0x14 (20)
 *   +0x2E (short at byte+0x2e): height = 0x14 (20)
 *   +0x28 (short at puVar1+0xa): flags = 0x8020
 *   +0x38 (byte at puVar1+0xe): alpha = 0x80 (soft) or 0xC0 (D3D)
 *   +0x3C (byte at puVar1+0xf): 0
 *   +0x3E (short at byte+0x3e): UV size = 0x10
 *   +0x40 (short at puVar1+0x10): active = 1
 *   +0x2A (short at byte+0x2a): lifetime = 0
 *   +0x39 (byte at byte+0x39): UV base = 0xB0
 */
void InitParticleDisplayParams(void)
{
    for (int i = 0; i < MAX_PARTICLES; i++) {
        OtherParticle *p = &g_particleArray[i];
        p->width = 0x14;
        p->height = 0x14;
        p->flags = 0x8020;
        p->alpha = 0x80;
        p->tpage = 0;
        p->uvStep = 0x10;
        p->active = 1;
        p->lifetime = 0;
        p->uvBaseY = 0xB0;
    }
    g_nextParticleSlot = 0;
}

/* =====================================================================
 * InitSnowParticleSystem — 0x004dea50 — 232 bytes
 * Initializes 128 particle entries (stride 56 bytes) at 0x94bd1a area.
 * Clears velocity/position fields, sets random X velocity (rand/256),
 * size 0x20, color 0xE0 (or 0xD0/0x60 for display mode 2).
 * No parameters.
 * ===================================================================== */
void InitSnowParticleSystem(void)
{
    for (int i = 0; i < 128; i++) {
        PrecipParticle *p = &g_precipParticles[i];

        p->posX = 0;
        p->posY = 0;
        p->posZ = 0;
        p->targetX = 0;
        p->targetY = 0;
        p->targetZ = 0;
        p->velX = 0;
        p->velY = 0;
        p->velZ = 0;

        p->flags = (short)0x8020;
        p->age = (short)(Random() / 256);        /* random X velocity */
        p->sizeW = 8;
        p->sizeH = 8;

        /* Binary D3D path sets 0xE0/0xE0; software path (unused) sets 0xD0/0x60 */
        p->uvBaseY = 0xE0;
        p->uvBaseX = 0xE0;

        p->tpage = g_tpageParticle1;
        p->frameW = 0x20;                        /* uvSizeW */
        p->frameH = 0x20;                        /* uvSizeH */
        p->state = 1;                             /* active */
    }
}

/* =====================================================================
 * InitRainParticleSystem — 0x004ded7c — 238 bytes
 * Initializes 128 particle entries like InitSnowParticleSystem but with
 * different defaults: size 0x40, color 0x80/0xB0, velocity step 6,
 * gravity -0x1000. For rain/heavier particles.
 * No parameters.
 * ===================================================================== */
void InitRainParticleSystem(void)
{
    for (int i = 0; i < 128; i++) {
        PrecipParticle *p = &g_precipParticles[i];

        p->velY = -0x1000;                       /* gravity */

        p->posX = 0;
        p->posY = 0;
        p->posZ = 0;
        p->targetX = 0;
        p->targetY = 0;
        p->targetZ = 0;

        p->flags = (short)0x8020;
        p->age = (short)(Random() / 256);
        p->sizeW = 6;                            /* velocity step */
        p->sizeH = 6;

        /* Binary D3D path sets 0x80/0xB0 with size 0x40;
         * software path (unused) sets 0xD0/0xC0 with size 0x20 */
        p->uvBaseX = 0xD0;//0x80;
        p->uvBaseY = 0xC0;//0xB0;
        p->frameW = 0x20;//0x40;
        p->frameH = 0x20;//0x40;

        p->tpage = g_tpageParticle1;
        p->state = 1;                            /* active */
    }
}

/* =====================================================================
 * SpawnIslandParticle — FUN_0046E378 — 215 bytes
 * Initializes one rain/snow particle at a random offset from a
 * target position. Called by InitWeatherProps_Island.
 *
 * Original: EAX=particle ptr, EBX=targetX, ECX=targetY,
 *   stack[0]=targetZ, stack[1]=randomSeed
 * ===================================================================== */
static void SpawnIslandParticle(OtherParticle *p, int targetX, int targetY, int targetZ, int seed)
{
    int r;
    p->targetX = targetX;
    p->targetY = targetY;
    p->targetZ = targetZ;

    r = Random();
    p->posX = targetX + r * 3 - 0xBFFF;
    r = Random();
    p->posY = targetY + r * 3 - 0xBFFF;
    r = Random();
    p->posZ = targetZ + r * 3 - 0xBFFF;

    p->velX = 0;
    p->velY = 0;
    p->velZ = 0;
    p->gravity = -0x100;
    p->flags = 0x8000;
    p->lifetime = 1;
    p->animFrame = 0;
    p->animMax = 0xC0;
    p->frameDelay = 2;
    p->width = 0x20;
    p->height = 0x20;
    p->alpha = 0;
    p->uvStep = 0x20;
    p->frameTimer = p->frameDelay;
    p->active = 1;
    int sv = seed >> 31;
    int mod = (int)(((seed + sv * -0x2000) - (unsigned)((sv << 12) < 0)) >> 13);
    p->uvBaseY = (unsigned char)((mod << 5) - 0x80);
    p->tpage = g_tpageParticle2;
}

/* =====================================================================
 * InitWeatherProps_Island — 0x0046E450 — 564 bytes
 * Resort Island particle initialization.
 * Spawns "static" particles around 6 predefined positions (waterfalls etc.),
 * then fills remaining slots with random rain particles.
 * ===================================================================== */
void InitWeatherProps_Island(void)
{
    OtherParticle *part = g_particleArray;
    g_islandStaticParticleCount = 0;
    Random();

    /* spawn static particles around 6 predefined positions */
    for (int spawnIdx = 0; spawnIdx < 6; spawnIdx++) {
        int baseX = s_islandSpawnPos[spawnIdx * 3 + 0] << 8;
        int baseY = s_islandSpawnPos[spawnIdx * 3 + 1] << 8;
        int baseZ = s_islandSpawnPos[spawnIdx * 3 + 2];

        int count = Random() / 0x2AAB + 3;
        for (int j = 0; j < count; j++) {
            int seed = Random();
            SpawnIslandParticle(part, baseX, baseY, baseZ * 0x100, seed);
            part++;
            g_islandStaticParticleCount++;
        }
    }

    /* fill remaining slots (up to 64) with random rain particles */
    if ((g_raceType == RACE_TIMEATTACK && g_raceSubMode == SUBMODE_REVERSE) || g_islandStaticParticleCount >= 64) {
        return;
    }

    for (int slot = g_islandStaticParticleCount; slot < 64; slot++) {
        unsigned short rv;

        rv = *g_randomStream; g_randomStream++;
        part->posX = (0x1FC0 - (short)(rv & 0x3FFF)) * 0x10;

        part->posY = (short)*g_randomStream;
        g_randomStream += 2;
        rv = *(g_randomStream - 1);
        part->posZ = (-0x1068 - (short)(rv & 0xFF)) * 0x100;

        part->velX = (short)(*g_randomStream & 0x7F) - 0x1500;
        part->velY = (short)(g_randomStream[1] & 0x1FF) + 0x100;
        rv = g_randomStream[2];
        part->gravity = 3;
        part->flags = 0x8000;
        part->lifetime = 0x2EE;
        part->animFrame = 0;
        part->animMax = 0xC0;
        part->velZ = (short)(rv & 0x7F) * 0x14;
        rv = g_randomStream[3];
        part->width = 0x20;
        part->height = 0x20;
        part->alpha = 0;
        short frameDelay = (short)((rv & 1) + 1);
        g_randomStream += 4;
        part->frameDelay = frameDelay;
        part->frameTimer = frameDelay;

        part->uvStep = 0x20;
        part->active = 1;
        int rSeed = Random();
        int sv = rSeed >> 31;
        int mod = (int)(((rSeed + sv * -0x2000) - (unsigned)((sv << 12) < 0)) >> 13);
        part->uvBaseY = (unsigned char)((mod << 5) - 0x80);

        part->tpage = g_tpageParticle2;
        part++;
    }
}

/* =====================================================================
 * InitWeatherProps_Factory — 0x0046E820 — 27 bytes
 * Regal Ruin: set all 64 particle active flags to 1 (enables sandstorm).
 * ===================================================================== */
void InitWeatherProps_Factory(void)
{
    for (int i = 0; i < MAX_PARTICLES; i++) {
        g_particleArray[i].active = 1;
    }
}

/* =====================================================================
 * InitWeatherProps_Emerald — 0x0046E9E0 — 101 bytes
 * Radiant Emerald: configure all 64 particles with emerald-specific
 * display properties (larger sprites, different UV, alpha).
 * ===================================================================== */
void InitWeatherProps_Emerald(void)
{
    for (int i = 0; i < MAX_PARTICLES; i++) {
        OtherParticle *p = &g_particleArray[i];
        p->width = 0x14;
        p->height = 0x14;
        p->flags = 0x8020;
        p->alpha = 0;     /* 0x46ea30: cmp renderMode,2 — software→0, D3D→0x40.
                           * Sprite COLUMN BASE, not opacity: uvX = animFrame
                           * + alpha, four 16-texel frames per set. */
        p->uvStep = 0x10;
        p->active = 1;
        p->lifetime = 0;
        p->uvBaseY = 0x60;
        p->tpage = g_tpageParticle1;
    }
}

/**
 * UpdateParticleSpawning — FUN_0046E2B8 — 192 bytes
 * Spawns new particles per frame and updates particle animation state.
 */
void UpdateParticleSpawning(void)
{
    int slot = g_nextParticleSlot;
    OtherParticle *s = &g_particleArray[slot];
    int r1 = Random();
    s->posX = r1 * 3 - 0x132FE;
    int r2 = Random();
    s->posY = r2 * 2 - 0x3500;
    int r3 = Random();
    s->lifetime = 8;
    g_nextParticleSlot = (g_nextParticleSlot + 1) & 0x3F;
    s->posZ = r3 * 4 + 0x10000;

    for (int i = 0; i < MAX_PARTICLES; i++) {
        OtherParticle *p = &g_particleArray[i];
        if (p->lifetime != 0) {
            p->lifetime -= 1;
            int phase = p->lifetime;
            p->animFrame = (short)((3 - phase / 2) * 0x10);
        }
        p->uvX = (unsigned char)p->animFrame + p->alpha;
        p->uvY = p->uvBaseY;
    }
}

/**
 * TickParticleAnimation — shared animation frame advancement
 * Used by both loop 1 (rain) and loop 2 (static) in TickIslandParticles/Ruin.
 * Binary: 0x46E6E4-0x46E738 and 0x46E7A8-0x46E7ED (identical logic).
 */
static void TickParticleAnimation(OtherParticle *p)
{
    p->frameTimer -= 1;
    if (p->frameTimer == 0) {
        p->frameTimer = p->frameDelay;
        p->animFrame += p->uvStep;
        if (p->animFrame == p->animMax) {
            p->animFrame = 0;
        }
    }
    p->uvX = p->alpha + (unsigned char)p->animFrame;
    p->uvY = p->uvBaseY;
}

/**
 * TickIslandParticles — 0x0046E684 — 409 bytes
 * Resort Island per-frame particle update. Tick only — no spawning.
 *
 * Loop 1 (slots g_nextParticleSlot..63)
 * Loop 2 (slots 0..g_nextParticleSlot-1)
 */
void TickIslandParticles(void)
{
    int startSlot = g_nextParticleSlot;

    /* one loop is probably waterfall, other prob birds */

    for (int i = startSlot; i < MAX_PARTICLES; i++) {
        OtherParticle *p = &g_particleArray[i];

        if (p->active == 0) {
            continue;
        }
        if (p->lifetime == 0) {
            continue;
        }

        p->lifetime -= 1;
        p->posX += p->velX;
        p->velY += p->gravity;
        p->posY += p->velY;
        p->posZ += p->velZ;

        TickParticleAnimation(p);
    }

    for (int i = 0; i < startSlot; i++) {
        OtherParticle *p = &g_particleArray[i];

        if (p->targetX > p->posX) {
            p->velX += 0x100;
        }
        else {
            p->velX -= 0x100;
        }

        if (p->targetY > p->posY) {
            p->velY += 0x100;
        }
        else {
            p->velY -= 0x100;
        }

        if (p->targetZ > p->posZ) {
            p->velZ += 0x100;
        }
        else {
            p->velZ -= 0x100;
        }

        p->posX += p->velX;
        p->posY += p->velY;
        p->posZ += p->velZ;

        TickParticleAnimation(p);
    }
}

/**
 * TickFactoryParticles — 0x0046E83C — 418 bytes
 * Per-frame effect spawner + particle update.
 * Spawns 2 particles per frame from a 30-entry position/velocity table
 * (indexed by g_totalFrames/150 % 30 — 5-second cycle per entry).
 * Particles use tpage 3 with a 6×2 grid of 32×32 UV frames for animation.
 * Disasm verified with capstone at 0x46E83C-0x46E9DE.
 */
void TickFactoryParticles(void)
{
    /* Spawn 2 particles (0x46E848-0x46E953) */
    for (int i = 0; i < 2; i++) {
        OtherParticle *p = &g_particleArray[g_particleSlotB];

        /* Table index: (g_totalFrames % 150) / 30 → cycles 0-4 every 5 sec */
        int tableIdx = (g_totalFrames % 150) / 30;                   /* 0x46e84f-0x46e86a */
        g_particleTableIdx = tableIdx;                                 /* 0x46e86c */
        const int *entry = &s_factoryParticleTable[tableIdx * 6];        /* 0x46e871 */

        /* Position = table[i] << 8 + (Random() - 0x4000) */
        int r;
        r = Random();
        p->posX = (entry[0] << 8) + (r - 0x4000);                   /* 0x46e891 */
        r = Random();
        p->posY = (entry[1] << 8) + (r - 0x4000);                   /* 0x46e8a5 */
        r = Random();
        p->posZ = (entry[2] << 8) + (r - 0x4000);                   /* 0x46e8ba */

        /* Velocity = table[i+3] << 8 + (Random() / 15 - 0x444) */
        r = Random();
        p->velX = (entry[3] << 8) + (r / 15 - 0x444);               /* 0x46e8d6 */
        r = Random();
        p->velY = (entry[4] << 8) + (r / 15 - 0x444);               /* 0x46e8f2 */
        r = Random();
        p->velZ = (entry[5] << 8) + (r / 15 - 0x444);               /* 0x46e937 */

        /* Particle display properties */
        p->lifetime = 0xC;                                            /* 12 frames */
        p->frameDelay = 0;                                            /* frame counter = 0 */
        p->tpage = 3;
        p->flags = 0x8010;
        p->width = 0x40;                                              /* UV width = 64 */
        p->height = 0x40;                                             /* UV height = 64 */
        p->uvStep = 0x20;                                             /* anim step = 32 */

        /* Advance slot, wrap at 64 */
        g_particleSlotB = (g_particleSlotB + 1) & 0x3F;              /* 0x46e93c-0x46e94a */
    }

    /* Tick all 64 particles (0x46E959-0x46E9DC) */
    for (int i = 0; i < MAX_PARTICLES; i++) {
        OtherParticle *p = &g_particleArray[i];
        if (p->lifetime == 0) {
            continue; /* 0x46e97b */
        }

        p->lifetime -= 1; /* 0x46e980 */
        p->posX += p->velX;
        p->posY += p->velY;
        p->posZ += p->velZ;

        /* UV animation: 6×2 grid of 32×32 sprites starting at (64, 128) */
        int frame = p->frameDelay;
        p->uvX = (unsigned char)((frame % 6) * 32 + 0x40);
        p->uvY = (unsigned char)((frame / 6) * 32 + 0x80);

        p->frameDelay += 1;  /* 0x46e9d8 */
    }
}

/**
 * TickEmeraldParticles — 0x0046EA48 — 449 bytes
 * Radiant Emerald per-frame particle update. Single tick loop, no homing.
 * Particles drift with velocity, lifetime decrements, animation ping-pongs.
 */
void TickEmeraldParticles(void)
{
    /* --- Spawn phase (binary 0x46ea48-0x46eb37) ---
     * Each frame seed ONE sparkle at a racer's position plus random jitter, with
     * velocity = a fraction of that racer's velocity; racers are cycled by frame.
     * The binary's source "table" at 0x8fd4f4 is the player struct array. */
    int row = g_netSessionActive ? (int)g_localPlayerIndex
                              : (g_totalFrames % g_numViewports);
    Player *pl = &g_playerBase[row];
    OtherParticle *sp = &g_particleArray[g_totalFrames & 0x3F];
    sp->posX = (pl->posX >> 4) + (Random() * 4 - 0xFFFE);    /* 0x46eaaa */
    sp->posY = ((-pl->posY) >> 4) + (Random() * 2 - 0x7FFF); /* 0x46eac3 */
    sp->posZ = (pl->posZ >> 4) + (Random() * 4 - 0xFFFE);    /* 0x46ead9 */
    sp->velX = (pl->velX * 12) >> 8;                         /* 0x46eaf1: *3/64 */
    sp->velY = ((-pl->velY) * 12) >> 8;                      /* 0x46eb06 */
    sp->velZ = (pl->velZ * 12) >> 8;                         /* 0x46eb1d */
    sp->lifetime = 0xE;                                      /* 0x46eb2c */

    for (int i = 0; i < MAX_PARTICLES; i++) {
        OtherParticle *p = &g_particleArray[i];
        if (p->lifetime != 0) {
            p->posX += p->velX;
            p->lifetime -= 1;
            p->posZ += p->velZ;

            int halfLife = p->lifetime / 2;
            int frame;
            if (halfLife < 4) {
                frame = 3 - halfLife;
            }
            else {
                frame = halfLife - 3;
            }
            p->animFrame = (short)(frame << 4);

            p->posY += p->velY;
        }

        /* Binary 0x46eb51-0x46eb63: the renderer samples uvX/uvY, so map the
         * anim frame + alpha into uvX and uvBaseY into uvY every frame. Without
         * this, Emerald particles sample a stale UV (0,0) → wrong sprite (the
         * "shield quadrant"). alpha shifts the sprite column (D3D 0 vs sw 0x40). */
        p->uvX = (unsigned char)((p->animFrame & 0xFF) + p->alpha);
        p->uvY = p->uvBaseY;
    }
}

/**
 * DrawOtherParticles — 0x0046EF4C — 1252 bytes
 * D3D mode: transforms particles and submits as textured quads to the
 * per-tpage vertex/index buffers for batch drawing.
 * Uses the same 3x3 view matrix transform as the software path.
 */
void DrawOtherParticles(void)
{
    int farClipThresh = g_farClipDepth << 3;
    if (farClipThresh <= 0) {
        farClipThresh = 0x15800;
    }

    R_SetTexEnv(R_TEXENV_MODULATE);
    R_SetBlendMode(R_BLEND_ALPHA);

    for (int pi = 0; pi < MAX_PARTICLES; pi++) {
        OtherParticle *p = &g_particleArray[pi];
        /* Check: particle active, has lifetime, tpage ready */
        if (p->active == 0) {
            continue;
        }

        if (p->lifetime == 0) {
            continue;
        }

        unsigned char tpage = p->tpage;
        if (g_tpageStateArray[tpage] != 0x04) {
            continue;
        }

        /* Transform world position to camera space.
         * Binary: sar particle>>8, sub camera (no shift). (0x46ef8c-0x46efbb) */
        int dx = (p->posX >> 8) - g_camIntX;
        int dy = (p->posY >> 8) - g_camIntY;
        int dz = (p->posZ >> 8) - g_camIntZ;

        /* Forward (Z in camera space) */
        int camZ_raw = g_viewMtx22 * dz + g_viewMtx12 * dy + g_viewMtx02 * dx;
        int camZ = camZ_raw / 4096;

        if (camZ <= 0 || camZ > farClipThresh) {
            continue;
        }

        /* Right (X in camera space) */
        int camX_raw = g_viewMtx20 * dz + g_viewMtx00 * dx + g_viewMtx10 * dy;
        int camX = camX_raw / 4096;

        /* Up (Y in camera space) */
        int camY_raw = g_viewMtx21 * dz + dx * g_viewMtx01 + dy * g_viewMtx11;
        int camY = camY_raw / 4096;

        /* Quad half-size from particle data */
        int halfSize = p->width;
        int heightOff = p->height;

        /* Project to screen quad corners */
        int x0 = g_screenCenterX + ((camX - halfSize) * g_projScaleXCurrent) / camZ;
        int y0 = g_screenCenterY - ((camY + heightOff) * g_projScaleY) / camZ;
        int x1 = g_screenCenterX + ((camX + halfSize) * g_projScaleXCurrent) / camZ;
        int y1 = g_screenCenterY - ((camY - heightOff) * g_projScaleY) / camZ;

        /* Clip test */
        if (x0 > g_clipRight || x1 < g_clipLeft ||
            y0 > g_clipBottom || y1 < g_clipTop)
        {
            continue;
        }

        /* Alpha and blend mode from flags.
         * Bit 0x20: sparkle — additive blend, alpha 0xC0.
         * Bit 0x10: snow   — alpha blend, alpha 0x80.
         * Neither:  rain   — alpha blend, alpha 0xFF. */
        unsigned char flags = (unsigned char)p->flags;
        int alpha;
        if (flags & 0x20) {
            alpha = 0xFF;
            R_SetBlendMode(R_BLEND_ADDITIVE);
        }
        else {
            alpha = (flags & 0x10) ? 0x80 : 0xFF;
            R_SetBlendMode(R_BLEND_ALPHA);
        }

        /* Z and 1/Z */
        float fZ = (g_farClipFloat > 0.0f) ? ((float)camZ / g_farClipFloat) : 0.5f;
        float fRHW = 1.0f / (float)camZ;

        /* UV from particle bytes */
        float u0 = g_uvLUT256[p->uvX];
        float v0 = g_uvLUT256[p->uvY];
        /* Second UV pair: offset by sprite size. Original uses DAT_0063fcd8
         * (table base - 1 entry) for bottom-right UVs, pulling edge in by
         * half a texel to avoid sampling the next sprite row. */
        int uvSize = p->uvStep;

        int uvXIdx2 = p->uvX + uvSize - 1;
        if (uvXIdx2 > 255) {
            uvXIdx2 = 255;
        }
        if (uvXIdx2 < 0) {
            uvXIdx2 = 0;
        }

        int uvYIdx2 = p->uvY + uvSize - 1;
        if (uvYIdx2 > 255) {
            uvYIdx2 = 255;
        }
        if (uvYIdx2 < 0) {
            uvYIdx2 = 0;
        }

        float u1 = g_uvLUT256[uvXIdx2];
        float v1 = g_uvLUT256[uvYIdx2];
        /* Vertex color: binary uses 0xE0E0E0 RGB (0x46f1e5: or edx,0xe0e0e0) */
        unsigned int color = (unsigned int)alpha << 24 | VERTEX_WHITE_RGB; //0x00E0E0E0;

        R_SetTexture(tpage);

        RenderVertex verts[4] = {
            { (float)x0, (float)y0, fZ, fRHW, color, 0, u0, v0 },
            { (float)x1, (float)y0, fZ, fRHW, color, 0, u1, v0 },
            { (float)x1, (float)y1, fZ, fRHW, color, 0, u1, v1 },
            { (float)x0, (float)y1, fZ, fRHW, color, 0, u0, v1 },
        };
        R_DrawQuad(verts);
    }

    R_SetBlendMode(R_BLEND_ALPHA);
}

/* =====================================================================
 * DrawSnowParticles — FUN_004df480 — 1441 bytes
 * D3D renderer for snow/rain leaf particles (128 entries, stride 0x38).
 * Transforms each particle through the camera view matrix, does
 * perspective projection, viewport clipping, distance fog/alpha,
 * and writes vertices into per-tpage D3D batch buffers.
 *
 * EAX = viewport index (used in split-screen mode to select particle range)
 *
 * Particle struct (56 = 0x38 bytes) at g_precipParticles:
 *   +0x00: posX      +0x04: posY      +0x08: posZ
 *   +0x26(>>16): halfW   +0x28(>>16): halfH
 *   +0x2C(byte): uvBaseX   +0x2D(byte): uvBaseY
 *   +0x2E(byte): tpage     +0x2E(>>16): frameIdx
 *   +0x30(>>16): frameRange
 *   +0x34(short): visibility flag (written by this function)
 * ===================================================================== */
void DrawSnowParticles(int viewportIdx)
{
    PrecipParticle *pBase;
    int count;
    int farClipTimes8 = g_farClipDepth << 3;

    if (farClipTimes8 <= 0) {
        farClipTimes8 = 0x15800;  /* same fallback as DrawOtherParticles */
    }

    /* Check if the particle tpage is loaded */
    if (g_tpageStateArray[g_tpageCharBase] != 4) {
        return;
    }

    /* Additive blend + depth write off for snow/rain particles */
    R_PushState();
    R_SetBlendMode(R_BLEND_ADDITIVE);
    R_SetDepthWrite(0);
#ifdef SONICR_XBOX
    R_SetDepthTest(0);   /* drops were cut off by the track surface before reaching the ground */
#endif
    R_SetTexEnv(R_TEXENV_MODULATE);

    if (g_netSessionActive != 0) {
        /* Multiplayer: render all 128 particles */
        count = 128;
        pBase = g_precipParticles;
    }
    else {
        /* Split-screen: render only this viewport's particle range.
         * 0x4DF4BA/0x4DF4C0 read the pair [v] and [v+1] straight off the
         * table, so it needs all five entries — a four-entry copy leaves
         * viewport 3 reading past the end. */
        int startIdx = g_precipVpRange[viewportIdx];
        int endIdx   = g_precipVpRange[viewportIdx + 1];
        count = endIdx - startIdx;
        pBase = &g_precipParticles[startIdx];
    }

    for (int i = 0; i < count; i++) {
        PrecipParticle *p = &pBase[i];
        /* Clear visibility flag */
        p->state = 0;

        /* Check posY >= 0 (negative = dead particle) */
        if (p->posY < 0) {
            continue;
        }

        /* Transform world position to camera space (binary: particle>>8, camera raw) */
        int dx = (p->posX >> 8) - g_camIntX;
        int dy = (p->posY >> 8) - g_camIntY;
        int dz = (p->posZ >> 8) - g_camIntZ;

        /* Forward (Z in camera space) */
        int camZ = (g_viewMtx02 * dx + g_viewMtx12 * dy + g_viewMtx22 * dz) / 4096;
        if (camZ < 1 || camZ > farClipTimes8) {
            continue;
        }

        /* Right (X in camera space) */
        int camX = (g_viewMtx00 * dx + g_viewMtx10 * dy + g_viewMtx20 * dz) / 4096;

        /* Up (Y in camera space) */
        int camY = (g_viewMtx01 * dx + g_viewMtx11 * dy + g_viewMtx21 * dz) / 4096;

        /* Quad half-sizes */
        int halfW = p->sizeW;
        int halfH = p->sizeH;

        /* Project to screen quad corners */
        int x0 = g_screenCenterX + ((camX - halfW) * g_projScaleXCurrent) / camZ;
        int y0 = g_screenCenterY - ((camY + halfH) * g_projScaleY) / camZ;
        int x1 = g_screenCenterX + ((camX + halfW) * g_projScaleXCurrent) / camZ;
        int y1 = g_screenCenterY - ((camY - halfH) * g_projScaleY) / camZ;

        /* Mark as visible */
        p->state = 1;

        /* Viewport clip test */
        if (x0 > g_clipRight || x1 < g_clipLeft || y0 > g_clipBottom || y1 < g_clipTop) {
            continue;
        }

        /* Z-buffer depth and reciprocal W */
        float fZ   = (float)camZ / g_farClipFloat;
        float fRHW = 1.0f / (float)camZ;

        /* UV coordinates from particle bytes */
        float u0 = g_uvLUT256[p->uvBaseX];
        float v0 = g_uvLUT256[p->uvBaseY];

        /* Second UV pair: base + frame offset, minus 1 (table at 0x63FCD8 = table[-1]) */
        int uIdx2 = p->uvBaseX + p->frameW - 1; if (uIdx2 < 0) uIdx2 = 0; if (uIdx2 > 255) uIdx2 = 255;
        int vIdx2 = p->uvBaseY + p->frameH - 1; if (vIdx2 < 0) vIdx2 = 0; if (vIdx2 > 255) vIdx2 = 255;
        float u1 = g_uvLUT256[uIdx2];
        float v1 = g_uvLUT256[vIdx2];

        /* Distance fog/alpha:
         * fogFactor = fZ (0.0..1.0 range)
         * > 0.9  -> alpha = 0 (fully transparent, far)
         * <= 0.7 -> alpha = 0xC0 (192, near/opaque)
         * 0.7..0.9 -> interpolate: abs(trunc((fogFactor - 0.9) * 1275.0)) */
        int alpha;
        if ((sr_double)fZ > 0.9) {
            alpha = 0;
        }
        else if ((sr_double)fZ <= 0.7) {
            alpha = 0xC0;
        }
        else {
            int raw = (int)(((sr_double)fZ - 0.9) * 1275.0);
            alpha = SRABS(raw);
        }

        /* Fully fogged out — nothing to contribute. The depth cull above only
         * rejects past the far plane; fog hits 0 at 0.9x of it. */
        if (alpha == 0) {
            continue;
        }

        /* Vertex color: alpha | RGB white (no alpha) */
        unsigned int color = ((unsigned int)alpha << 24) | VERTEX_WHITE_RGB;

        R_SetTexture(p->tpage);

        RenderVertex verts[4] = {
            { (float)x0, (float)y0, fZ, fRHW, color, 0, u0, v0 },
            { (float)x1, (float)y0, fZ, fRHW, color, 0, u1, v0 },
            { (float)x1, (float)y1, fZ, fRHW, color, 0, u1, v1 },
            { (float)x0, (float)y1, fZ, fRHW, color, 0, u0, v1 },
        };
        R_DrawQuad(verts);
    }

    R_PopState();
}

/**
 * DrawSnowParticlesGate — FUN_004e0540 — 54 bytes
 * Gates DrawSnowParticles on weather conditions.
 * Called from the main render loop with EAX = viewport index.
 */
void DrawSnowParticlesGate(int viewportIdx)
{
    if (g_trackId == TRACK_RADIANT_EMERALD) {
        return;
    }
    if (g_raceType == RACE_TIMEATTACK && g_raceSubMode < SUBMODE_TAG) {
        return;
    }
    if (g_weatherType != WEATHER_RAIN && g_weatherType != WEATHER_SNOW) {
        return;
    }

    DrawSnowParticles(viewportIdx);
}

/**
 * UpdateWeatherCounters — 0x004E0578 — 120 bytes
 * Advances weather particle age counters and decrements lifetimes.
 */
void UpdateWeatherCounters(void)
{
    if (g_weatherType == WEATHER_SNOW) {
        for (int i = 0; i < 16; i++) {
            if (g_particleAge[i] < g_particleMaxAge[i]) {
                g_particleAge[i]++;
                g_particleLifetime[i]--;
            }
        }
    }
    else {
        for (int i = 0; i < 16; i++) {
            if (g_particleLifetime[i] != 0) {
                g_particleLifetime[i]--;
                g_particleAge[i]++;
            }
        }
    }
}

/**
 * SpawnOtherParticle — 0x004E05F0 — 357 bytes
 * Weather particle spawner. Called per-viewport per frame.
 * EAX = viewport index.
 */
void SpawnOtherParticle(int viewportIdx)
{
    GroundParticle *gp;
    int *vpPtr;
    int slot;

    g_particleFrameIdx++;

    if (g_particleFrameIdx >= 16) {
        g_particleFrameIdx = 0;
    }

    slot = g_particleFrameIdx;
    gp = &g_particleSlots[slot];

    vpPtr = (int *)((char *)g_viewportConfigArray + viewportIdx * 0xC8);

    if (g_particleLifetime[slot] != 0) {
        return;
    }

    int rx = Random() / 16 - 0x400;
    int worldX = rx + vpPtr[3] + (vpPtr[9] >> 2);

    int rz = Random() / 16 - 0x400;
    int worldZ = rz + vpPtr[5] + (vpPtr[11] >> 2);

    if (IsOnTrackSurface((float)worldX, (float)worldZ) == 1) {
        return;
    }

    g_particleLifetime[slot] = 14;
    g_particleAge[slot] = 0;

    if (g_weatherType == WEATHER_SNOW) {
        int rAge = Random() / 8192;
        g_particleMaxAge[slot] = rAge * 2 + 7;
    }

    int xHi = worldX + 0x28;
    int zHi = worldZ + 0x28;
    int xLo = worldX - 0x28;
    int zLo = worldZ - 0x28;

    gp->corners[0].x = xLo;
    gp->corners[0].z = zHi;
    gp->corners[1].x = xHi;
    gp->corners[1].z = zHi;
    gp->corners[2].x = xHi;
    gp->corners[2].z = zLo;
    gp->corners[3].x = xLo;
    gp->corners[3].z = zLo;

    g_particleOwner[slot] = viewportIdx;
}

/* =====================================================================
 * SetWeatherTint — FUN_004e0758 — 370 bytes
 *
 * Per-track weather/lighting initialization. Sets lighting phase,
 * weather RGB, and normalized fade steps from ROM tables indexed by
 * weather set and time-of-day variant flags.
 * ===================================================================== */
void SetWeatherTint(void)
{
    int trackId = g_trackId;
    int setIdx = s_gameModeWeatherSet[trackId - 1];

    int byteOff = setIdx * 27;
    int wordOff = setIdx * 9;

    if (trackId != TRACK_RADIANT_EMERALD) {
        int wtv = g_weatherType;

        if (wtv == WEATHER_RAIN) {
            byteOff += 9;
            wordOff += 3;
        }
        else if (wtv == WEATHER_SNOW) {
            byteOff += 18;
            wordOff += 6;
        }
    }

    int todv = g_timeOfDay;
    if (todv == TOD_DAY) {
        byteOff += 3;
        wordOff += 1;
    }
    else if (todv == TOD_NIGHT) {
        byteOff += 6;
        wordOff += 2;
    }

    const short *wp = &s_weatherPhase[0][0] + wordOff * 2;
    g_lightingPhaseGlobal = (int)wp[0];
    g_lightingParam2 = (int)wp[1];

    const unsigned char *bp = &s_weatherRGB[0][0] + byteOff;

    int R = bp[0];
    int G = bp[1];
    int B = bp[2];

    g_weatherR = R;
    g_weatherG = G;
    g_weatherB = B;

    int mn = R;
    int mx = R;

    if (G < mn) {
        mn = G;
    }
    if (G > mx) {
        mx = G;
    }

    if (B < mn) {
        mn = B;
    }
    if (B > mx) {
        mx = B;
    }

    int range = mx - mn;
    if (range == 0) {
        g_weatherR = (R * 255) / mn;
        g_weatherG = (G * 255) / mn;
        g_weatherB = (B * 255) / mn;
    }
    else {
        g_weatherR = ((R - mn) * 255) / range;
        g_weatherG = ((G - mn) * 255) / range;
        g_weatherB = ((B - mn) * 255) / range;
    }
}
