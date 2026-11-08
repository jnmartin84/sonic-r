/**
 * render_char_sprites.c — Character-attached sprite rendering
 *
 * SubmitCharacterSprite @ 0x004502E8 — 1132 bytes
 * RenderItemSprite     @ 0x0044F9C4 — 1139 bytes
 * RenderPlayerItemEffect @ 0x0044FE38 — 1200 bytes (was RenderWarpEffect)
 * RenderAirWaterEffect @ 0x004599BC — 1862 bytes
 * RenderNetworkPlayers @ 0x00465500 — 670 bytes
 *
 * All functions share the same rendering pattern:
 *   1. Transform world position through view matrix (3x3 at 0x6E9C44)
 *   2. Perspective project to screen coordinates
 *   3. Viewport bounds check
 *   4. Submit textured quad (4 verts + 6 indices) to D3D tpage buffer
 *   5. Fog/alpha ramp: >0.9 → 0, <0.7 → max, else linear
 */

#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "sonicr_functions.h"
#include "r_types.h"
#include "r_state.h"
#include "r_draw.h"
#include "net_transport.h"
#include <math.h>
#include <string.h>

/* Per-player decoration data: 0x64 byte stride, entries are ints terminated by -1 */
extern const char *net_get_slot_name(int slot);

/* View matrix access: g_viewMtx[16] with stride-4 (matches binary layout).
 * Binary addresses: row0 at 0x6E9C44, row1 at 0x6E9C54, row2 at 0x6E9C64. */
#define VM(r,c) (g_viewMtx[(r)*4 + (c)])

/* UV table offsets: +47=0x63FD98 (char sprites), +63=0x63FDD8 (item sprites) */

/* Depth fog constants (same for all sprite functions) */
#define DEPTH_THRESH_HI   0.9    /* 0x52C228 etc — beyond this: fully transparent */
#define DEPTH_THRESH_LO   0.7    /* 0x52C230 etc — below this: fully opaque */
#define DEPTH_FOG_OFFSET  (-0.9) /* 0x52C238 etc */
#define DEPTH_FOG_SCALE   1275.0 /* 0x52C240 etc */

/* Per-function depth scale addend (added to g_farClipFloat) */
#define DEPTH_ADD_CHAR    (-24.0f) /* 0x52C224 — character sprite */
#define DEPTH_ADD_ITEM    (-72.0f) /* 0x52C1DC — item sprite */
#define DEPTH_ADD_WARP    (-24.0f) /* 0x52C200 — warp effect */


/* fixmul12: binary's signed >>12 with round-toward-zero */
static inline int fixmul12(int val) {
    int s = val >> 31;
    return (val - (s << 12)) >> 12;
}

/* Compute fog alpha from normalized depth */
static int computeFogAlpha(float depthNorm, int maxAlpha) {
    sr_double dv = (sr_double)depthNorm;
    if (dv > DEPTH_THRESH_HI) {
        return 0;
    }
    if (dv <= DEPTH_THRESH_LO) {
        return maxAlpha;
    }
    int raw = (int)sr_lrint((dv + DEPTH_FOG_OFFSET) * DEPTH_FOG_SCALE);
    return (raw < 0) ? -raw : raw;   /* abs — binary: cdq; xor eax,edx; sub eax,edx */
}

/**
 * SubmitCharacterSprite — FUN_004502E8 — 1132 bytes
 * Renders a character trail/sparkle sprite at the player's position.
 * Watcom: EAX=player, EDX=yOffset, EBX=charId
 * Sprite half-size: 0x28. Depth subtract: 0x18.
 */
void SubmitCharacterSprite(Player *player, int yOffset, int charId)
{
    /* 0x4502F1: world pos relative to camera */
    int dx = (player->posX - g_camOrientX) >> 12;                  /* 0x4502F7 */
    int dy = (-(yOffset + player->posY) - g_camOrientY) >> 12;     /* 0x4502FE */
    int dz = (player->posZ - g_camOrientZ) >> 12;                  /* 0x45031C */

    /* 0x450313: view Z (depth) — column 2 of view matrix */
    int viewZ = fixmul12(VM(0,2)*dx + VM(1,2)*dy + VM(2,2)*dz);
    if (viewZ < 1 || viewZ > g_farClipTimes8) {
        return;                /* 0x450351 */
    }

    /* 0x450366: view X and Y */
    int viewX = fixmul12(VM(0,0)*dx + VM(1,0)*dy + VM(2,0)*dz);
    int viewY = fixmul12(VM(0,1)*dx + VM(1,1)*dy + VM(2,1)*dz);

    /* 0x4503C7: project 4 screen corners (half-size 0x28 = 40) */
    int scrL = g_screenCenterX + (viewX - 0x28) * g_projScaleXCurrent / viewZ;
    int scrT = g_screenCenterY - (viewY + 0x28) * g_projScaleY / viewZ;
    int scrR = g_screenCenterX + (viewX + 0x28) * g_projScaleXCurrent / viewZ;
    int scrB = g_screenCenterY - (viewY - 0x28) * g_projScaleY / viewZ;

    /* 0x45043B: reduce depth for sprite sorting */
    int depth = viewZ - 0x18;
    if (depth < 1) {
        return;                                          /* 0x450446 */
    }

    /* 0x45044F: viewport bounds check */
    if (scrL > g_clipRight) {
        return;
    }
    if (scrR < g_clipLeft) {
        return;
    }
    if (scrT > g_clipBottom) {
        return;
    }
    if (scrB < g_clipTop) {
        return;
    }

    /* 0x45048B: UV coords from charId */
    int uvA, uvB;
    if (charId == CHAR_SUPER_SONIC) {                                              /* Super Sonic */
        uvB = 0x60;
        uvA = 0xC0;                                   /* 0x450490 */
    }
    else {
        /* binary: (charId*4-charId)*16 + 0x60 = charId*48 + 96 */
        uvA = charId * 48 + 0x60;                                  /* 0x45049C */
        uvB = 0x30;                                                 /* 0x4504A8 */
    }

    /* 0x4504B0: depth normalization */
    float depthScale = g_farClipFloat + DEPTH_ADD_CHAR;
    float fDepth = (float)depth;
    float depthNorm = fDepth / depthScale;                          /* z-buffer value */
    float rhw = 1.0f / fDepth;                                     /* perspective W */

    /* 0x4504C7: UV from sprite table (0x63FCDC base, +47 offset for V) */
    float uTL = g_uvLUT256[uvA];                             /* top-left U */
    float vTL = g_uvLUT256[uvA + 47];                        /* top-left V */
    float uBR = g_uvLUT256[uvB];                             /* bottom-right U */
    float vBR = g_uvLUT256[uvB + 47];                        /* bottom-right V */

    int maxAlpha = 0xFF;                                            /* 0x4504CE */

    /* 0x4504FC: tpage check */
    int tpage = g_tpageCharBase;
    if (g_tpageStateArray[tpage] != 4) {
        return;                      /* 0x450517 */
    }

    /* Compute fog alpha */
    int alpha = computeFogAlpha(depthNorm, maxAlpha);
    /* Fully fogged out — nothing to submit. The viewZ cull above rejects
     * at the far plane; computeFogAlpha reaches 0 at 0.9x of it. */
    if (alpha == 0) {
        return;
    }
    uint32_t argb = ((uint32_t)alpha << 24) | VERTEX_WHITE_RGB;

    R_SetTexture(tpage);
    R_SetTexEnv(R_TEXENV_MODULATE);

    RenderVertex v[4] = {
        { (float)scrL, (float)scrT, depthNorm, rhw, argb, 0, uTL, uBR },
        { (float)scrR, (float)scrT, depthNorm, rhw, argb, 0, vTL, uBR },
        { (float)scrR, (float)scrB, depthNorm, rhw, argb, 0, vTL, vBR },
        { (float)scrL, (float)scrB, depthNorm, rhw, argb, 0, uTL, vBR },
    };
    R_DrawQuad(v);
}

/**
 * RenderNetworkPlayers — FUN_00465500 — 670 bytes
 * Renders remote network players as billboard sprites with name/decoration labels.
 * Iterates all players, skips the local player, projects each to screen,
 * then calls DrawTexturedQuad for the character sprite and decoration entries.
 *
 * No parameters (void). Uses globals for player data and network state.
 */
void RenderNetworkPlayers(void)
{
    int numPlayers = g_numViewports;                                /* [0x6E9910] */
    if (numPlayers <= 0) {
        return;
    }

    char *decoPtr = g_netPlayerDecorations;                         /* 0x68ACF8 */

    for (int i = 0; i < numPlayers; i++, decoPtr += 0x64) {
        /* 0x46552D: skip local player */
        int localIdx = g_localPlayerIndex & 0xFFFF;                 /* word [0x68ACDC] */
        if (localIdx == i) {
            continue;
        }

        Player *player = &g_playerBase[i];
        { extern int NetPlayerGone(int); if (NetPlayerGone(i)) continue; }   /* left the online race */

        /* 0x465540: world position relative to camera */
        int dx = (player->posX - g_camOrientX) >> 12;              /* 0x465549 */
        int rawY = player->posY + player->yOffset;                 /* 0x465562: playerY + player+0x68 */
        int dy = (-rawY + 0x64000 - g_camOrientY) >> 12;           /* 0x465565-0x465581 */
        int dz = (player->posZ - g_camOrientZ) >> 12;              /* 0x46557E */

        /* 0x46554D: view Z (depth) — simplified, no fixmul12 rounding */
        int viewZ = (VM(0,2)*dx + VM(1,2)*dy + VM(2,2)*dz) >> 12;  /* 0x46559F */
        if (viewZ < 1 || viewZ > g_farClipTimes8) {
            continue;        /* 0x4655A5 */
        }

        /* 0x4655BA: view X and Y — compute directly, no fixmul12 */
        int vxRaw = (VM(1,0)*dy + VM(0,0)*dx + VM(2,0)*dz) >> 12;  /* 0x4655F0 */
        int vyRaw = (VM(0,1)*dx + VM(1,1)*dy + VM(2,1)*dz) >> 12;  /* 0x4655F5 */

        /* 0x4655F7: simplified projection (<<9 instead of projScale) */
        int screenX = (vxRaw << 9) / viewZ + 0x140;                /* 0x465612: +320 */
        int screenY = 0xF0 - (vyRaw << 9) / viewZ;                 /* 0x465627: 240-result */

        /* 0x465629: depth reduction */
        int depthReduced = viewZ - 0x60;                            /* 0x465629 */

        /* 0x46562C: depth normalization */
        float fDepthNorm = (float)viewZ / g_farClipFloat;           /* 0x465604 */

        /* 0x465632: check reduced depth >= 1 (float comparison) */
        if ((float)depthReduced < 1.0f) continue;                   /* 0x46563C */

        /* 0x46563E: fog/alpha computation */
        int alpha;
        sr_double dv = (sr_double)fDepthNorm;                   /* 0x465641 */
        /* ROM constants at 0x52C5CC/D4/DC/C4 — same thresholds */
        if (dv > DEPTH_THRESH_HI) {                             /* 0x465644 */
            alpha = 0;
        } else if (dv <= DEPTH_THRESH_LO) {                    /* 0x465682 */
            alpha = 0xFF;                                       /* 0x4656B1 */
        } else {
            int raw = (int)sr_lrint((dv + DEPTH_FOG_OFFSET) * DEPTH_FOG_SCALE); /* 0x465690 */
            alpha = (raw < 0) ? -raw : raw;
        }

        unsigned int argb = ((unsigned int)alpha << 24) | 0x00FFFFFF; /* 0x4656B6: white */

        /* 0x4656BF: draw character sprite — base character billboard */
        /* Float depth for DrawTexturedQuad — store as int bits */
        float fDepthR = (float)depthReduced;
        int depthBits;
        *(float *)&depthBits = fDepthR;                         /* reinterpret */

        /* Adjust screen position for sprite placement */
        int sprX = screenX - 8;                                  /* 0x4656D7 */
        int sprY = screenY - 0x14;                               /* 0x4656DC */

        DrawTexturedQuad(
            sprX, sprY,                                          /* EAX, EDX */
            depthBits,                                           /* depth (float bits) */
            0x10,                                                /* width = 16 */
            0x14,                                                /* height = 20 */
            g_tpageParallax1,                                    /* tpage [0x8F6C3C] */
            0x40,                                                /* uvX */
            0x9D,                                                /* uvY */
            8,                                                   /* uvW */
            0xA,                                                 /* uvH */
            argb);                                               /* color */

        screenX = sprX + 0x14;                                   /* 0x4656F1: advance X */

        /* In-race nameplate. Binary read deco glyph entries (16-slot cap)
         * from decoPtr + 0x14; we read the raw 32-char username instead so
         * longer names aren't truncated mid-flight. Menu/results screens
         * still walk the deco entry for binary-faithful glyph layout. */
        const char *name = net_get_slot_name(i);
        screenY -= 6;                                            /* 0x4656FB: edi-6 */

        while (*name) {
            char c = *name++;
            int glyph;
            if (c >= 'a' && c <= 'z') {
                glyph = c - 'a';
            }
            else if (c >= 'A' && c <= 'Z') {
                glyph = c - 'A';
            }
            else if (c >= '0' && c <= '9') {
                glyph = 26 + (c - '0');
            }
            else {
                continue;                                       /* skip non-alphanum, mirrors net_deco_set_name */
            }

            int *tbl = g_romGlyphTable + glyph * 4;              /* 0x501F84 + idx*16 */
            int uvX = tbl[0] + 0x40;
            int uvYraw = tbl[1] - 0xC6;
            int uvY = (uvYraw / 10) * 11 + 0xA7;
            int uvW = tbl[2];
            int width = uvW * 2;

            float fDepthR2 = (float)depthReduced;
            int depthBits2;
            *(float *)&depthBits2 = fDepthR2;

            DrawTexturedQuad(
                screenX, screenY,
                depthBits2,
                width,
                0x16,
                g_tpageParallax1,
                uvX, uvY, uvW, 0x0B,
                argb);

            screenX += width;
        }

        int textH = 0x16;
        int iconH = 26;
        int iconYOff = (textH - iconH) / 2;
        const char *plat = net_get_slot_platform(i);
        uint8_t reg = net_get_slot_region(i);
        int iconIdx = net_platform_icon(plat, reg);
        int icoUvX, icoUvY;
        net_platform_icon_uv(iconIdx, &icoUvX, &icoUvY);
        float fDepthIco = (float)depthReduced;
        int depthBitsIco;
        *(float *)&depthBitsIco = fDepthIco;
        screenX += 2;
        DrawTexturedQuad(
            screenX, screenY + iconYOff,
            depthBitsIco,
            iconH, iconH,
            TPAGE_PLATFORM_ICONS,
            icoUvX, icoUvY, PLATFORM_ICON_SIZE, PLATFORM_ICON_SIZE,
            argb);        
    }
}

/**
 * RenderItemSprite — FUN_0044F9C4 — 1139 bytes
 * Renders a held-item sprite (shield, ring magnet, etc.) at the player's position.
 * Watcom: EAX=player, EDX=itemModelId (from player+0x100), EBX=itemType (player+0x62>>16)
 * Sprite half-size: 0x20. Depth subtract: 0x48. UV table offset: +63 (0x63FDD8).
 */
void RenderItemSprite(Player *player, int modelId, int itemType)
{
    /* 0x44F9CD: world pos relative to camera */
    int dx = (player->posX - g_camOrientX) >> 12;
    int dy = (-(modelId + player->posY) - g_camOrientY) >> 12;     /* EDX=modelId used as Y offset! */
    int dz = (player->posZ - g_camOrientZ) >> 12;

    /* 0x44FA00: view Z (depth) */
    int viewZ = fixmul12(VM(0,2)*dx + VM(1,2)*dy + VM(2,2)*dz);
    if (viewZ < 1 || viewZ > g_farClipTimes8) {
        return;
    }

    /* view X and Y */
    int viewX = fixmul12(VM(0,0)*dx + VM(1,0)*dy + VM(2,0)*dz);
    int viewY = fixmul12(VM(0,1)*dx + VM(1,1)*dy + VM(2,1)*dz);

    /* 0x44FAA0: project 4 screen corners (half-size 0x20) */
    int scrL = g_screenCenterX + (viewX - 0x20) * g_projScaleXCurrent / viewZ;
    int scrT = g_screenCenterY - (viewY + 0x10) * g_projScaleY / viewZ;
    int scrR = g_screenCenterX + (viewX + 0x20) * g_projScaleXCurrent / viewZ;
    int scrB = g_screenCenterY - (viewY - 0x10) * g_projScaleY / viewZ;

    /* depth reduction */
    int depth = viewZ - 0x48;
    if (depth < 1) {
        return;
    }

    /* viewport bounds check */
    if (scrL > g_clipRight) {
        return;
    }
    if (scrR < g_clipLeft) {
        return;
    }
    if (scrT > g_clipBottom) {
        return;
    }
    if (scrB < g_clipTop) {
        return;
    }

    /* 0x44FB79: UV from itemType
     * typeSlot = ((itemType-1)*64) & 0xC0 → selects U column (4 frames)
     * frameOff = floor_div(itemType-1, 4) * 32 + 0x90 → selects V row */
    int typeSlot, frameOff;
    int t = itemType - 1;
    int s = t >> 31;
    frameOff = ((t + (s & 3)) >> 2) * 32 + 0x90;              /* 0x44FB88-0x44FBB3 */
    typeSlot = ((itemType << 6) - 0x40) & 0xC0;               /* 0x44FB9A */

    /* depth normalization */
    float depthScale = g_farClipFloat + DEPTH_ADD_ITEM;
    float fDepth = (float)depth;
    float depthNorm = fDepth / depthScale;
    float rhw = 1.0f / fDepth;

    /* UV from sprite table:
     * U left/right from typeSlot: base(+0) and +63(=0x63FDD8)
     * V top/bottom from frameOff: base(+0) and +31(=0x63FD58) */
    float uvUleft  = g_uvLUT256[typeSlot];                   /* [typeSlot*4 + 0x63FCDC] */
    float uvUright = g_uvLUT256[typeSlot + 63];              /* [typeSlot*4 + 0x63FDD8] */
    float uvVtop   = g_uvLUT256[frameOff];                   /* [frameOff*4 + 0x63FCDC] */
    float uvVbot   = g_uvLUT256[frameOff + 31];              /* [frameOff*4 + 0x63FD58] */

    int maxAlpha = 0xFF;

    /* tpage check */
    int tpage = g_tpageCharBase;
    if (g_tpageStateArray[tpage] != 4) {
        return;
    }

    int alpha = computeFogAlpha(depthNorm, maxAlpha);
    /* Fully fogged out — nothing to submit. The viewZ cull above rejects
     * at the far plane; computeFogAlpha reaches 0 at 0.9x of it. */
    if (alpha == 0) {
        return;
    }
    uint32_t argb = ((uint32_t)alpha << 24) | VERTEX_WHITE_RGB;

    R_SetTexture(tpage);
    R_SetTexEnv(R_TEXENV_MODULATE);

    RenderVertex v[4] = {
        { (float)scrL, (float)scrT, depthNorm, rhw, argb, 0, uvUleft,  uvVtop },
        { (float)scrR, (float)scrT, depthNorm, rhw, argb, 0, uvUright, uvVtop },
        { (float)scrR, (float)scrB, depthNorm, rhw, argb, 0, uvUright, uvVbot },
        { (float)scrL, (float)scrB, depthNorm, rhw, argb, 0, uvUleft,  uvVbot },
    };
    R_DrawQuad(v);
}

/**
 * RenderPlayerItemEffect — FUN_0044FE38 — 1200 bytes
 * Renders a player-attached item effect sprite. Two visual modes selected
 * by the sign of effectState (player->itemEffectState, short at +0x82):
 *   effectState < 0  → water shield bubble (uses g_tpageCharBase, atlas
 *                      region U=[uvB..uvB+63], V=[uvA..uvA+63])
 *   effectState > 0  → the ring attractor's gold shield, animated from
 *                      g_itemEffectAnimPhase on g_tpagePlayfield2.
 *                      race.c:223 sets the timer from item id 6, and
 *                      race_update.c:433 is what it buys: a 500/1000-unit
 *                      ring attraction radius while it runs.
 *
 * Watcom: EAX=player, EDX=yOffset, EBX=effectState, ECX=modelId
 *
 * Quad corner offsets are derived from modelId:
 *   halfW = 0x30 - modelId, halfH = modelId + 0x30
 */
void RenderPlayerItemEffect(Player *player, int yOffset, int effectState, int modelId)
{
    /* 0x44FE45: expiry flicker. Under 60 frames left the shield draws on even
     * frames only — 0x44FE4E reads [0x902078], which UpdateLapCounter toggles
     * 0/1 every frame at 0x482028. That address is g_raceOrder[2]; reading it
     * through any other name gives a constant and the flicker never happens. */
    if (effectState > 0 && effectState < 0x3C && g_raceOrder[2] != 0) {
        return;
    }

    /* world pos relative to camera */
    int dx = (player->posX - g_camOrientX) >> 12;
    int dy = (-(yOffset + player->posY) - g_camOrientY) >> 12;
    int dz = (player->posZ - g_camOrientZ) >> 12;

    /* view Z */
    int viewZ = fixmul12(VM(0,2)*dx + VM(1,2)*dy + VM(2,2)*dz);
    if (viewZ < 1 || viewZ > g_farClipTimes8) {
        return;
    }

    /* view X and Y */
    int viewX = fixmul12(VM(0,0)*dx + VM(1,0)*dy + VM(2,0)*dz);
    int viewY = fixmul12(VM(0,1)*dx + VM(1,1)*dy + VM(2,1)*dz);

    /* 0x44FF32: quad corners — offset from modelId */
    int halfW = 0x30 - modelId;                                     /* 0x44FF37: 0x30 - ecx(modelId) */
    int halfH = modelId + 0x30;                                     /* 0x44FF5A */

    int scrL = g_screenCenterX + (viewX - halfW) * g_projScaleXCurrent / viewZ;
    int scrT = g_screenCenterY - (viewY + halfH) * g_projScaleY / viewZ;
    int scrR = g_screenCenterX + (viewX + halfW) * g_projScaleXCurrent / viewZ;
    int scrB = g_screenCenterY - (viewY - halfH) * g_projScaleY / viewZ;

    /* depth reduction */
    int depth = viewZ - 0x48;
    if (depth < 1) {
        return;
    }

    /* viewport bounds check */
    if (scrL > g_clipRight) {
        return;
    }
    if (scrR < g_clipLeft) {
        return;
    }
    if (scrT > g_clipBottom) {
        return;
    }
    if (scrB < g_clipTop) {
        return;
    }

    /* UV coords — negative branch uses fixed atlas region (water shield);
     * positive branch uses animation phase + secondary tpage (unknown item).
     * uvB is the U-pixel-base, uvA is the V-pixel-base (verified against
     * binary 0x44fe38 2026-04-14). maxAlpha also branches on sign. */
    int uvA, uvB, maxAlpha;
    if (effectState < 0) {
        uvB = 0x80;
        uvA = 0xB0;
        maxAlpha = 0x80;                                            /* 0x45001f: [bp-0x28]=ebx=0x80 */
    }
    else {
        /* 0x450024: g_itemEffectAnimPhase based UV */
        int phase = g_itemEffectAnimPhase;
        int s = phase >> 31;
        uvA = ((phase - (s << 2)) >> 2) * 64;                       /* 0x450050: shl eax, 6 — no +0x60 */
        int bits = (phase & 3) << 6;
        uvB = bits;
        maxAlpha = 0x80;                                            /* 0x450044: [bp-0x28]=ecx=0x60 */
    }
    int tpageToUse = (effectState < 0) ? g_tpageCharBase : g_tpagePlayfield2;

    /* depth normalization */
    float depthScale = g_farClipFloat + DEPTH_ADD_WARP;
    float fDepth = (float)depth;
    float depthNorm = fDepth / depthScale;
    float rhw = 1.0f / fDepth;

    /* UV from sprite table. Per binary vertex writes at 0x4501ee/0x4501f7/
     * 0x450283/0x45028e/0x4502b2/0x4502b8/0x4502d6/0x4502de:
     *   uvB → U axis (uLeft at LUT[uvB], uRight at LUT[uvB+63])
     *   uvA → V axis (vTop at LUT[uvA], vBot at LUT[uvA+63]) */
    float uLeft  = g_uvLUT256[uvB];
    float uRight = g_uvLUT256[uvB + 63];
    float vTop   = g_uvLUT256[uvA];
    float vBot   = g_uvLUT256[uvA + 63];

    /* tpage check */
    int tpage = tpageToUse;
    if (g_tpageStateArray[tpage] != 4) {
        return;
    }

    int alpha = computeFogAlpha(depthNorm, maxAlpha);
    /* Fully fogged out — nothing to submit. The viewZ cull above rejects
     * at the far plane; computeFogAlpha reaches 0 at 0.9x of it. */
    if (alpha == 0) {
        return;
    }
    uint32_t argb = ((uint32_t)alpha << 24) | VERTEX_WHITE_RGB;

    R_SetTexture(tpage);
    R_SetTexEnv(R_TEXENV_MODULATE);//ADD_SIGNED);
    R_SetDepthWrite(0);

    RenderVertex v[4] = {
        { (float)scrL, (float)scrT, depthNorm, rhw, argb, 0, uLeft,  vTop },
        { (float)scrR, (float)scrT, depthNorm, rhw, argb, 0, uRight, vTop },
        { (float)scrR, (float)scrB, depthNorm, rhw, argb, 0, uRight, vBot },
        { (float)scrL, (float)scrB, depthNorm, rhw, argb, 0, uLeft,  vBot },
    };
    R_DrawQuad(v);
    R_SetDepthWrite(1);

}

/**
 * RenderAirWaterEffect — FUN_004599BC — 1862 bytes
 * Renders splash/bubble/airborne trail effects for a player.
 * Two-pass structure:
 *   Pass 1: transform position history entries through view matrix, store screen coords
 *   Pass 2: read stored coords, submit textured quads with per-vertex fog alpha
 *
 * Watcom: EAX = playerIdx (0-4)
 */
void RenderAirWaterEffect(int playerIdx)
{
    /* 0x459ACA: tpage check */
    int tpage = g_tpageCharBase;
    if (g_tpageStateArray[tpage] != 4) {
        return;
    }

    /* 0x459ADC: player pointer */
    Player *player = &g_playerBase[playerIdx];

    /* 0x459AFC: effect count check */
    if (player->_unk_0x1E6 == 0) {
        return;
    }

    /* 0x459A0A: step size = 100 / effectCount */
    int effectN = (int)player->_unk_0x1E6;
    int stepSize = 100 / effectN;

    int loopCount = 0;
    int remaining = 100;
    char *histEntry = g_gateHistoryBuf + playerIdx * 0x800;

    /* ================================================================
     * PASS 1 (0x459A39-0x459C65): view transform + screen projection
     * For each active entry, compute viewXYZ and screenXY, storing them
     * back into the history buffer. Also computes a second point (pt_B)
     * at Y offset = remaining+1 into the adjacent entry (+0x40).
     * ================================================================ */
    while (1) {
        int curN = (int)player->_unk_0x1E6;
        if (curN <= loopCount) {
            break;
        }

        int wX = *(int *)(histEntry + 0x14);
        int wY = *(int *)(histEntry + 0x18);
        int wZ = *(int *)(histEntry + 0x1C);
        int dx = wX - g_camIntX;
        int dy = wY - g_camIntY;
        int dz = wZ - g_camIntZ;

        /* View transform — pt_A */
        int viewX = fixmul12(g_viewMtx00*dx + g_viewMtx10*dy + g_viewMtx20*dz);
        *(int *)(histEntry + 0x28) = viewX;

        int viewY = fixmul12(g_viewMtx01*dx + g_viewMtx11*dy + g_viewMtx21*dz);
        *(int *)(histEntry + 0x2C) = viewY;

        int viewZ = fixmul12(g_viewMtx02*dx + g_viewMtx12*dy + g_viewMtx22*dz);
        *(int *)(histEntry + 0x30) = viewZ;

        if (viewZ >= 1) {
            *(int *)(histEntry + 0x00) = g_screenCenterX + (viewX * g_projScaleXCurrent) / viewZ;
            *(int *)(histEntry + 0x04) = g_screenCenterY - (viewY * g_projScaleY) / viewZ;
        }

        /* View transform — pt_B (Y offset by remaining+1) */
        int dy2 = dy + (remaining + 1);
        int viewX2 = fixmul12(g_viewMtx00*dx + g_viewMtx10*dy2 + g_viewMtx20*dz);
        *(int *)(histEntry + 0x68) = viewX2;

        int viewY2 = fixmul12(g_viewMtx01*dx + g_viewMtx11*dy2 + g_viewMtx21*dz);
        *(int *)(histEntry + 0x6C) = viewY2;

        int viewZ2 = fixmul12(g_viewMtx02*dx + g_viewMtx12*dy2 + g_viewMtx22*dz);
        char *nextEntry = histEntry + 0x40;
        *(int *)(nextEntry + 0x30) = viewZ2;

        if (viewZ2 >= 1) {
            *(int *)(nextEntry + 0x00) = g_screenCenterX + (viewX2 * g_projScaleXCurrent) / viewZ2;
            *(int *)(nextEntry + 0x04) = g_screenCenterY - (viewY2 * g_projScaleY) / viewZ2;
        }

        remaining -= stepSize;
        loopCount++;
        histEntry += 0x80;
    }

    /* ================================================================
     * PASS 2 (0x459C6A-end): read stored coords, submit quads
     * Groups of 4 entries form a quad strip. UV cycles through 8 frames.
     * ================================================================ */
    char *p2Base = g_gateHistoryBuf + playerIdx * 0x800;
    char *p2Next = p2Base + 0x40;
    int loopCount2 = 0;

    while (1) {
        int curN2 = (int)player->_unk_0x1E6;
        if (curN2 <= loopCount2) {
            return;
        }

        char *pt0 = p2Base;
        char *pt1 = p2Next;
        char *pt2 = p2Next + 0x80;
        char *pt3 = p2Base + 0x80;

        /* Depth cull: all 4 viewZ must be in [1, farClip] */
        int vz0 = *(int *)(pt0 + 0x30);
        int vz1 = *(int *)(pt1 + 0x30);
        int vz2 = *(int *)(pt2 + 0x30);
        int vz3 = *(int *)(pt3 + 0x30);
        if (vz0 < 1 || vz0 > g_farClipTimes8 ||
            vz1 < 1 || vz1 > g_farClipTimes8 ||
            vz2 < 1 || vz2 > g_farClipTimes8 ||
            vz3 < 1 || vz3 > g_farClipTimes8)
        {
            goto pass2_next;
        }
        /* Viewport cull: skip if ALL 4 corners outside any edge */
        int sx0=*(int*)(pt0), sx1=*(int*)(pt1), sx2=*(int*)(pt2), sx3=*(int*)(pt3);
        int sy0=*(int*)(pt0+4), sy1=*(int*)(pt1+4), sy2=*(int*)(pt2+4), sy3=*(int*)(pt3+4);
        if (g_clipLeft > sx0 && g_clipLeft > sx1 && g_clipLeft > sx2 && g_clipLeft > sx3) {
            goto pass2_next;
        }
        if (g_clipRight < sx0 && g_clipRight < sx1 && g_clipRight < sx2 && g_clipRight < sx3) {
            goto pass2_next;
        }
        if (g_clipTop > sy0 && g_clipTop > sy1 && g_clipTop > sy2 && g_clipTop > sy3) {
            goto pass2_next;
        }
        if (g_clipBottom < sy0 && g_clipBottom < sy1 && g_clipBottom < sy2 && g_clipBottom < sy3) {
            goto pass2_next;
        }

        /* UV from frame cycle: 8 frames, 16 texels per slot.
         * Binary: U1 = uvSlot * (1/256) [0x52C338], U2 = U1 + 0.0625 [0x52C344].
         * V: binary uses 0.94117647 (16/17) for D3D top-down textures.
         * GL uploads row 0 as texture bottom, so V must be flipped: 1.0 - V_d3d. */
        int uvSlot = (loopCount2 & 7) << 4;
        float U1 = (float)uvSlot * (1.0f / 256.0f);         /* 0x52C338 = 1/256 */
        float U2 = (float)((sr_double)U1 + 0.0625);         /* 0x52C344 = 1/16 */

        R_SetTexture(tpage);
        R_SetTexEnv(R_TEXENV_ADD_SIGNED);

        /* Build 4 vertices with per-vertex fog alpha */
        RenderVertex rv[4];
        float zBuf;
        int fa;

        zBuf = (float)vz0 / g_farClipFloat;
        fa = ((sr_double)zBuf <= DEPTH_THRESH_HI) ? 0x80
               : computeFogAlpha(zBuf, 0x80);
        rv[0] = (RenderVertex){ (float)*(int*)(pt0), (float)*(int*)(pt0+4),
            zBuf, 1.0f / (float)vz0,
            ((uint32_t)fa << 24) | VERTEX_WHITE_RGB, 0, U1, 0.94117647f };

        zBuf = (float)vz1 / g_farClipFloat;
        fa = ((sr_double)zBuf > DEPTH_THRESH_LO) ? 0
               : ((sr_double)zBuf > DEPTH_THRESH_HI) ? computeFogAlpha(zBuf, 0x80)
               : 0x80;
        rv[1] = (RenderVertex){ (float)*(int*)(pt1), (float)*(int*)(pt1+4),
            zBuf, 1.0f / (float)vz1,
            ((uint32_t)fa << 24) | VERTEX_WHITE_RGB, 0, U1, 1.0f };

        zBuf = (float)vz2 / g_farClipFloat;
        fa = ((sr_double)zBuf > DEPTH_THRESH_LO) ? 0
               : ((sr_double)zBuf > DEPTH_THRESH_HI) ? computeFogAlpha(zBuf, 0x80)
               : 0x80;
        rv[2] = (RenderVertex){ (float)*(int*)(pt2), (float)*(int*)(pt2+4),
            zBuf, 1.0f / (float)vz2,
            ((uint32_t)fa << 24) | VERTEX_WHITE_RGB, 0, U2, 1.0f };

        zBuf = (float)vz3 / g_farClipFloat;
        fa = ((sr_double)zBuf > DEPTH_THRESH_LO) ? 0
               : ((sr_double)zBuf > DEPTH_THRESH_HI) ? computeFogAlpha(zBuf, 0x80)
               : 0x80;
        rv[3] = (RenderVertex){ (float)*(int*)(pt3), (float)*(int*)(pt3+4),
            zBuf, 1.0f / (float)vz3,
            ((uint32_t)fa << 24) | VERTEX_WHITE_RGB, 0, U2, 0.94117647f };

        R_DrawQuad(rv);

    pass2_next:
        loopCount2++;
        p2Base += 0x80;
        p2Next += 0x80;
    }
}
