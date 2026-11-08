/**
 * sky_render.c — Perspective-corrected quad rasterizer
 *
 * General-purpose software quad rasterizer used for the sky/parallax
 * background and ring sprites. Faithfully translates the following
 * functions from the original SONICR.EXE:
 *
 *   SkyVertexTransform      — FUN_00497898 — 291 bytes
 *   UpdatePlayfieldGridPositions  — FUN_0049777C — 283 bytes
 *
 * ComputeParallaxAndPlayfieldState itself remains in stubs.c — it computes scroll
 * offsets and calls these functions for vertex transform + rasterize.
 */

#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "sonicr_functions.h"
#include <math.h>
#include <string.h>

/* =====================================================================
 * Sky vertex data — 0x0069B2C0
 *
 * 12 floats (48 bytes = 0x30) per vertex, 4 vertices per viewport,
 * up to 4 viewports. Stride 0xC0 (192 bytes) per viewport.
 *
 * Per-vertex layout:
 *   [0]  worldX       [1]  worldZ
 *   [2]  texU         [3]  texV
 *   [4]  camX         [5]  camY         [6]  camZ (depth)
 *   [7]  screenX(f)   [8]  screenY(i)   [9]  1/Z
 *   [10] perspU(U/Z)  [11] perspV(V/Z)
 * ===================================================================== */
#define VERTS_PER_VP   4
#define FLOATS_PER_VTX 12
#define VP_STRIDE      (VERTS_PER_VP * FLOATS_PER_VTX)  /* 48 */
#define MAX_VIEWPORTS  4

float g_playfieldVertices[MAX_VIEWPORTS * VP_STRIDE];  /* 0x0069B2C0 */

/* Camera position scale: 1/4096 = 2^-12 (from EXE .data 0x005310BC) */
static const float CAM_POS_SCALE = 1.0f / 4096.0f;

/* Parallax externs */

/* Camera struct — populated by BuildChaseCamera in camera.c */

/* =====================================================================
 * SkyVertexTransform — FUN_00497898 — 291 bytes
 *
 * Transforms one vertex through the camera view matrix.
 * Reads world position from vtx[0],[1] and camera state from cam[].
 * Writes camera-space coords to vtx[4]-[6], screen coords to vtx[7]-[8],
 * and perspective-correct UV to vtx[9]-[11].
 *
 * Original: in_EAX = float* vertex, param_2 = int* camera struct.
 * Camera struct offsets (int*):
 *   [0]-[2]     position (int, worldCoord << 12)
 *   [0x22]-[0x24] view matrix column 0 (int, cast to float for multiply)
 *   [0x26]-[0x28] view matrix column 1
 *   [0x2a]-[0x2c] view matrix column 2
 * ===================================================================== */
void SkyVertexTransform(float *vtx, int *cam)
{
    /* Camera-relative position (world space) */
    float dx = vtx[0] - (float)cam[0] * CAM_POS_SCALE;
    float dy = -((float)cam[1] * CAM_POS_SCALE);
    float dz = vtx[1] - (float)cam[2] * CAM_POS_SCALE;

    /* View matrix rotation: cam[0x22..0x2c] as floats */
    vtx[4] = dz * (float)cam[0x2a] + dy * (float)cam[0x26] + dx * (float)cam[0x22];
    vtx[5] = dz * (float)cam[0x2b] + dy * (float)cam[0x27] + dx * (float)cam[0x23];
    vtx[6] = dz * (float)cam[0x2c] + dy * (float)cam[0x28] + dx * (float)cam[0x24];

    /* Perspective projection — only if in front of near plane (Z >= 1.0) */
    if (vtx[6] >= 1.0f) {
        float invZ = 1.0f / vtx[6];
        vtx[9]  = invZ;
        vtx[10] = vtx[2] * invZ;   /* perspective U = U / Z */
        vtx[11] = vtx[3] * invZ;   /* perspective V = V / Z */

        /* Screen projection — original uses atan2_compute (x87 FPU artifact).
         * Actual operation: screenX = centerX + camX * invZ * projScaleX
         *                   screenY = centerY - camY * invZ * projScaleY */
        vtx[7] = (float)g_screenCenterX + vtx[4] * invZ;
        vtx[8] = (float)(int)((float)g_screenCenterY - vtx[5] * invZ);
    }
}

/**
 * ComputeParallaxAndPlayfieldState — 0x004979BC — 468 bytes
 *
 * Computes the per-viewport horizon Y (from camera pitch) and panorama
 * scroll value (from camera yaw), stores them in g_parallaxState, then
 * calls SkyVertexTransform x4 to transform the playfield grid corners
 * through the camera view matrix.
 *
 * Original: in_EAX = viewport index, param_2 = camera struct pointer.
 * Camera struct fields used:
 *   +0x1C: yaw   (int, 12-bit fixed-point: 0..4095 = 0..360°)
 *   +0x3C: pitch (float, radians)
 *
 * horizonY = (int)(g_screenCenterY + g_projScaleY * sin(pitch) / cos(pitch))
 *          → stored at g_parallaxState[vp*7 + 0] (= vp_struct[+0x14] in binary,
 *            consumed by RenderParallaxStripsD3D / FUN_0045be88).
 *
 * scrollVal = (g_parallaxWidth - 1) - (int)(yaw * parallaxWidth / 4096)
 *           + parallaxWidth * trackMult / 256
 *   Per-track multipliers: Island=0x60, City=0x46, Ruin=0x66,
 *                          Factory=0x6F, Emerald=0x4D.
 *           → stored at g_parallaxState[vp*7 + 1] (= vp_struct[+0x18]),
 *             scrollVal mod parallaxWidth → stripStart=scrollVal/256 in
 *             FUN_0045be88's bandIdx walk.
 *
 * Verified against disassembly: fsin/fcos/fdiv sequence, 0x4e08ca = trunc(),
 * ROM constants: 10000.0 (0x5310c8), -10000.0 (0x5310c0), 4096.0 (0x5310d0).
 * BSS globals: 0x6e98f4 = g_projScaleY, 0x6e98d8 = g_screenCenterY.
 */
/* Binary: EAX = viewport index, EDX = camera struct pointer.
 * Caller (hud_full.c) iterates viewports — pass vpIdx and cam. */
void ComputeParallaxAndPlayfieldState(int vpIdx, int *cam)
{
    int trackId = g_trackId;
    int parallaxW = g_parallaxWidth;
    int parallaxH = g_parallaxExtraX;
    int stateOff = vpIdx * 7;  /* stride 0x1C = 7 ints per viewport */

    /* Horizon Y from camera pitch */
    float pitchRad = *(float *)((char *)cam + 0x3C);
    float sinP = sinf(pitchRad);
    float cosP = cosf(pitchRad);

    int horizonY;
    if (fabsf(cosP) > 1e-7f) {
        horizonY = (int)((float)g_screenCenterY
                       + (float)g_projScaleY * sinP / cosP);
    }
    else {
        horizonY = (sinP >= 0) ? 0x7FFFFFFF : (int)0x80000001;
    }

    if (trackId == TRACK_RADIANT_EMERALD) {
        horizonY += parallaxH / 2;
    }

    g_parallaxState[stateOff + 0] = horizonY;

    /* scrollVal from camera yaw */
    int yawRaw = *(int *)((char *)cam + 0x1C);
    int scrollVal = (parallaxW - 1)
                  - (int)((float)yawRaw * (float)parallaxW / 4096.0f);

    int trackMult;
    switch (trackId) {
        case TRACK_RESORT_ISLAND:
            trackMult = parallaxW * 0x60;
            break;
        case TRACK_RADICAL_CITY:
            trackMult = parallaxW * 0x46;
            break;
        case TRACK_REGAL_RUIN:              /* 0x497a7b: binary id 4 (ruin) -> 111*W */
            trackMult = parallaxW * 0x6F;
            break;
        case TRACK_REACTIVE_FACTORY:        /* 0x497a98: binary id 3 (factory) -> 102*W */
            trackMult = parallaxW * 0x66;
            break;
        case TRACK_RADIANT_EMERALD:
            trackMult = parallaxW * 0x4D;
            break;
        default:
            goto skip_mult;
    }
    scrollVal += trackMult / 256;

skip_mult:
    if (scrollVal >= parallaxW) {
        scrollVal -= parallaxW;
    }

    if (g_mirrorMode != 0) {
        scrollVal = (parallaxW - 1 - scrollVal) + parallaxW / 2;
        if (scrollVal >= parallaxW) {
            scrollVal -= parallaxW;
        }
    }

    g_parallaxState[stateOff + 1] = scrollVal;

    /* Transform 4 playfield grid vertices */
    if (trackId != TRACK_RADIANT_EMERALD) {
        float *base = &g_playfieldVertices[vpIdx * VP_STRIDE];
        SkyVertexTransform(base + 0,  cam);
        SkyVertexTransform(base + 12, cam);
        SkyVertexTransform(base + 24, cam);
        SkyVertexTransform(base + 36, cam);
    }
}

/* =====================================================================
 * UpdatePlayfieldGridPositions — FUN_0049777C — 283 bytes
 *
 * Initializes the 4 playfield grid world-space vertex positions from
 * per-track world bounds. Called during track init.
 * The quad forms a large rectangle in the XZ plane representing
 * the playfield tile grid boundary.
* ===================================================================== */
void UpdatePlayfieldGridPositions(void)
{
    /* Set per-track in track_per_level.c via g_worldBoundsA. */
    float worldScale = g_worldBoundsA;
    if (worldScale < 20.0f) {
        worldScale = 20.0f;
    }
    if (worldScale > 30.0f) {
        worldScale = 30.0f;
    }

    /* Compute quad corners from world bounds and scale.
     * Binary 0x497780: vtx0=(B*A,C*A), vtx1=(D*A,C*A), vtx2=(D*A,E*A), vtx3=(B*A,E*A).
     * So B=leftX, D=rightX, C=topZ, E=botZ. */
    float leftX = g_worldBoundsB * worldScale;
    float rightX = g_worldBoundsD * worldScale;
    float topZ = g_worldBoundsC * worldScale;
    float botZ = g_worldBoundsE * worldScale;

    for (int vp = 0; vp < MAX_VIEWPORTS; vp++) {
        float *v = &g_playfieldVertices[vp * VP_STRIDE];
        v[0] = leftX;
        v[1] = topZ; /* vertex 0: top-left */
        v[12] = rightX;
        v[13] = topZ; /* vertex 1: top-right */
        v[24] = rightX;
        v[25] = botZ; /* vertex 2: bottom-right */
        v[36] = leftX;
        v[37] = botZ; /* vertex 3: bottom-left */
    }
}
