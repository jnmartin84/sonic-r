/**
 * render_scene.c — 3D scene rendering (camera setup)
 *
 * BuildCameraView — sets up the camera for the current viewport.
 * Faithful instruction-by-instruction translation from binary at 0x00423574.
 */

#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "sonicr_functions.h"
#include <math.h>
#include <stdio.h>

/* Byte-offset accessor macros */
#define P_SHORT(p, off)   (*(short *)((char *)(p) + (off)))
#define P_USHORT(p, off)  (*(unsigned short *)((char *)(p) + (off)))
#define P_INT(p, off)     (*(int *)((char *)(p) + (off)))
#define P_BYTE(p, off)    (*(unsigned char *)((char *)(p) + (off)))

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ROM double constants used by the binary */
#define POS_SCALE   0.000244140625          /* 0x51FCA0: 1/4096 — fixed-point → float position scale */
#define ANGLE_SCALE 0.001533980787896       /* 0x51FCA8: 2*pi/4096 — angle to radian */

/* atan2 radians-to-12-bit conversion:
 * Binary multiplies fpatan result by [0x51fc90] * [0x51fc98].
 * The combined product = 4096 / (2*pi) = RAD_TO_ANGLE12. */
#define RAD_TO_ANGLE12  (4096.0 / (2.0 * M_PI))

/* Terrain collision functions from ground_collision.c */
extern int s_hitSurfaceIdx;     /* _DAT_006da584 */
extern int s_hitEdgeIdx;        /* _DAT_006da588 */
extern int s_hitSurfaceHeight;  /* _DAT_006da58c */
extern int PointInPolygon(int playerX, int playerZ, int surfIdx);
extern int InterpolateGroundHeight(int playerX, int playerZ, int surfIdx, int edgeIdx);

static const int s_camDistTable[4]   = { 768, 1152, 1536, 2 };   /* 0x4FBE8C */
static const int s_camHeightTable[4] = { 0,   48,   96,   0 };   /* 0x4FBE90 */

/* Watcom-style signed fixed-point multiply: (a * b) >> 16 with round toward zero.
 * Binary pattern: imul; sar 0x1f; shl 0x10; sbb; sar 0x10. */
static inline int fixmul16(int a, int b) {
    int v = a * b;
    int sign = v >> 31;             /* 0 or -1 */
    /* shl sign, 16 → 0 or 0xFFFF0000; sbb v, that → v - 0xFFFF0000 - CF
     * where CF = (sign was -1 ? 1 : 0) from the shl. Net effect for negative:
     * v + 0x10000 - 1 = v + 0xFFFF. Then sar 16. */
    return (v + (sign & 0xFFFF)) >> 16;
}

/**
 * SampleTerrainGrid — 0x004d7c08 — 308 bytes (107 instructions)
 * Finds the lowest ground surface height at a given fixed-point XZ position
 * by looking up the terrain grid cell, iterating surface entries, testing
 * point-in-polygon, and interpolating ground height for each match.
 *
 * Watcom fastcall: EAX = worldX (fixed-point <<12), EDX = worldZ (fixed-point <<12),
 *                  EBX = height threshold (from player+0xa4).
 * Returns: lowest terrain height at that position, or 0 if none found.
 */
extern void xbox_race_mark(const char *s);
int SampleTerrainGrid(int worldX, int worldZ, int heightThreshold)
{
    xbox_race_mark("SampleTerrainGrid enter");
    /* 0x4d7c11-0x4d7c22: convert to world integer coords */
    int xInt = worldX >> 12;                             /* sar ebx, 0xc */
    int zInt = worldZ >> 12;                             /* sar ecx, 0xc */
    int bestHeight = 0;                                  /* 0x4d7c25: xor edx, edx */

    /* 0x4d7c28-0x4d7c5d: compute grid cell index */
    int cellX = (xInt - g_aiGridOriginX) / g_aiGridCellWidth;   /* 0x4d7c2a-0x4d7c37: idiv */
    int cellZ = (zInt - g_aiGridOriginZ) / g_aiGridCellHeight;  /* 0x4d7c46-0x4d7c4f: idiv */
    int cellOff = (((cellZ << 5) + cellX) << 1);                /* 0x4d7c58-0x4d7c5d: shl 5; add; add eax,eax */

    /* 0x4d7c62-0x4d7c79: look up polygon list via two-level grid index */
    short *gridIdx = (short *)g_terGridIndex;            /* 0x6da57c */
    short *gridData = (short *)g_terGridData;            /* 0x6da580 */
    short listOff = *(short *)((char *)gridIdx + cellOff); /* 0x4d7c6c: movsx word */
    short *polyPtr = (short *)((char *)gridData + listOff * 2); /* 0x4d7c75-0x4d7c77: add eax,eax; add edx,eax */

    /* 0x4d7c7f-0x4d7c82: if first entry is -1, return 0 (empty cell) */
    if (*polyPtr == (short)-1) {
        return 0;
    }

    /* 0x4d7c91-0x4d7d2b: iterate polygon list entries */
    for (;;) {
        /* 0x4d7c91-0x4d7c9d: read entry as signed short, advance pointer by 2 */
        short entry = *polyPtr;
        polyPtr++;

        /* 0x4d7ca0-0x4d7ca9: test bit 14 (0x4000) — skip wall/non-ground entries */
        if ((entry & 0x4000) != 0) {
            goto loop_end;
        }

        /* 0x4d7caf-0x4d7cfb: bounds check — distance from polygon center */
        int surfIdx = entry & 0xFFF;                 /* 0x4d7cb2: and ebx, 0xfff */
        TerSurface *surf = (TerSurface *)g_trackSurfaceData + surfIdx;

        /* 0x4d7cc5-0x4d7cd0: check surf height limit vs threshold */
        if (surf->layer > heightThreshold) {
            goto loop_end;
        }

        /* 0x4d7cd2-0x4d7cfb: bounding radius distance check */
        int sx = xInt - surf->centerX;
        int sz = zInt - surf->centerZ;
        if (sx * sx + sz * sz >= surf->radiusSq) {
            goto loop_end;
        }

        /* 0x4d7cfd-0x4d7d06: point-in-polygon test */
        if (!PointInPolygon(worldX, worldZ, surfIdx)) {
            goto loop_end;
        }

        /* 0x4d7d0a-0x4d7d1a: interpolate ground height at exact XZ
         * ECX = s_hitEdgeIdx (0x6da588), EBX = s_hitSurfaceIdx (0x6da584) */
        int height = InterpolateGroundHeight(worldX, worldZ,
                         s_hitSurfaceIdx, s_hitEdgeIdx);

        /* 0x4d7d1f-0x4d7d24: keep lowest (most negative) height */
        if (height < bestHeight) {
            bestHeight = height;
        }

    loop_end:
        /* 0x4d7d27-0x4d7d2b: bit 15 of entry = end-of-list marker */
        if ((entry & (short)0x8000) != 0) {
            break;
        }
    }

    return bestHeight;                                   /* 0x4d7d31 */
}

/**
 * SampleTerrainAtCamera — 0x004d7d3c — 118 bytes (62 instructions)
 * Checks if a world-coordinate XZ position is within the terrain grid bounds,
 * then samples the terrain height at that position by calling SampleTerrainGrid.
 *
 * Watcom fastcall: EAX = int[3] pointer (camX, camY, camZ) in world coords,
 *                  EDX = player byte offset 0xa4 value (height threshold).
 * Returns: terrain height in fixed-point, or 0 if out of bounds.
 */
int SampleTerrainAtCamera(int *pos, int param)
{
    /* 0x4d7d40-0x4d7d50 */
    if (g_aiGridOriginX > pos[0]) {
        return 0;
    }

    /* 0x4d7d51-0x4d7d62 */
    if (g_aiGridOriginZ > pos[2]) {
        return 0;
    }

    /* 0x4d7d63-0x4d7d7b */
    if (((g_aiGridCellWidth << 5) + g_aiGridOriginX - 1) < pos[0]) {
        return 0;
    }

    /* 0x4d7d7c-0x4d7d95 */
    if (((g_aiGridCellHeight << 5) + g_aiGridOriginZ - 1) < pos[2]) {
        return 0;
    }

    /* 0x4d7d96-0x4d7da8 */
    int fixedZ = (-(pos[2])) << 12;                      /* neg ecx; shl ecx, 0xc */
    int fixedX = (-(pos[0])) << 12;                      /* neg eax; shl eax, 0xc */

    return SampleTerrainGrid(fixedX, fixedZ, param);     /* 0x4d7da8: call 0x4d7c08 */
}

/* NegateLookAtGravityVector — 0x004234B8 — 59 bytes
 * Reads loop-surface entry 0's norm vector (g_terLoopTable[0].normX/Y/Z at
 * +8/+0xa/+0xc — the high 16 bits of dwords@+6/+8/+0xA), negates, writes out[0..2].
 * EAX = output int[3] pointer. */
void NegateLookAtGravityVector(int *out)
{
    /* Reads loop-surface entry 0's 3-vector (norm, +8/+0xa/+0xc) and negates it.
     * Binary loads dword@+6/+8/+0xa and keeps each high 16 bits = normX/Y/Z. */
    TerLoopEntry *le = (TerLoopEntry *)g_terLoopTable;

    out[0] = -(int)le->normX;
    out[1] = -(int)le->normY;
    out[2] = -(int)le->normZ;
}

/* =====================================================================
 * SmoothCamera — 0x004234f4 — 127 bytes
 * Smoothly interpolates [edx+0x16] toward a
 * target value determined by object state flags.
 * Target: 0x12 (default), 0x16 if [+0x6c]>>16==1, 0x20 if [+0x78]
 * or [+0x88] non-zero, 0x40 if [+0xa0] non-zero.
 * Steps by ±2 per tick, clamped to target.
 * EAX = object struct, EDX = output struct
 * ===================================================================== */
void SmoothCamera(Player *obj, short *out)  /* EAX, EDX */
{
    char *objBytes = (char *)obj;
    short target = 0x12;

    if (*(short *)(objBytes + 0x6e) == 1) {
        target = 0x16;
    }
    if (*(short *)(objBytes + 0x78) != 0 || *(short *)(objBytes + 0x88) != 0) {
        target = 0x20;
    }
    if (*(short *)(objBytes + 0xa0) != 0) {
        target = 0x40;
        out[11] = 0x40;  /* [edx+0x16] as short array index 11 */
    }

    short current = out[11];  /* [edx+0x16] */

    if (target < current) {
        short next = current - 2;
        out[11] = next;
        if (target > next) {
            out[11] = target;
        }
    }
    if (target > current) {
        short next = current + 2;
        out[11] = next;
        if (target < next) {
            out[11] = target;
        }
    }
}

/**
 * UpdateCameraConfig — 0x0042431C — 125 bytes
 * Called per-frame before BuildCameraView. Ramps camDist/camHeight
 * from a ROM table over 3 frames after the camera becomes active.
 *
 * ROM table at 0x4FBE8C (camDist) / 0x4FBE90 (camHeight), indexed by state*8:
 *   state 0: dist=768  height=0
 *   state 1: dist=1152 height=48
 *   state 2: dist=1536 height=96
 *
 * Watcom fastcall: EAX = cameraState struct, EDX = input flags (low byte).
 */
void UpdateCameraConfig(CamStateEntry *camState, int flags)
{
    /* 0x42431e-0x42434b: network/multiplayer early-out checks */
#if SONICR_NETCODE
    /* g_netGameStarted at 0x68ACE4 — declared in sonicr_globals.h */

    if (g_netSessionActive != 0 || g_isNetworkGame != 0) {
        if (g_netGameStartState == 3 && g_netGameStarted == 0) {
            camState->stateCounter = 1;                      /* 0x424342 */
            return;
        }
    }
#endif

    /* 0x42434c: test dl, 0x40 — check bit 6 of flags */
    if ((flags & 0x40) == 0) {
        /* 0x42438f: camera not active — reset state */
        camState->initFlag = 0;                              /* 0x42438f */
        return;
    }

    /* 0x424351: camera is active — ramp config from ROM table */
    int initialized = camState->initFlag;                     /* +0x24: init flag */
    if (initialized != 0) {
        /* Already at final state — just mark initialized */
        camState->initFlag = 1;                              /* 0x424385 */
        return;
    }

    /* Advance the state counter, cycling 0 → 1 → 2 → 0. The initFlag check above
     * edge-gates this so it steps once per look-back press, not every frame.
     * Binary 0x424368 wraps to 0 via ebp (= initFlag, which is 0 on this path). */
    int state = camState->stateCounter;                       /* +0x20: state counter */
    if (state < 2) {
        camState->stateCounter = state + 1;                  /* 0x424360 */
    }
    else {
        camState->stateCounter = 0;                          /* 0x424368 */
    }

    /* Read camDist/camHeight from ROM table indexed by state */
    state = camState->stateCounter;                           /* re-read after increment */
    camState->camDist = s_camDistTable[state];               /* 0x424375 */
    camState->camHeight = s_camHeightTable[state];           /* 0x424382 */

    /* Mark as initialized for next frame */
    camState->initFlag = 1;                                   /* 0x424385 */
}

/**
 * BuildCameraView — 0x00423574 — 1569 bytes
 *
 * Sets up the 3D camera for the current player's viewport.
 * Computes camera world position, derives yaw/pitch, builds view matrix.
 *
 * Watcom fastcall: EAX=player, EDX=cameraParams, EBX=renderCam, ECX=smoothedCam
 */
extern void xbox_race_mark(const char *s);
void BuildCameraView(Player *player, CamStateEntry *cameraParams,
                        RenderCamera *renderCam, CamStateEntry *smoothedCam)
{
    /* esi = player (EAX), ecx = cameraParams (EDX initially) */

    /* 0x423577-0x42358a: read camera params */
    int camHeight = cameraParams->camHeight;     /* [edx + 0x1C] → ebp-0x10 */
    int camDist   = cameraParams->camDist;       /* [edx + 0x18] → edi */

    /* ================================================================
     * Adjust camera distance/height — 0x42358a-0x42360e
     * ================================================================ */

    /* 0x42358a: movsx eax, word ptr [esi + 0xF2] — character ID check */
    short charId = player->charId;

    /* 0x423591-0x4235bd: adjust for char type 4/8, or unconditionally in D3D.
     * Binary 0x4235a2: cmp [g_renderMode], 1 — mode 1 is Direct3D
     * (SetRenderMode at 0x4322c6 computes 1 + software-flag), so the
     * pull-back is the hardware camera's, not the software one's. We run
     * RENDER_SOFT and skip it. Define ENABLE_D3D_CAMERA to restore the D3D
     * camera pull-back (wider FOV, higher angle). */
#ifdef ENABLE_D3D_CAMERA
    if (charId == CHAR_EGGMAN || charId == CHAR_EGG_ROBO || 1) {
#else
    if (charId == CHAR_EGGMAN || charId == CHAR_EGG_ROBO) {
#endif
        if (camDist > 0) {                  /* 0x4235ab: test edi, edi; jle */
            camDist += 0x100;               /* 0x4235af */
        }
        else {
            camDist -= 0x200;               /* 0x4235b7 */
        }
        camHeight += 0x40;                  /* 0x4235bd */
    }

    /* 0x4235c1-0x4235f8: Grand Prix 2-player push camera back */
    if (g_raceType == RACE_MULTIPLAYER && g_numViewports == 2 && g_netSessionActive == 0) {
        if (g_viewportIndex == 1) {         /* 0x4235e4 */
            camDist += 0x200;               /* 0x4235e6 */
        }
        else if (g_viewportIndex == 0) {    /* 0x4235ee */
            camDist += 0x300;               /* 0x4235f2 */
        }
    }

    /* 0x4235f8-0x42360e: D3D-mode near-ground extra height (cmp ..., 1). */
#ifdef ENABLE_D3D_CAMERA
    if (player->posY > (int)0xFFFF0000) {
        camHeight += 0x40;                  /* 0x42360a */
    }
#endif

    /* ================================================================
     * Compute camera world position — 0x42360e-0x4237c7
     * ================================================================ */

    int camX, camY, camZ;
    xbox_race_mark("BCV pos");


    /* INTRO FLYOVER CAMERA 0x42361b-0x42371f */
    if (g_introCountdown > 0x1E) { /* 0x42360e: cmp g_introCountdown, 0x1e; jle → normal gameplay */
        /* 0x42361b-0x423634: determine intro length */
        int introLength;
        if (g_raceType == RACE_TIMEATTACK && g_raceSubMode == SUBMODE_REVERSE) {
            introLength = 0x186;            /* 0x42362d */
        }
        else {
            introLength = 0xD2;             /* 0x423634 */
        }

        /* 0x423639-0x423644: splineIdx = introLength - g_introCountdown */
        int splineIdx = introLength - g_introCountdown;

        /* 0x423647-0x42365a: compute spline data pointer
         * offset = splineIdx * 12: (splineIdx << 2) - splineIdx = splineIdx*3,
         * then << 2 = splineIdx*12. Add g_introSplineBase.
         * Read spline[0] = *(int*)(base + offset) */
        int *splineBase = (int *)g_introSplineBase;
        int splineOff = splineIdx * 12;
        int splineX = *(int *)((char *)splineBase + splineOff);      /* 0x42365a: [eax] */

        /* 0x42365e-0x423661: negate splineX */
        splineX = -splineX;                /* neg eax */

        /* 0x423664-0x42367d: read splineY and splineZ
         * recompute offset same way, read [+4] and [+8] */
        int splineY = *(int *)((char *)splineBase + splineOff + 4);  /* 0x42367d: [eax+4] */
        int splineZ = *(int *)((char *)splineBase + splineOff + 8);  /* 0x423680: [eax+8] */

        /* 0x423683-0x42368b: negate splineZ */
        splineZ = -splineZ;                /* neg eax at 0x423686 */

        /* 0x42368e-0x4236b9: camX = (-player[0] >> 12) - fixmul16(sinTable[behindAngle], camDist)
         * playerYaw from player+0x6E dword >> 16
         * behindAngle = 0xFFF - playerYaw
         * sinBehind = g_sinTable[behindAngle] */
        int playerYaw = player->moveMode;     /* 0x42368e: sar eax, 0x10 */
        int behindAngle = 0xFFF - playerYaw;            /* 0x423691-0x423699 */

        /* 0x42369b-0x4236af: fixmul16(g_sinTable[behindAngle], camDist) */
        int sinProd = fixmul16(g_sinTable[behindAngle], camDist);

        /* 0x4236b2-0x4236bb: camX_base = (-player->posX >> 12) - sinProd */
        int camX_base = ((-player->posX) >> 12) - sinProd;  /* neg edx; sar 0xc; sub */

        /* 0x4236bd-0x4236c2: camX = splineX + camX_base */
        camX = splineX + camX_base;        /* 0x4236c0-0x4236c3 */

        /* 0x4236c5-0x4236d3: camY = splineY + (camHeight + 0x70) - (player->posY >> 12)
         * NOTE: player->posY is NOT negated here — binary does sar, not neg+sar */
        camY = splineY + ((camHeight + 0x70) - (player->posY >> 12));  /* 0x4236cb-0x4236d1 */

        /* 0x4236dd-0x42370d: camZ_base = (-player[2] >> 12) + fixmul16(cosTable[behindAngle], camDist) */
        /* Recompute behindAngle from player+0x6E again (same value) */
        int cosProd = fixmul16(
            *(int *)((char *)g_cosTable + behindAngle * 4),  /* 0x4236ea-0x4236f9 */
            camDist);

        int camZ_base = ((-player->posZ) >> 12) + cosProd;

        /* 0x42371a-0x42371c: camZ = splineZ + camZ_base */
        camZ = splineZ + camZ_base;

        /* 0x42371f: jmp to phase 2b */
    }
    /* NORMAL GAMEPLAY CAMERA 0x423724-0x4237c7 */
    else {
        /* 0x423724-0x42372f: playerYaw, behindAngle (same calculation) */
        int playerYaw = player->moveMode;
        int behindAngle = 0xFFF - playerYaw;

        /* 0x423731-0x42374f: camX = (-player->posX >> 12) - fixmul16(sinTable[behindAngle], camDist)
         * Binary uses behindAngle DIRECTLY — no -0x400 offset! */
        camX = ((-player->posX) >> 12) - fixmul16(g_sinTable[behindAngle], camDist);

        /* 0x423754-0x42378e: camZ = (-player->posZ >> 12) + fixmul16(cosTable[behindAngle], camDist)
         * Reloads behindAngle from player+0x6E, same value */
        camZ = ((-player->posZ) >> 12) + fixmul16(
            *(int *)((char *)g_cosTable + behindAngle * 4),
            camDist);

        /* 0x423791-0x42379f: camY via FUN_004d7d3c terrain height lookup
         * lea eax, [ebp-0x3c] → pointer to camX/camY/camZ local array
         * mov edx, [esi+0xa4] → player byte offset 0xa4
         * call FUN_004d7d3c → returns terrain height in EAX */
        int camPos[3];
        camPos[0] = camX;
        camPos[1] = 0;  /* placeholder, overwritten below */
        camPos[2] = camZ;
        int terrainH = SampleTerrainAtCamera(camPos, player->collisionLayer);

        /* 0x42379f-0x4237a4: camY = -(terrainH) >> 12 */
        camY = (-terrainH) >> 12;

        /* 0x4237a7-0x4237b6: clamp: if (-player->posY >> 12) > camY, use player Y instead */
        int playerY = (-player->posY) >> 12;

        if (playerY > camY) {
            camY = playerY;             /* 0x4237b4-0x4237b6 */
        }

        /* 0x4237b9-0x4237c4: camY += camHeight + 0x70 */
        camY = camY + camHeight + 0x70;
    }

    /* ================================================================
     * Post-race camera override — 0x4237c7-0x423906
     * ================================================================ */

    /* 0x4237c7: cmp g_postRaceCameraMode, 0; je skip */
    if (g_postRaceCameraMode != 0) {
        /* 0x4237d4: cmp g_raceType, 3; jne non-podium */
        /* PODIUM CAMERA 0x4237e1-0x42387e */
        if (g_raceType == RACE_SPECIAL) {
            /* 0x4237e1-0x4237f7: orbitAngle = (0x800 - player->angleYaw) & 0xFFF
             * orbitRadius = g_orbitAngle * 2 + 0x258 */
            int orbitAng = (0x800 - player->angleYaw) & 0xFFF;
            int orbitRadius = g_orbitAngle * 2 + 0x258;    /* 0x258 = 600 */

            /* 0x423802-0x423826: camX = -fixmul16(sinTable[orbitAng], orbitRadius) - pod[0x54] */
            int *pod = (int *)g_podiumCenter;              /* 0x423819: [0x9024a8] */
            camX = -fixmul16(g_sinTable[orbitAng], orbitRadius)
                   - P_INT(pod, 0x54);                     /* 0x42381f-0x423826 */

            /* 0x423829-0x423841: camY = pod[0x58] + 0x90 + (g_orbitAngle >> 4) */
            camY = P_INT(pod, 0x58) + 0x90;
            camY += (g_orbitAngle >> 4);                   /* 0x42383c-0x423841 */

            /* 0x423844-0x42387b: camZ = fixmul16(cosTable[orbitAng], orbitRadius) - pod[0x5C] */
            int orbitAng2 = (0x800 - player->angleYaw) & 0xFFF;  /* recomputed at 0x423844 */
            camZ = fixmul16(
                *(int *)((char *)g_cosTable + orbitAng2 * 4),
                orbitRadius)
                   - P_INT(pod, 0x5C);                     /* 0x423876-0x42387b */
        }
        /* POST-RACE ORBIT CAMERA 0x423883-0x423903 */
        else {
            /* 0x423883-0x4238a4: orbitAng = (g_orbitAngle - 0x400) & 0xFFF
             * orbitRadius = g_orbitAngle / 2 + 0x190   (signed div toward zero) */
            int orbitAng = (g_orbitAngle - 0x400) & 0xFFF;
            int orbitRadius = (g_orbitAngle / 2) + 0x190;  /* 0x190 = 400 */

            /* 0x4238a9-0x4238cb: camX = (-player->posX >> 12) - fixmul16(sinTable[orbitAng], orbitRadius) */
            camX = ((-player->posX) >> 12) - fixmul16(
                *(int *)((char *)g_sinTable + orbitAng * 4),
                orbitRadius);

            /* 0x4238ce-0x4238db: camY = (-player->posY >> 12) + 0xC0 */
            camY = ((-player->posY) >> 12) + 0xC0;

            /* 0x4238de-0x423903: camZ = (-player->posZ >> 12) + fixmul16(cosTable[orbitAng], orbitRadius) */
            camZ = ((-player->posZ) >> 12) + fixmul16(
                *(int *)((char *)g_cosTable + orbitAng * 4),
                orbitRadius);
        }
    }

    /* ================================================================
     * Look-at target override — 0x423906-0x42391b
     * If player+0x9E upper 16 != 0, call NegateLookAtGravityVector to override camXYZ
     * ================================================================ */
    /* 0x423906: mov eax, [esi+0x9e]; sar 0x10; test; je */
    if (player->loopMode != 0) {
        /* 0x423913: lea eax, [ebp-0x3c] → pointer to camX,camY,camZ
         * 0x423916: call NegateLookAtGravityVector */
        int camPos[3];
        camPos[0] = camX;
        camPos[1] = camY;
        camPos[2] = camZ;
        NegateLookAtGravityVector(camPos);
        camX = camPos[0];
        camY = camPos[1];
        camZ = camPos[2];
    }

    /* ================================================================
     * Smooth camera interpolation — 0x42391b-0x423980
     *
     * smoothedCam stores position at 16x scale. Each axis converges:
     *   cam[i] -= (cam[i] - camPos*16) / divisor
     *
     * Divisor for X,Z: DWORD(+0x14) >> 16 >> 2  (= detailLevel >> 2)
     * Divisor for Y:   DWORD(+0x14) >> 16 >> 1  (= detailLevel >> 1)
     * ================================================================ */
    int target16;
    int diff;
    int divisor;
    int step;
     /* 0x42391b: mov edx, ecx; mov eax, esi; call 0x4234f4 */
    xbox_race_mark("BCV smooth");
    SmoothCamera(player, (short *)smoothedCam);

    /* 0x423924-0x42393e: smooth X */
    target16 = camX << 4;                           /* shl edx, 4 */
    diff = smoothedCam->posX - target16;            /* sub eax, edx */
    divisor = smoothedCam->fovDetail >> 2;           /* sar 0x10; sar 2 = >> 18 */
    if (divisor == 0) {
        divisor = 1;
    }
    /* idiv: signed divide diff by divisor */
    step = diff / divisor;                           /* 0x42393c: idiv edi */
    smoothedCam->posX -= step;                           /* 0x42393e: sub [ecx], eax */
    
    /* 0x423940-0x423962: smooth Y */
    target16 = camY << 4;
    diff = smoothedCam->posY - target16;
    divisor = smoothedCam->fovDetail >> 1;           /* sar 0x10; sar 1 = >> 17 */
    if (divisor == 0) {
        divisor = 1;
    }
    step = diff / divisor;
    smoothedCam->posY -= step;

    /* 0x423965-0x423980: smooth Z */
    target16 = camZ << 4;
    diff = smoothedCam->posZ - target16;
    divisor = smoothedCam->fovDetail >> 2;           /* sar 0x10; sar 2 = >> 18 */
    if (divisor == 0) {
        divisor = 1;
    }
    step = diff / divisor;
    smoothedCam->posZ -= step;

    /* ================================================================
     * Compute yaw/pitch via atan2 — 0x423983-0x423a5b
     *
     * Direction vector from smoothed camera to player (world coords).
     * Camera position = smoothedCam[i] >> 4 (divide by 16).
     * Player position = -player[i] >> 12.
     * ================================================================ */

    int dx = ((-player->posX) >> 12) - (smoothedCam->posX >> 4);          /* 0x423983-0x42398f */
    int dy_raw = ((-player->posY) >> 12) - (smoothedCam->posY >> 4);    /* 0x423991-0x42399f */
    int dy = dy_raw + 0x28;                                             /* 0x4239a1: lea edi,[eax+0x28] */

    if (g_numViewports == 2 && g_viewportIndex == 0 && g_netSessionActive == 0) {
        dy -= 0x28;                                                     /* 0x4239c3: sub edi, 0x28 */
    }

    int dz = ((-player->posZ) >> 12) - (smoothedCam->posZ >> 4);          /* 0x4239c6-0x4239d6 */

    /* 0x4239d9-0x423a0c: YAW — atan2(dz, dx) → store as word at smoothedCam+0x14
     *
     * fild dz → ST0; fild dx → ST0 (dz→ST1)
     * fpatan → atan2(ST1=dz, ST0=dx) in radians
     * * [0x51fc90] * [0x51fc98] → 12-bit angle (combined = 4096/(2*pi))
     * ROUND → nearest integer
     * fistp → store
     * (val << 4) >> 4 → sign-extend 28-bit
     * + 0x800 → add 180 degrees
     * → word store at [ecx + 0x14] */
    /* Binary: fild dz, fild dx, call 0x4e08fd (wrapper does fxch+fpatan).
     * fild dz→ST0=dz; fild dx→ST0=dx,ST1=dz; fxch→ST0=dz,ST1=dx;
     * fpatan = atan2(ST1=dx, ST0=dz) → C: atan2(dx, dz). */
    xbox_race_mark("BCV atan2");
    sr_double yawRad = sr_atan2((sr_double)dx, (sr_double)dz);
    int yawAngle = (int)sr_lrint(yawRad * RAD_TO_ANGLE12);
    yawAngle = (yawAngle << 4) >> 4;                    /* sign-extend 28-bit */
    yawAngle += 0x800;                                   /* 0x423a07: unconditional */
    smoothedCam->targetPitch = (short)yawAngle;          /* 0x423a0c: mov word [ecx+0x14], ax */

    /* 0x423a10-0x423a5b: PITCH — atan2(hDistSq, dy_modified)
     *
     * dx*dx → edx; dz*dz + dx*dx → hDistSq
     * dy_modified: if dy < 0 → dy*dy (positive); if dy >= 0 → (-dy)*dy (negative)
     * fpatan(hDistSq, dy_modified) → atan2(ST1=hDistSq, ST0=dy_modified)
     * * conversion constants → 12-bit angle
     * ROUND; (val << 4) >> 4; neg → store as word at [ecx + 0x12] */
    int dx2 = dx * dx;                                  /* 0x4239e4-0x4239e7 */
    int dz2 = dz * dz;                                  /* 0x423a10-0x423a13 (EAX) */
    int hDistSq = dz2 + dx2;                            /* 0x423a16: add eax, edx */

    int dy_mod;
    if (dy < 0) {                                        /* 0x423a1b: test edi; jge */
        dy_mod = dy * dy;                                /* 0x423a1f: imul edi, edi (positive) */
    }
    else {
        dy_mod = (-dy) * dy;                             /* 0x423a27-0x423a2b: neg; imul (negative) */
    }

    /* Binary: fild hDistSq, fild dy_mod, call 0x4e08fd.
     * Same fxch+fpatan pattern → atan2(dy_mod, hDistSq). */
    sr_double pitchRad = sr_atan2((sr_double)dy_mod, (sr_double)hDistSq);
    int pitchAngle = (int)sr_lrint(pitchRad * RAD_TO_ANGLE12);
    pitchAngle = (pitchAngle << 4) >> 4;                /* sign-extend 28-bit */
    pitchAngle = -pitchAngle;                            /* 0x423a59: neg eax */
    smoothedCam->targetYaw = (short)pitchAngle;          /* 0x423a5b: mov word [ecx+0x12], ax */
    

    /* ================================================================
     * Smooth yaw/pitch — 0x423a5f-0x423adf
     *
     * Angle smoothing: converge current toward target by 1/4 per frame.
     * Wraps around 12-bit circle (0..0xFFF).
     * ================================================================ */

    /* Smooth angle A (goes to renderCam yaw) 0x423a5f-0x423a95 */
    /* Read current smoothed value A = upper word of dword at +0x0A = word at +0x0C
     * Read target A = upper word of dword at +0x10 = word at +0x12 (target pitch from atan2) */
    int currentA = smoothedCam->smoothYaw;                  /* 0x423a62: sar esi, 0x10 */
    int targetA  = smoothedCam->targetYaw;               /* 0x423a5f: sar edx, 0x10 */

    int deltaA = (currentA - targetA) & 0xFFF;           /* 0x423a6b-0x423a6d */
    if (deltaA >= 0x800) {                                /* 0x423a73-0x423a84 */
        deltaA = -(0x1000 - deltaA);                       /* wrap short way */
    }
    deltaA >>= 2;                                         /* 0x423a89: sar esi, 2 */

    /* smoothedA = currentA - delta/4 */
    int smoothA = smoothedCam->smoothYaw - deltaA;              /* 0x423a86-0x423a8f */
    smoothedCam->smoothYaw = (short)smoothA;             /* 0x423a91: mov word [ecx+0xc], ax */
    smoothedCam->smoothYaw &= 0x0FFF;                   /* 0x423a95: and byte [ecx+0xd], 0xf */

    /* Smooth angle B (goes to renderCam pitch) 0x423a99-0x423adf */
    /* Read current smoothed value B = upper word of dword at +0x0C = word at +0x0E
     * Read target B = upper word of dword at +0x12 = word at +0x14 (target yaw from atan2) */
    int currentB = smoothedCam->smoothPitch;                /* 0x423a9c: sar eax, 0x10 */
    int targetB  = smoothedCam->targetPitch;             /* 0x423a99: sar esi, 0x10 */

    int deltaB = (currentB - targetB) & 0xFFF;           /* 0x423aa5-0x423aa9 */
    if (deltaB >= 0x800) {                                /* 0x423aaf-0x423ac0 */
        deltaB = -(0x1000 - deltaB);
    }
    deltaB >>= 2;                                         /* 0x423ac5: sar esi, 2 */

    int smoothB = smoothedCam->smoothPitch - deltaB;            /* 0x423ac2-0x423acb */
    smoothedCam->smoothPitch = (short)smoothB;           /* 0x423acd: mov word [ecx+0xe], ax */
    smoothedCam->smoothPitch &= 0x0FFF;                 /* 0x423adf (inferred from 0x423ad3 block) */

    /* ================================================================
     * Build render camera — 0x423ad1-0x423b25
     * ================================================================ */

    /* 0x423ad1-0x423ae1 */
    renderCam->worldX = (-(smoothedCam->posX)) << 8;              /* neg; shl 8 = *-256 */

    /* 0x423ae3-0x423ae9 */
    renderCam->worldY = smoothedCam->posY << 8;                  /* shl 8 = *256 */

    /* 0x423aec-0x423af4 */
    renderCam->worldZ = (-(smoothedCam->posZ)) << 8;             /* neg; shl 8 = *-256 */

    renderCam->yaw = smoothedCam->smoothYaw;                     /* 0x423af7-0x423afd */

    int pitchRaw = smoothedCam->smoothPitch;                 /* 0x423b00-0x423b03 */
    renderCam->pitch = (-pitchRaw) & 0xFFF;                 /* 0x423b09-0x423b15 */

    /* 0x423b16-0x423b1e: clamp worldY minimum to 0x19000 */
    if (renderCam->worldY < 0x19000) {
        renderCam->worldY = 0x19000;
    }

    /* ================================================================
     * Float camera position + integer positions — 0x423b25-0x423b77
     *
     * FPU operations: load doubles from ROM, multiply with int fields.
     * 0x51FCA0 (g_fixedToFloat) = 1/4096 = 0.000244140625 (double)
     * 0x51FCA8 (g_angleToRadians) = 2*pi/4096 = 0.001533980787896 (double)
     *
     * FPU stack trace:
     *   fld [0x51fca0]         ; ST0 = posScale
     *   fild [ebx+0]          ; ST0 = worldX (int), ST1 = posScale
     *   fmul st(1)            ; ST0 = worldX*posScale, ST1 = posScale
     *   fild [ebx+4]          ; ST0 = worldY, ST1 = wX*pS, ST2 = posScale
     *   fmul st(2)            ; ST0 = worldY*posScale, ST1 = wX*pS, ST2 = posScale
     *   fild [ebx+8]          ; ST0 = worldZ, ST1 = wY*pS, ST2 = wX*pS, ST3 = posScale
     *   fmulp st(3)           ; ST0 = wY*pS, ST1 = wX*pS, ST2 = wZ*posScale
     *   fld [0x51fca8]         ; ST0 = angScale, ST1 = wY*pS, ST2 = wX*pS, ST3 = wZ*pS
     *   fild [ebx+0x18]       ; ST0 = yaw, ST1 = angScale, ST2 = wY*pS, ST3 = wX*pS, ST4 = wZ*pS
     *   fmul st(1)            ; ST0 = yaw*angScale, ST1 = angScale, ...
     *   fild [ebx+0x1c]       ; ST0 = pitch, ST1 = yaw*aS, ST2 = angScale, ...
     *   fmulp st(2)           ; ST0 = yaw*aS, ST1 = pitch*angScale, ST2 = wY*pS, ST3 = wX*pS, ST4 = wZ*pS
     *
     * Interleaved integer stores:
     *   mov [ebx+0xc], worldX >> 12
     *   mov [ebx+0x10], worldY >> 12
     *
     * fxch st(3); fstp [ebx+0x30]   ; camFloatX = wX*posScale (float)
     * fxch st(1); fstp [ebx+0x34]   ; camFloatY = wY*posScale (float)
     *   mov [ebx+0x14], worldZ >> 12
     * fxch st(2); fstp [ebx+0x38]   ; camFloatZ = wZ*posScale (float)
     * fstp [ebx+0x3c]               ; yawFloat = yaw*angScale (float)
     * fstp [ebx+0x40]               ; pitchFloat = pitch*angScale (float)
     * ================================================================ */

    /* Integer positions */
    renderCam->intX = renderCam->worldX >> 12;               /* 0x423b49-0x423b4e */
    renderCam->intY = renderCam->worldY >> 12;               /* 0x423b51-0x423b57 */
    renderCam->intZ = renderCam->worldZ >> 12;               /* 0x423b64-0x423b72 */

    /* Float camera params (the binary stores all five as fstp dword — fixed/int → float):
     *   camFloatX/Y/Z (+0x30/+0x34/+0x38): camera world position * (1/4096) = position in
     *       float world units. Copied to the g_camFloatX/Y/Z globals (camera.c) and used by
     *       the D3D playfield/tile-grid renderer for camera-relative math.
     *   yawFloat/pitchFloat (+0x3C/+0x40): camera yaw/pitch * (2*pi/4096) = angles in radians,
     *       consumed by BuildD3DViewMatrix (sinf/cosf); also copied to the (unread)
     *       g_camYawRad/g_camPitchRad globals. */
    sr_double posScale = POS_SCALE;    /* g_fixedToFloat  @0x51FCA0 = 1/4096    */
    sr_double angScale = ANGLE_SCALE;  /* g_angleToRadians @0x51FCA8 = 2*pi/4096 */

    renderCam->camFloatX = (float)((sr_double)renderCam->worldX * posScale);   /* +0x30 */
    renderCam->camFloatY = (float)((sr_double)renderCam->worldY * posScale);   /* +0x34 */
    renderCam->camFloatZ = (float)((sr_double)renderCam->worldZ * posScale);   /* +0x38 */

    renderCam->yawFloat   = (float)((sr_double)renderCam->yaw   * angScale);  /* +0x3C (radians) */
    renderCam->pitchFloat = (float)((sr_double)renderCam->pitch * angScale);  /* +0x40 (radians) */

    /* ================================================================
     * Build transformation matrices — 0x423b75-0x423b94
     * ================================================================ */
    SetCameraStructPtr((int *)renderCam);                    /* 0x423b75-0x423b7a */
    BuildViewMatrix();                                       /* 0x423b7a: call 0x42316c */
    ComputeCameraBasis();                                    /* 0x423b81: call 0x4230e0 */
    BuildD3DViewMatrix();                                    /* 0x423b88: call 0x42320c */

    /* 0x423b8d-0x423b94: epilogue — restore ESP, pop edi/esi/ecx/ebp, ret */
}
