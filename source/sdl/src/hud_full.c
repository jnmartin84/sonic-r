/**
 * hud_full.c — Full HUD rendering dispatcher and countdown
 *
 * RenderHUD dispatches to sub-functions per viewport.
 * DrawFootShadows renders the per-character footstep shadow quads.
 * See HUD_annotated.c for full documentation.
 */

#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "sonicr_functions.h"
#include "player_struct.h"
#include "r_types.h"
#include "r_state.h"
#include "r_draw.h"
#include "nearclip.h"   /* g_scissorEdge + ScissorEdge enum */
#ifdef SONICR_DC
extern void R_SetTileClip(int sx, int sy, int ex, int ey);
extern void R_SetTileClipFullScreen(void);
extern void Pad448_Draw(void);   /* dc/src/pad448.c */
#endif
#include <string.h>
#include <math.h>

/* Digit sprite UV layout on g_tpageParallax1:
 *   Large digits: (digit*8 + baseX, 0x88), size 8×16 each
 *   Small digits: (digit*8 + baseX, 0x92+yoff), size 8×3 each
 *   Colon: (0x50+baseX, 0x88), size 3×16 (large) or (0xB1, y), 8×3 (small)
 *   Results mode: baseX offset by 0x56
 */

#define DIGIT_WIDTH     8
#define LARGE_HEIGHT    16
#define SMALL_HEIGHT    3
#define COLON_WIDTH     3
#define DIGIT_ROW_Y     0x88
#define SMALL_ROW_Y     0x92
#define COLON_UV_X      0x50
#define SMALL_COLON_X   0xB1
#define HUD_COLOR       VERTEX_WHITE  /* light gray tint */
#define HUD_DEPTH_LARGE 0x41200000  /* z-depth for large mode (float 10.0) */

void SetViewportClipRect(int *camBlock);
extern void SetViewportFromConfig(int *);
extern float g_playfieldVertices[];
/* UpdateTrackVertexColors is no longer called from here — the Emerald recolour
 * moved into RenderTrackD3D's per-object body. See the note at the track draw
 * below. */
extern void RenderTrackD3D(void);
extern void RenderAllCharacterModels(int numPlayers);
extern void DrawSnowParticlesGate(int vp);
extern void RenderEnvMappedModel3D(int xOff, int yOff, int zBase,
    int rotA, int rotB, int rotC, int modelIdx, int zNearBias);
extern void R_ClearDepth(void);
extern void RenderHiddenSubEntry(int worldX, int worldZ, int worldY);
extern void DrawMinimapWidget(char *vpPlayer);
extern int QueryTerrainHeight(int worldX, int worldZ, int minY);

extern int g_glBackingWidth;
extern int g_glBackingHeight;

#ifdef SONICR_DC
/* DC split-screen scissor strip-emit for character shadow/trail billboards.
 * Implementation in playfield_render_d3d.c next to the grid scissor path. */
extern int BillboardScissorClipEmitStrip(const RenderVertex *quad);
#endif

extern float g_farClipFullScreenF;

extern GroundParticle g_particleSlots[];  /* 0x008F6C60 — 16 slots */
extern int g_particleAge[];         /* 0x008F6FA0 — per-slot age counter */
extern int g_particleOwner[];       /* 0x008F7020 — viewport that owns this slot */
extern int g_renderStateBlock[];    /* 0x008F6F60 — particle lifetime / visibility */

/* Per-rank placement-glyph UV table at 0x504278 — used by the race-end
 * overlay (Block 1) and the 80x60 placement glyph (Block 3, raceType 1/4).
 *
 * Four parallel arrays in the binary; bases sit 1 dword before the rank-1
 * entry so [base + rank*4] for rank in 1..5 reads valid data. We mirror
 * that 6-entry layout exactly (index 0 is the unused/sentinel slot the
 * binary leaves at base+0). Verified by dumping the bytes from SONICR.EXE
 * and visually confirming the glyphs in ICON00.RAW — see
 * RESULTS_SCREEN_NOTES.md and debug_images/ICON00_rank{1..5}.png. */
static const int s_placementUvX[6] = { -1,   0xC0, 0x00, 0x40, 0x80, 0xC0 };
static const int s_placementUvY[6] = { 0xC0, 0xA9, 0xD4, 0xD4, 0xD4, 0xD4 };
static const int s_placementUvW[6] = { 0xD4, 0x3F, 0x40, 0x40, 0x40, 0x3F };
static const int s_placementUvH[6] = { 0x3F, 0x2B, 0x2C, 0x2C, 0x2C, 0x2B };

/* Per-track minimap viewport offsets — ROM table at 0x5042C4, stride 8.
 * Read in DrawMinimapWidget (0x4D275A) for the vertical 2P split.
 *
 * SWAP FAMILY: the binary indexes this with binary g_trackId (Factory=3,
 * Ruin=4), so ROM slot 3 holds Factory's offsets and slot 4 holds Ruin's.
 * Entries [3] and [4] are swapped from ROM order below so the table stays in
 * OUR trackId order (Ruin=3, Factory=4). Raw ROM order is
 * {44,43} {-3,-7} {2,2} {4,-2} {-16,-7} {-4,1}. */
static const int s_mmViewportTable[][2] = {
    {44, 43},     /* track 0 (unused) */
    {-3, -7},     /* Island */
    { 2,  2},     /* City */
    {-16, -7},    /* Ruin    — ROM slot 4 */
    { 4, -2},     /* Factory — ROM slot 3 */
    {-4,  1},     /* Emerald */
};

/* Per-track ground-shadow entries: (visibility_offset_in_objArray, buffer_index).
 * One per pickup: emeralds (halfSize 0x28), Sonic Tokens (0x1E, five per
 * track), item pickups (0x2D). Sizes live in the init tables in
 * track_anim_init.c. */
typedef struct { int visOff; int bufIdx; } PickupShadowItem;

static const PickupShadowItem s_pickupShadowIsland[] = {
    {0x8472,5}, {0xA122,0}, {0xA166,1}, {0x8582,2},
    {0xA09A,3}, {0xA0DE,4}, {0x9FCE,7}, {0x84FA,8}, {-1,0}
};
static const PickupShadowItem s_pickupShadowCity[] = {
    {0xEE2A,5}, {0xEE6E,6}, {0x10DC6,0}, {0x10D82,1}, {0x10D3E,2},
    {0x10CFA,3}, {0xEEB2,4}, {0x107AA,7}, {0x10766,8}, {0xBBF6,9}, {-1,0}
};
/* Ruin/Factory lists un-crosswired 2026-07-13: binary case 3 (0x46254A,
 * binary convention 3=FACTORY) owns the 0xCF16... list; binary case 4
 * (0x462420, =RUIN) owns the 0xD686... list (whose 0xD686/0xD6CA sit next
 * to the 0xD688/0xD6CC rival objects Ruin's SetupSpecialRace disables). */
static const PickupShadowItem s_pickupShadowRuin[] = {
    {0xD686,5}, {0xD6CA,6}, {0xD026,0}, {0x10656,1}, {0x1069A,2},
    {0x106DE,3}, {0x10722,4}, {0xCF5A,7}, {0x104BE,8}, {0x10502,9}, {-1,0}
};
static const PickupShadowItem s_pickupShadowFactory[] = {
    {0xCF16,0}, {0x104BE,1}, {0x10502,2}, {0x10546,3},
    {0x1058A,4}, {0xCED2,7}, {0x10436,8}, {0x1047A,9}, {-1,0}
};

static const PickupShadowItem *s_pickupShadowTables[] = {
    NULL, s_pickupShadowIsland, s_pickupShadowCity, s_pickupShadowRuin, s_pickupShadowFactory, NULL  /* Emerald: no pickups */
};

/* Shared ground-shadow quad buffers — replaces BSS region 0x68102C-0x68120B.
 * 10 slots × 12 ints, stride 0x30. */
int g_pickupShadowVerts[10 * 12];

/**
 * DrawTimer — 0x004D1970 — 963 bytes
 *
 * Renders a time value as digit sprites: MM:SS:FF
 *
 * timeInFrames: time value in frames (at 60fps internal clock)
 * displayMode: 0=small (3px tall), 1=large (16px tall), 2=results screen
 */
void DrawTimer(int xPos, int yPos, int depth, int timeInFrames, int displayMode)
{
    int baseX = 0;
    int yOffset = 0;

    if (displayMode == 2) {
        yOffset = 3;
        baseX = 0x56;       /* results screen offset */
    }

    /* Time conversion (60fps internal timer):
     *   totalSeconds = time / 60
     *   minutes = totalSeconds / 60
     *   seconds = totalSeconds % 60
     *   frames  = time % 60 → converted to centiseconds for display */
    int totalSeconds = timeInFrames / 60;
    int minutes = totalSeconds / 60;
    int seconds = totalSeconds % 60;
    int subFrames = timeInFrames % 60;
    int centiseconds = (subFrames * 100) / 60;

    /* X cursor advances per digit; Y is fixed from yPos */
    int cx = xPos;

    if (displayMode == 0) {
        /* Small mode (3px tall, used for lap splits) */
        int y = yOffset + SMALL_ROW_Y;
        int sy = yPos + 0x0E;                    /* 0x4d1a72: lea esi, [ebx + 0xe] */

        DrawTexturedQuad(cx, sy, depth, 0x10, 6, g_tpageParallax1,
                         (minutes / 10) * DIGIT_WIDTH + baseX, y,
                         DIGIT_WIDTH, SMALL_HEIGHT, HUD_COLOR);
        cx += 0x10;
        DrawTexturedQuad(cx, sy, depth, 0x10, 6, g_tpageParallax1,
                         (minutes % 10) * DIGIT_WIDTH + baseX, y,
                         DIGIT_WIDTH, SMALL_HEIGHT, HUD_COLOR);
        cx += 0x10;
        DrawTexturedQuad(cx, sy, depth, 6, 6, g_tpageParallax1,
                         SMALL_COLON_X, y,
                         DIGIT_WIDTH, SMALL_HEIGHT, HUD_COLOR);
        cx += 6;
        DrawTexturedQuad(cx, sy, depth, 0x10, 6, g_tpageParallax1,
                         (seconds / 10) * DIGIT_WIDTH + baseX, y,
                         DIGIT_WIDTH, SMALL_HEIGHT, HUD_COLOR);
        cx += 0x10;
        DrawTexturedQuad(cx, sy, depth, 0x10, 6, g_tpageParallax1,
                         (seconds % 10) * DIGIT_WIDTH + baseX, y,
                         DIGIT_WIDTH, SMALL_HEIGHT, HUD_COLOR);
        cx += 0x10;
        DrawTexturedQuad(cx, sy, depth, 6, 6, g_tpageParallax1,
                         SMALL_COLON_X, y,
                         DIGIT_WIDTH, SMALL_HEIGHT, HUD_COLOR);
        cx += 6;
        DrawTexturedQuad(cx, sy, depth, 0x10, 6, g_tpageParallax1,
                         (centiseconds / 10) * DIGIT_WIDTH + baseX, y,
                         DIGIT_WIDTH, SMALL_HEIGHT, HUD_COLOR);
        cx += 0x10;
        DrawTexturedQuad(cx, sy, depth, 0x10, 6, g_tpageParallax1,
                         (centiseconds % 10) * DIGIT_WIDTH + baseX, y,
                         DIGIT_WIDTH, SMALL_HEIGHT, HUD_COLOR);
    }
    else {
        /* Large mode (16px tall, main race timer) */
        DrawTexturedQuad(cx, yPos, HUD_DEPTH_LARGE, 0x10, 0x20, g_tpageParallax1,
                         (minutes / 10) * DIGIT_WIDTH + baseX, DIGIT_ROW_Y,
                         DIGIT_WIDTH, LARGE_HEIGHT, HUD_COLOR);
        cx += 0x10;
        DrawTexturedQuad(cx, yPos, HUD_DEPTH_LARGE, 0x10, 0x20, g_tpageParallax1,
                         (minutes % 10) * DIGIT_WIDTH + baseX, DIGIT_ROW_Y,
                         DIGIT_WIDTH, LARGE_HEIGHT, HUD_COLOR);
        cx += 0x10;
        DrawTexturedQuad(cx, yPos, HUD_DEPTH_LARGE, 6, 0x20, g_tpageParallax1,
                         COLON_UV_X + baseX, DIGIT_ROW_Y,
                         COLON_WIDTH, LARGE_HEIGHT, HUD_COLOR);
        cx += 6;
        DrawTexturedQuad(cx, yPos, HUD_DEPTH_LARGE, 0x10, 0x20, g_tpageParallax1,
                         (seconds / 10) * DIGIT_WIDTH + baseX, DIGIT_ROW_Y,
                         DIGIT_WIDTH, LARGE_HEIGHT, HUD_COLOR);
        cx += 0x10;
        DrawTexturedQuad(cx, yPos, HUD_DEPTH_LARGE, 0x10, 0x20, g_tpageParallax1,
                         (seconds % 10) * DIGIT_WIDTH + baseX, DIGIT_ROW_Y,
                         DIGIT_WIDTH, LARGE_HEIGHT, HUD_COLOR);
        cx += 0x10;
        DrawTexturedQuad(cx, yPos, HUD_DEPTH_LARGE, 6, 0x20, g_tpageParallax1,
                         COLON_UV_X + baseX, DIGIT_ROW_Y,
                         COLON_WIDTH, LARGE_HEIGHT, HUD_COLOR);
        cx += 6;
        DrawTexturedQuad(cx, yPos, HUD_DEPTH_LARGE, 0x10, 0x20, g_tpageParallax1,
                         (centiseconds / 10) * DIGIT_WIDTH + baseX, DIGIT_ROW_Y,
                         DIGIT_WIDTH, LARGE_HEIGHT, HUD_COLOR);
        cx += 0x10;
        DrawTexturedQuad(cx, yPos, HUD_DEPTH_LARGE, 0x10, 0x20, g_tpageParallax1,
                         (centiseconds % 10) * DIGIT_WIDTH + baseX, DIGIT_ROW_Y,
                         DIGIT_WIDTH, LARGE_HEIGHT, HUD_COLOR);
    }
}

/**
 * DrawLapTime — FUN_004D20D0 — 678 bytes
 * Renders individual lap time as smaller digits (6px-wide UV, 12px screen width).
 * EAX=xPos, EDX=yPos, EBX=lapTimeValue (24-bit time, high byte = UV flag)
 *
 * Digit UV: row 0x80, each digit 6px wide (digit * 6 + baseX)
 * Colon UV: baseX + 0x3C, 2px wide
 * Screen: 12px per digit, 4px per colon
 */
void DrawLapTime(int xPos, int yPos, int lapTimeValue)
{
    int baseX = (lapTimeValue & 0xFF000000) ? 0x40 : 0;

    int time24 = lapTimeValue & 0xFFFFFF;
    int totalSeconds = time24 / 60;
    int subFrames = time24 % 60;
    int minutes = totalSeconds / 60;
    int seconds = totalSeconds % 60;
    int centiseconds = (subFrames * 100) / 60;

    int cx = xPos;

    DrawTexturedQuad(cx, yPos, HUD_DEPTH_LARGE, 0xC, 0x10, g_tpageParallax1,
                     (minutes / 10) * 6 + baseX, 0x80, 6, 8, HUD_COLOR);
    cx += 0xC;
    DrawTexturedQuad(cx, yPos, HUD_DEPTH_LARGE, 0xC, 0x10, g_tpageParallax1,
                     (minutes % 10) * 6 + baseX, 0x80, 6, 8, HUD_COLOR);
    cx += 0xC;
    DrawTexturedQuad(cx, yPos, HUD_DEPTH_LARGE, 4, 0x10, g_tpageParallax1,
                     baseX + 0x3C, 0x80, 2, 8, HUD_COLOR);
    cx += 4;
    DrawTexturedQuad(cx, yPos, HUD_DEPTH_LARGE, 0xC, 0x10, g_tpageParallax1,
                     (seconds / 10) * 6 + baseX, 0x80, 6, 8, HUD_COLOR);
    cx += 0xC;
    DrawTexturedQuad(cx, yPos, HUD_DEPTH_LARGE, 0xC, 0x10, g_tpageParallax1,
                     (seconds % 10) * 6 + baseX, 0x80, 6, 8, HUD_COLOR);
    cx += 0xC;
    DrawTexturedQuad(cx, yPos, HUD_DEPTH_LARGE, 4, 0x10, g_tpageParallax1,
                     baseX + 0x3C, 0x80, 2, 8, HUD_COLOR);
    cx += 4;
    DrawTexturedQuad(cx, yPos, HUD_DEPTH_LARGE, 0xC, 0x10, g_tpageParallax1,
                     (centiseconds / 10) * 6 + baseX, 0x80, 6, 8, HUD_COLOR);
    cx += 0xC;
    DrawTexturedQuad(cx, yPos, HUD_DEPTH_LARGE, 0xC, 0x10, g_tpageParallax1,
                     (centiseconds % 10) * 6 + baseX, 0x80, 6, 8, HUD_COLOR);
}

/**
 * DrawItemBoxD3D — 0x0044F98C — 56 bytes (outer loop)
 * Iterates 32 item box entries (stride 0x20) from g_ringChaseArray (0x907B20).
 * For each active entry (+0x18 != 0), extracts world position and calls
 * RenderHiddenSubEntry (0x44F584) to render the billboard.
 */
void DrawItemBoxD3D(void)
{
    char *p = (char *)g_ringChaseArray;
    for (int i = 0; i < 32; i++, p += 0x20) {               /* 0x44F99A: cmp esi, 0x20 */
        if (*(int *)(p + 0x18) == 0) {
            continue;                                       /* 0x44F99F: skip inactive */
        }
        int posX = *(int *)(p + 0) >> 8;                    /* 0x44F9AB: EAX = [ecx] >> 8 */
        int posY = (-(*(int *)(p + 4))) >> 8;               /* 0x44F9B0: neg edx; sar 8 */
        int posZ = *(int *)(p + 8) >> 8;                    /* 0x44F9A5: EBX = [ecx+8] >> 8 */
        RenderHiddenSubEntry(posX, posY, posZ);             /* 0x44F9B8: call 0x44F584 */
    }
}

/**
 * DrawCollectEffectsD3D — 0x00450754 — 1251 bytes
 * Renders collect effect billboard particles (birds, dust, sparkles, etc.)
 * from a 64-entry circular buffer (stride 0x3C = 60 bytes each).
 *
 * EAX = particle buffer base (g_collectEffectBuf, 0x907F20).
 *
 * Per entry layout:
 *   +0x00 int posX        +0x04 int posY (negated)    +0x08 int posZ
 *   +0x1C short halfW     +0x1E short lifetime/halfH
 *   +0x24 int  timer      +0x28 int  scale
 *   +0x2C short uvW       +0x2E short uvH
 *   +0x30 short sp1(size) +0x32 short sp2(animDiv)
 *   +0x34 byte  uvBaseX   +0x35 byte  uvBaseY
 *   +0x36 byte  tpage     +0x38 short uvSpan
 *
 * Billboard projected as axis-aligned screen quad, UV animated from
 * timer/sp2/uvSpan, submitted to tpage batch as 2 triangles (6 indices).
 */
void DrawCollectEffectsD3D(CollectEffect *buf)  /* EAX = buffer */
{
    float invDepthDenom = g_farClipFloat + 16.0f;  /* 0x52C248 = 16.0f */

    R_SetTexEnv(R_TEXENV_MODULATE);

    CollectEffect *entry = buf;
    int addBlend = 0;   /* inside a PushState'd additive run */

    for (int idx = 0; idx < COLLECT_EFFECT_COUNT; idx++, entry++) {
        if (entry->lifetime == 0) {
            continue;
        }

        /* --- Position transform (0x4509F8-0x450A66) --- */
        int dY = ((-entry->posY) >> 8) - g_camIntY;            /* negate Y, >>8 */
        int dX = (entry->posX >> 8) - g_camIntX;
        int dZ = (entry->posZ >> 8) - g_camIntZ;

        /* Camera Z (depth) */
        int camZ = (g_viewMtx02 * dX + g_viewMtx12 * dY + g_viewMtx22 * dZ) / 4096;
        if (camZ < 1 || camZ > g_farClipTimes8) {
            continue;       /* 0x450A60: cmp [0x8fb360] */
        }

        /* Camera X, Y */
        int camX = (g_viewMtx00 * dX + g_viewMtx10 * dY + g_viewMtx20 * dZ) / 4096;
        int camY = (g_viewMtx01 * dX + g_viewMtx11 * dY + g_viewMtx21 * dZ) / 4096;

        /* --- Billboard projection (0x450AC5-0x450B4A) --- */
        int halfSize = entry->billboardSize;

        int sx0 = g_screenCenterX + ((camX - halfSize) * g_projScaleXCurrent) / camZ;
        int sy0 = g_screenCenterY - ((camY + halfSize) * g_projScaleY) / camZ;
        int sx1 = g_screenCenterX + ((camX + halfSize) * g_projScaleXCurrent) / camZ;
        int sy1 = g_screenCenterY - ((camY - halfSize) * g_projScaleY) / camZ;

        /* Adjusted depth for z-buffer (0x450B42): bias closer to camera
         * so effects render in front of the models they're attached to. */
        int adjZ = camZ - 0x48;
        if (adjZ < 1) {
            continue;                                /* 0x450B4D */
        }

        /* --- Clip test (0x450B56-0x450B8C) --- */
        if (sx0 > g_clipRight || sx1 < g_clipLeft) {
            continue;
        }
        if (sy0 > g_clipBottom || sy1 < g_clipTop) {
            continue;
        }

        /* --- UV animation (0x450B92-0x450C0D) --- */
        int timer = entry->timer;
        int animDiv = entry->animDiv;
        int frameIdx = (animDiv != 0) ? (timer / animDiv) : 0;
        int uvSpan = entry->uvSpan;
        int animOff = frameIdx * uvSpan;

        int uvX = entry->uvBaseX + animOff;
        int uvY = (int)entry->uvBaseY;

        /* Direct3d Special case: sparkle particles (uvBaseX=0, uvBaseY=0x60, tpage=charBase) */
#if 0
        if (entry->uvBaseX == 0 && entry->uvBaseY == 0x60 &&   /* 0x450BB7-0x450BCD */
            entry->tpage == (unsigned char)g_tpageCharBase) {
            uvX += 0x40;                                        /* add 64 */
        }
#endif

        float u0 = g_uvLUT256[uvX];                      /* 0x450BD0: [eax*4+0x63FCDC] */
        float v0 = g_uvLUT256[uvY];
        float u1 = g_uvLUT256[uvX + uvSpan - 1];         /* 0x450BF9: [eax*4+0x63FCD8] = table[idx-1] offset trick */
        float v1 = g_uvLUT256[uvY + uvSpan - 1];

        /* --- Alpha from halfW flags (0x450C12-0x450C28 → 0x45077A-0x45078F) --- */
        unsigned char halfWByte = (unsigned char)entry->halfW;
        int alpha;
        if (halfWByte & 0x10) {
            alpha = 0x80;               /* 0x450C18 */
        }
        else if (halfWByte & 0x20) {
            alpha = 0xC0;               /* 0x45077A */
        }
        else {
            alpha = 0xFF;               /* 0x450788 */
        }

        /* --- Fog: depth-based alpha modulation (0x45078F-0x450935) ---
         * Binary uses adjZ (camZ-0x48) for both depth and rhw. */
        float fDepthRatio = reciprocal(adjZ); // 1.0f / (float)adjZ;
        float fDepth = (float)adjZ * reciprocal(invDepthDenom); // / invDepthDenom;
        unsigned char tpage = entry->tpage;
        if (g_tpageStateArray[tpage] != 4) {
            continue;            /* 0x4507A9 */
        }

        /* Fog alpha modulation: near=alpha, far=0, threshold 0.7-0.9 */
        int fogAlpha;
        if (fDepth <= 0.7) {
            fogAlpha = alpha;
        }
        else if (fDepth > 0.9) {
            fogAlpha = 0;
        }
        else {
            int raw = (int)(((sr_double)fDepth - 0.9) * 960.0);
            fogAlpha = SRABS(raw);
        }

        /* Fully fogged out — the quad would contribute nothing. Fog reaches 0
         * at 0.9x the far plane while the depth cull above only rejects at
         * 1.0x, so without this the last 10% of the range is submitted
         * invisible, once per viewport. */
        if (fogAlpha == 0) {
            continue;
        }

        unsigned int baseColor = VERTEX_WHITE_RGB;
        unsigned int color = ((unsigned int)fogAlpha << 24) | baseColor;

        /* Sparkle entries (halfW bit 0x20: ring/item collect, invincibility,
         * Super Sonic aura — the MISC00 y=0x60 family) draw ADDITIVE so they
         * glow; dust/fire/splashes/birds keep the ambient blend. Push/pop
         * only on runs so state churn stays minimal. */
        if (halfWByte & 0x20) {
            if (!addBlend) {
                R_PushState();
                R_SetBlendMode(R_BLEND_ADDITIVE);
                addBlend = 1;
            }
        }
        else if (addBlend) {
            R_PopState();
            addBlend = 0;
        }

        R_SetTexture(tpage);

        RenderVertex __attribute__((aligned(32))) verts[4] = {
            { (float)sx0, (float)sy0, fDepth, fDepthRatio, color, 0, u0, v0 },
            { (float)sx1, (float)sy0, fDepth, fDepthRatio, color, 0, u1, v0 },
            { (float)sx1, (float)sy1, fDepth, fDepthRatio, color, 0, u1, v1 },
            { (float)sx0, (float)sy1, fDepth, fDepthRatio, color, 0, u0, v1 },
        };
        R_DrawQuad(verts);
    }

    if (addBlend) {
        R_PopState();
    }
}

/**
 * RenderHUD — 0x004CD2A8 — 1257 bytes
 * Master HUD function. Two complete render paths (software + D3D).
 */
extern void xbox_race_mark(const char *s);
void RenderHUD(void)
{
    unsigned int startViewport, viewportCount;

    if (g_netSessionActive == 0 && g_isNetworkGame == 0) {
        startViewport = 0;
        viewportCount = g_numHumans;
    }
    else {
        startViewport = (unsigned int)(unsigned short)g_localPlayerIndex; /* 0x4CD2CD */
        viewportCount = 1;
    }

    xbox_race_mark("HUD ProcessTpageStates");
    ProcessTpageStates();
    xbox_race_mark("HUD BeginFrame");
    BeginFrame();

    /* Background (full-screen) */
    SetViewportFromConfig(g_viewportArray);
    if (g_numHumans > 1) {
        g_clipLeft = 0; g_clipTop = 0;
        g_clipRight = g_screenWidth - 1; g_clipBottom = g_screenHeight - 1;
    }
    g_scissorEdge = SCISSOR_NONE;
    g_cpuClipEdge = SCISSOR_NONE;
#ifdef SONICR_DC
    /* The background is full-screen, and the clip region survives from the
     * last viewport of the previous frame — widen it before drawing. */
    R_SetTileClipFullScreen();
#endif

    xbox_race_mark("HUD RenderBackground");
    RenderBackground();

    /* Per-viewport rendering */
    unsigned int endVp = startViewport + viewportCount;
    for (unsigned int vp = startViewport; vp < endVp; vp++) {

        /* Set per-viewport camera before rendering */
        g_currentRenderCam = (int *)((char *)g_viewportConfigArray + vp * 0xC8);
        SetViewportFromConfig(g_viewportArray + vp * 21);

        /* Enable scissor for split-screen viewports */
        if (g_numHumans > 1) {
            int *vpc = g_viewportArray + vp * 21;
            float sx = (float)g_glBackingWidth / (float)g_screenWidth;
            float sy = (float)g_glBackingHeight / (float)g_screenHeight;
            R_SetScissor(g_glViewportOffsetX + (int)(vpc[0] * sx),
                         g_glViewportOffsetY + (int)((g_screenHeight - vpc[3] - 1) * sy),
                         (int)(vpc[9] * sx),
                         (int)(vpc[10] * sy));
            R_FlushState();
        }
        SetViewportClipRect(g_currentRenderCam);

        /* Derive the inward-facing edges for DC CPU scissoring. An edge is
         * inward when this viewport's clip rect stops short of the physical
         * screen edge — PVR culls the real screen edges at tile boundaries
         * for free, so only these need a CPU clip.
         *
         * A 2-player split yields exactly one bit (horizontal or vertical);
         * a 4-player quadrant yields two (BOTTOM|RIGHT for the top-left view,
         * and so on). OR rather than else-if so both are picked up; with a
         * single bit set every consumer takes the same single-edge path it
         * took before. SCISSOR_NONE in single-player. */
        g_scissorEdge = SCISSOR_NONE;
        g_cpuClipEdge = SCISSOR_NONE;
        if (g_numHumans > 1) {
            if (g_clipBottom < g_screenHeight - 1) {
                g_scissorEdge |= SCISSOR_BOTTOM;
            }
            if (g_clipTop > 0) {
                g_scissorEdge |= SCISSOR_TOP;
            }
            if (g_clipRight  < g_screenWidth  - 1) {
                g_scissorEdge |= SCISSOR_RIGHT;
            }
            if (g_clipLeft   > 0) {
                g_scissorEdge |= SCISSOR_LEFT;
            }
        }

        /* Split the two roles apart. g_scissorEdge above stays the
         * split-screen state that fog, grid LOD, the far-clip reduction and
         * the static-water switch read. g_cpuClipEdge is the clip
         * specification, and on DC the PVR user tile clip owns the viewport
         * edges — so the CPU scissor stands down and near-plane clipping is
         * the only CPU clip left. See PLAN_DC_HW_TILE_CLIP.md. */
#ifdef SONICR_DC
        g_cpuClipEdge = SCISSOR_NONE;
#else
        g_cpuClipEdge = g_scissorEdge;
#endif

#ifdef SONICR_DC
        /* Hand this viewport's bounds to the PVR as an inclusive tile rect.
         * The rects are already inclusive and tile-aligned by
         * ApplyViewportGeometry, so a plain >> 5 is exact. */
#ifdef SONICR_DC_240P
        /* A tile is 64 virtual units in 240P — 32 real, and the backend halves
         * x/y at submit — hence >> 6 rather than >> 5. These bounds are
         * INCLUSIVE coordinates, so the shift already yields the inclusive
         * tile index; no -1, exactly as in the 480 branch below. */
        R_SetTileClip(g_clipLeft >> 6, g_clipTop >> 6,
                      g_clipRight >> 6, g_clipBottom >> 6);
#else
        R_SetTileClip(g_clipLeft >> 5, g_clipTop >> 5,
                      g_clipRight >> 5, g_clipBottom >> 5);
#endif
#endif

#ifdef SONICR_DC
        /* Reduce the far-plane distance in split-screen on DC. Each frame
         * has to render two viewports of geometry; clipping draw distance
         * keeps the per-frame load down. 3/4 was tuned by hand — 1/2 was
         * locked at 30 fps but unplayable. Restored at end of iter below. */
        int   savedFarClipDepth = g_farClipDepth;
        float savedFarClipFloat = g_farClipFloat;
        int   savedFarClipTimes8 = g_farClipTimes8;
        /* Capture the unmodified far value so the grid LOD quad can
         * extend out to the original far plane even though per-tile
         * rendering uses the halved value. Set every viewport iter so
         * subsequent reads always see the latest pre-halve value. */
        g_farClipFullScreenF = g_farClipFloat;
        if (g_scissorEdge != SCISSOR_NONE) {
            g_farClipDepth = g_farClipDepth * 0.7f;//(g_farClipDepth >> 1) + (g_farClipDepth >> 3);
            g_farClipFloat *= 0.7f;//625f;
            /* g_farClipTimes8 is the distance-cull threshold every object
             * renderer compares against (the binary's 0x8FB360; see
             * RenderHiddenSubEntry 0x44F5F3, DrawCharacterSprite, the HUD
             * sites here). It must follow the reduction or those culls keep
             * using the full-screen distance and objects draw far past the
             * geometry around them. Derived from g_farClipDepth so it stays
             * exactly `farClip << 3` as InitFarClipAndFog defines it. */
            g_farClipTimes8 = g_farClipDepth << 3;
        }
#endif

        xbox_race_mark("HUD ComputeParallaxAndPlayfieldState");
        ComputeParallaxAndPlayfieldState(vp, g_currentRenderCam);

        xbox_race_mark("HUD RenderParallaxStripsD3D");
        RenderParallaxStripsD3D((int)vp);

        if (g_trackId != TRACK_RADIANT_EMERALD) {
            xbox_race_mark("HUD RenderWaterReflectionD3D");
            RenderWaterReflectionD3D((int)vp);
        }

        if (g_trackId != TRACK_RADIANT_EMERALD) {
            if (g_objectRenderEnable) {
                float *skyBase = &g_playfieldVertices[vp * 48];
                float *skyUpper = skyBase + 24;
                int *vpCfg = (int *)((char *)g_viewportConfigArray + vp * 0xC8);
                xbox_race_mark("HUD RenderPlayfieldGridD3D");
                RenderPlayfieldGridD3D(skyBase, skyUpper, vpCfg);
            }
        }

        if (g_demoMode == DEMO_NONE) {
            int hudPlayerIdx = (g_isNetworkGame != 0) ? g_localPlayerIndex : (int)vp;
            xbox_race_mark("HUD DrawTimerAndStatus");
            DrawTimerAndStatus(hudPlayerIdx);
        }

        /* Track draws before characters — matches old batch flush order.
         *
         * Emerald's vertex recolour used to be hoisted here as one pass over
         * the whole vertex array, per viewport. The binary does it per object
         * inside RenderTrackD3D (0x453B47) — after that object's transform
         * loop, only for objects that survived culling, and only for vertices
         * in front of the camera. See TrackEmeraldRecolourObject in
         * render_track_internal.h. */
#ifdef BLURRY
//SONICR_DC
        R_SetFilter(R_FILTER_LINEAR);
#endif
        xbox_race_mark("HUD RenderTrackD3D");
        RenderTrackD3D();
#ifdef BLURRY
//SONICR_DC
        R_SetFilter(R_FILTER_NEAREST);
#endif
        xbox_race_mark("HUD RenderAllCharacterModels");
        RenderAllCharacterModels(g_numPlayers);

        if (g_raceSubMode == SUBMODE_BALLOON) {
            xbox_race_mark("HUD AnimateBalloons");
            AnimateBalloons();
        }

        xbox_race_mark("HUD DrawItemBoxD3D");
        DrawItemBoxD3D();

        xbox_race_mark("HUD DrawCollectEffectsD3D");
        DrawCollectEffectsD3D(g_collectEffectBuf);

        if (SHADOWS_ENABLED(SPLIT_PICKUP_SHADOWS)) {
            xbox_race_mark("HUD DrawPickupShadows");
            DrawPickupShadows();
        }

        if (g_trackId != TRACK_RADIANT_EMERALD) {
            xbox_race_mark("HUD DrawGroundParticlesD3D");
            DrawGroundParticlesD3D((int)vp);
            xbox_race_mark("HUD DrawSnowParticlesGate");
            DrawSnowParticlesGate(vp);
        }

        if (g_postRaceCameraMode == 0 && SHADOWS_ENABLED(SPLIT_FOOT_SHADOWS)) {
            xbox_race_mark("HUD DrawFootShadows");
            DrawFootShadows();
        }

        xbox_race_mark("HUD DrawOtherParticles");
        DrawOtherParticles();

        /* Demo/replay rotating "R" overlay — binary 0x4CD71B */
        if (g_skipThisFrame == 0 && g_demoMode != DEMO_NONE && vp == startViewport) {
            R_ClearDepth();
            RenderEnvMappedModel3D(
                -1500,                              /* xOff — top-left */
                1125,                               /* yOff */
                3072,                               /* zBase — small/far */
                (g_totalFrames & 0x3F) << 6,        /* rotA — yaw, 64-frame cycle */
                (g_totalFrames & 0x7F) << 5,        /* rotB — pitch, 128-frame cycle */
                (g_totalFrames & 0xFF) << 4,        /* rotC — roll, 256-frame cycle */
                0,                                  /* modelIdx 0 = "R" letter */
                -2816);                             /* zNearBias */
        }

        if (g_numHumans > 1) {
            R_DisableScissor();
            R_FlushState();
        }
        g_scissorEdge = SCISSOR_NONE;
        g_cpuClipEdge = SCISSOR_NONE;
#ifdef SONICR_DC
        g_farClipDepth = savedFarClipDepth;
        g_farClipFloat = savedFarClipFloat;
        g_farClipTimes8 = savedFarClipTimes8;
#endif

    }

    /* Full-screen overlays — 0x4cd484: mov eax, 0x8fb4b8 / call 0x4cc0e8.
     *
     * Slot 4 is the full-screen UI viewport (SetupViewportConfig fills it).
     * It has to be slot 4 rather than slot 0 with a widened clip rect:
     * SetViewportFromConfig also loads g_screenCenterX/Y and
     * g_projScaleXCurrent/Y, and the split border, pause overlay and fade
     * iris all position and scale from those. Slot 0 in split-screen is a
     * half-screen viewport, so the iris would open around the top-half
     * centre at half-height scale. */
    SetViewportFromConfig(g_viewportArray + 4 * 21);

    g_scissorEdge = SCISSOR_NONE;
    g_cpuClipEdge = SCISSOR_NONE;
#ifdef SONICR_DC
    /* Widen the tile clip to match. Everything below is full-screen — the
     * split border, pause overlay and fade iris named in the comment above —
     * and the region is still the last viewport's, so without this they get
     * clipped into that quadrant. */
    R_SetTileClipFullScreen();

    /* Fill the scanlines below the render height in the modes that render
     * 448 into a 480-line framebuffer. Has to be after the widen above — the
     * bar is outside every split viewport's clip region — and before the fade
     * iris below, so a fade to black covers it like everything else. */
    Pad448_Draw();
#endif

    if (g_numHumans > 1) {
        xbox_race_mark("HUD DrawSplitBorder");
        DrawSplitBorder();
    }

    xbox_race_mark("HUD DrawPauseOverlay");
    DrawPauseOverlay();

    /* Fade iris — 0x4cd48e: cmp [0x901c44], 0 / jge */
    if (g_fadeLevel < 0) {
        xbox_race_mark("HUD RenderFadeOverlay");
        RenderFadeOverlay();                            /* 0x4cd497 */
    }

    xbox_race_mark("HUD EndFrame");
    EndFrame();
}

/* Character footstep-shadow billboards — populated by BuildFootShadowQuad (0x482BFC),
 * drawn by DrawFootShadows, cleared at race start by the 80-entry loops in
 * InitTrackCommon / SetupSpecialRace / AdvanceGrandPrixTrack. */
int g_footShadowCtrl[80 * 4];    /* 0x00908E20 — 80 entries × 4 ints, visibility at [3] */
int g_footShadowVerts[80 * 64];  /* 0x00909320 — 80 entries × 4 verts × 16 ints */


/**
 * DrawFootShadows — 0x00458E6C — 2896 bytes — VALIDATED
 *
 * Renders the footstep shadow quads as 3D projected geometry.
 * Iterates 80 quad entries (5 players × 16 rotating slots): transforms
 * 4 vertices through camera matrix, projects to screen, backface culls,
 * clips, and submits to tpage batch.
 *
 * Binary call: EAX = 0x908E20 (control/visibility array base).
 * Vertex data at 0x909320 (80 entries × 4 verts × 16 ints per vert).
 * UV from g_uvLUT256. Renders on g_tpageCharBase.
 * Data populated by BuildFootShadowQuad (0x482BFC).
 */
void DrawFootShadows(void)
{
    int tpage = g_tpageCharBase;

    /* depthScale = g_farClipFloat + (-28.0f) — ROM constant at 0x52C314 */
    float depthScale = g_farClipFloat - 28.0f;
    float recipDepthScale = reciprocal(depthScale);

    if (g_tpageStateArray[tpage] != 4) {
        return;
    }

    /* Load UV coordinates from g_uvLUT256 */
    float uvLeft   = g_uvLUT256[0];    /* 0x63FCDC */
    float uvRight  = g_uvLUT256[31];   /* 0x63FD58 — (0xFD58-0xFCDC)/4 = 31 */
    float uvTop    = g_uvLUT256[48];   /* 0x63FD9C — (0xFD9C-0xFCDC)/4 = 48 */
    float uvBottom = g_uvLUT256[79];   /* 0x63FE18 — (0xFE18-0xFCDC)/4 = 79 */

    int *control = g_footShadowCtrl;       /* 0x908E20 — visibility array */
    int *v0base = g_footShadowVerts;       /* 0x909320 — vertex 0 */
    int *v1base = g_footShadowVerts + 16;  /* +0x40 bytes — vertex 1 */
    int *v2base = g_footShadowVerts + 32;  /* +0x80 bytes — vertex 2 */
    int *v3base = g_footShadowVerts + 48;  /* +0xC0 bytes — vertex 3 */

    for (int i = 0; i < 80; i++,
         control += 4, v0base += 64, v1base += 64, v2base += 64, v3base += 64)
    {
        /* Visibility check: control[3] (offset 0x0C) != 0 */
        if (control[3] == 0) {
            continue;
        }

        /* Transform vertex 0 (edi in binary) */
        int dX, dY, dZ, cz0, cx0, cy0;
        dX = v0base[5] - g_camIntX;   /* world X at offset 0x14 */
        dY = v0base[6] - g_camIntY;   /* world Y at offset 0x18 */
        dZ = v0base[7] - g_camIntZ;   /* world Z at offset 0x1C */

        cz0 = (g_viewMtx02 * dX + g_viewMtx12 * dY + g_viewMtx22 * dZ) / 4096;
        if (cz0 < 1 || cz0 > g_farClipTimes8) {
            continue; /* 0x458FFB: cmp [0x8fb360] */
        }

        cx0 = (g_viewMtx00 * dX + g_viewMtx10 * dY + g_viewMtx20 * dZ) / 4096;
        cy0 = (g_viewMtx01 * dX + g_viewMtx11 * dY + g_viewMtx21 * dZ) / 4096;

        v0base[0] = g_screenCenterX + (g_projScaleXCurrent * cx0) / cz0;
        v0base[1] = g_screenCenterY - (g_projScaleY * cy0) / cz0;
        v0base[12] = cz0 - 0x1C;  /* adjusted depth at offset 0x30 */
        if (v0base[12] < 1) {
            continue;
        }

        /* Transform vertex 1 (ecx in binary) */
        int cz1, cx1, cy1;
        dX = v1base[5] - g_camIntX;
        dY = v1base[6] - g_camIntY;
        dZ = v1base[7] - g_camIntZ;

        cz1 = (g_viewMtx02 * dX + g_viewMtx12 * dY + g_viewMtx22 * dZ) / 4096;
        if (cz1 < 1 || cz1 > g_farClipTimes8) {
            continue; /* cmp [0x8fb360] */
        }

        cx1 = (g_viewMtx00 * dX + g_viewMtx10 * dY + g_viewMtx20 * dZ) / 4096;
        cy1 = (g_viewMtx01 * dX + g_viewMtx11 * dY + g_viewMtx21 * dZ) / 4096;

        v1base[0] = g_screenCenterX + (g_projScaleXCurrent * cx1) / cz1;
        v1base[1] = g_screenCenterY - (g_projScaleY * cy1) / cz1;
        v1base[12] = cz1 - 0x1C;
        if (v1base[12] < 1) {
            continue;
        }

        /* Transform vertex 2 */
        int cz2, cx2, cy2;
        dX = v2base[5] - g_camIntX;
        dY = v2base[6] - g_camIntY;
        dZ = v2base[7] - g_camIntZ;

        cz2 = (g_viewMtx02 * dX + g_viewMtx12 * dY + g_viewMtx22 * dZ) / 4096;
        if (cz2 < 1 || cz2 > g_farClipTimes8) {
            continue; /* cmp [0x8fb360] */
        }

        cx2 = (g_viewMtx00 * dX + g_viewMtx10 * dY + g_viewMtx20 * dZ) / 4096;
        cy2 = (g_viewMtx01 * dX + g_viewMtx11 * dY + g_viewMtx21 * dZ) / 4096;

        v2base[0] = g_screenCenterX + (g_projScaleXCurrent * cx2) / cz2;
        v2base[1] = g_screenCenterY - (g_projScaleY * cy2) / cz2;
        v2base[12] = cz2 - 0x1C;
        if (v2base[12] < 1) {
            continue;
        }

        /* Backface cull using vertices 0, 1, 2 */
        int cross;
        if (g_mirrorMode == 0) {
            cross = (v0base[1] - v1base[1]) * (v2base[0] - v1base[0])
                  - (v0base[0] - v1base[0]) * (v2base[1] - v1base[1]);
        }
        else {
            cross = (v2base[1] - v1base[1]) * (v0base[0] - v1base[0])
                  - (v0base[1] - v1base[1]) * (v2base[0] - v1base[0]);
        }
        if (cross < 0) {
            continue;
        }

        /* Transform vertex 3 */
        int cz3, cx3, cy3;
        dX = v3base[5] - g_camIntX;
        dY = v3base[6] - g_camIntY;
        dZ = v3base[7] - g_camIntZ;

        cz3 = (g_viewMtx02 * dX + g_viewMtx12 * dY + g_viewMtx22 * dZ) / 4096;
        if (cz3 < 1 || cz3 > g_farClipTimes8) {
            continue; /* cmp [0x8fb360] */
        }

        cx3 = (g_viewMtx00 * dX + g_viewMtx10 * dY + g_viewMtx20 * dZ) / 4096;
        cy3 = (g_viewMtx01 * dX + g_viewMtx11 * dY + g_viewMtx21 * dZ) / 4096;

        v3base[0] = g_screenCenterX + (g_projScaleXCurrent * cx3) / cz3;
        v3base[1] = g_screenCenterY - (g_projScaleY * cy3) / cz3;
        v3base[12] = cz3 - 0x1C;
        if (v3base[12] < 1) {
            continue;
        }
        /* Clip test */
        if (!(g_clipLeft <= v0base[0] || g_clipLeft <= v1base[0] ||
              g_clipLeft <= v2base[0] || g_clipLeft <= v3base[0])) {
            continue;
        }

        if (!(v0base[0] <= g_clipRight || v1base[0] <= g_clipRight ||
              v2base[0] <= g_clipRight || v3base[0] <= g_clipRight)) {
            continue;
        }
        if (!(g_clipTop <= v0base[1] || g_clipTop <= v1base[1] ||
              g_clipTop <= v2base[1] || g_clipTop <= v3base[1])) {
            continue;
        }
        if (!(v0base[1] <= g_clipBottom || v1base[1] <= g_clipBottom ||
              v2base[1] <= g_clipBottom || v3base[1] <= g_clipBottom)) {
            continue;
        }
        /* Build 4 vertices with fog/alpha */
        int *vertPtrs[4] = { v0base, v1base, v2base, v3base };
        float us[4] = { uvLeft, uvRight, uvRight, uvLeft };
        float uvVs[4] = { uvTop, uvTop, uvBottom, uvBottom };

        RenderVertex quadVerts[4];
        for (int vi = 0; vi < 4; vi++) {
            int adjDepth = vertPtrs[vi][12];
            float fDepth = (float)adjDepth * recipDepthScale; // / depthScale;

            /* Fog alpha: 0xC0 (near) → 0 (far), thresholds 0.7 / 0.9 */
            int alpha;
            if (fDepth > 0.9f) {
                alpha = 0;
            }
            else if ((sr_double)fDepth <= 0.7) {
                alpha = 0xC0;
            }
            else {
                int raw = (int)(((sr_double)fDepth - 0.9) * 960.0);
                alpha = SRABS(raw);
            }

            quadVerts[vi].sx = (float)vertPtrs[vi][0];
            quadVerts[vi].sy = (float)vertPtrs[vi][1];
            quadVerts[vi].sz = fDepth;
            quadVerts[vi].rhw = reciprocal((float)adjDepth); // 1.0f / (float)adjDepth;
            quadVerts[vi].color = ((unsigned int)alpha << 24) | VERTEX_WHITE_RGB;
            quadVerts[vi].specular = 0;
            quadVerts[vi].u = us[vi];
            quadVerts[vi].v = uvVs[vi];
        }

        R_SetTexture(tpage);
        R_SetTexEnv(R_TEXENV_MODULATE);
        R_SetDepthWrite(0);
#ifdef SONICR_DC
        if (g_scissorEdge != SCISSOR_NONE) {
            /* Split-screen: clip the billboard quad against the active
             * viewport edge so character shadows / boost trails don't
             * bleed into the other player's view. */
            BillboardScissorClipEmitStrip(quadVerts);
        } else {
            R_DrawQuad(quadVerts);
        }
#else
        R_DrawQuad(quadVerts);
#endif
    }
    R_SetDepthWrite(1);
}

/* Token-challenge rival per track — see the identical table in
 * track_anim_init.c for the derivation. The binary's ring-token counter is
 * gated on `[g_trackId*4 + 0x8FBA74]` (0x4D3733), which is really
 * g_charUnlockTable[4 + binary trackId]; our g_trackId has Ruin/Factory
 * swapped, so index by CHARACTER. Entry [5] (Radiant Emerald) is never
 * reached — the caller already excludes it. */
static const int s_tokenRivalChar[] = {
    -1, CHAR_METAL_SONIC, CHAR_TAILS_DOLL, CHAR_EGG_ROBO,
    CHAR_METAL_KNUCKLES, -1
};

/**
 * DrawTimerAndStatus — 0x004D2C68 — 3808 bytes
 * Main in-race status: timer, lap times, character icons, position ladder.
 * Translated from binary disassembly.
 */
void DrawTimerAndStatus(int vpIndex)
{
    /* Binary receives viewport index in EAX (Watcom fastcall), saved to [ebp-0x24].
     * g_viewportIndex is a SPLIT MODE indicator (0=horiz, 1=vert), NOT the loop
     * counter — the binary never writes g_viewportIndex during the render loop.
     * All positioning checks read g_viewportIndex, matching the binary;
     * player indexing uses vpIndex. Do NOT substitute g_splitScreenMode
     * (0x8FD45C) here — that is the persisted options value, read by the
     * binary in only 9 places (options screen, save, and the one derivation
     * in SetupViewportConfig). The two agree for g_numHumans == 2 but
     * diverge at 3-4 players, where g_viewportIndex stays pinned at 1. */

    /* Visibility checks (0x4D2C76-0x4D2CCA) */
    int showTimer = 1;
    if (g_numHumans > 2 && g_screenWidthFull < 0xA0) {
        showTimer = 0;
    }

    /* Binary 0x4D2C99-0x4D2CCB: split-screen vertical uses 0xA0 threshold,
     * everything else uses 0x140. */
    int showLapTimes = 1;
    if (g_numHumans == 2 && g_viewportIndex == 1) {           /* 0x4D2CA9 */
        if (g_screenWidthFull < 0xA0) {
            showLapTimes = 0;
        }
    } else {
        if (g_screenWidthFull < 0x140) {
            showLapTimes = 0;
        }
    }

    /* Player pointer (0x4D2CCC-0x4D2CE9) */
    Player *vpPlayer = &((Player *)g_playerBase)[vpIndex];

    /* Position indicator (0x4D2CEB) — binary passes player ptr in EAX */
    DrawReverseIndicator(vpPlayer);

    /* Timer + lap times (0x4D2D01-0x4D2EAD) */
    if (g_postRaceCameraMode == 0) {
        int lap1 = vpPlayer->lap1Time & 0xFFFFFF;
        int lap2 = vpPlayer->lap2Time & 0xFFFFFF;
        int lap3 = vpPlayer->lap3Time & 0xFFFFFF;
        int totalTime = lap1 + lap2 + lap3;

        /* Main timer — top right.  Binary 0x4D2D81-0x4D2D9B:
         * 2P vertical split uses X=0xC0, else X=0x200. */
        int timerX = 0x200;                                      /* 0x4D2D8F / 0x4D2E52 */
        if (g_numHumans == 2 && g_viewportIndex == 1) {           /* 0x4D2D82 */
            timerX = 0xC0;                                       /* 0x4D2D9B */
        }
        DrawTimer(timerX, 0x10, 0x41200000, totalTime, 1);

        /* Individual lap times — binary 0x4D2DF0: X = timerX + 0x20 */
        if (g_raceSubMode < 2 && showLapTimes) {
            int lapX = timerX + 0x20;                            /* 0x4D2DF0 */
            /* Pass the UNMASKED lap values (binary 0x4D2DF8/0x4D2E08/0x4D2E18
             * load [player+0x50/54/58] whole). The high-byte flag, set by
             * ValidateLapCompletion when a lap ties/beats the per-character
             * best, drives DrawLapTime's +0x40 UV offset → the best lap renders
             * in the yellow glyph row. The masked lap1/2/3 locals above are only
             * for the totalTime sum (which the binary also masks). */
            DrawLapTime(lapX, 0x34, vpPlayer->lap1Time);
            DrawLapTime(lapX, 0x46, vpPlayer->lap2Time);
            DrawLapTime(lapX, 0x58, vpPlayer->lap3Time);
        }

        /* Character-specific icons (0x4D2EAD-0x4D2FC9) */
        int charId = vpPlayer->charId;
        int iconX = (g_numHumans == 2 && g_viewportIndex == 1)   /* 0x4D2EF4 */
                        ? 0x98 : 0x1D8;
        if (!showTimer) {
            iconX += 0x78;
        }

        if (charId == CHAR_AMY) {
            int boostTimer = vpPlayer->abilityTimer;             /* P_INT(0x7C)>>16 = abilityTimer */
            if (boostTimer == 0 || (g_collectAnimTimer < 8 && boostTimer > 300)) {
                DrawTexturedQuad(iconX, 0x10, 0x41200000, 0x20, 0x20, g_tpageParallax1,
                                 0x90, 0x70, 0x10, 0x10, VERTEX_WHITE);
            }
        }
        if ((charId == CHAR_EGGMAN || charId == CHAR_EGG_ROBO) &&
            vpPlayer->ringCount >= 0xA &&                        /* P_INT(0x16)>>16 = ringCount */
            vpPlayer->abilityTimer == 0) {                       /* P_INT(0x7C)>>16 = abilityTimer */
            DrawTexturedQuad(iconX, 0x10, 0x41200000, 0x20, 0x20, g_tpageParallax1,
                             0xA0, 0x70, 0x10, 0x10, VERTEX_WHITE);
        }
    }

    /* ==== raceSubMode==2 (Tag 4) per-player status loop — 0x4D2FC9-0x4D306F ====
     *
     * Per Sonic Retro wiki, raceSubMode==2 is "Tag 4" (NOT Balloon Hunt —
     * that's subMode==3). Iterates players 1..4 (player 0 is the viewport
     * player and is skipped — ecx starts at 0x71C, the player stride, not 0).
     *
     * Each iteration draws ONE icon on the LEFT side of the screen at a
     * fixed Y. The icon is BIG (48x48) when player.finishState == 2 (the
     * field appears repurposed as "tagged" in Tag 4), or SMALL (32x32 with
     * Y offset by +8) otherwise.
     *
     * After the loop, the binary's je 0x4D38FA jumps PAST every other
     * in-race HUD element directly to the countdown SFX block. Replicated
     * in C as a goto past Block 2, Block 1, ring counter, position ladder,
     * Block 3, ring tokens, and (future) Block 5 to the subMode2_loop_exit
     * label right before the countdown SFX block.
     *
     * Binary structure:
     *   0x4D2FD6: ebx=0x60 (Y), ecx=0x71C (player offset)
     *             esi=0x18, edi=0x10 (used as positions/sizes by sub-branch)
     *   0x4D2FEA: jmp 0x4D3031 (skip first iter draw, go straight to test)
     *   0x4D2FEC: <small draw> at (0x18, ebx+8) 32x32, uv (charId*16, 0xD8, 16, 16)
     *   0x4D301C: ebx += 0x48; ecx += 0x71C
     *   0x4D3025: cmp ebx, 0x180; je 0x4D38FA  (loop exit jumps far)
     *   0x4D3031: load [ecx + 0x8FD6B2] dword, sar 16 → player[i].finishState (short @ +0x1C0)
     *   0x4D303D: jne 0x4D2FEC (do small draw)
     *   0x4D303F: <big draw> at (0x10, ebx) 48x48, uv (charId*0x18, 0xE8, 0x18, 0x18)
     *   0x4D306F: jmp 0x4D301C (back to increment)
     */
    if (g_raceSubMode == 2) {
        Player *p = &((Player *)g_playerBase)[1];   /* 0x4D2FDB: ecx = 0x71C, one stride */
        for (int i = 1; i < 5; i++, p++) {
            int yPos = (i - 1) * 0x48 + 0x60;        /* 0x60, 0xA8, 0xF0, 0x138 */
            if (p->finishState == 2) {                /* 0x4D3031: dword @ +0x1BE >>16 == 2 */
                /* Big 48x48 icon — finished/tagged */
                DrawTexturedQuad(0x10, yPos, 0x41200000, 0x30, 0x30,    /* 0x4D306A */
                                 g_tpageParallax1,
                                 p->charId * 0x18, 0xE8, 0x18, 0x18,
                                 VERTEX_WHITE);
            } else {
                /* Small 32x32 icon — still racing/untagged */
                DrawTexturedQuad(0x18, yPos + 8, 0x41200000, 0x20, 0x20, /* 0x4D3017 */
                                 g_tpageParallax1,
                                 p->charId * 0x10, 0xD8, 0x10, 0x10,
                                 VERTEX_WHITE);
            }
        }
        goto subMode2_loop_exit;                       /* 0x4D302B: je 0x4D38FA */
    }

    /* ==== raceType==4 race-end sub-block — 0x4D307E-0x4D3105 ====
     *
     * raceType==4 is NOT in the Sonic Retro wiki's authoritative game-mode
     * table (which lists 0=GP, 1=Multiplayer, 2=Time Attack, 3=VS Challenge).
     * It appears to be an internal post-upgrade state — main.c:1768 sets
     * g_raceType=4 from inside the raceType==3 (VS Challenge) handler, and
     * line 1803 resets it to 0. Purpose unclear: leave as ???.
     *
     * The block reads player[0].racePosition vs player[1].racePosition
     * (16-bit shorts) and draws a single WIN or LOSE banner.
     *
     * Binary structure:
     *   0x4D3071: cmp [g_raceType], 4
     *   0x4D3078: jne 0x4D310B          ; not raceType==4 → fall through to main race-end
     *   0x4D307E: cmp [g_postRaceCameraMode], 0
     *   0x4D3085: je  0x4D310B          ; not race-end → fall through to main race-end
     *   0x4D308B: mov dx, [0x8FDC6C]    ; player[1].racePosition (16-bit short)
     *   0x4D3092: cmp dx, [0x8FD550]    ; vs player[0].racePosition
     *   0x4D3099: jle 0x4D30CF          ; if p1.rank <= p0.rank → LOSE branch
     *   ...WIN draw, jmp 0x4D3B3E (epilogue)
     *   ...LOSE draw, falls into ret at 0x4D3101
     *
     * jle takes if p1.rank <= p0.rank (player 1 matched-or-beat player 0
     * → LOSE). jle not taken means p1.rank > p0.rank (player 0 ahead → WIN).
     * Both sub-paths return before reaching the main race-end overlay below.
     */
    if (g_raceType == 4 && g_postRaceCameraMode != 0) {
        Player *pBase = (Player *)g_playerBase;
        short p0place = pBase[0].racePosition;                  /* 0x4D308B: word @ 0x8FD550 */
        short p1place = pBase[1].racePosition;                  /* 0x4D3092: word @ 0x8FDC6C */

        if (p1place > p0place) {
            /* WIN — player 0 ahead of player 1 (jle to LOSE not taken) */
            DrawTexturedQuad(0xE0, 0x52, 0x41200000, 0xC0, 0x4A,        /* 0x4D30C5 */
                             g_tpageObjects, 0x40, 0x6C, 0x60, 0x25,
                             VERTEX_WHITE);
        } else {
            /* LOSE — player 1 caught or beat player 0 */
            DrawTexturedQuad(0xE0, 0x52, 0x41200000, 0xC0, 0x4C,        /* 0x4D30FC */
                             g_tpageObjects, 0x40, 0x91, 0x60, 0x26,
                             VERTEX_WHITE);
        }
        return;                                                          /* 0x4D30CA jmp 0x4D3B3E / 0x4D3101 fall to ret */
    }

    /* ==== Race-end overlay branch — binary 0x4D3118-0x4D3417 ====
     *
     * When g_postRaceCameraMode != 0, the binary at 0x4D310B does
     *   cmp [0x901c84], 0; je 0x4D3419
     * which jumps OVER this branch into the ring counter when in normal-race
     * mode. In race-end mode we fall through into the dispatcher on
     * g_numHumans (which lives at 0x6E9908 — same as the showTimer formula
     * above, NOT a separate "race-end submode" as I first thought).
     *
     * Each sub-path returns from the function, which is how the binary skips
     * the rings/ladder/token/countdown/minimap blocks that follow this
     * insertion point — they only run when postRaceCameraMode == 0.
     *
     * Block 2 (raceType==4 race-end sub-block, 0x4D307E-0x4D3105) and the
     * balloon-mode loop (0x4D2FC9-0x4D306F) are not yet translated; they go
     * here too, before this branch, in future blocks.
     */
    if (g_postRaceCameraMode != 0) {
        if (g_numHumans == 1) {                                 /* 0x4D3118-0x4D3315 */
            if (g_raceType == RACE_SPECIAL) {                   /* 0x4D3127: cmp [0x8FB950], 3 */
                /* VS Challenge race-end — 3 alt quads instead of placement+
                 * CONGRATULATIONS+ladder. Per Sonic Retro wiki, raceType==3
                 * is "VS. Challenge" mode (1v1 against a hidden character). */
                DrawTexturedQuad(0x5E, 0x40, 0x41200000, 0x144, 0x34,    /* 0x4D3161 */
                                 g_tpageObjects, 0, 0x18, 0xA2, 0x1A,
                                 VERTEX_WHITE);
                DrawTexturedQuad(0x1C0, 0x40, 0x41200000, 0x64, 0x36,    /* 0x4D318D */
                                 g_tpageObjects, 0, 0x32, 0x32, 0x1B,
                                 VERTEX_WHITE);
                DrawTexturedQuad(0x6F, 0x80, 0x41200000, 0x1A2, 0x34,    /* 0x4D31BE */
                                 g_tpageObjects, 0, 0x4D, 0xD1, 0x1A,
                                 VERTEX_WHITE);
                return;                                                  /* 0x4D31CC */
            }

            /* Single-player race-end (raceType 0/1/2 — GP / Multiplayer / TA),
             * not VS Challenge: main race-end overlay (placement + CONGRATS + ladder) */
            int rank = vpPlayer->racePosition;                  /* 0x4D31CD: player+0x5A >> 16 */
            DrawTexturedQuad(0x100, 0x60, 0x41200000, 0x80, 0x60,         /* 0x4D3217 */
                             g_tpageObjects,
                             s_placementUvX[rank], s_placementUvY[rank],
                             s_placementUvW[rank], s_placementUvH[rank],
                             VERTEX_WHITE);

            rank = vpPlayer->racePosition;                      /* 0x4D321C: reload */
            if (rank < 4 && g_netSessionActive == 0) {             /* 0x4D3225/0x4D3232 */
                /* CONGRATULATIONS — top 3 finishers in offline single-player */
                DrawTexturedQuad(0x40, 0x20, 0x41200000, 0x200, 0x30,    /* 0x4D325F */
                                 g_tpageObjects, 0, 0, 0x100, 0x18,
                                 VERTEX_WHITE);
            }

            if (g_netSessionActive != 0) return;                   /* 0x4D3264-0x4D326B */

            /* Bottom horizontal portrait+label ladder — 5 player records.
             * Binary loop ebx 0..0x238C step 0x71C; in C this is just i in 0..4.
             * X position uses each player's own racePosition (not loop index). */
            for (int i = 0; i < 5; i++) {                       /* 0x4D3271-0x4D3315 */
                Player *p = &((Player *)g_playerBase)[i];
                int prank = p->racePosition;                    /* 0x4D32A1: player+0x5A>>16 */
                int x = (prank << 6) + 0x70;                    /* 0x4D32AC: rank*64+0x70 */

                /* Portrait 32x32 at (x, 0x190) — uvX = charId*16, uvY = 0xD8 */
                DrawTexturedQuad(x, 0x190, 0x41200000, 0x20, 0x20,       /* 0x4D32BD */
                                 g_tpageParallax1,
                                 p->charId * 0x10, 0xD8, 0x10, 0x10,
                                 VERTEX_WHITE);

                /* "1ST"/"2ND"/etc. label 32x16 at (x, 0x17E), gated on edi
                 * (= showLapTimes per binary 0x4D32CE: test edi, edi). */
                if (showLapTimes != 0) {                        /* 0x4D32CE-0x4D32D0 */
                    DrawTexturedQuad(x, 0x17E, 0x41200000, 0x20, 0x10,   /* 0x4D330D */
                                     g_tpageParallax1,
                                     prank * 0x10 + 0x90, 0xE0, 0x10, 8,
                                     VERTEX_WHITE);
                }
            }
            return;
        }

        if (g_numHumans == 2) {                                 /* 0x4D3317-0x4D33BE */
            /* 2P split-screen WIN/LOSE banner per viewport.
             * X position depends on viewport index and the viewport player's rank;
             * the banner picture (WIN vs LOSE) is chosen by rank alone. */
            int rank = vpPlayer->racePosition;
            int x;
            if (g_viewportIndex != 0) {                          /* 0x4D3321-0x4D334B */
                x = 0x40;
            } else if (rank == 1) {                             /* 0x4D3338-0x4D333D */
                x = 0x60;
            } else {                                            /* 0x4D3344 */
                x = 0x160;
            }

            if (rank == 1) {                                    /* 0x4D3359 */
                /* WIN banner — 192x74 at (x, 82), uv (0x40, 0x6C, 0x60, 0x25) */
                DrawTexturedQuad(x, 0x52, 0x41200000, 0xC0, 0x4A,        /* 0x4D337E */
                                 g_tpageObjects, 0x40, 0x6C, 0x60, 0x25,
                                 VERTEX_WHITE);
            } else {
                /* LOSE banner — 192x76 at (x, 82), uv (0x40, 0x91, 0x60, 0x26) */
                DrawTexturedQuad(x, 0x52, 0x41200000, 0xC0, 0x4C,        /* 0x4D33B0 */
                                 g_tpageObjects, 0x40, 0x91, 0x60, 0x26,
                                 VERTEX_WHITE);
            }
            return;
        }

        /* numHumans >= 3 default — big placement glyph only, no banner, no ladder. */
        int rank = vpPlayer->racePosition;                      /* 0x4D33BF */
        DrawTexturedQuad(0x100, 0x60, 0x41200000, 0x80, 0x60,            /* 0x4D340A */
                         g_tpageObjects,
                         s_placementUvX[rank], s_placementUvY[rank],
                         s_placementUvW[rank], s_placementUvH[rank],
                         VERTEX_WHITE);
        return;
    }

    /* Ring counter — 0x4D3419-0x4D354B
     * Animated ring icon + 3-digit ring count (upper-left HUD).
     * Ring icon: 16-frame animated sprite on g_tpageCharBase.
     * Ring count: player.ringCount, large digits on g_tpageParallax1, UV row 0x88. */
    if ((g_raceType != RACE_TIMEATTACK) && (g_demoMode != DEMO_REPLAY)) { /* not shown in GP results mode */
        Player *vpPlayerRing = &((Player *)g_playerBase)[vpIndex];
        int ringCount = vpPlayerRing->ringCount;

        /* Ring icon — animated 16-frame sprite on g_tpageCharBase */
        int ringUVX = (15 - g_ringAnimFrame) * 16;
        DrawTexturedQuad(0x10, 0x10, 0x41200000, 0x20, 0x20, g_tpageCharBase,
                         ringUVX, 0, 0x10, 0x10, VERTEX_WHITE);

        /* 3-digit ring count — large digits (UV row 0x88, width 8) on g_tpageParallax1 */
        int hundreds = ringCount / 100;
        int remainder = ringCount % 100;
        int tens = remainder / 10;
        int ones = remainder % 10;

        DrawTexturedQuad(0x38, 0x10, 0x41200000, 0x10, 0x20, g_tpageParallax1,
                         hundreds * 8, 0x88, 8, 0x10, VERTEX_WHITE);
        DrawTexturedQuad(0x48, 0x10, 0x41200000, 0x10, 0x20, g_tpageParallax1,
                         tens * 8, 0x88, 8, 0x10, VERTEX_WHITE);
        DrawTexturedQuad(0x58, 0x10, 0x41200000, 0x10, 0x20, g_tpageParallax1,
                         ones * 8, 0x88, 8, 0x10, VERTEX_WHITE);
    }

    /* 5-player position ladder — 0x4D3559-0x4D3693
     * Draws face icons + position labels for all 5 players, sorted by race position.
     * Y = (racePosition - 1) * 0x2C + 0x88, where racePosition from player+0x5C (1-5).
     * Viewport player gets large face; others get small face.
     * Position labels on g_tpageParallax1 at UV (position*16+0x90, 0xE0).
     *
     * Binary gate at 0x4D354B-0x4D3553:
     *   mov ecx, [0x8FB950]   ; g_raceType
     *   test ecx, ecx
     *   jne 0x4D3693          ; if raceType != 0 (not GP), skip ladder
     *
     * Earlier C used `g_numPlayers >= 2`, which happens to be functionally
     * similar in current game state but is NOT the variable the binary reads.
     * Per feedback_same_variable_as_binary.md, fixed to use g_raceType
     * directly to match the binary at 0x4D354B. */
    if (g_raceType == RACE_GP) {
        Player *vpPlayerPtr = &((Player *)g_playerBase)[vpIndex];
        for (int p = 0; p < 5; p++) {
            Player *rp = &((Player *)g_playerBase)[p];
            int racePos = rp->racePosition;                    /* P_INT(0x5A)>>16 = racePosition */
            if (racePos < 1 || racePos > 5) {
                racePos = p + 1;  /* fallback */
            }
            int ladderY = (racePos - 1) * 0x2C + 0x88;
            int rCharId = rp->charId;

            if (rp != vpPlayerPtr) {
                /* Other players: small face icon — 0x4D3568 */
                DrawTexturedQuad(0x20, ladderY, 0x41200000, 0x20, 0x20,
                                 g_tpageParallax1,
                                 rCharId * 0x10, 0xD8, 0x10, 0x10, VERTEX_WHITE);
                /* Position label — 0x4D35A0 */
                DrawTexturedQuad(0x42, ladderY + 0x10, 0x41200000, 0x20, 0x10,
                                 g_tpageParallax1,
                                 racePos * 0x10 + 0x90, 0xE0, 0x10, 8, VERTEX_WHITE);
            } else {
                /* Viewport player: large face icon — 0x4D3618 */
                DrawTexturedQuad(0x18, ladderY - 8, 0x41200000, 0x30, 0x30,
                                 g_tpageParallax1,
                                 rCharId * 0x18, 0xE8, 0x18, 0x18, VERTEX_WHITE);
                /* Position label — 0x4D3651 */
                DrawTexturedQuad(0x4A, ladderY + 0x18, 0x41200000, 0x20, 0x10,
                                 g_tpageParallax1,
                                 racePos * 0x10 + 0x90, 0xE0, 0x10, 8, VERTEX_WHITE);
            }
        }
    }

    /* ==== Block 3: 80x60 placement glyph — binary 0x4D3693-0x4D3716 ====
     *
     * Drawn for Multiplayer (raceType==1, but only when raceSubMode != 3,
     * i.e. NOT Balloon Hunt) AND for raceType==4.
     * Uses the SAME per-rank UV table as the race-end overlay's big 128x96
     * glyph, but draws at 80x60 at one of two screen positions.
     *
     * Note: per Sonic Retro wiki, raceType==1 is Multiplayer. 
     * So this glyph shows in MP for each viewport's player rank, not in TA.
     *
     * Note this block is reachable in NORMAL race mode (not just race-end) —
     * it sits between the position ladder and the ring token counter, after
     * the postRaceCameraMode gate has been passed via je 0x4D3419 from the
     * race-end check at 0x4D310B.
     *
     * Binary structure:
     *   0x4D3693: cmp ecx, 1                ; ecx = g_raceType (loaded at 0x4D354B)
     *   0x4D3696: jne 0x4D36A1
     *   0x4D3698: cmp [g_raceSubMode], 3
     *   0x4D369F: jne 0x4D36AA              ; raceType==1 && subMode != 3 → DRAW
     *   0x4D36A1: cmp [g_raceType], 4
     *   0x4D36A8: jne 0x4D3717              ; raceType != 4 → SKIP block
     *   0x4D36AA: cmp [g_numHumans], 2
     *   0x4D36B1: jne 0x4D36C8
     *   0x4D36B3: cmp [g_viewportIndex], 0
     *   0x4D36BA: jne 0x4D36C8              ; 2P viewport-0: x=0x220, y=0xA4
     *   0x4D36C8: x=0x10, y=0x194           ; default position
     *   ... DrawTexturedQuad(x, y, ..., 0x50, 0x3C, g_tpageObjects, per-rank UV) at 0x4D3712
     */
    if ((g_raceType == RACE_MULTIPLAYER && g_raceSubMode != SUBMODE_BALLOON) || g_raceType == 4) {
        int x, y;
        if (g_numHumans == 2 && g_viewportIndex == 0) {                  /* 0x4D36AA-0x4D36BA */
            x = 0x220;                                                   /* 0x4D36C1 */
            y = 0xA4;                                                    /* 0x4D36BC */
        } else {
            x = 0x10;                                                    /* 0x4D36CD */
            y = 0x194;                                                   /* 0x4D36C8 */
        }
        int rank = vpPlayer->racePosition;                               /* 0x4D36D5: player+0x5A>>16 */
        DrawTexturedQuad(x, y, 0x41200000, 0x50, 0x3C,                   /* 0x4D3712 */
                         g_tpageObjects,
                         s_placementUvX[rank], s_placementUvY[rank],
                         s_placementUvW[rank], s_placementUvH[rank],
                         VERTEX_WHITE);
    }

    /* Ring token counter — 0x4D3717-0x4D3806
     * Shows collected/total ring tokens in Grand Prix mode.
     * 4 quads: ring icon + count digit + "/5" separator + "5" total.
     * Gated: raceTypeConfig==0, lightingMode!=5, per-mode check. */
    if (g_raceType == RACE_GP &&              /* 0x4D3717: GP mode only */
        g_trackId != TRACK_RADIANT_EMERALD && /* 0x4D372A */
        g_charUnlockTable[s_tokenRivalChar[g_trackId]] != 2)
                                              /* 0x4D3733: [ecx*4+0x8FBA74] */
    {
        /* Ring icon — pos(16, 432) */ /* 0x4D3741-0x4D376B */
        DrawTexturedQuad(0x10, 0x1B0, 0x41200000, 0x20, 0x20,
                         g_tpageParallax1,
                         0xB0, 0x70, 0x10, 0x10, VERTEX_WHITE);

        /* Count digit — pos(56, 432), UV X = count * 8 */ /* 0x4D3770-0x4D37A0 */
        DrawTexturedQuad(0x38, 0x1B0, 0x41200000, 0x10, 0x20,
                         g_tpageParallax1,
                         g_p1CollectionCount * 8, 0x88, 8, 0x10, VERTEX_WHITE);

        /* "/5" separator — pos(74, 436) */ /* 0x4D37A5-0x4D37D2 */
        DrawTexturedQuad(0x4A, 0x1B4, 0x41200000, 0xA, 0x1A,
                         g_tpageParallax1,
                         0xAC, 0x8A, 5, 0xD, VERTEX_WHITE);

        /* "5" total — pos(84, 432) */ /* 0x4D37D7-0x4D3801 */
        DrawTexturedQuad(0x54, 0x1B0, 0x41200000, 0x10, 0x20,
                         g_tpageParallax1,
                         0x28, 0x88, 8, 0x10, VERTEX_WHITE);
    }

    /* raceSubMode==3 (Balloon Hunt) 4-quad collected counter — 0x4D3806-0x4D38F9 ====
     *
     * Per Sonic Retro wiki, raceSubMode==3 is "Balloon Hunt". Same 4-quad
     * pattern as the GP ring token counter above (icon + count digit + "/"
     * separator + total digit), but:
     *   - Different gate: showTimer && raceSubMode==3 (vs raceType==0)
     *   - Different icon UV: (0x80, 0x70) — balloon sprite, not ring
     *   - Count from vpPlayer->collisionCount (player+0x1F4) — repurposed
     *     in Balloon Hunt as "balloons collected"
     *   - Viewport-aware Y: 2P viewport-0 gets Y=0xC0 (192, upper area),
     *     all other configurations get Y=0x1B0 (432, bottom area)
     *
     * Binary structure:
     *   0x4D3806: cmp [showTimer], 0; je 0x4D38FA      ; gate 1
     *   0x4D3810: cmp [g_raceSubMode], 3; jne 0x4D38FA ; gate 2
     *   0x4D381D: cmp [g_numHumans], 2; jne default
     *   0x4D3826: cmp [g_viewportIndex], 0; jne default
     *   0x4D382F: ebx = 0xC0   (2P vp-0 Y)
     *   0x4D3836: ebx = 0x1B0  (default Y)
     *   ...4 quad draws using ebx as Y...
     *
     * Note: when both gates fail (subMode != 3 or showTimer == 0),
     * the binary's je 0x4D38FA jumps to countdown SFX — same target as Block
     * 4's loop-exit. We don't need a goto here because falling through the
     * skipped if-block lands on the label naturally.
     */
    if (showTimer != 0 && g_raceSubMode == 3) {
        int yPos;
        if (g_numHumans == 2 && g_viewportIndex == 0) {               /* 0x4D381D-0x4D382D */
            yPos = 0xC0;                                              /* 0x4D382F */
        } else {
            yPos = 0x1B0;                                             /* 0x4D3836 */
        }

        /* Balloon icon — 32x32 at (16, yPos), uv (0x80, 0x70, 0x10, 0x10) */
        DrawTexturedQuad(0x10, yPos, 0x41200000, 0x20, 0x20,         /* 0x4D3862 */
                         g_tpageParallax1,
                         0x80, 0x70, 0x10, 0x10, VERTEX_WHITE);

        /* Count digit — 16x32 at (56, yPos), uvX = collected*8 on row 0x88 */
        int collected = vpPlayer->collisionCount;                    /* 0x4D386E: player+0x1F4 (int) */
        DrawTexturedQuad(0x38, yPos, 0x41200000, 0x10, 0x20,         /* 0x4D3899 */
                         g_tpageParallax1,
                         collected * 8, 0x88, 8, 0x10, VERTEX_WHITE);

        /* "/" separator — 10x26 at (74, yPos+4), uv (0xAC, 0x8A, 5, 0xD) */
        DrawTexturedQuad(0x4A, yPos + 4, 0x41200000, 0xA, 0x1A,      /* 0x4D38C9 */
                         g_tpageParallax1,
                         0xAC, 0x8A, 5, 0xD, VERTEX_WHITE);

        /* "5" total digit — 16x32 at (84, yPos), uv (0x28=5*8, 0x88, 8, 0x10) */
        DrawTexturedQuad(0x54, yPos, 0x41200000, 0x10, 0x20,         /* 0x4D38F5 */
                         g_tpageParallax1,
                         0x28, 0x88, 8, 0x10, VERTEX_WHITE);
    }

subMode2_loop_exit:
    /* Block 4's loop-exit jumps here, skipping past Blocks 2/1, ring counter,
     * position ladder, Block 3, ring tokens, and Block 5. Mirrors the
     * binary's je 0x4D38FA at 0x4D302B. Other code paths fall through
     * naturally; the label is a no-op for them. */

    /* Countdown SFX triggers — 0x4D38FA-0x4D3935
     * Play READY / SET / GO sounds at specific introCountdown frames.
     * Binary gates on [ebp-0x24] (viewport param): only viewport 0 plays
     * sounds so they don't double-fire in 2P. */
    /* Binary gates on viewport 0 to dedup the SFX across local split-screen
     * viewports. In a network game DrawTimerAndStatus is called once per
     * machine with vpIndex = g_localPlayerIndex (see caller ~line 334), so a
     * non-host client (player 1/2/3) would be silenced by the vpIndex==0 gate.
     * Network play never double-fires, so allow it unconditionally there. */
    if (g_isNetworkGame != 0 || vpIndex == 0) {                 /* 0x4D38FA: test ebx; jne skip */
        if (g_introCountdown == 0x3B) {
            PlaySoundEffect(0x35, 0, 0);                                   /* 0x4D390C: "READY" */
        } else if (g_introCountdown == 0x1D) {
            PlaySoundEffect(0x36, 0, 0);                                   /* 0x4D3918: "SET" */
        } else if (g_introTimer == 0x3C) {
#ifdef SONICR_DC
            UpdateCDPlayback(g_cdPlaybackTarget);
#endif
            PlaySoundEffect(0x37, 0, 0);                                   /* 0x4D3928: "GO" */
        }
    }

    /* Countdown graphic — 0x4D3936-0x4D3B10
     * Animated READY / SET / GO text quad, centered on screen.
     * Three phases keyed off g_introTimer and g_introCountdown:
     *   g_introTimer != 0:              "GO"    (UV Y=0xB0)
     *   g_introCountdown in (0, 0x1E):  "SET"   (UV Y=0x8E)
     *   g_introCountdown in [0x1E,0x3C):"READY" (UV Y=0x6C)
     * Each phase scales width/height from the countdown frame delta. */

    /* Compute center Y and screen width based on split-screen config.
     * Binary uses g_viewportIndex (split mode indicator, NOT loop counter). */
    int centerY; /* ecx — 0x4D3936 */
    if (g_numHumans == 2 && g_viewportIndex == 0) {              /* 0x4D3940 */
        centerY = 0x3C;
    }
    else {
        centerY = 0x78;
    }

    int scrW; /* ebx — 0x4D3954 */
    if (g_numHumans == 2 && g_viewportIndex == 1) {              /* 0x4D395E */
        scrW = 0xA0;
    }
    else {
        scrW = 0x140;
    }

    if (g_introTimer != 0)
    {
        /* "GO" phase — 0x4D3972-0x4D39E0 */
        int delta = 0x3C - g_introTimer;           /* edi = 0x3C - [0x901CC8] */
        int scaleW = (delta << 6) / 0x18 + 0x40;   /* esi = (delta*64)/24 + 64 */
        int scaleH = (delta * 0x22) / 0x18 + 0x22; /* eax = (delta*34)/24 + 34 */
        DrawTexturedQuad(scrW - scaleW, centerY - scaleH, 0x41200000,
                         scaleW * 2, scaleH * 2,
                         g_tpageObjects,
                         0, 0xB0, 0x40, 0x22, VERTEX_WHITE); /* 0x4D3B09 */
    }
    else if (g_introCountdown > 0 && g_introCountdown < 0x1E)
    {
        /* "SET" phase — 0x4D3A3A-0x4D3AA0 ====
         *
         * Network sync suppression gate (binary 0x4D39FB-0x4D3A38).
         *
         * In multiplayer/network mode, the SET graphic is suppressed for
         * half of every 16-frame window when the player isn't yet ready
         * for the network race start. The binary jumps past the entire
         * draw to 0x4D3B15 (minimap area).
         *
         * Suppression conditions (ALL must hold):
         *   - g_netSessionActive != 0 OR g_isNetworkGame != 0  (any MP mode)
         *   - g_netGameStartState == 3                       (game started)
         *   - g_introCountdown    == 3                       (specific frame)
         *   - g_netReadyFlag      == 0                       (player not ready)
         *   - (g_totalFrames & 0xF) < 7                      (8 of 16 frames)
         *
         * For pure single-player (both MP flags zero), the gate is
         * bypassed at 0x4D3A0B (je to draw) and the draw runs normally.
         */

        int suppressed = 0;
        /* 0x4D39FB-0x4D3A0B: only enter the network gate if either MP flag
         * is set; pure single-player skips the gate entirely. */
        if (g_netSessionActive != 0 || g_isNetworkGame != 0)
        {
            /* 0x4D3A0D-0x4D3A34: 4-condition AND, then frame-mask test */
            if (g_netGameStartState == 3 && /* 0x4D3A13: cmp esi, 3 */
                g_introCountdown == 3 &&    /* 0x4D3A18: cmp esi, [introCountdown] (esi is still 3) */
                g_netReadyFlag == 0 &&      /* 0x4D3A20: cmp [netReady], 0 */
                (g_totalFrames & 0xF) < 7)
            { /* 0x4D3A29-0x4D3A34: jl skip */
                suppressed = 1;
            }
        }

        if (!suppressed)
        {
            int delta = 0x1D - g_introCountdown;       /* edi = 0x1D - [0x901CC4] */
            int scaleW = (delta << 6) / 0x18 + 0x40;   /* esi */
            int scaleH = (delta * 0x22) / 0x18 + 0x22; /* eax+0x22 */
            DrawTexturedQuad(scrW - scaleW, centerY - scaleH, 0x41200000,
                             scaleW * 2, scaleH * 2,
                             g_tpageObjects,
                             0, 0x8E, 0x40, 0x22, VERTEX_WHITE);
        }
    }
    else if (g_introCountdown >= 0x1E && g_introCountdown < 0x3C)
    {
        /* "READY" phase — 0x4D3AA2-0x4D3B07 */
        int frame = g_introCountdown - 0x1E;       /* edi = eax - 0x1E */
        int delta = 0x1D - frame;                  /* eax = 0x1D - edi */
        int scaleW = (delta << 6) / 0x18 + 0x40;   /* esi */
        int scaleH = (delta * 0x22) / 0x18 + 0x22; /* edx = eax+0x22 */
        DrawTexturedQuad(scrW - scaleW, centerY - scaleH, 0x41200000,
                         scaleW * 2, scaleH * 2,
                         g_tpageObjects,
                         0, 0x6C, 0x40, 0x22, VERTEX_WHITE);
    }

    /* Minimap widget — binary 0x4D3B15-0x4D3B3E */
    if (g_demoMode == DEMO_NONE && g_postRaceCameraMode == 0 &&
        g_minimapToggle != 0 && showTimer != 0) {
        DrawMinimapWidget((char *)vpPlayer);
    }
}

/**
 * DrawReverseIndicator — 0x004D2600 — 277 bytes
 * Wrong-way detection + REVERSE indicator display.
 * Compares current trackProgress (0x4C) against progressHighWater (0xE0) to detect
 * reverse travel. Bumps display counter (0x1E4) when going wrong way,
 * decays it each frame otherwise. Draws REVERSE graphic when counter > 15.
 *
 * in_EAX = viewport player pointer (Watcom fastcall).
 */
void DrawReverseIndicator(Player *pl)
{
    /* Binary receives player pointer in EAX (Watcom fastcall), saved in ESI.
     * g_viewportIndex references in this function are split mode checks. */

    if (g_postRaceCameraMode != 0) {
        return;                     /* 0x4D2606 */
    }

    /* Mode gate: only in standard race (0) or mode 1 with subType 0 */
    if (g_raceType != RACE_GP) {                           /* 0x4D2613 */
        if (g_raceType != RACE_MULTIPLAYER) {
            return;      /* 0x4D261D */
        }
        if (g_raceSubMode != SUBMODE_NORMAL) {
            return;           /* 0x4D2626 */
        }
    }

    /* Wrong-way detection (0x4D2633-0x4D2697) */
    /* Progress wraps in 32-bit two's complement; the binary adds the thresholds
     * with a plain ADD and compares with JAE (UNSIGNED) at 0x4D2643/0x4D264F.
     * Do the arithmetic and comparisons unsigned to match — a signed translation
     * both trips UBSan (overflow near INT_MAX) and can spuriously fire wrong-way
     * detection when the wrapped sum crosses the sign boundary. */
    unsigned int progress = (unsigned int)pl->trackProgress;   /* current progress */
    unsigned int savedProgress = (unsigned int)pl->progressHighWater;  /* saved progress */

    if (progress + 0xE00000u < savedProgress) {                /* 0x4D263C-0x4D2643: jae → unsigned <, wrong way */
        if (progress + 0x2800000u >= savedProgress) {          /* 0x4D2648-0x4D264F: jae → unsigned >=, not too far back */
            pl->progressHighWater = (int)(progress + 0xE00001u);       /* 0x4D265C: update saved */

            int wrongWayCount = pl->_unk_0x1E4;               /* 0x4D266A: P_INT(0x1E2)>>16 = _unk_0x1E4 */
            if (wrongWayCount < 0xA) {                         /* 0x4D2676: haven't played SFX yet */
                if (g_isPaused == 0) {                         /* 0x4D267E */
                    PlaySoundEffect(1, 0, 0);                            /* 0x4D268B: wrong-way beep */
                }
                pl->_unk_0x1E4 += 0x20;                        /* 0x4D2690: bump display counter */
            }
        }
        else {
            pl->progressHighWater = (int)progress;                     /* 0x4D2651: reset (way too far) */
        }
    }

    /* Draw REVERSE indicator (0x4D2698-0x4D26F2) */
    int displayCount = pl->_unk_0x1E4; /* 0x4D2698: P_INT(0x1E2)>>16 = _unk_0x1E4 */
    if (displayCount > 0xF) { /* 0x4D26A1: only draw if > 15 */
        int xPos, yPos;
        if (g_numHumans == 2 && g_viewportIndex == 1) { /* 0x4D26A6-0x4D26B6 */
            xPos = 0x68;
            yPos = 0xA4;
        }
        else {
            xPos = 0x108; /* 0x4D26C4-0x4D26C9 */
            yPos = 0x10;
        }
        DrawTexturedQuad(xPos, yPos, 0x41200000, 0x70, 0x34,
                         g_tpageObjects, /* 0x4D26DE: [0x8F6C38] */
                         0x40, 0xB8, 0x38, 0x1A, VERTEX_WHITE);
    }

    /* Counter decay (0x4D26F3-0x4D2710) */
    displayCount = pl->_unk_0x1E4;                     /* 0x4D26F3: P_INT(0x1E2)>>16 = _unk_0x1E4 */
    if (displayCount > 0 && g_isPaused == 0) {             /* 0x4D26FE, 0x4D2700 */
        pl->_unk_0x1E4 -= 1;                               /* 0x4D2709: dec word */
    }
}

/* =====================================================================
 * DrawMinimapWidget — FUN_004D2718 — 634 bytes
 * Draws the 2D minimap HUD widget: track background + player position dots.
 *
 * Called from DrawTimerAndStatus when:
 *   g_demoMode == DEMO_NONE, g_postRaceCameraMode == 0, g_minimapToggle != 0
 *
 * Background: 96×80 texture from MAP_*.RAW on g_tpageParallax1 (UV 128,0).
 * Dots: character icons from parallax tpage, sized by viewport ownership.
 * Per-track center/scale constants position world coords → minimap coords.
 * ===================================================================== */
void DrawMinimapWidget(char *vpPlayer)  /* EAX = current viewport's player pointer */
{
    /* Phase 1: Minimap screen position (0x4D2726-0x4D279A) */
    int mapX, mapY;

    if (g_numHumans == 2) {
        /* Horizontal split — fixed top corner, side picked by race sub-mode */
        if (g_viewportIndex == 0) {                              /* 0x4D2730 */
            mapX = (g_raceSubMode == SUBMODE_NORMAL) ? 0x10 : 0x1B0;
            mapY = 0x40;
        }
        /* Vertical split (0x4D275A) — per-track offset from ROM 0x5042C4 */
        else {
            int xOff = s_mmViewportTable[g_trackId][0];
            int yOff = s_mmViewportTable[g_trackId][1];
            mapX = 0x140 - (xOff + 0x60) * 2;
            mapY = 0x1E0 - (yOff + 0x50) * 2;
        }
    } 
    /* Single player (or 3-4 player): bottom-right */
    else {
        mapX = 0x1B0;
        mapY = 0x130;
    }

    /* Phase 2: Draw minimap background (0x4D279D-0x4D27D9)
     * MAP texture loaded at (128,0) in tpage — matches binary UV. */
    DrawTexturedQuad(mapX, mapY, 0x41200000, 0xC0, 0xA0,
                     g_tpageParallax1,
                     0x80, 0, 0x60, 0x50, VERTEX_WHITE);

    /* Phase 3: Per-track center coordinates and scale (0x4D27DE-0x4D2820) */
    int centerX = 0x2D37;   /* default: Island / City / Factory */
    int centerZ = 0x28EB;
    int scaleVal = 0xE8;

    /* Binary 0x4D27E8 `cmp eax,4` = RUIN in binary convention (3=Factory,
     * 4=Ruin) — un-crosswired 2026-07-13; Ruin's map was drawn with the
     * default center/scale and Factory with Ruin's. */
    if (g_trackId == TRACK_REGAL_RUIN) {
        centerX = 0x362E;
        centerZ = 0x3108;
        scaleVal = 0x116;
    }
    if (g_trackId == TRACK_RADIANT_EMERALD) {                   /* Emerald */
        centerX = 0x43D2;
        centerZ = 0x3D60;
        scaleVal = 0x15C;
    }

    /* Phase 4: Player position dots (0x4D2820-0x4D2988) */
    int halfScale = scaleVal / 2;           /* binary: (scaleVal + (scaleVal>>31)) >> 1 */
    int playerIndex = g_numPlayers - 1;
    if (playerIndex < 0) {
        return;
    }

    Player *pBase = (Player *)g_playerBase;

    for (; playerIndex >= 0; playerIndex--) {
        Player *playerPtr = &pBase[playerIndex];

        /* Ghost dot blink: in Time Attack the ghost's dot is hidden on odd
         * frames, not hidden outright. 0x4D288C tests [0x902078] =
         * g_raceOrder[2], the per-frame 0/1 toggle, and only skips when it is
         * set. There is no g_ghostDataExists term here — that one belongs to
         * the model skip at 0x45B120, not to this site. */
        if (playerIndex == 1 && playerIndex == g_ghostToggle &&
            g_raceType == RACE_TIMEATTACK && g_raceType > g_raceSubMode &&
            g_raceOrder[2] != 0) {
            continue;                       /* binary 0x4D286C-0x4D2893 */
        }

        /* Transform world XZ → minimap coordinates */
        int playerX = playerPtr->posX;
        int playerZ = playerPtr->posZ;

        int mmX = ((playerX >> 12) + centerX) / halfScale;
        int mmY = (centerZ - (playerZ >> 12)) / halfScale;

        int screenX = mmX + mapX;
        int screenY = mmY + mapY;

        /* Dot size and depth: viewport player gets larger, closer dot */
        int halfSize, dotDepth;
        if (vpPlayer == (char *)playerPtr) {
            dotDepth = 0x40800000;          /* 4.0f as int bits */
            halfSize = 10;                  /* 0x0A → 20px dot */
        }
        else {
            float fDepth = (float)(playerIndex + 5);
            memcpy(&dotDepth, &fDepth, sizeof(int));
            halfSize = 8;                   /* 0x08 → 16px dot */
        }

        int dotDiam = halfSize * 2;

        /* Character icon UV on g_tpageParallax1 (5-column layout) */
        int charId = playerPtr->charId;
        int uvX = (charId % 5) * 16 + 0x80;
        int uvY = (charId / 5) * 16 + 0x50;

        DrawTexturedQuad(screenX - halfSize, screenY - halfSize,
                         dotDepth, dotDiam, dotDiam,
                         g_tpageParallax1,
                         uvX, uvY, 16, 16, VERTEX_WHITE);
    }
}

/* =====================================================================
 * DrawMovingPickupShadow — FUN_0045B15C — 3369 bytes
 *
 * Ground shadow for a pickup that MOVES — BuildGroundShadowQuad and
 * SubmitGroundShadow fused into one per-frame function. The static pickups
 * get their quad baked once at track load; an object whose position changes
 * has to re-query terrain height and surface normal every frame, so it
 * cannot use the baked path.
 *
 * Only Reactive Factory calls this, for its two Chaos Emeralds — they have
 * a scripted launch and travel a parabolic arc.
 *
 * Watcom fastcall: EAX=worldX, EDX=worldY, EBX=worldZ, ECX=scale — same
 * argument roles as BuildGroundShadowQuad (p1=X, p2=height, p3=Z).
 * Called from DrawPickupShadows.
 * ===================================================================== */
void DrawMovingPickupShadow(int worldX, int worldY, int worldZ, int scale)
{
    int tpage = g_tpageCharBase;                                     /* 0x45b172: [0x8f6c28] */

    if (g_tpageStateArray[tpage] != 4) {
        return;                      /* 0x45b177 */
    }

    /* Query terrain height */
    /* 0x45B184-0x45B18C: eax=p1, edx=-p2, and ebx still holds p3 from the
     * prologue (never reloaded). QueryTerrainHeight's C signature is
     * (x, z, minY), which is NOT the binary's register order. */
    int height = QueryTerrainHeight(worldX, worldZ, -worldY);
    height = -height;                                                /* 0x45b196 */

    /* Get surface normal */
    int normX, normY, normZ;
    if (height == 0) {                                               /* 0x45b19d */
        normX = 0;                                                   /* 0x45b19f */
        normZ = 0;                                                   /* 0x45b1ac */
        normY = 0x1000;                                              /* 0x45b1b2 */
    }
    /* Look up face/edge normal from terrain data */
    else {
        TerSurface *surf = (TerSurface *)g_trackSurfaceData + g_qtSurfIdx; /* 0x45b1bf */
        int edgeBase = surf->faceBase + g_qtEdgeIdx;                 /* 0x45b1c5-0x45b1d2 */
        TerFace *face = &((TerFace *)g_terFaceTable)[edgeBase];     /* 0x45b1d4-0x45b1e5 */
        normX = face->normalX;                                       /* 0x45b1e7-0x45b1ed */
        normY = -(face->normalY);                                    /* 0x45b1f3-0x45b209 */
        normZ = face->normalZ;                                       /* 0x45b201-0x45b20f */
    }

    /* Heading angle from terrain normal */
    /* atan2(normZ, normY) * 4096 / (2*PI) */
    sr_double a1 = sr_atan2((sr_double)normZ, (sr_double)normY);    /* 0x45b215-0x45b221: fild normY; fild normZ; atan2(B,A) */
    int iAngle = (int)(a1 * 4096.0 * 0.15915494327375637);         /* 0x45b226-0x45b23f */
    iAngle = ((iAngle << 4) >> 4) & 0xFFF;                         /* 0x45b24b-0x45b256 */
    int angleComp = 0xFFF - iAngle;                                 /* 0x45b25b */

    /* Pitch component */
    int cosA = g_cosTable[angleComp] >> 2;                          /* 0x45b26c-0x45b272 */
    int sinA = g_sinTable[angleComp] >> 2;                          /* 0x45b265, 0x45b27f-0x45b280 */
    int pitchComp = (normY * cosA - sinA * normZ) / 4096;          /* 0x45b275-0x45b295 */

    /* Second angle from pitch + normX */
    sr_double a2 = sr_atan2((sr_double)normX, (sr_double)pitchComp); /* 0x45b29e-0x45b2aa: fild pitchComp; fild normX; atan2(B,A) */
    int cosI = g_cosTable[iAngle] >> 2;                             /* 0x45b2b8-0x45b2cc: loaded before a2 truncate */
    int iAngle2 = (int)(a2 * 4096.0 * 0.15915494327375637);        /* 0x45b2af-0x45b2c0 */
    iAngle2 = ((iAngle2 << 4) >> 4) & 0xFFF;                      /* 0x45b2cf-0x45b2da */
    int angle2Comp = 0xFFF - iAngle2;                               /* 0x45b2e2 */

    /* Trig table lookups for rotation matrix */
    int cos0 = g_cosTable[0] >> 2;                                  /* 0x45b2d8-0x45b2e4 */
    int sinB = g_sinTable[angle2Comp] >> 2;                         /* 0x45b2ee-0x45b2f7 */
    int cosB = g_cosTable[angle2Comp] >> 2;                         /* 0x45b2fa-0x45b301 */
    int sinI = g_sinTable[iAngle] >> 2;                             /* 0x45b320-0x45b330 */
    /* sin0 = g_sinTable[0] >> 2 = 0 — all sin0 terms vanish */

    /* Build rotation matrix (sin0=0 → many terms vanish) */
    int m48 = cos0 * cosB;                     /* 0x45b30a: cos0*cosB - 0 */
    int m28 = cos0 * sinB;                     /* 0x45b31c: cos0*sinB + 0 */
    /* m44 = 0, mA4 = 0 (all sin0 terms = 0) */

    int r0 = m48 / 4096;                      /* 0x45b35a: [ebp-0x2c] */
    int r2 = m28 / 4096;                      /* 0x45b369: ecx after sar */
    /* r1 = 0 from [ebp-0x44]/4096 */

    /* Combine rotation angles: 3 row vectors */
    int rowA_0 = (cosI * r2) / 4096;          /* 0x45b372→0x45b406: [ebp-0x28]/4096 */
    int rowA_1 = r0;                           /* [ebp-0x2c] unchanged */
    int rowA_2 = (sinI * r2) / 4096;          /* 0x45b3b9→0x45b42a: esi/4096 */

    int rowB_0 = 0;                            /* [ebp-0x1c] = 0 */
    int rowB_1 = 0;                            /* [ebp-0xa4]/4096 = 0 */
    int rowB_2 = (cos0 * cosI) / 4096;        /* 0x45b3c6-0x45b3cc: [ebp-0x14] */

    /* Camera-relative position */
    int dX = (worldX - g_camOrientX) / 4096;  /* 0x45b3d8: [ebp-0x18] */
    int dY = (height - g_camOrientY) / 4096;  /* 0x45b3e9: [ebp-0x10] */
    int dZ = (worldZ - g_camOrientZ) / 4096;  /* 0x45b403: eax, shifted at 0x45b4e6 */

    /* View matrix multiply
     * Binary uses 4-int stride per row (0x6e9c44/54/64 = col 0).
     * g_viewMtx[16] now matches binary layout (stride-4). */
    int *vm = &g_viewMtx[0];
    #define VM_R0C0 vm[0]   /* 0x6e9c44 */
    #define VM_R0C1 vm[1]   /* 0x6e9c48 */
    #define VM_R0C2 vm[2]   /* 0x6e9c4c */
    #define VM_R1C0 vm[4]   /* 0x6e9c54 */
    #define VM_R1C1 vm[5]   /* 0x6e9c58 */
    #define VM_R1C2 vm[6]   /* 0x6e9c5c */
    #define VM_R2C0 vm[8]   /* 0x6e9c64 */
    #define VM_R2C1 vm[9]   /* 0x6e9c68 */
    #define VM_R2C2 vm[10]  /* 0x6e9c6c */

    /* Row A * view matrix → view-space forward vector */
    int vA_x = rowA_1 * VM_R0C0 + rowA_0 * VM_R1C0 + rowA_2 * VM_R2C0; /* 0x45b40b-0x45b44e: [ebp-0x48] */
    int vA_y = rowA_1 * VM_R0C1 + rowA_0 * VM_R1C1 + rowA_2 * VM_R2C1; /* 0x45b451-0x45b491: [ebp-0x28] */
    int vA_z = rowA_0 * VM_R0C2 + rowA_1 * VM_R1C2 + rowA_2 * VM_R2C2; /* 0x45b494-0x45b4c0: esi */

    /* Row B * view matrix → view-space up vector */
    int vB_x = rowB_0 * VM_R0C0 + rowB_1 * VM_R1C0 + rowB_2 * VM_R2C0; /* 0x45b4c2-0x45b4e9: [ebp-0x44] */
    int vB_y = rowB_0 * VM_R0C1 + rowB_1 * VM_R1C1 + rowB_2 * VM_R2C1; /* 0x45b4ec-0x45b525: [ebp-0xa4] */
    int vB_z = rowB_0 * VM_R0C2 + rowB_1 * VM_R1C2 + rowB_2 * VM_R2C2; /* 0x45b527-0x45b54c: [ebp-0xb0] */

    /* Delta position * view matrix → view-space center */
    int vC_x = dY * VM_R1C0 + dX * VM_R0C0 + dZ * VM_R2C0;             /* 0x45b557-0x45b575: [ebp-0xa8] */
    int vC_y = dX * VM_R0C1 + dY * VM_R1C1 + dZ * VM_R2C1;             /* 0x45b580-0x45b59e: [ebp-0xac] */
    int vC_z = dX * VM_R0C2 + dY * VM_R1C2 + dZ * VM_R2C2;             /* 0x45b5a9-0x45b5c5: eax */

    /* Normalize to 12-bit fixed point */
    int fwd_x = vA_x / 4096;    /* 0x45b5ca: [ebp-0x2c] */
    int fwd_y = vA_y / 4096;    /* 0x45b5d3: [ebp-0x1c] (reused name) */
    int up_x = vB_x / 4096;    /* (not stored separately, inline) */
    int up_y = vB_y / 4096;
    int up_z = vB_z / 4096;    /* 0x45b5df: [ebp-0x14] */
    int ctr_x = vC_x / 4096;    /* 0x45b5eb: [ebp-0x18] */
    int ctr_y = vC_y / 4096;    /* 0x45b5f7: [ebp-0x10] */
    int fwd_z = vA_z / 4096;    /* 0x45b604: esi (shifted at 0x45b604) */
    int ctr_z = vC_z / 4096;    /* 0x45b61c: eax shifted */

    #undef VM_R0C0
    #undef VM_R0C1
    #undef VM_R0C2
    #undef VM_R1C0
    #undef VM_R1C1
    #undef VM_R1C2
    #undef VM_R2C0
    #undef VM_R2C1
    #undef VM_R2C2

    /* Generate 4 vertices: center ± fwd*scale ± up*scale */
    /* Vertex order: (-fwd+up), (+fwd+up), (+fwd-up), (-fwd-up)  0x45b5fd-0x45b6ce */
    int negScale = -scale;                                           /* 0x45b602 */
    int ctr_z_sc = ctr_z << 12;                                     /* 0x45b622 */
    int ctr_x_sc = ctr_x << 12;                                     /* 0x45b70f */
    int ctr_y_sc = ctr_y << 12;                                     /* 0x45b738 */

    /* Pre-compute repeated products */
    int nsFz = negScale * fwd_z;    /* -scale*fwd_z: [ebp-0x58] */
    int sFz = scale * fwd_z;       /* fwd_z*scale after 0x45b655 */
    int sUz = scale * up_z;        /* up_z*scale:  [ebp-0x64] */
    int nsUz = negScale * up_z;     /* -scale*up_z: [ebp-0x60] */

    int depth[4];
    depth[0] = (nsFz + sUz  + ctr_z_sc) / 4096;                    /* 0x45b646 */
    if (depth[0] < 1) {
        return;
    }
    depth[1] = (sFz  + sUz  + ctr_z_sc) / 4096;                    /* 0x45b66f */
    if (depth[1] < 1) {
        return;
    }
    depth[2] = (sFz  + nsUz + ctr_z_sc) / 4096;                    /* 0x45b69f */
    if (depth[2] < 1) {
        return;
    }
    depth[3] = (nsFz + nsUz + ctr_z_sc) / 4096;                    /* 0x45b6c8 */
    if (depth[3] < 1) {
        return;
    }

    /* Far clip: average of diag pair + 12 */
    if (((depth[0] + depth[2]) >> 4) + 0xC > g_farClipDepth) {
        return; /* 0x45b6d7-0x45b6f1 */
    }

    /* View-space X,Y per vertex */
    int nsFx = negScale * fwd_x;
    int sFx = scale * fwd_x;
    int sUx = scale * up_x;
    int nsUx = negScale * up_x;
    int nsFy = negScale * fwd_y;
    int sFy = scale * fwd_y;
    int sUy = scale * up_y;
    int nsUy = negScale * up_y;

    int vx[4], vy[4];
    vx[0] = (nsFx + sUx + ctr_x_sc) / 4096;                      /* 0x45b727: [ebp-0x74] */
    vx[1] = (sFx + sUx + ctr_x_sc) / 4096;
    vx[2] = (sFx + nsUx + ctr_x_sc) / 4096;
    vx[3] = (nsFx + nsUx + ctr_x_sc) / 4096;

    vy[0] = (nsFy + sUy + ctr_y_sc) / 4096;                       /* 0x45b752: [ebp-0x80] */
    vy[1] = (sFy + sUy + ctr_y_sc) / 4096;
    vy[2] = (sFy + nsUy + ctr_y_sc) / 4096;
    vy[3] = (nsFy + nsUy + ctr_y_sc) / 4096;

    /* Perspective projection */
    int sx[4], sy[4];
    for (int i = 0; i < 4; i++) {                                       /* 0x45b758-0x45b99a */
        sx[i] = g_screenCenterX + (g_projScaleXCurrent * vx[i]) / depth[i];
        sy[i] = g_screenCenterY - (g_projScaleY * vy[i]) / depth[i];
    }

    /* Viewport clipping */                                /* 0x45b99c-0x45ba4a */
    if (sx[0] < g_clipLeft && sx[1] < g_clipLeft && sx[2] < g_clipLeft && sx[3] < g_clipLeft) {
        return;
    }
    if (sx[0] > g_clipRight && sx[1] > g_clipRight && sx[2] > g_clipRight && sx[3] > g_clipRight) {
        return;
    }
    if (sy[0] < g_clipTop && sy[1] < g_clipTop && sy[2] < g_clipTop && sy[3] < g_clipTop) {
        return;
    }
    if (sy[0] > g_clipBottom && sy[1] > g_clipBottom && sy[2] > g_clipBottom && sy[3] > g_clipBottom) {
        return;
    }

    /* Backface cull */                                    /* 0x45b8f7-0x45b912 */
    int cross1 = (sx[2] - sx[1]) * (sy[0] - sy[1]);
    int cross2 = (sy[2] - sy[1]) * (sx[0] - sx[1]);
    int facing = (g_mirrorMode != 0) ? (cross2 - cross1) : (cross1 - cross2);
    if (facing < 0) {
        return;
    }

    /* Z bias: subtract 16, clamp to 1 */                 /* 0x45ba50-0x45bac8 */
    for (int i = 0; i < 4; i++) {
        depth[i] -= 0x10;
        if (depth[i] < 1) {
            depth[i] = 1;
        }
    }

    /* UV coords per vertex (same disc as SubmitGroundShadow) */
    static const float uvU[4] = {0.0f, 0.125f, 0.125f, 0.0f};
    static const float uvV[4] = {0.1875f, 0.1875f, 0.3125f, 0.3125f};

    /* Fog constants — ROM 0x52c3b8..0x52c3d0 */
    #define PFOG_NEAR  0.9     /* above this: alpha = 0 (invisible) */
    #define PFOG_FAR   0.7     /* below this: alpha = 0x80 (full) */
    #define PFOG_OFF  (-0.9)
    #define PFOG_SCALE 1275.0

    int maxAlpha = 0x80;                                             /* [ebp-0x30] */
    RenderVertex verts[4];
    for (int i = 0; i < 4; i++) {                                       /* 0x45bc0f-0x45be7b */
        float fDepth = (float)depth[i];
        float normZ = fDepth / g_farClipFloat;

        /* Fog alpha from normalized depth */
        int alpha;
        if ((sr_double)normZ > PFOG_NEAR) {
            alpha = 0;
        }
        else if ((sr_double)normZ <= PFOG_FAR) {
            alpha = maxAlpha;
        }
        else {
            alpha = (int)(((sr_double)normZ + PFOG_OFF) * PFOG_SCALE);  /* 0x45bc65-0x45bc76 */
            if (alpha < 0) {
                alpha = -alpha;                           /* abs via cdq/xor/sub */
            }
        }

        verts[i].sx = (float)sx[i];
        verts[i].sy = (float)sy[i];
        verts[i].sz = normZ;
        verts[i].rhw = 1.0f / fDepth;
        verts[i].color = ((unsigned int)alpha << 24) | VERTEX_WHITE_RGB;     /* 0x45bc9e-0x45bca4 */
        verts[i].specular = 0;
        verts[i].u = uvU[i];
        verts[i].v = uvV[i];
    }

    R_SetTexture(tpage);
    R_SetTexEnv(R_TEXENV_MODULATE);
    R_DrawQuad(verts);

    #undef PFOG_NEAR
    #undef PFOG_FAR
    #undef PFOG_OFF
    #undef PFOG_SCALE
}

/* =====================================================================
 * DrawPickupShadows — FUN_004621EC — 1220 bytes
 *
 * Once per frame: walks the track's pickup list and submits a ground
 * shadow for each one still present. A pickup is gone when the visibility
 * word at objStruct+0x2C reads -1 — InitObjectVisibility writes 0xFFFF
 * there for emeralds whose character is already unlocked.
 *
 * Per track: 1-2 Chaos Emeralds (halfSize 0x28), 5 Sonic Tokens (0x1E),
 * 2-3 item pickups (0x2D). Radiant Emerald has none.
 *
 * 10 shared quad buffers (12 ints each = 4 vertices × XYZ), reused per
 * track, populated by BuildGroundShadowQuad during track init. Factory's
 * two emeralds move, so they go through DrawMovingPickupShadow instead of
 * the baked buffers.
 *
 * Entries verified against the binary switch at 0x4621EC (jump table
 * 0x4621DC): case2 = trackId 3, case3 = trackId 4.
 * ===================================================================== */
void DrawPickupShadows(void)
{
    if (g_trackId < TRACK_RESORT_ISLAND || g_trackId > TRACK_REACTIVE_FACTORY) {
        return;  /* binary: dec eax; cmp eax,3; ja */
    }

    /* REACTIVE FACTORY (our 4; binary case 3 at 0x46254A — meaning-correct
     * despite the literal, verified vs jump table 0x4621DC 2026-07-13):
     * its two Chaos Emeralds are launched on a scripted parabolic arc, so
     * their shadows are rebuilt per frame instead of read from the baked
     * quad buffers. Objects 777 and 781. Submitted before the static ones. */
    if (g_trackId == TRACK_REACTIVE_FACTORY) {
        char *obj = (char *)g_objectStructArray;

        /* Emerald 1: offsets 0xce84/88/8c/90 (X/Y/Z/vis) — 0x462562-0x462580 */
        int vis1 = *(short *)(obj + 0xce90);
        if (vis1 != -1) {
            int x1 = *(int *)(obj + 0xce84) << 12;
            int y1 = (*(int *)(obj + 0xce88) + 0x20) << 12;
            int z1 = *(int *)(obj + 0xce8c) << 12;
            DrawMovingPickupShadow(x1, y1, z1, 0x20);
        }

        /* Emerald 2: offsets 0xcf94/98/9c/a0 — 0x46259d-0x4625bb */
        int vis2 = *(short *)(obj + 0xcfa0);
        if (vis2 != -1) {
            int x2 = *(int *)(obj + 0xcf94) << 12;
            int y2 = (*(int *)(obj + 0xcf98) + 0x20) << 12;
            int z2 = *(int *)(obj + 0xcf9c) << 12;
            DrawMovingPickupShadow(x2, y2, z2, 0x20);
        }
    }

    const PickupShadowItem *items = s_pickupShadowTables[g_trackId];
    if (items == NULL) {
        return;
    }

    while (items->visOff != -1) {
        int objMode = *(short *)((char *)g_objectStructArray + items->visOff + 2);
        if (objMode != -1) {
            SubmitGroundShadow((intptr_t)&g_pickupShadowVerts[items->bufIdx * 12]);
        }
        items++;
    }
}

/**
 * DrawGroundParticlesD3D — FUN_004626B0 — 2064 bytes
 *
 * Renders leaf/confetti particle billboards from the 16-slot particle array.
 * Each slot has 4 vertices (XYZ), transformed through camera matrix, projected
 * to screen, and submitted to the tpage batch system on g_tpageParallax1.
 *
 * Particle Y is assumed 0 (ground level); only XZ are used from vertex data.
 * Slots with g_renderStateBlock[slot] == 0 are skipped (inactive).
 * Slots that fail depth/clip tests are cleared if owned by current viewport.
 */
void DrawGroundParticlesD3D(int vpIdx)
{
    if (g_tpageStateArray[g_tpageParallax1] != 4) {
        return;
    }

    /* Pre-compute camera Y contribution (particle Y assumed 0, so dY = -camY) */
    int negCamY = -g_camIntY;
    int camY_R0 = g_viewMtx10 * negCamY;   /* view row 1, applied to X output */
    int camY_R1 = g_viewMtx11 * negCamY;   /* view row 1, applied to Y output */
    int camY_R2 = g_viewMtx12 * negCamY;   /* view row 1, applied to Z output */

    float invDepthScale = 1.0f / (g_farClipFloat + 16.0f);

    /* Do not depend on whatever state the previous draw left behind. */
    R_PushState();
    R_SetBlendMode(R_BLEND_ALPHA);
    R_SetAlphaTest(1);
    R_SetAlphaRef(0.01f);
    R_SetDepthTest(1);
    R_SetDepthFunc(R_DEPTH_LEQUAL);
    R_SetDepthWrite(0);

    for (int slot = 0; slot < 16; slot++) {
        GroundParticle *gp = &g_particleSlots[slot];
        int tpage = g_tpageParallax1;

        if (g_renderStateBlock[slot] == 0) {
            continue;
        }

        /* Transform 4 vertices: depth check first */
        int dX0 = gp->corners[0].x - g_camIntX;
        int dZ0 = gp->corners[0].z - g_camIntZ;
        int cz0 = (g_viewMtx02 * dX0 + camY_R2 + g_viewMtx22 * dZ0) / 4096;
        if (cz0 <= 0 || cz0 > g_farClipDepth) {
            goto clearCheck;
        }

        int dX1 = gp->corners[1].x - g_camIntX;
        int dZ1 = gp->corners[1].z - g_camIntZ;
        int cz1 = (g_viewMtx02 * dX1 + camY_R2 + g_viewMtx22 * dZ1) / 4096;
        if (cz1 <= 0 || cz1 > g_farClipDepth) {
            goto clearCheck;
        }

        int dX2 = gp->corners[2].x - g_camIntX;
        int dZ2 = gp->corners[2].z - g_camIntZ;
        int cz2 = (g_viewMtx02 * dX2 + camY_R2 + g_viewMtx22 * dZ2) / 4096;
        if (cz2 <= 0 || cz2 > g_farClipDepth) {
            goto clearCheck;
        }

        int dX3 = gp->corners[3].x - g_camIntX;
        int dZ3 = gp->corners[3].z - g_camIntZ;
        int cz3 = (g_viewMtx02 * dX3 + camY_R2 + g_viewMtx22 * dZ3) / 4096;
        if (cz3 <= 0 || cz3 > g_farClipDepth) {
            goto clearCheck;
        }

        /* Project all 4 vertices to screen */
        int cx, cy;

        cx = (g_viewMtx00 * dX0 + camY_R0 + g_viewMtx20 * dZ0) / 4096;
        cy = (g_viewMtx01 * dX0 + camY_R1 + g_viewMtx21 * dZ0) / 4096;
        int sx0 = g_screenCenterX + (g_projScaleXCurrent * cx) / cz0;
        int sy0 = g_screenCenterY - (g_projScaleY * cy) / cz0;

        cx = (g_viewMtx00 * dX1 + camY_R0 + g_viewMtx20 * dZ1) / 4096;
        cy = (g_viewMtx01 * dX1 + camY_R1 + g_viewMtx21 * dZ1) / 4096;
        int sx1 = g_screenCenterX + (g_projScaleXCurrent * cx) / cz1;
        int sy1 = g_screenCenterY - (g_projScaleY * cy) / cz1;

        cx = (g_viewMtx00 * dX2 + camY_R0 + g_viewMtx20 * dZ2) / 4096;
        cy = (g_viewMtx01 * dX2 + camY_R1 + g_viewMtx21 * dZ2) / 4096;
        int sx2 = g_screenCenterX + (g_projScaleXCurrent * cx) / cz2;
        int sy2 = g_screenCenterY - (g_projScaleY * cy) / cz2;

        cx = (g_viewMtx00 * dX3 + camY_R0 + g_viewMtx20 * dZ3) / 4096;
        cy = (g_viewMtx01 * dX3 + camY_R1 + g_viewMtx21 * dZ3) / 4096;
        int sx3 = g_screenCenterX + (g_projScaleXCurrent * cx) / cz3;
        int sy3 = g_screenCenterY - (g_projScaleY * cy) / cz3;

        /* Viewport clip test */
        if (!(g_clipLeft <= sx0 || g_clipLeft <= sx1 || g_clipLeft <= sx2 || g_clipLeft <= sx3)) {
            goto next;
        }
        if (!(g_clipTop <= sy0 || g_clipTop <= sy1 || g_clipTop <= sy2 || g_clipTop <= sy3)) {
            goto next;
        }
        if (!(sx0 <= g_clipRight || sx1 <= g_clipRight || sx2 <= g_clipRight || sx3 <= g_clipRight)) {
            goto next;
        }
        if (!(sy0 <= g_clipBottom || sy1 <= g_clipBottom || sy2 <= g_clipBottom || sy3 <= g_clipBottom)) {
            goto next;
        }

        /* Submit quad via immediate mode */
        int age = g_particleAge[slot];

        /* UV: sprite from rightmost 32px column of tpage, V row selected by age */
        float vStart = (float)(((age >> 1) << 5)) * (1.0f / 256.0f); /* DAT_0052c590 = 1/256 */
        float vSize = 0.125f;                                        /* DAT_0052c598 = 32/256 */

        /* ARGB color: 0x80E0E0E0 (50% alpha, light gray) */
        unsigned int dotColor = 0x80000000 | VERTEX_WHITE_RGB; // 0x80E0E0E0;

        R_SetTexture(tpage);
        R_SetTexEnv(R_TEXENV_MODULATE);

        RenderVertex dotVerts[4] = {
            {(float)sx0, (float)sy0, (float)((cz0 > 40 ? cz0 - 24 : 16)) * invDepthScale,
             1.0f / (float)(cz0 + 16), dotColor, 0, 0.875f, vStart},
            {(float)sx1, (float)sy1, (float)((cz1 > 40 ? cz1 - 24 : 16)) * invDepthScale,
             1.0f / (float)(cz1 + 16), dotColor, 0, 1.0f, vStart},
            {(float)sx2, (float)sy2, (float)((cz2 > 40 ? cz2 - 24 : 16)) * invDepthScale,
             1.0f / (float)(cz2 + 16), dotColor, 0, 1.0f, vStart + vSize},
            {(float)sx3, (float)sy3, (float)((cz3 > 40 ? cz3 - 24 : 16)) * invDepthScale,
             1.0f / (float)(cz3 + 16), dotColor, 0, 0.875f, vStart + vSize},
        };
        R_DrawQuad(dotVerts);

        goto next;

    clearCheck:
        /* Clear slot if owned by current viewport and it failed depth test */
        if (g_particleOwner[slot] == vpIdx) {
            g_renderStateBlock[slot] = 0;
        }
    next:
        ;  /* continue to next slot */
    }
    R_PopState();
}

/**
 * DrawCheckpointComparison — 0x004C5D60 — 455 bytes
 *
 * Draws a checkpoint time comparison indicator for one race result entry.
 * Searches player entries for one with type field == 3 (active racer),
 * reads checkpoint time from g_cpTableA keyed by character ID and track,
 * compares against accumulated race points, then draws a background quad
 * and timer value with status coloring.
 *
 * Called from FUN_004c63ec case 2 (results display).
 *
 * EAX = xPos (half-resolution screen X)
 * EDX = yPos (half-resolution screen Y)
 * EBX = checkpointType (0-4, selects column in g_cpTableA)
 */
void DrawCheckpointComparison(int xPos, int yPos, int checkpointType)
{
    /* Column offsets (ints) from g_cpTableA base per checkpointType:
     * case 0: (0x8FBCAC - 0x8FBC84) / 4 = 10
     * case 1: (0x8FBCD4 - 0x8FBC84) / 4 = 20
     * case 2: (0x8FBCFC - 0x8FBC84) / 4 = 30
     * case 3: (0x8FBD10 - 0x8FBC84) / 4 = 35
     * case 4: (0x8FBC84 - 0x8FBC84) / 4 =  0
     * Jump table at 0x4C5D4C. */
    static const int colOffsets[5] = { 10, 20, 30, 35, 0 };

    int timeValue = g_raceSpeedMult * 480;  /* VALIDATED: (x<<4 - x)<<5 = x*15*32 = x*480 */
    int displayMode = 0;

    if (g_numViewports > 0) {
        Player *pArr = g_playerBase;

        for (int playerIdx = 0; playerIdx < g_numViewports; playerIdx++) {
            Player *pp = &pArr[playerIdx];
            if (pp->lapsCompleted == 3) {                             /* P_INT(0x5C)>>16 = lapsCompleted */
                int cpValue = 0;

                if ((unsigned int)checkpointType <= 4) {
                    short charId = pp->charId;
                    /* Row index: VALIDATED (c*4+c)*8+c = c*41; stride = 41 ints = 0xA4 bytes */
                    int rowIdx = charId * 41;
                    cpValue = g_cpTableA[rowIdx + colOffsets[checkpointType] + g_trackId];
                }

                timeValue = g_racePointsTotal[playerIdx];  /* [playerIdx*4 + 0x8FB67C] */
                if (cpValue == timeValue) {
                    displayMode = 2;
                }
                else {
                    displayMode = 1;
                }
                break;
            }
        }
    }

    /* Draw background quad */
    DrawTexturedQuad(xPos * 2 - 0xD0, yPos * 2,
                     0x41200000, 200, 32,
                     g_uiTexPage + 1,
                     0, 192, 100, 16,
                     VERTEX_WHITE);

    DrawTimer(xPos * 2, yPos * 2, 0x41200000, timeValue, displayMode);
}

/**
 * DrawPlayerCheckpoint — FUN_004c5974 — 511 bytes
 *
 * Draws a specific player's checkpoint time comparison indicator.
 * Unlike DrawCheckpointComparison (which searches viewport entries),
 * this reads a single player entry by index.
 *
 * Two quad sizes: multi-viewport (small) or single-viewport (large).
 *
 * EAX = xPos, EDX = yPos, EBX = playerIndex, ECX = checkpointType (0-4)
 */
void DrawPlayerCheckpoint(int xPos, int yPos, int playerIndex, int checkpointType)
{
    static const int colOffsets[5] = { 10, 20, 30, 35, 0 };  /* VALIDATED: jump table at 0x4C5960 */

    /* Read this player's type field */
    Player *pp = &g_playerBase[playerIndex];

    int displayMode = 0;

    if (pp->lapsCompleted == 3 && (unsigned int)checkpointType <= 4) {  /* P_INT(0x5C)>>16 = lapsCompleted */
        short charId = pp->charId;
        int rowIdx = charId * 41;
        int cpValue = g_cpTableA[rowIdx + colOffsets[checkpointType] + g_trackId];
        int pointsValue = g_racePointsTotal[playerIndex];

        if (pointsValue == cpValue) {
            displayMode = 2;
        }
        else {
            displayMode = 1;
        }
    }

    int screenX = xPos * 2;
    int screenY = yPos * 2;
    int tpage = g_uiTexPage + 1;

    /* Single viewport: large quad */
    if (g_numViewports <= 1) {
        DrawTexturedQuad(screenX - 0xD0, screenY,
                         0x41200000, 200, 32,
                         tpage, 0, 112, 100, 16,
                         VERTEX_WHITE);
    }
    /* Multi-viewport: small quad */
    else {    
        DrawTexturedQuad(screenX - 0x58, screenY,
                         0x41200000, 80, 32,
                         tpage, 0, 160, 40, 16,
                         VERTEX_WHITE);
    }

    int timeValue = g_racePointsTotal[playerIndex];
    DrawTimer(xPos * 2, yPos * 2, 0x41200000, timeValue, displayMode);
}

/**
 * DrawLapCheckpoint — FUN_004c5698 — 387 bytes
 *
 * Draws a per-player per-lap checkpoint time comparison.
 * Compares player's race position against the lap index threshold,
 * then looks up lap time from g_cpTableA with one of 3 column offsets.
 *
 * EAX = xPos, EDX = yPos, EBX = playerIndex, ECX = lapIndex,
 * [stack] = checkpointType (0, 4, or other)
 *
 * Uses a per-player-per-lap time array at 0x8FB64C (stride: 3 ints per player).
 */
void DrawLapCheckpoint(int xPos, int yPos, int playerIndex, int lapIndex, int checkpointType)
{
    int *lapTimes = g_racePointsLaps;

    int displayMode = 0;

    /* Compute player entry offset */
    Player *pp = &g_playerBase[playerIndex];
    int typeField = pp->lapsCompleted;                                /* P_INT(0x5C)>>16 = lapsCompleted */

    if (typeField > lapIndex) {
        /* Player has passed this lap — look up checkpoint time */
        /* Compute comparison array index: lapIndex + playerIndex * 3 */
        int lapTimeIdx = playerIndex * 3 + lapIndex;

        short charId = pp->charId;
        int rowIdx = charId * 41;  /* VALIDATED: (c*4+c)*8+c = c*41 */

        int cpValue;
        /* Column offsets from g_cpTableA:
         * type 4: 0x8FBC98 = base + 5 ints
         * type 0: 0x8FBCC0 = base + 15 ints
         * other:  0x8FBCE8 = base + 25 ints */
        if (checkpointType == 4) {
            cpValue = g_cpTableA[rowIdx + 5 + g_trackId];
        }
        else if (checkpointType == 0) {
            cpValue = g_cpTableA[rowIdx + 15 + g_trackId];
        }
        else {
            cpValue = g_cpTableA[rowIdx + 25 + g_trackId];
        }

        int lapTime = lapTimes[lapTimeIdx];
        if (lapTime == cpValue) {
            displayMode = 2;
        }
        else {
            displayMode = 1;
        }
    }

    /* Draw background quad — UV Y varies by lap index */
    int uvY = lapIndex * 16 + 0x40;
    DrawTexturedQuad(xPos * 2 - 0x58, yPos * 2,
                     0x41200000, 80, 32,
                     g_uiTexPage + 1,
                     0, uvY, 40, 16,
                     VERTEX_WHITE);

    int timeValue = lapTimes[playerIndex * 3 + lapIndex];
    DrawTimer(xPos * 2, yPos * 2, 0x41200000, timeValue, displayMode);
}
