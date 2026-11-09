/**
 * render_character_d3d.c — D3D character model renderer for gameplay
 *
 * RenderAllCharacterModels — FUN_0045AFFC — 350 bytes
 *   Iterates all players and renders each character's 3D model.
 *
 * RenderCharacterD3D — FUN_00462EC0 — 9789 bytes
 *   Per-player D3D character model renderer. Transforms character model
 *   vertices through bone rotation + view matrix, projects to screen,
 *   and submits textured triangles to per-tpage vertex/index batches.
 *
 * The original binary's software-path equivalent (SoftwareRasterizer_Main
 * at 0x0044CD7C, 6741 bytes) is excluded from the port — the GL/D3D path
 * is the only character renderer we ship. See cross_reference.py
 * EXCLUDED_ADDRS and reference/excluded_disasm/.
 */

#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "sonicr_functions.h"
#include "r_types.h"
#include "r_state.h"
#include "r_draw.h"
#include <math.h>

#include <math.h>

#ifdef SONICR_DC
#include <kos.h>
extern __attribute__((aligned(32))) pvr_vertex_t s_ptScratch[16];
extern void R_DrawPvrTri(pvr_vertex_t *v);
extern void R_DrawPvrQuad(pvr_vertex_t *v);
extern void R_DrawPvrStrip(pvr_vertex_t *v, int count);
#endif

extern void DispatchFaceUVAnimation(Player *player);  /* FUN_0047fea0 — 163 bytes */
extern void UpdateVertexLightingPhase(Player *player); /* FUN_00430ed4 — 282 bytes */
extern void RelocateModelVertices(int *arr, int count, int newBase, int oldBase, int fileBase);  /* FUN_00421790 */
/* Sub-functions for character sprite effects */
extern void SubmitCharacterSprite(Player *player, int yOffset, int charId);  /* 0x004502E8 */
extern void RenderItemSprite(Player *player, int modelId, int itemType);     /* 0x0044F9C4 */
extern void RenderPlayerItemEffect(Player *player, int yOffset, int effectState, int modelId); /* 0x0044FE38 — water shield / speed boost visual (was RenderWarpEffect) */
extern void RenderAirWaterEffect(int playerIdx);                          /* 0x004599BC */
extern void RenderNetworkPlayers(void);                                   /* 0x00465500 */

/* Forward declaration */
void SetupCharacterShadowQuad(Player *player);

/* ROM double constants (from PE .rdata) */
#define ROM_RAD_SCALE_A    4096.0             /* 0x52c364 */
#define ROM_RAD_SCALE_B    0.15915494327375637 /* 0x52c36c — 1/(2*pi) */
/* Combined: atan2 * ROM_RAD_SCALE_A * ROM_RAD_SCALE_B = atan2 * 4096/(2*pi) */
#define ROM_INV_256        0.00390625         /* 0x52c374 — 1/256 */
#define ROM_FLOAT_NEG24    (-24.0f)           /* 0x52c37c (float, not double) */
#define ROM_UV_OFFSET        0.125            /* 0x52c3a0 */

const int g_tailSegVtx[4][12] = {               /* ROM 0x4FC39C */
    {175,176,178,179,177,174, 187,181,180,193,195,188},
    {184,185,197,192,191,194, 208,207,210,209,206,205},
    {303,304,306,308,307,305, 319,318,325,322,312,311},
    {320,321,326,314,315,323, 334,338,339,337,336,335},
};

const int g_tailFaceMap[6][4] = {               /* ROM 0x4FC45C */
    {0,1,7,6}, {1,2,8,7}, {2,3,9,8}, {3,4,10,9}, {4,5,11,10}, {5,0,6,11},
};

/* Near-plane triangle clipper */
#include "nearclip.h"

#ifdef SONICR_DC

static RenderVertex BuildNearClipVertex(const NearClipVert *v, float depthScale)
{
    RenderVertex rv;
    int pz = v->depth - 0x20;
    if (pz < 1) {
        pz = 1;
    }
    float fpz = (float)pz;
    NearClipVert_ProjectFloat(v->camX, v->camY, v->depth > 0 ? v->depth : 1, &rv.sx, &rv.sy);
    rv.sz = fpz * reciprocal(depthScale);
    rv.rhw = reciprocal(fpz);

    uint8_t r = (v->colorR >> 13);
    uint8_t g = (v->colorG >> 13);
    uint8_t b = (v->colorB >> 13);
    if (r > 255) {
        r = 255;
    }
    if (g > 255) {
        g = 255;
    }
    if (b > 255) {
        b = 255;
    }
    rv.color = 0xFF000000 | (r << 16) | (g << 8) | b;
    rv.specular = 0;
    rv.u = v->u;
    rv.v = v->v;
    return rv;
}


static inline void modify_game_character_color(uint32_t c, uint32_t *bc, uint32_t *oc) {
    uint32_t cr = (c >> 16) & 0xff;
    uint32_t cg = (c >> 8) & 0xff;
    uint32_t cb = (c) & 0xff;

    uint32_t br, bg, bb, ocr, ocg, ocb;
    #if 1
#if 0
    br = cr > 0x7F ? 0x7F : cr;
    ocr = cr > 0x7F ? (cr - 0x7F) : 0;
    bg = cg > 0x7F ? 0x7F : cg;
    ocg = cg > 0x7F ? (cg - 0x7F) : 0;
    bb = cb > 0x7F ? 0x7F : cb;
    ocb = cb > 0x7F ? (cb - 0x7F) : 0;

    br = 0xe0 - (0x7f - br);
    bg = 0xe0 - (0x7f - bg);
    bb = 0xe0 - (0x7f - bb);
#endif
    int dr = cr - 0x7F;
    int dg = cg - 0x7F;
    int db = cb - 0x7F;

    ocr = dr > 0 ? dr : 0;
    ocg = dg > 0 ? dg : 0;
    ocb = db > 0 ? db : 0;

    br = cr - ocr;
    bg = cg - ocg;
    bb = cb - ocb;

    br = br + 0x80;
    bg = bg + 0x80;
    bb = bb + 0x80;
/*     br = (br * br) >> 8;
    bg = (bg * bg) >> 8;
    bb = (bb * bb) >> 8;
 */
    ocr -= (ocr >> 3);
    ocg -= (ocg >> 3);
    ocb -= (ocb >> 3);

    *bc  = (c & 0xff000000) | (br << 16) | (bg << 8) | (bb << 0);
    *oc = (ocr << 16) | (ocg << 8) | ocb;
    #else

    br = cr > 0x7F ? 0x7F : cr;
    ocr = cr > 0x7F ? (cr - 0x7F) : 0;

    bg = cg > 0x7F ? 0x7F : cg;
    ocg = cg > 0x7F ? (cg - 0x7F) : 0;

    bb = cb > 0x7F ? 0x7F : cb;
    ocb = cb > 0x7F ? (cb - 0x7F) : 0;

    br -= 0x30;
    bg -= 0x30;
    bb -= 0x30;

    if (br < 0) br = 0;
    if (bg < 0) bg = 0;
    if (bb < 0) bb = 0;
    br = (br << 1) + br;
    bg = (bg << 1) + bg;
    bb = (bb << 1) + bb;

//    br = 0xe0 - (0x7f - br);
  //  bg = 0xe0 - (0x7f - bg);
    //bb = 0xe0 - (0x7f - bb);

//    br += 0x3f;
  //  bg += 0x3f;
    //bb += 0x3f;
#if 0
    if (br < 0x40 && bg < 0x40 && bb < 0x40) {
            br <<= 1;
            bg <<= 1;
            bb <<= 1;
    }
#endif

    ocr -= (ocr >> 2);
    ocg -= (ocg >> 2);
    ocb -= (ocb >> 2);

    *bc  = (c & 0xff000000) | (br << 16) | (bg << 8) | (bb << 0);
    *oc = (ocr << 16) | (ocg << 8) | ocb;
#endif
}

/* DC fast path: write a NearClipVert straight into pvr_vertex_t scratch.
 * Same math as BuildNearClipVertex above (sqrt gamma intact, depthScale
 * unused since fill_vertex's z = rhw); skips the RenderVertex copy and
 * the per-vertex oargb store. */
static inline void BuildPvrNearClipVertex(pvr_vertex_t *pv, const NearClipVert *v, uint32_t flags) {
    int pz = v->depth - 0x20;
    if (pz < 1) {
        pz = 1;
    }
    float fpz = (float)pz;

    uint32_t r = (v->colorR >> 13);
    uint32_t g = (v->colorG >> 13);
    uint32_t b = (v->colorB >> 13);

    if (r > 255) {
        r = 255;
    }
    if (g > 255) {
        g = 255;
    }
    if (b > 255) {
        b = 255;
    }

    pv->flags = flags;
    NearClipVert_ProjectFloat(v->camX, v->camY, v->depth > 0 ? v->depth : 1, &pv->x, &pv->y);
    pv->z     = reciprocal(fpz);
    pv->u     = v->u;
    pv->v     = v->v;

    uint32_t baseColor = 0xff000000 | (r << 16) | (g << 8) | b;
    modify_game_character_color(baseColor, &pv->argb, &pv->oargb);
}
#else
static RenderVertex BuildNearClipVertex(const NearClipVert *v, float depthScale) {
    RenderVertex rv;
    int pz = v->depth - 0x20;
    if (pz < 1) {
        pz = 1;
    }
    float fpz = (float)pz;
    NearClipVert_ProjectFloat(v->camX, v->camY, v->depth > 0 ? v->depth : 1, &rv.sx, &rv.sy);
    rv.sz = fpz * reciprocal(depthScale);
    rv.rhw = reciprocal(fpz);

    rv.color = 0xFF000000
        | (((v->colorR >> 13) & 0xFF) << 16)
        | (((v->colorG >> 13) & 0xFF) << 8)
        |  ((v->colorB >> 13) & 0xFF);
    rv.specular = 0;
    rv.u = v->u;
    rv.v = v->v;
    return rv;
}
#endif

#ifdef SONICR_DC
/* DC strip emit for a clipped triangle. Output is a 3- or 4-vert PVR strip
 * via one R_DrawPvrStrip call. Cases mirror TrackClipAndEmitTri_DC's
 * vismask switch — same topology, different per-vertex builder. */
static int NearClipAndEmitTriStripDC(NearClipVert cv[3], int tpage)
{
    /* Tri input slots map straight: slot[i] = src[i]. */
    NearClipVert slot[4];  /* up to 4 final verts */
    int vismask = 0;
    for (int i = 0; i < 3; i++) {
        slot[i] = cv[i];
        if (slot[i].depth >= 1) {
            vismask |= (1 << i);
        }
    }
    if (vismask == 0) {
        return 0;
    }

    R_SetTexture(tpage);
    R_SetTexEnv(R_TEXENV_ADD_SIGNED);

    int sendverts = 3;

    switch (vismask) {
        case 7: /* all visible */
            break;
        case 1: /* slot 0 visible */
            NearClipLerp(&slot[0], &slot[1], &slot[1]);
            NearClipLerp(&slot[0], &slot[2], &slot[2]);
            break;
        case 2: /* slot 1 visible */
            NearClipLerp(&slot[0], &slot[1], &slot[0]);
            NearClipLerp(&slot[1], &slot[2], &slot[2]);
            break;
        case 4: /* slot 2 visible */
            NearClipLerp(&slot[0], &slot[2], &slot[0]);
            NearClipLerp(&slot[1], &slot[2], &slot[1]);
            break;
        case 3: /* slots 0+1 visible — 4-vert strip */
            sendverts = 4;
            /* slot[2] is the input far vert; lerp 1→2 needs it before lerp 0→2 overwrites it. */
            NearClipLerp(&slot[1], &slot[2], &slot[3]);
            NearClipLerp(&slot[0], &slot[2], &slot[2]);
            break;
        case 5: /* slots 0+2 visible — 4-vert strip */
            sendverts = 4;
            NearClipLerp(&slot[1], &slot[2], &slot[3]);
            NearClipLerp(&slot[0], &slot[1], &slot[1]);
            break;
        case 6: /* slots 1+2 visible — 4-vert strip */
            sendverts = 4;
            slot[3] = slot[2];                /* save visible slot 2 to position 3 */
            NearClipLerp(&slot[0], &slot[2], &slot[2]);
            NearClipLerp(&slot[0], &slot[1], &slot[0]);
            break;
        default:
            return 0;
    }

    for (int i = 0; i < sendverts; i++) {
        uint32_t f = (i == sendverts - 1) ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX;
        BuildPvrNearClipVertex(&s_ptScratch[i], &slot[i], f);
    }

    R_DrawPvrStrip(s_ptScratch, sendverts);
    return sendverts - 2;
}

/* DC strip emit for a clipped convex quad. Output is a 3-, 4-, or 5-vert
 * PVR strip via one R_DrawPvrStrip call. Cases mirror
 * TrackClipAndEmitQuad_DC's vismask switch. */
static int NearClipAndEmitQuadStripDC(NearClipVert v[4], int tpage)
{
    /* Source order TL,TR,BR,BL = src 0,1,2,3. PVR strip slot order is
     * TL,TR,BL,BR = src 0,1,3,2. */
    static const int srcFromStrip[4] = { 0, 1, 3, 2 };
    NearClipVert slot[5];  /* up to 5 final verts */
    int vismask = 0;
    for (int i = 0; i < 4; i++) {
        slot[i] = v[srcFromStrip[i]];
        if (slot[i].depth >= 1) {
            vismask |= (1 << i);
        }
    }
    if (vismask == 0) {
        return 0;
    }

    R_SetTexture(tpage);
    R_SetTexEnv(R_TEXENV_ADD_SIGNED);

    int sendverts = 4;

    switch (vismask) {
        case 15: /* all visible — fast path */
            break;
        case 1: /* slot 0 visible */
            sendverts = 3;
            NearClipLerp(&slot[0], &slot[1], &slot[1]);
            NearClipLerp(&slot[0], &slot[2], &slot[2]);
            break;
        case 2: /* slot 1 visible */
            sendverts = 3;
            NearClipLerp(&slot[1], &slot[0], &slot[0]);
            NearClipLerp(&slot[1], &slot[3], &slot[2]);
            break;
        case 4: /* slot 2 visible */
            sendverts = 3;
            NearClipLerp(&slot[2], &slot[0], &slot[0]);
            NearClipLerp(&slot[2], &slot[3], &slot[1]);
            break;
        case 8: /* slot 3 visible */
            sendverts = 3;
            /* Lerp results first (use original slot[3]), then move slot[3] into pos 1. */
            NearClipLerp(&slot[1], &slot[3], &slot[0]);
            NearClipLerp(&slot[2], &slot[3], &slot[2]);
            slot[1] = slot[3];
            break;
        case 3: /* slots 0+1 visible */
            NearClipLerp(&slot[0], &slot[2], &slot[2]);
            NearClipLerp(&slot[1], &slot[3], &slot[3]);
            break;
        case 5: /* slots 0+2 visible */
            NearClipLerp(&slot[0], &slot[1], &slot[1]);
            NearClipLerp(&slot[2], &slot[3], &slot[3]);
            break;
        case 10: /* slots 1+3 visible */
            NearClipLerp(&slot[0], &slot[1], &slot[0]);
            NearClipLerp(&slot[2], &slot[3], &slot[2]);
            break;
        case 12: /* slots 2+3 visible */
            NearClipLerp(&slot[0], &slot[2], &slot[0]);
            NearClipLerp(&slot[1], &slot[3], &slot[1]);
            break;
        case 7: /* slots 0+1+2 visible — 5-vert strip */
            sendverts = 5;
            /* slot[3] needed by both lerps below; do dest=4 first so dest=3 doesn't clobber it. */
            NearClipLerp(&slot[2], &slot[3], &slot[4]);
            NearClipLerp(&slot[1], &slot[3], &slot[3]);
            break;
        case 11: /* slots 0+1+3 visible — 5-vert strip */
            sendverts = 5;
            NearClipLerp(&slot[2], &slot[3], &slot[4]);
            NearClipLerp(&slot[0], &slot[2], &slot[2]);
            break;
        case 13: /* slots 0+2+3 visible — 5-vert strip */
            sendverts = 5;
            slot[4] = slot[3];                /* save visible slot 3 */
            NearClipLerp(&slot[1], &slot[3], &slot[3]);
            NearClipLerp(&slot[0], &slot[1], &slot[1]);
            break;
        case 14: /* slots 1+2+3 visible — 5-vert strip */
            sendverts = 5;
            slot[4] = slot[2];                /* save visible slot 2 */
            NearClipLerp(&slot[0], &slot[2], &slot[2]);
            NearClipLerp(&slot[0], &slot[1], &slot[0]);
            break;
        default:
            /* vismask 6 / 9 — diagonal pairs, impossible for convex quads. */
            return 0;
    }

    for (int i = 0; i < sendverts; i++) {
        uint32_t f = (i == sendverts - 1) ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX;
        BuildPvrNearClipVertex(&s_ptScratch[i], &slot[i], f);
    }

    R_DrawPvrStrip(s_ptScratch, sendverts);
    return sendverts - 2;
}

/* ============================================================
 * DC viewport-edge scissor path (split-screen only).
 *
 * When g_scissorEdge != SCISSOR_NONE, geometry that survives the
 * near-plane clip can still bleed across the inward-facing virtual
 * viewport edge that PVR has no knowledge of. Run a second SH-clip
 * pass against that one screen-space half-plane.
 *
 * Output is fan-emitted as N-2 individual R_DrawPvrTri calls — we
 * intentionally don't try to reconstruct a strip for arbitrary
 * convex N-gons (zig-zag winding interactions with the per-poly
 * cull mode aren't worth the risk for the split-screen-only path).
 * ============================================================ */

/* File-static perimeter-order temps for the scissor path. Reused across
 * calls — split-screen path is single-threaded per frame. */
static NearClipVert s_charNearClipBuf[6];   /* near-plane output: up to 5 (quad+1) */

/* Strip-emit a convex polygon (perimeter order) as one R_DrawPvrStrip
 * call via zig-zag vertex ordering: 0,1,N-1,2,N-2,3,N-3,...
 * Builds pvr_vertex_t directly into s_ptScratch in strip slot order. */
static void strip_emit_pvr_polygon(const NearClipVert *poly, int n)
{
    for (int k = 0; k < n; k++) {
        int srcIdx;
        if (k < 2) {
            srcIdx = k;
        }
        else if (k & 1) {
            srcIdx = (k + 1) >> 1;     /* odd: from start */
        }
        else {
            srcIdx = n - (k >> 1);     /* even: from end */
        }
        uint32_t flags = (k == n - 1) ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX;
        BuildPvrNearClipVertex(&s_ptScratch[k], &poly[srcIdx], flags);
    }
    R_DrawPvrStrip(s_ptScratch, n);
}

#endif /* SONICR_DC */

/* Clip triangle cv[0..2] against near plane (depth < 1) and emit via immediate mode.
 * Returns number of triangles emitted (0, 1, or 2). */
static int NearClipAndEmitTri(NearClipVert cv[3], int tpage, float depthScale) {
#ifdef SONICR_DC
    (void)depthScale;  /* depthScale unused on DC — strip path uses reciprocal(depth) */

    /* The PVR user tile clip owns the viewport edges, so there is no CPU
     * scissor pass — only the near-plane clip inside the strip variant.
     * Submitting a triangle that lands wholly in another viewport still costs
     * list bandwidth though, so reject it. Only valid once every vert is in
     * front of the near plane. */
    if (cv[0].depth >= 1 && cv[1].depth >= 1 && cv[2].depth >= 1 &&
        ScissorAllOutside(cv, 3))
    {
        return 0;
    }
    return NearClipAndEmitTriStripDC(cv, tpage);
#else
    int b0 = (cv[0].depth < 1);
    int b1 = (cv[1].depth < 1);
    int b2 = (cv[2].depth < 1);
    int behindCount = b0 + b1 + b2;

    if (behindCount == 3) {
        return 0;
    }

    R_SetTexture(tpage);
    R_SetTexEnv(R_TEXENV_ADD_SIGNED);

    if (behindCount == 0) {
        /* No clipping — emit triangle as-is */
        RenderVertex tri[3] = {
            BuildNearClipVertex(&cv[0], depthScale),
            BuildNearClipVertex(&cv[1], depthScale),
            BuildNearClipVertex(&cv[2], depthScale),
        };
        R_DrawTri(tri);
        return 1;
    }

    if (behindCount == 1) {
        /* One vertex behind — rotate so cv[0] is behind, clip to quad */
        NearClipVert tmp;
        if (b1) {
            tmp = cv[0];
            cv[0] = cv[1];
            cv[1] = cv[2];
            cv[2] = tmp;
        }
        else if (b2) {
            tmp = cv[2];
            cv[2] = cv[1];
            cv[1] = cv[0];
            cv[0] = tmp;
        }

        NearClipVert c01;
        NearClipVert c02;
        NearClipLerp(&cv[0], &cv[1], &c01);
        NearClipLerp(&cv[0], &cv[2], &c02);

        /* Emit quad TL,TR,BR,BL = (c01, cv1, cv2, c02). */
        RenderVertex quad[4] = {
            BuildNearClipVertex(&c01,   depthScale),
            BuildNearClipVertex(&cv[1], depthScale),
            BuildNearClipVertex(&cv[2], depthScale),
            BuildNearClipVertex(&c02,   depthScale),
        };
        R_DrawQuad(quad);
        return 2;
    }

    /* Two vertices behind — rotate so cv[0] is in front, clip to smaller triangle */
    NearClipVert tmp;
    if (!b0) {
        /* cv[0] is already in front */
    }
    else if (!b1) {
        tmp = cv[0];
        cv[0] = cv[1];
        cv[1] = cv[2];
        cv[2] = tmp;
    }
    else {
        tmp = cv[2];
        cv[2] = cv[1];
        cv[1] = cv[0];
        cv[0] = tmp;
    }

    NearClipVert c10;
    NearClipVert c20;
    NearClipLerp(&cv[1], &cv[0], &c10);
    NearClipLerp(&cv[2], &cv[0], &c20);

    RenderVertex tri[3] = {
        BuildNearClipVertex(&cv[0], depthScale),
        BuildNearClipVertex(&c10,   depthScale),
        BuildNearClipVertex(&c20,   depthScale),
    };
    R_DrawTri(tri);
    return 1;
#endif /* SONICR_DC */
}

/* Clip a 4-vertex convex quad against the near plane (depth=1) as a single
 * polygon and emit. Non-DC: Sutherland-Hodgman → triangle fan. DC: in-place
 * vismask switch → 3/4/5-vert triangle strip via one R_DrawPvrStrip call. */
static int NearClipAndEmitQuad(NearClipVert v[4], int tpage, float depthScale) {
#ifdef SONICR_DC
    (void)depthScale;

    /* Viewport reject — see NearClipAndEmitTri above. */
    if (v[0].depth >= 1 && v[1].depth >= 1 &&
        v[2].depth >= 1 && v[3].depth >= 1 &&
        ScissorAllOutside(v, 4))
    {
        return 0;
    }
    return NearClipAndEmitQuadStripDC(v, tpage);
#else
    /* Up to 6 outputs: an in-out-in-out pattern keeps two source verts and
     * adds one lerp per crossed edge. */
    NearClipVert out[6];
    int outCount = 0;

    for (int i = 0; i < 4; i++) {
        const NearClipVert *cur = &v[i];
        const NearClipVert *nxt = &v[(i + 1) & 3];
        int curIn = (cur->depth >= 1);
        int nxtIn = (nxt->depth >= 1);

        if (curIn) {
            out[outCount++] = *cur;
            if (!nxtIn) {
                NearClipLerp(nxt, cur, &out[outCount++]);
            }
        }
        else if (nxtIn) {
            NearClipLerp(cur, nxt, &out[outCount++]);
        }
    }

    if (outCount < 3) {
        return 0;
    }

    R_SetTexture(tpage);
    R_SetTexEnv(R_TEXENV_ADD_SIGNED);

    RenderVertex rv[6];
    for (int i = 0; i < outCount; i++) {
        rv[i] = BuildNearClipVertex(&out[i], depthScale);
    }
    R_DrawTriFan(rv, outCount);
    return outCount - 2;
#endif
}

/* ROM double constants for depth-based fog (from PE .rdata) */
#define ROM_DEPTH_THRESH_HI  0.9             /* 0x52c380 / 0x52c5ac */
#define ROM_DEPTH_THRESH_LO  0.7             /* 0x52c388 / 0x52c5b4 */
#define ROM_DEPTH_OFFSET     (-0.9)           /* 0x52c390 / 0x52c5bc */
#define ROM_DEPTH_SCALE      1275.0           /* 0x52c398 / 0x52c5a4 */

#define LIMB(base, off) (*(int *)((char *)g_limbMetaTable + (base) + (off)))

/* Set to 0 to draw every character face regardless of winding — a diagnostic
 * for telling backface culling apart from clipping or transform faults. */
#define CHAR_BACKFACE_CULL 1

/* Signed divide-by-4096 matching binary's sar/sbb pattern */
static inline int SDIV4096(int val) {
    int sign = val >> 31;
    return (val + (sign & 0xFFF)) >> 12;
}

/* =====================================================================
 * BuildBodyRotationSteps12 — Steps 1-2 for BODY rotation (charRot)
 *
 * Step 1: Y-axis rotation from (pitchAngle + 0x400)
 * Step 2: Z-axis rotation from rollAngle — mixes columns 0 and 1
 *
 * Binary 0x4631EF-0x463365: charRot Step 2 mixes rows 0,1 within each
 * column (column-major). Row-major equivalent: mix cols 0,1 per row.
 * Column 2 is unchanged.
 *
 * Step 3 (X-axis, anglePitch) applied by ApplyBodyStep3.
 * Output: 9-element matrix in row-major order (mat[row*3+col]).
 * ===================================================================== */
static void BuildBodyRotationSteps12(int pitchAngle, int rollAngle, int mat[9])
{
    int pidx = (pitchAngle + 0x400) & 0xFFF;
    int sp = g_sinTable[pidx] >> 2;
    int cp = g_cosTable[pidx] >> 2;

    mat[0] = cp;
    mat[1] = 0;
    mat[2] = -sp;
    mat[3] = 0;
    mat[4] = 0x1000;
    mat[5] = 0;
    mat[6] = sp;
    mat[7] = 0;
    mat[8] = cp;

    /* Step 2: Z-axis rotation (roll) — mix columns 0 and 1.
     * Binary 0x463254-0x463365: charRot mixes rows 0,1 per column.
     * Row-major equivalent: mix cols 0,1 per row. Col 2 unchanged. */
    int cr = g_cosTable[rollAngle & 0xFFF] >> 2;
    int sr = g_sinTable[rollAngle & 0xFFF] >> 2;
    for (int i = 0; i < 3; i++) {
        int c0 = mat[i * 3 + 0];
        int c1 = mat[i * 3 + 1];
        mat[i * 3 + 0] = (c0 * cr - c1 * sr) >> 12;
        mat[i * 3 + 1] = (c0 * sr + c1 * cr) >> 12;
    }
}

/* =====================================================================
 * BuildBoneRotationSteps12 — Steps 1-2 for BONE rotation (boneRot)
 *
 * Step 1: Y-axis rotation (same as body)
 * Step 2: X-axis rotation from rollAngle — mixes columns 1 and 2
 *
 * Binary 0x4638E0-0x463988: boneRot Step 2 mixes rows 1,2 within each
 * column (column-major). Row-major equivalent: mix cols 1,2 per row.
 * Column 0 is unchanged.
 *
 * Step 3 (Z-axis + negate col 2) applied by ApplyBoneStep3.
 * ===================================================================== */
static void BuildBoneRotationSteps12(int pitchAngle, int rollAngle, int mat[9])
{
    int pidx = (pitchAngle + 0x400) & 0xFFF;
    int sp = g_sinTable[pidx] >> 2;
    int cp = g_cosTable[pidx] >> 2;

    mat[0] = cp;
    mat[1] = 0;
    mat[2] = -sp;
    mat[3] = 0;
    mat[4] = 0x1000;
    mat[5] = 0;
    mat[6] = sp;
    mat[7] = 0;
    mat[8] = cp;

    /* Step 2: X-axis rotation (roll) — mix columns 1 and 2.
     * Binary 0x4638E0-0x463988: boneRot mixes rows 1,2 per column.
     * Row-major equivalent: mix cols 1,2 per row. Col 0 unchanged. */
    int cr = g_cosTable[rollAngle & 0xFFF] >> 2;
    int sr = g_sinTable[rollAngle & 0xFFF] >> 2;
    for (int i = 0; i < 3; i++) {
        int c1 = mat[i * 3 + 1];
        int c2 = mat[i * 3 + 2];
        mat[i * 3 + 1] = (c1 * cr - c2 * sr) >> 12;
        mat[i * 3 + 2] = (c1 * sr + c2 * cr) >> 12;
    }
}

/* Body rotation step 3: X-axis rotation (cols 1,2).
 * Binary verified: body yaw at 0x46338c rotates cols 1 and 2. */
static void ApplyBodyStep3(int yawAngle, int mat[9])
{
    int cy = g_cosTable[yawAngle & 0xFFF] >> 2;
    int sy = g_sinTable[yawAngle & 0xFFF] >> 2;
    for (int i = 0; i < 3; i++) {
        int c1 = mat[i * 3 + 1];
        int c2 = mat[i * 3 + 2];
        mat[i * 3 + 1] = (c1 * cy - c2 * sy) >> 12;
        mat[i * 3 + 2] = (c2 * cy + c1 * sy) >> 12;
    }
}

/* Bone rotation step 3: Z-axis rotation (cols 0,1) + negate col 2.
 * Binary at 0x463a15-0x463b1e operates on column-major boneRot in the player
 * struct, rotating rows 0,1 per column. For our row-major boneMat[9], this
 * maps to rotating cols 0,1 per row (equivalent transposed operation).
 * Col 2 negation matches binary's row 2 negation under the transpose. */
static void ApplyBoneStep3(int yawAngle, int mat[9])
{
    int cy = g_cosTable[yawAngle & 0xFFF] >> 2;
    int sy = g_sinTable[yawAngle & 0xFFF] >> 2;
    for (int i = 0; i < 3; i++) {
        int c0 = mat[i * 3 + 0];
        int c1 = mat[i * 3 + 1];
        mat[i * 3 + 0] = (c0 * cy - c1 * sy) >> 12;
        mat[i * 3 + 1] = (c0 * sy + c1 * cy) >> 12;
        mat[i * 3 + 2] = -mat[i * 3 + 2];
    }
}

/* =====================================================================
 * Character-specific UV animation functions — VALIDATED
 *
 * All three share identical logic: compute brightness from player
 * velocity (player[0x44] >> 11, clamped 0-32), halved if [0x90207C]==0,
 * then call RelocateModelVertices to shift face UVs for glow effects.
 *
 * Differences: ROM state pointer, model meta offset, face add, vertex count.
 * ===================================================================== */

/* Helper — shared logic for AnimateMetal, AnimateEggRobo (velocity-based glow) */
static void AnimateCharacterGlow(Player *player, int *glowState, int modelIdx,
                                 int faceAdd, int relocCount)
{
    int vel = player->forwardSpeed;
    int brightness = vel >> 11;
    if (brightness > 0x20) {
        brightness = 0x20;
    }
    if (brightness < 0) {
        brightness = 0;
    }

    /* Binary 0x421984: cmp [0x90207C],0 — that is g_raceOrder[3], the
     * per-frame 0..2 cycle counter (race_timing.c g_sfxCycleCounter), not
     * g_ghostDataExists (0x8FB960). Halving on one frame in three is what
     * makes the jet/glow flicker; reading the wrong flag halved it on every
     * frame in normal races, leaving a dim, static flame. */
    extern int g_raceOrder[];
    if (g_raceOrder[3] == 0) {
        /* abs(brightness) / 2 */
        int sign = brightness >> 31;
        brightness = ((brightness ^ sign) - sign) >> 1;
    }

    int uvShift;
    if (vel > 0) {
        uvShift = (0x60 - brightness) << 16;
    }
    else {
        uvShift = 0x600000;
    }

    /* Face base: this model's polyStart + faceAdd → index into g_charFaceBase */
    int faceStartIdx = g_modelMeta[modelIdx].polyStart + faceAdd;
    int framePhase = g_totalFrames & 3;
    int prevState = *glowState;

    /* RelocateModelVertices(arr, count, newBase, oldBase, fileBase)
     * arr = g_charFaceBase + faceStartIdx * 0x30
     * count = relocCount
     * newBase = (framePhase * 8 + 0x80) << 16
     * oldBase = prevState
     * fileBase = uvShift */
    int *facePtr = (int *)((char *)g_charFaceBase + faceStartIdx * 0x30);
    int newBase = ((framePhase * 8) + 0x80) << 16;

    RelocateModelVertices(facePtr, relocCount, newBase, prevState, uvShift);

    *glowState = uvShift;
}

/* FUN_00421968 — 147 bytes — Metal Sonic jet glow */
static void AnimateMetalSonic(Player *player)
{
    static int s_metalSonicState = 0x600000;  /* ROM at 0x4FBE48: init 0x600000 */
    AnimateCharacterGlow(player, &s_metalSonicState,
                         5,                 /* model 5 (Metal Sonic) */
                         0x30, 4);
}

/* FUN_004219fc — 147 bytes — Metal Knuckles glow */
static void AnimateMetalKnuckles(Player *player)
{
    static int s_metalKnuxState = 0x600000;   /* ROM at 0x4FBE4C: init 0x600000 */
    AnimateCharacterGlow(player, &s_metalKnuxState,
                         7,                 /* model 7 (Metal Knuckles) */
                         0x1C, 4);
}

/* FUN_00421a90 — 147 bytes — Egg Robo glow */
static void AnimateEggRobo(Player *player)
{
    static int s_eggRoboState = 0x600000;     /* ROM at 0x4FBE50: init 0x600000 */
    AnimateCharacterGlow(player, &s_eggRoboState,
                         8,                 /* model 8 (Egg Robo) */
                         0x38, 8);
}

/* =====================================================================
 * RenderCharacterD3D — FUN_00462EC0 — 9789 bytes
 * Per-player D3D character model renderer.
 * EAX = player struct pointer (Watcom fastcall).
 * ===================================================================== */
void RenderCharacterD3D(Player *player)
{
    if (player == NULL) {
        return;
    }

    /* Model lookup and world-to-camera transform */

    /* Binary uses player+0x1DE >> 16 for model index, NOT player->charId.
     * 0xF2 gets zeroed by InitRaceStart, but 0x1DE is preserved. */
    int charId = (int)player->_unk_0x1E0;
    if (charId < 0 || charId >= CHAR_COUNT) {
        return;
    }
    const ModelMeta *charMeta = &g_modelMeta[charId];

    /* Binary 0x462EF6-0x462F47: world-to-camera delta using g_camOrientX/Y/Z
     * (0x6E9C84-8C = camBlock[0]-[2], full-precision position << 4).
     * Subtract BEFORE shifting to preserve sub-unit precision. */
    int dx = (player->posX - g_camOrientX) >> 12;
    int dy = (-player->posY + 0x23000 - g_camOrientY) >> 12;
    int dz = (player->posZ - g_camOrientZ) >> 12;

    /* Camera-space depth (Z) via view matrix row 2 (forward) */
    long long czRaw = (long long)g_viewMtx02 * dx +
                      (long long)g_viewMtx12 * dy +
                      (long long)g_viewMtx22 * dz;
    int cz = SDIV4096((int)czRaw);

    int boundRadius = charMeta->boundRadius;

    /* Far clip — binary 0x462F78: cull if the sphere's near edge exceeds far depth.
     * Uses raw cz (before the >=1 clamp below). */
    if (((cz - boundRadius) >> 3) > g_farClipDepth) {
        return;
    }
    /* Near / behind-camera clip — binary 0x462F92: cull if fully behind the camera. */
    if (cz < -boundRadius) {
        return;
    }

    /* Camera-space X (right) — binary lines 66-80 */
    long long cxRaw = (long long)g_viewMtx00 * dx +
                      (long long)g_viewMtx10 * dy +
                      (long long)g_viewMtx20 * dz;
    int cx = SDIV4096((int)cxRaw);

    /* Camera-space Y (up) — binary lines 81-93 */
    long long cyRaw = (long long)g_viewMtx01 * dx +
                      (long long)g_viewMtx11 * dy +
                      (long long)g_viewMtx21 * dz;
    int cy = SDIV4096((int)cyRaw);

    if (cz < 1) {
        cz = 1;
    }

    /* Screen projection and clip test */
    int screenX = g_screenCenterX + (cx * g_projScaleXCurrent) / cz;
    int screenY = g_screenCenterY - (cy * g_projScaleY) / cz;
    int projRadius = (boundRadius * g_projScaleXCurrent) / cz;

    /* Bounding-circle screen cull — binary 0x463061-0x46309D */
    if (screenX - projRadius > g_clipRight) {
        return;
    }
    if (screenX + projRadius < g_clipLeft) {
        return;
    }
    if (screenY - projRadius > g_clipBottom) {
        return;
    }
    if (screenY + projRadius < g_clipTop) {
        return;
    }

    /* LOD distance — binary 0x4630A3: g_screenScale (0x6E98B8, = screenW/320) * (cz>>3).
     * Grows with camera depth; drives the per-limb detail cull in the limb loop below. */
    int lodDist = g_screenScale * (cz >> 3);

    /* Setup calls */

    /* animation/lighting setup + character-specific hooks */
    DispatchFaceUVAnimation(player);
    UpdateVertexLightingPhase(player);

    if (charId == CHAR_METAL_SONIC) {
        AnimateMetalSonic(player);
    }
    else if (charId == CHAR_METAL_KNUCKLES) {
        AnimateMetalKnuckles(player);
    }
    else if (charId == CHAR_EGG_ROBO) {
        AnimateEggRobo(player);
    }

    /* Re-read position deltas at higher precision (>>8) */

    /* Binary 0x463125-0x46316D: subtract g_camOrientX/Y/Z before shifting */
    int pDx = (player->posX - g_camOrientX) >> 8;
    int pDy = (-(player->posY + player->yOffset) + 0x23000 - g_camOrientY) >> 8;
    int pDz = (player->posZ - g_camOrientZ) >> 8;

    /* Build rotation matrix */

    /* Binary 0x463187+ builds a 3-step Euler (Ry×Rz×Rx) via BuildBodyRotationSteps12
     * + ApplyBodyStep3:
     *   step 1 pitch = pitchCombo + angleYaw (heading; the +0x400 is applied inside the builder),
     *   step 2 roll  = angleRoll,
     *   step 3 yaw   = anglePitch (surface tilt). */
    int bodyMat[9];
    int pitchAngle = player->pitchCombo + player->angleYaw;
    int rollAngle = player->angleRoll;   /* surface roll */
    int yawAngle = player->anglePitch;   /* surface tilt */
    BuildBodyRotationSteps12(pitchAngle, rollAngle, bodyMat);
    ApplyBodyStep3(yawAngle, bodyMat);

    int cm00 = bodyMat[0];
    int cm01 = bodyMat[1];
    int cm02 = bodyMat[2];
    int cm10 = bodyMat[3];
    int cm11 = bodyMat[4];
    int cm12 = bodyMat[5];
    int cm20 = bodyMat[6];
    int cm21 = bodyMat[7];
    int cm22 = bodyMat[8];

    /* Final transform: charRot × viewMatrix.
     * Binary references 0x6e9c44 (viewMtx row 0 = right),
     * 0x6e9c48 (row 1 = up), 0x6e9c4c (row 2 = forward). */
    int fmR0 = (int)(((long long)cm00 * g_viewMtx00 + (long long)cm01 * g_viewMtx10 + (long long)cm02 * g_viewMtx20) >> 12);
    int fmR1 = (int)(((long long)cm00 * g_viewMtx01 + (long long)cm01 * g_viewMtx11 + (long long)cm02 * g_viewMtx21) >> 12);
    int fmR2 = (int)(((long long)cm00 * g_viewMtx02 + (long long)cm01 * g_viewMtx12 + (long long)cm02 * g_viewMtx22) >> 12);

    int fmU0 = (int)(((long long)cm10 * g_viewMtx00 + (long long)cm11 * g_viewMtx10 + (long long)cm12 * g_viewMtx20) >> 12);
    int fmU1 = (int)(((long long)cm10 * g_viewMtx01 + (long long)cm11 * g_viewMtx11 + (long long)cm12 * g_viewMtx21) >> 12);
    int fmU2 = (int)(((long long)cm10 * g_viewMtx02 + (long long)cm11 * g_viewMtx12 + (long long)cm12 * g_viewMtx22) >> 12);

    int fmF0 = (int)(((long long)cm20 * g_viewMtx00 + (long long)cm21 * g_viewMtx10 + (long long)cm22 * g_viewMtx20) >> 12);
    int fmF1 = (int)(((long long)cm20 * g_viewMtx01 + (long long)cm21 * g_viewMtx11 + (long long)cm22 * g_viewMtx21) >> 12);
    int fmF2 = (int)(((long long)cm20 * g_viewMtx02 + (long long)cm21 * g_viewMtx12 + (long long)cm22 * g_viewMtx22) >> 12);

    /* Per-limb vertex transform and face output */

    /* The main rendering loop.
     *
     * Structure from binary:
     *   - Transformed vertices stored at stride 0x40 (16 ints per vertex)
     *     in a temp buffer. Layout: [0]=screenX, [1]=screenY, ..., [12]=depth
     *   - Face data from g_charFaceBase at stride 0x30 (48 bytes per face)
     *   - Tpage output matches render_character_credits.c format exactly (same D3D buffers)
     *
     * This section translated with reference to render_character_credits.c
     * (the credits version of the same pipeline). */

    float depthScale = g_farClipFloat;
    if (depthScale <= 0.0f) {
        depthScale = 88064.0f;
    }

    /* Limb table: same layout as render_character_credits.c.
     * META offset 0x10 = first limb index into g_limbMetaTable
     * META offset 0x14 = limb count
     * META offset 0x18 = animation base index
     * g_limbMetaTable stride 0x18 per limb:
     *   [0] = vertex start index (into g_vertexArrayBase, stride 0x40)
     *   [1] = vertex count
     *   [2] = face start index (into g_charFaceBase, stride 0x30)
     *   [3] = face count
     *   [4],[5] = unused/padding */
    int *limbMeta = (int *)((char *)g_limbMetaTable +
                    charMeta->limbStart * 0x18);
    int limbCount = charMeta->limbCount;

    /* Animation frame data — stride 0x18 per limb per frame.
     * Provides per-limb rotation angles and position offsets. */
    int animLimbBase = charMeta->animFrameBase;
    int animFrameIdx = player->animFrameIdx;
    int *animFrame = (int *)((char *)g_animFrameData +
                    (animLimbBase + animFrameIdx * limbCount) * 0x18);
#ifdef SONICR_XBOX
    /* 60 fps (interp.c, after the PSP port): the halfway frame's pose, each limb
     * halfway from the last tick's animation frame to this one's: offsets
     * halved, angles the short way round (12 bits) */
    {
        extern int Interp_PrevAnimFrame(const Player *p);
        static int s_halfPose[48 * 6];
        int prevIdx = Interp_PrevAnimFrame(player);
        if (prevIdx >= 0 && limbCount > 0 && limbCount <= 48) {
            const int *a = (const int *)((const char *)g_animFrameData + (animLimbBase + prevIdx * limbCount) * 0x18);
            for (int i = 0; i < limbCount * 6; i++) {
                int k = i % 6;
                if (k < 3) {
                    s_halfPose[i] = a[i] + (animFrame[i] - a[i]) / 2;
                } else {
                    int d = ((animFrame[i] - a[i] + 0x800) & 0xFFF) - 0x800;
                    s_halfPose[i] = (a[i] + d / 2) & 0xFFF;
                }
            }
            animFrame = s_halfPose;
        }
    }
#endif

    #define MAX_LIMB_VERTS 512  /* raised from 128 — Eggman's limbs exceed 128 */

    int farClip = g_farClipDepth << 3;
    if (farClip <= 0) farClip = 0x15800;

    int totalFacesSubmitted = 0;

    for (int limbIdx = 0; limbIdx < limbCount; limbIdx++) {
        int limbVertStart = limbMeta[0];
        int limbVertCount = limbMeta[1];
        int limbFaceStart = limbMeta[2];
        int limbFaceCount = limbMeta[3];

        /* LOD cull — binary 0x463807: when far (lodDist > 0x200), skip small
         * detail limbs (fewer than 4 faces). */
        if (lodDist > 0x200 && limbFaceCount < 4) {
            goto next_limb;
        }
        if (limbVertCount <= 0 || limbVertCount > MAX_LIMB_VERTS) {
            goto next_limb;
        }

        /* Per-limb bone rotation from animation frame */
        /* Build bone rotation matrix. Steps 1-2 same as body (Y-axis + Z-roll).
         * Step 3 DIFFERS: bone uses Z-axis rotation (cols 0,1) + negate col2
         * (binary: 0x463a15-0x463b27), vs body's X-axis rotation (cols 1,2). */
        int boneMat[9];
        BuildBoneRotationSteps12(animFrame[4], animFrame[5], boneMat);
        ApplyBoneStep3(animFrame[3], boneMat);

        /* Compose bone × bodyView matrix */
        /* bodyView = fmR/fmU/fmF (body × view, computed in Section 6).
         * composed = bone × bodyView: transforms model-space → view-space.
         * Binary verified: uses C^T × v convention for vertex transform. */
        int c00 = (boneMat[0] * fmR0 + boneMat[1] * fmU0 + boneMat[2] * fmF0) >> 12;
        int c01 = (boneMat[0] * fmR1 + boneMat[1] * fmU1 + boneMat[2] * fmF1) >> 12;
        int c02 = (boneMat[0] * fmR2 + boneMat[1] * fmU2 + boneMat[2] * fmF2) >> 12;
        int c10 = (boneMat[3] * fmR0 + boneMat[4] * fmU0 + boneMat[5] * fmF0) >> 12;
        int c11 = (boneMat[3] * fmR1 + boneMat[4] * fmU1 + boneMat[5] * fmF1) >> 12;
        int c12 = (boneMat[3] * fmR2 + boneMat[4] * fmU2 + boneMat[5] * fmF2) >> 12;
        int c20 = (boneMat[6] * fmR0 + boneMat[7] * fmU0 + boneMat[8] * fmF0) >> 12;
        int c21 = (boneMat[6] * fmR1 + boneMat[7] * fmU1 + boneMat[8] * fmF1) >> 12;
        int c22 = (boneMat[6] * fmR2 + boneMat[7] * fmU2 + boneMat[8] * fmF2) >> 12;

        /* Limb translation: body-rotate offset, add camera delta, view transform */
        int limbX = animFrame[0];
        int limbY = animFrame[1] + charMeta->modelYOffset * 16;  /* binary: charMeta[0x4C] << 4 */
        int limbZ = -animFrame[2];  /* binary negates Z: 0x463d6d */

        /* Body-rotate the limb offset (binary: 0x463d84-0x463eda) */
        int rotLX = (limbX * cm00 + limbY * cm10 + limbZ * cm20) >> 12;
        int rotLY = (limbX * cm01 + limbY * cm11 + limbZ * cm21) >> 12;
        int rotLZ = (limbX * cm02 + limbY * cm12 + limbZ * cm22) >> 12;

#if 0
        /* Limb visibility cull — binary 0x463DE4: hide the limb when its rotated Y
         * offset falls below the character's world Y. limbMeta[5] is the per-limb
         * visibility flag (0=hidden, 1=visible), rewritten every frame.
         * Binary-faithful (relative rotLY vs absolute (posY+yOffset)>>8); revert if
         * it culls valid limbs. */
        if (((rotLY - ((player->posY + player->yOffset) >> 8)) >> 4) < 0) {
            limbMeta[5] = 0;   /* binary 0x463E04: mark hidden */
            goto next_limb;
        }
#endif
        limbMeta[5] = 1;       /* binary 0x463E64: mark visible */

        /* Total camera-space delta = player-camera offset + body-rotated limb */
        int totalX = pDx + rotLX;
        int totalY = pDy + rotLY;
        int totalZ = pDz + rotLZ;

        /* View-transform the translation (NOT >>12 — raw products, >>16 in vertex loop) */
        int transViewX = totalX * g_viewMtx00 + totalY * g_viewMtx10 + totalZ * g_viewMtx20;
        int transViewY = totalX * g_viewMtx01 + totalY * g_viewMtx11 + totalZ * g_viewMtx21;
        int transViewZ = totalX * g_viewMtx02 + totalY * g_viewMtx12 + totalZ * g_viewMtx22;

        /* Per-vertex transform using composed matrix */
        SrcVertex *vtxPtr = &g_vertexArrayBase[limbVertStart];

        for (int vi = 0; vi < limbVertCount; vi++) {
            int wx = vtxPtr->posX;
            int wy = vtxPtr->posY;
            int wz = vtxPtr->posZ;

            /* Depth (view Z) = C column 2 · vertex + transViewZ, then >>16 */
            int depth = (c02 * wx + c12 * wy + c22 * wz + transViewZ) >> 16;

            /* Screen X (view X) = C column 0 · vertex + transViewX, then >>16 */
            int cx_v = (c00 * wx + c10 * wy + c20 * wz + transViewX) >> 16;

            /* Screen Y (view Y) = C column 1 · vertex + transViewY, then >>16 */
            int cy_v = (c01 * wx + c11 * wy + c21 * wz + transViewY) >> 16;

            /* Always store camera-space coords for near-plane clipping */
            vtxPtr->camX = cx_v;
            vtxPtr->camY = cy_v;
            vtxPtr->depth = depth;  /* raw depth, no bias — bias applied at GL submission */

            /* Project to screen (only valid when in front of camera) */
            if (depth > 0) {
#ifdef SONICR_DC
                /* SH4: replace ___sdivsi3_i4i softdiv with fsrra-based reciprocal.
                 * depth>0 guaranteed by the if. Int multiplies kept identical to
                 * the SDL path so any silent-overflow behavior matches. */
                float invDepth = reciprocal((float)depth);
                int sxNum = g_projScaleXCurrent * cx_v;
                int syNum = g_projScaleY        * cy_v;
                vtxPtr->screenX = g_screenCenterX + (int)((float)sxNum * invDepth);
                vtxPtr->screenY = g_screenCenterY - (int)((float)syNum * invDepth);
#else
                vtxPtr->screenX = g_screenCenterX + (g_projScaleXCurrent * cx_v) / depth;
                vtxPtr->screenY = g_screenCenterY - (g_projScaleY * cy_v) / depth;
#endif
            }

            vtxPtr++;
        }

        /* Per-face polygon output to tpage buffers */
        #define VTX(idx) (&g_vertexArrayBase[(unsigned int)(idx)])

        for (int fi = 0; fi < limbFaceCount; fi++) {
            int *face = (int *)((char *)g_charFaceBase + (limbFaceStart + fi) * 0x30);

            unsigned short viA = ((unsigned short *)face)[0x20 / 2];
            unsigned short viB = ((unsigned short *)face)[0x22 / 2];
            unsigned short viC = ((unsigned short *)face)[0x24 / 2];

            SrcVertex *vA = VTX(viA);
            SrcVertex *vB = VTX(viB);
            SrcVertex *vC = VTX(viC);

            /* Far clip — skip if ALL vertices are beyond far plane */
            if (vA->depth > farClip && vB->depth > farClip && vC->depth > farClip) {
                continue;
            }

            unsigned char flags = *(unsigned char *)((char *)face + 0x2E);
            int tpage = *(unsigned char *)((char *)face + 0x28);
            if (tpage >= 52) {
                continue;
            }

            /* Backface cull — 0x4553A9 (triangles) and 0x455502 (quads).
             * Flag bit 0x04 marks the face double-sided → never culled.
             * Flag bit 0x02 on a quad means the two halves may face different
             * ways: ABC failing does not condemn the face, so ACD is tested as
             * well and the face is culled only when both are turned away.
             * RenderCharacterOnPodium runs the same test pivoted on B / C. */
#if CHAR_BACKFACE_CULL
            if ((flags & 4) == 0) {
                unsigned int cross =
                    (unsigned int)(vB->screenY - vA->screenY) * (unsigned int)(vC->screenX - vA->screenX)
                  - (unsigned int)(vC->screenY - vA->screenY) * (unsigned int)(vB->screenX - vA->screenX);

                /* Two variants, selected at 0x4641b1 by `cmp [0x6e9920],0`.
                 * Mirror mode negates view-matrix X (view_matrix.c:80), which
                 * reverses winding, so the comparison follows it:
                 *   normal (0x464244): cull when cross > 0
                 *   mirror (0x4641be): cull when cross < 0 */
                int cullBackface = (g_mirrorMode != 0) ? ((int)cross < 0)
                                                       : ((int)cross > 0);
                if (cullBackface) {
                    if ((flags & 3) != 3) {
                        continue;
                    }
                    SrcVertex *vDcull = VTX(((unsigned short *)face)[0x26 / 2]);
                    unsigned int cross2 =
                        (unsigned int)(vC->screenY - vA->screenY) * (unsigned int)(vDcull->screenX - vA->screenX)
                      - (unsigned int)(vDcull->screenY - vA->screenY) * (unsigned int)(vC->screenX - vA->screenX);

                    int cullSecond = (g_mirrorMode != 0) ? ((int)cross2 < 0)
                                                         : ((int)cross2 > 0);
                    if (cullSecond) {
                        continue;
                    }
                }
            }
#endif

            if ((flags & 1) == 0) {
                /* Triangle — near-plane clip and emit */
                NearClipVert cv[3];
                NearClipFillVert(&cv[0], vA, face, 0);
                NearClipFillVert(&cv[1], vB, face, 1);
                NearClipFillVert(&cv[2], vC, face, 2);

                /* Screen clip (only for vertices in front of camera) */
                int anyInFront = (cv[0].depth >= 1) || (cv[1].depth >= 1) || (cv[2].depth >= 1);
                if (!anyInFront) {
                    continue;
                }

                totalFacesSubmitted += NearClipAndEmitTri(cv, tpage, depthScale);
            }
            else {
                /* Quad — clip as one polygon, emit as triangle fan */
                unsigned short viD = ((unsigned short *)face)[0x26 / 2];
                SrcVertex *vD = VTX(viD);

                if (vA->depth > farClip && vD->depth > farClip) {
                    continue;
                }

                NearClipVert cv[4];
                NearClipFillVert(&cv[0], vA, face, 0);
                NearClipFillVert(&cv[1], vB, face, 1);
                NearClipFillVert(&cv[2], vC, face, 2);
                NearClipFillVert(&cv[3], vD, face, 3);

                int anyInFront = (cv[0].depth >= 1) || (cv[1].depth >= 1) ||
                                 (cv[2].depth >= 1) || (cv[3].depth >= 1);
                if (!anyInFront) {
                    continue;
                }

                totalFacesSubmitted += NearClipAndEmitQuad(cv, tpage, depthScale);
            }
        }

        #undef VTX

next_limb:
        limbMeta += 6;
        animFrame += 6;
    }

    /* Tails tail rendering — binary 0x464d8a-0x4654f5
     * 4 tail segments × 6 quads each, submitted as textured quads to
     * the character tpage batch. Only runs for charId == 1 (Tails).
     * Vertex indices from ROM tables at 0x4FC39C (segment) and
     * 0x4FC45C (face map). Uses projected vertex data from
     * g_vertexArrayBase with tailVertBase from g_modelMetaTable+0x50. */
    if (player->charId == CHAR_TAILS && totalFacesSubmitted > 0) {      /* 0x464d8a-0x464da1 */
        int tailVertBase = g_modelMeta[1].vertexStart;  /* 0x464db2: [0x7130f4] */
        unsigned char tailTpage = (unsigned char)g_tpageCharacters;    /* 0x464f8a: [0x8f6c30] */

        for (int seg = 0; seg < 4; seg++) {
            const int *segVtx = g_tailSegVtx[seg];
            for (int fq = 0; fq < 6; fq++) {
                int idxA = tailVertBase + segVtx[g_tailFaceMap[fq][0]];
                int idxB = tailVertBase + segVtx[g_tailFaceMap[fq][1]];
                int idxC = tailVertBase + segVtx[g_tailFaceMap[fq][2]];
                int idxD = tailVertBase + segVtx[g_tailFaceMap[fq][3]];

                SrcVertex *vA = &g_vertexArrayBase[idxA];
                SrcVertex *vB = &g_vertexArrayBase[idxB];
                SrcVertex *vC = &g_vertexArrayBase[idxC];
                SrcVertex *vD = &g_vertexArrayBase[idxD];

                /* Depth check — 0x464e26-0x464efc */
                if (vA->depth < 1 || (float)vA->depth > depthScale) {
                    continue;
                }
                if (vB->depth < 1 || (float)vB->depth > depthScale) {
                    continue;
                }
                if (vC->depth < 1 || (float)vC->depth > depthScale) {
                    continue;
                }
                if (vD->depth < 1 || (float)vD->depth > depthScale) {
                    continue;
                }

                /* Clip test — 0x464f02-0x464f84 */
                if (vA->screenX < g_clipLeft && vB->screenX < g_clipLeft &&
                    vC->screenX < g_clipLeft && vD->screenX < g_clipLeft)
                {
                    continue;
                }
                if (vA->screenY < g_clipTop && vB->screenY < g_clipTop &&
                    vC->screenY < g_clipTop && vD->screenY < g_clipTop)
                {
                    continue;
                }
                if (vA->screenX > g_clipRight && vB->screenX > g_clipRight &&
                    vC->screenX > g_clipRight && vD->screenX > g_clipRight)
                {
                    continue;
                }
                if (vA->screenY > g_clipBottom && vB->screenY > g_clipBottom &&
                    vC->screenY > g_clipBottom && vD->screenY > g_clipBottom)
                {
                    continue;
                }

                /* UV setup — 0x464f8a-0x465008 */
                int tailUV[8];
                if ((seg & 1) == 0) {
                    tailUV[0] = 0xC00000;
                    tailUV[1] = 0x800000;
                    tailUV[6] = 0xC00000;
                    tailUV[7] = 0x87FFFF;
                }
                else {
                    tailUV[0] = 0;
                    tailUV[1] = 0x800000;
                    tailUV[6] = 0;
                    tailUV[7] = 0x87FFFF;
                }
                tailUV[2] = tailUV[0] + 0x7FFFF;
                tailUV[3] = 0x87FFFF;
                tailUV[4] = tailUV[0] + 0x7FFFF;
                tailUV[5] = 0x800000;

                /* Submit tail quad via immediate mode.
                 * Source order is TL,TR,BR,BL = vA,vB,vC,vD; PVR strip
                 * order is 0,1,3,2 = vA,vB,vD,vC. The non-DC R_DrawQuad
                 * does that reorder for us; the DC direct-emit path
                 * writes verts straight into PVR strip order. */
                SrcVertex *tailVerts[4] = { vA, vB, vC, vD };
#ifdef SONICR_DC
                static const int s_pvrStripIdx[4] = { 0, 1, 3, 2 };
                static const uint32_t s_pvrFlags[4] = {
                    PVR_CMD_VERTEX, PVR_CMD_VERTEX, PVR_CMD_VERTEX, PVR_CMD_VERTEX_EOL
                };
#else
                RenderVertex tailQuad[4];
#endif
                for (int tv = 0; tv < 4; tv++) {
#ifdef SONICR_DC
                    int srcIdx = s_pvrStripIdx[tv];
#else
                    int srcIdx = tv;
#endif
                    SrcVertex *v = tailVerts[srcIdx];
                    int pz = v->depth - 0x20;                  /* apply depth bias at submission */
                    if (pz < 1) {
                        pz = 1;
                    }
                    float fpz = (float)pz;
                    float normZ = fpz / depthScale;

                    int uIdx = (tailUV[srcIdx * 2] >> 16) & 0xFF;
                    int vIdx = (tailUV[srcIdx * 2 + 1] >> 16) & 0xFF;

                    /* Fog alpha — 0x4651f5-0x465237 */
                    int alpha;
                    sr_double dv = (sr_double)normZ;
                    if (dv > ROM_DEPTH_THRESH_HI) {
                        alpha = 0;
                    }
                    else if (dv <= ROM_DEPTH_THRESH_LO) {
                        alpha = 0xFF;
                    }
                    else {
                        int raw = (int)sr_lrint((dv + ROM_DEPTH_OFFSET) * ROM_DEPTH_SCALE);
                        alpha = (raw < 0) ? -raw : raw;
                    }

                    uint32_t argb;
                    uint32_t r = (unsigned int)(v->colorR >> 13);
                    uint32_t g = (unsigned int)(v->colorG >> 13);
                    uint32_t b = (unsigned int)(v->colorB >> 13);
                    argb = ((unsigned int)alpha << 24) | (r << 16) | (g << 8) | b;
#ifdef SONICR_DC
                    pvr_vertex_t *pv = &s_ptScratch[tv];
                    pv->flags = s_pvrFlags[tv];
                    SrcVertex_ProjectFloat(v, &pv->x, &pv->y);
                    pv->z = reciprocal(fpz);
                    pv->u = g_uvLUT256[uIdx];
                    pv->v = g_uvLUT256[vIdx];
                    modify_game_character_color(argb, &pv->argb, &pv->oargb);
#else
                    SrcVertex_ProjectFloat(v, &tailQuad[tv].sx, &tailQuad[tv].sy);
                    tailQuad[tv].sz = normZ;
                    tailQuad[tv].rhw = 1.0f / fpz;
                    tailQuad[tv].color = argb;
                    tailQuad[tv].specular = 0;
                    tailQuad[tv].u = g_uvLUT256[uIdx];
                    tailQuad[tv].v = g_uvLUT256[vIdx];
#endif
                }

                R_SetTexture(tailTpage);
                R_SetTexEnv(R_TEXENV_ADD_SIGNED);
#ifdef SONICR_DC
                R_DrawPvrQuad(s_ptScratch);
#else
                R_DrawQuad(tailQuad);
#endif
            }
        }
    }

}

/* =====================================================================
 * SetupCharacterShadowQuad — FUN_0045A104 — 3829 bytes
 * Computes a character's ground shadow quad from surface normals,
 * builds orientation + view matrices, projects 4 corners to screen,
 * does frustum/backface culling, then submits 2 triangles to the
 * tpage vertex buffer with depth-based alpha.
 *
 * EAX = player pointer (Watcom fastcall).
 * Called when player+0x68 == 0 or == 0x60000.
 * ===================================================================== */
void SetupCharacterShadowQuad(Player *player)
{
    /* 0x0045a114: Check tpage state for the character tpage */
    int tpage = g_tpageCharBase;
    if (g_tpageStateArray[tpage] != 4) {
        return; /* 0x0045a119 */
    }

    /* Read surface normal vector (nX, nY, nZ) */
    int nX, nY, nZ;

    if (player->loopMode != 0) { /* 0x0045a126 */
        /* 0x0045a130: Compute surface index from player terrain ref */
        int terrIdx = player->_unk_0x94;               /* 0x0045a130: P_INT(p, 0x92)>>16 = short at 0x94 */
        /* 0x0045a147: loop surface entry → vtxBase (+0). Binary indexes
         * lookAt (short*) by terrIdx*11 = the first short of entry terrIdx = vtxBase. */
        int surfBC = player->_unk_0xBC;                      /* 0x0045a14d */
        int surfEntry = (int)((TerLoopEntry *)g_terLoopTable)[terrIdx].vtxBase + surfBC;

        /* 0x0045a159: Compute address into g_terUnknown74: entry * 34 (0x22) */
        /* (entry << 4) + entry = entry * 17; * 2 = entry * 34 */
        int byteOff = ((surfEntry << 4) + surfEntry) * 2;   /* 0x0045a15b-0x0045a166 */
        char *vtx = (char *)g_terUnknown74 + byteOff;       /* 0x0045a168 */

        /* 0x0045a16a-0x0045a18c: Read surface normal from vertex data */
        nX = *(short *)(vtx + 0x18);                             /* 0x0045a16a */
        int nY_raw = *(short *)(vtx + 0x1A);                   /* 0x0045a176 */
        nZ = *(short *)(vtx + 0x1C);                            /* 0x0045a184 */
        nY = -nY_raw;                                        /* 0x0045a187: neg esi */
    }
    else {
        /* 0x0045a194: Use player's stored surface normal */
        nX = (int)player->surfNormX;                          /* 0x0045a194: P_INT(p,0xB2)>>16 = short at 0xB4 → surfNormX */
        int nY_raw = (int)player->surfNormY;                  /* 0x0045a1a3: P_INT(p,0xB4)>>16 = short at 0xB6 → surfNormY */
        nZ = (int)player->surfNormZ;                          /* 0x0045a1b4: P_INT(p,0xB6)>>16 = short at 0xB8 → surfNormZ */
        nY = -nY_raw;                                        /* 0x0045a1ba: neg edx */
    }

    /* Compute yaw angle from surface normal via atan2 */
    /* 0x0045a1c5 */ /* nZ stored, nY stored */

    /* 0x0045a1cb: fild nY; fild nZ; call atan2 → atan2(nZ, nY) */
    sr_double yawRad = sr_atan2((sr_double)nZ, (sr_double)nY);          /* 0x0045a1d7 */

    /* 0x0045a1dc-0x0045a1f0: yawAngle = round(yawRad * 4096 * (1/(2*pi))) */
    int yawAngle = (int)sr_lrint(yawRad * ROM_RAD_SCALE_A * ROM_RAD_SCALE_B);  /* 0x0045a1f0 */

    /* 0x0045a1fb-0x0045a20c: Mask to 12-bit angle index */
    yawAngle = ((yawAngle << 4) >> 4) & 0xFFF;              /* 0x0045a201-0x0045a20c */
    int yawComp = 0xFFF - yawAngle;                          /* 0x0045a211 */

    /* 0x0045a213: Load cosTable pointer */
    int *cosTab = g_cosTable;                                /* 0x0045a213 */

    /* 0x0045a21b-0x0045a234: Compute cross product term for pitch */
    int sinYawComp = g_sinTable[yawComp] >> 2;               /* 0x0045a21b */
    int cosYawComp = cosTab[yawComp] >> 2;                   /* 0x0045a222 */
    int crossTerm = cosYawComp * nY - sinYawComp * nZ;       /* 0x0045a22e-0x0045a23b */

    /* 0x0045a23d: SDIV4096 */
    int hProj = (crossTerm + ((crossTerm >> 31) & 0xFFF)) >> 12;  /* 0x0045a23f-0x0045a247 */

    /* Compute pitch angle via atan2 */
    /* 0x0045a250: fild hProj; fild nX; call atan2 → atan2(nX, hProj) */
    sr_double pitchRad = sr_atan2((sr_double)nX, (sr_double)hProj);      /* 0x0045a25c */

    /* 0x0045a261-0x0045a265: Multiply by ROM constants (already on FPU st2 from before) */
    /* Binary: fmulp st(2), fmulp st(1) → result = pitchRad * ROM_A * ROM_B */
    int pitchAngle = (int)sr_lrint(pitchRad * ROM_RAD_SCALE_A * ROM_RAD_SCALE_B);

    /* 0x0045a276-0x0045a281: Mask to 12-bit */
    pitchAngle = ((pitchAngle << 4) >> 4) & 0xFFF;
    int pitchComp = 0xFFF - pitchAngle;                      /* 0x0045a286 */

    /* Build 3x3 rotation matrix from heading + pitch + yaw */

    /* 0x0045a288: Player heading angle */
    int headAngle = player->pitchCombo;                       /* 0x0045a288 */

    /* 0x0045a294-0x0045a29b: sin/cos of yaw angle */
    int sinYaw = g_sinTable[yawAngle] >> 2;                  /* 0x0045a294: eax */
    int cosYaw = cosTab[yawAngle] >> 2;                      /* 0x0045a29b: edx */

    /* 0x0045a29e: Combined heading = player[0x10] + player[0xC8] */
    int headIdx = (player->angleYaw + headAngle) & 0xFFF;    /* 0x0045a29e-0x0045a2a3 */

    /* 0x0045a2af-0x0045a2b6: sin/cos of heading */
    int sinHead = g_sinTable[headIdx] >> 2;                  /* 0x0045a2af: edi */
    int cosHead = cosTab[headIdx] >> 2;                      /* 0x0045a2b6: ecx */

    /* 0x0045a2bf-0x0045a2e5: sin/cos of pitch complement */
    int cosPitchComp = cosTab[pitchComp] >> 2;               /* 0x0045a2da-0x0045a2dc: esi → ebp-0x44 */
    int sinPitchComp = g_sinTable[pitchComp] >> 2;           /* 0x0045a2e5: ecx → stored via ecx+0x92568c */

    /* Note: the disasm loads g_cosTable[pitchComp] and g_sinTable[pitchComp] using
       shifted offsets. The variable naming is based on pitchComp = 0xFFF - pitchAngle. */

    /* Intermediate values for the negated heading sin */
    /* 0x0045a2eb: ebp-0x28 = sinHead, then neg → -sinHead */
    int negSinHead = -sinHead;                               /* 0x0045a2ee */

    /* Build the rotation matrix M = Rz(heading) * Rx(pitch) * Ry(yaw) */

    /* Row 0: ebp-0x64 (M00_pre), ebp-0x48 (M01_pre), ebp-0x84 / ebp-0x5c */
    /* 0x0045a2f6: imul esi, ecx, 0 → = 0 (sinPitchComp * 0) */
    int zero1 = 0;                                           /* 0x0045a2f6: imul esi, ecx, 0 */

    /* 0x0045a302: cosHead * cosPitchComp - 0 */
    int M00_pre = cosHead * cosPitchComp - zero1;            /* 0x0045a302-0x0045a305 → ebp-0x64 */

    /* 0x0045a30e: imul esi, cosPitchComp, 0 → = 0 */
    int zero2 = 0;                                           /* 0x0045a30e */

    /* 0x0045a314: cosHead * sinPitchComp + 0 */
    int M01_pre = cosHead * sinPitchComp + zero2;            /* 0x0045a314-0x0045a31a → ebp-0x48 */

    /* 0x0045a317-0x0045a31c: sinYaw >>= 2, cosYaw >>= 2 (already done) */

    /* 0x0045a322-0x0045a32d: sinPitchComp << 12 */
    int sinPC_shift = sinPitchComp << 12;                    /* 0x0045a32a */

    /* 0x0045a333-0x0045a33b: M10_pre = zero2 - sinPC_shift */
    int __attribute__((unused)) M10_pre = zero2 - sinPC_shift;                       /* 0x0045a335 → ebp-0x84 */

    /* 0x0045a341-0x0045a34d: cosPitchComp << 12 */
    int cosPC_shift = cosPitchComp << 12;                    /* 0x0045a34a */

    /* 0x0045a353-0x0045a35c: M11_pre = zero1 + cosPC_shift */
    int M11_pre = zero1 + cosPC_shift;                       /* 0x0045a356 → ebp-0x5c */

    /* 0x0045a35f-0x0045a362: sinHead * cosPitchComp - zero1 */
    int M20_pre = sinHead * cosPitchComp - zero1;            /* 0x0045a362 → ebp-0xb0 */

    /* 0x0045a36f-0x0045a373: sinPitchComp * sinHead + zero2 */
    int M21_pre = sinPitchComp * sinHead + zero2;            /* 0x0045a36f → stored in ecx */

    /* Apply sinYaw/cosYaw to build the final orientation matrix */
    /* At this point: eax = sinYaw>>2, edx = cosYaw>>2 */

    /* M01_pre >> 12 */
    int m01d = M01_pre >> 12;                                /* 0x0045a378 → esi */

    /* 0x0045a37d-0x0045a380: m01d * cosYaw, m01d * sinYaw */
    int t0 = m01d * cosYaw;                                  /* 0x0045a37d → ebp-0xac */
    int t1 = m01d * sinYaw;                                  /* 0x0045a380 */

    /* 0x0045a38c: negSinHead * sinYaw */
    int t2 = negSinHead * sinYaw;                            /* 0x0045a38c → ebp-0xa8 */

    /* 0x0045a395-0x0045a3a1: mat_R0C2 = t0 - t2 */
    int mat_R0C2 = t0 - t2;                                  /* 0x0045a39b → ebp-0x48 (reused) */

    /* 0x0045a3a4-0x0045a3a7: negSinHead * cosYaw */
    int t3 = negSinHead * cosYaw;                            /* 0x0045a3a7 */

    /* Save M21_pre */
    int saved_M21 = M21_pre;                                 /* 0x0045a3aa → ebp-0xd4 */

    /* 0x0045a3b0: M00_pre >> 12 */
    int mat_R0C0 = M00_pre >> 12;                            /* 0x0045a3b3 → ebp-0x38 */

    /* 0x0045a3b9: M10_pre >> 12 (result discarded — overwritten) */
    /* M10_pre >> 12; — binary computes but doesn't use this directly here */

    /* 0x0045a3c2: M11_pre >> 12 */
    int mat_R1C0 = M11_pre >> 12;                            /* 0x0045a3c5 → ebp-0x50 */

    /* 0x0045a3cb: t3 + t1 → combined for mat_R0C1 */
    int mat_R0C1_numer = t3 + t1;                            /* 0x0045a3cb → edi */

    /* 0x0045a3cd-0x0045a3d3: mat_R1C0 * cosYaw */
    int t4 = mat_R1C0 * cosYaw;                              /* 0x0045a3d0 → ebp-0xa8 */

    /* 0x0045a3d9: sinYaw * 0 = 0 */
    int t5 = 0;                                              /* 0x0045a3d9: imul esi, eax, 0 */

    /* 0x0045a3e2-0x0045a3ee: mat_R1C2 = t4 - t5 */
    int mat_R1C2 = t4 - t5;                                  /* → ebp-0x5c (reused) */

    /* 0x0045a3f1-0x0045a3f4: mat_R1C0 * sinYaw */
    int t6 = mat_R1C0 * sinYaw;                              /* 0x0045a3f4 → ebp-0xa8 */

    /* 0x0045a3fd: cosYaw * 0 = 0 */
    int t7 = 0;                                              /* 0x0045a3fd: imul esi, edx, 0 */

    /* M20_pre >> 12 */
    int mat_R2C0 = M20_pre >> 12;                            /* 0x0045a406 → ebp-0x34 */

    /* 0x0045a40c-0x0045a418: mat_R1C1 = t6 + t7 */
    int mat_R1C1 = t6 + t7;                                  /* 0x0045a418 → esi, ebp-0xb4 */

    /* saved_M21 >> 12 */
    int m21d = saved_M21 >> 12;                              /* 0x0045a424 → ecx */

    /* 0x0045a42f-0x0045a432: m21d * cosYaw */
    int t8 = m21d * cosYaw;                                  /* 0x0045a42f → ebp-0xa8 */

    /* 0x0045a438-0x0045a43b: cosHead * sinYaw */
    int t9 = cosHead * sinYaw;                               /* 0x0045a43b */

    /* 0x0045a43e: sinYaw * m21d */
    int t10 = sinYaw * m21d;                                 /* 0x0045a43e */

    /* 0x0045a441: cosYaw * cosHead */
    int t11 = cosYaw * cosHead;                              /* 0x0045a441 */

    /* 0x0045a445: mat_R0C1_numer >> 12 */
    int mat_R0C1 = mat_R0C1_numer >> 12;                     /* 0x0045a445 → ebp-0x28 */

    /* Camera-space transforms: multiply by view matrix */

    /* 0x0045a44b: Load camera orient X (g_camOrientX at 0x6e9c84) */
    /* 0x0045a451: t10 + t11 → combined term */
    int t10_11 = t10 + t11;                                  /* 0x0045a451 */

    /* mat_R1C2 >> 12, mat_R1C1 >> 12 */
    int __attribute__((unused)) mr1c2 = mat_R1C2 >> 12;                              /* 0x0045a459 */
    int mat_R2C2_val = t10_11 >> 12;                         /* 0x0045a456 → ebp-0x24 */

    /* mat_R1C1 >> 12 */
    int __attribute__((unused)) mr1c1 = mat_R1C1 >> 12;                              /* 0x0045a464: ebp-0xb4 >> 12 */

    /* 0x0045a462: Player world position → camera delta */
    int posX = player->posX;                                  /* 0x0045a462 */
    int camDx = (posX - g_camOrientX) >> 12;                 /* 0x0045a46a-0x0045a46f → ebp-0x20 */

    int posY = player->groundHeight;                         /* 0x0045a475: byte 0x38 */
    int camDy = (-posY - g_camOrientY) >> 12;                /* 0x0045a47e-0x0045a482 → ebp-0x1c */

    int posZ = player->posZ;                                 /* 0x0045a48e */
    int camDz = (posZ - g_camOrientZ) >> 12;                 /* 0x0045a494-0x0045a4b8 → ebp-0x4c */

    /* 0x0045a49d: t9 stored as ebp-0xac, t8 - t9 → mat_R2C2 computation */
    int mat_R2C2_numer = t8 - t9;                            /* 0x0045a4a3-0x0045a4a9 → ebp-0xd4 */

    /* mat_R0C2 >> 12 */
    int mr0c2 = mat_R0C2 >> 12;                              /* 0x0045a4bb → esi */

    /* Multiply orientation matrix rows by view matrix columns */
    /* The view matrix is at 0x6e9c44 (g_viewMtx[16], stride-4):
       row0 = [0x6e9c44, 0x6e9c48, 0x6e9c4c] = right   (indices 0,1,2)
       row1 = [0x6e9c54, 0x6e9c58, 0x6e9c5c] = up      (indices 4,5,6)
       row2 = [0x6e9c64, 0x6e9c68, 0x6e9c6c] = forward (indices 8,9,10) */

    /* Row 0 of composite: [mat_R0C0, mr0c2, mat_R0C1] * viewMatrix */
    /* 0x0045a496-0x0045a4e5: vm_00 */
    int vm_00 = mat_R0C0 * g_viewMtx00 + mr0c2 * g_viewMtx10 + mat_R0C1 * g_viewMtx20;  /* ebp-0x64 */
    int vm_01 = mat_R0C0 * g_viewMtx01 + mr0c2 * g_viewMtx11 + mat_R0C1 * g_viewMtx21;  /* ebp-0x48 */
    int vm_02 = mat_R0C0 * g_viewMtx02 + mr0c2 * g_viewMtx12 + mat_R0C1 * g_viewMtx22;  /* edi */

    /* Row 1: [mat_R2C0, mat_R2C2_numer>>12, mat_R2C2_val] * viewMatrix */
    int mr2cn = mat_R2C2_numer >> 12;                        /* 0x0045a529 → ecx */
    int vm_10 = mat_R2C0 * g_viewMtx00 + mr2cn * g_viewMtx10 + mat_R2C2_val * g_viewMtx20;  /* ebp-0xb0 */
    int vm_11 = mat_R2C0 * g_viewMtx01 + mr2cn * g_viewMtx11 + mat_R2C2_val * g_viewMtx21;  /* ebp-0xd4 */
    int vm_12 = mat_R2C0 * g_viewMtx02 + mr2cn * g_viewMtx12 + mat_R2C2_val * g_viewMtx22;  /* ecx */

    /* Row 2: [camDx, camDy, camDz] * viewMatrix → camera-space position */
    int vm_20 = camDx * g_viewMtx00 + camDy * g_viewMtx10 + camDz * g_viewMtx20;  /* ebp-0xc0 */
    int vm_21 = camDx * g_viewMtx01 + camDy * g_viewMtx11 + camDz * g_viewMtx21;  /* eax */
    int vm_22 = camDx * g_viewMtx02 + camDy * g_viewMtx12 + camDz * g_viewMtx22;  /* edx */

    /* 0x0045a61e-0x0045a668: Scale/shift all terms >> 12 */
    int r2c2_final = vm_02 >> 12;                            /* ebp-0x28: orient-A Z after view xform */
    int r2c2_b = vm_12 >> 12;                                /* ebp-0x24: orient-B Z after view xform */
    int vm20s = vm_20 >> 12;                                 /* ebp-0x20 */
    int vm21s = vm_21 >> 12;                                 /* ebp-0x1c */
    int vm_00s = vm_00 >> 12;                                /* ebp-0x38 */
    int vm_10s = vm_10 >> 12;                                /* ebp-0x34 */
    int vm_01s = vm_01 >> 12;                                /* esi */
    int vm_11s = vm_11 >> 12;                                /* ecx */
    int vm_02s = vm_22 >> 12;                                /* ebp-0x4c: camera Z after view xform */

    /* Model bounding size */
    /* 0x0045a66e: Get model meta offset 0x24 (bounding size for shadow) */
    int charId = (int)player->charId;                         /* 0x0045a66e: movsx */
    int boundSize = g_modelMeta[charId].lightGroup;  /* 0x0045a681: meta +0x24 (bounding size) */

    /* 0x0045a68a-0x0045a6a8: For Amy, scale bound by 4/3 */
    int halfExtent;
    if (charId == CHAR_AMY) {
        halfExtent = (boundSize * 4) / 3;                    /* 0x0045a699-0x0045a6a6 */
    }
    else {
        halfExtent = boundSize;                              /* 0x0045a6aa */
    }

    /* Compute 4 corner depths and screen positions */

    /* neg halfExtent for second direction */
    int negBound = -boundSize;                               /* 0x0045a6b5-0x0045a6b8 → ebp-0x6c */
    int negHalf = -halfExtent;                               /* 0x0045a74a → ebp-0x68 */

    /* Corner 0: (+halfExtent along row0, -boundSize along row1) */
    /* 0x0045a6bf: negBound * r2c2_final */
    int c0a = negBound * r2c2_final;                         /* ebp-0x7c */
    int c0b = halfExtent * r2c2_b;                           /* 0x0045a6ce → ebp-0x78 */
    int c0_depth_raw = c0a + c0b + (vm_02s << 12);           /* 0x0045a6d7-0x0045a6dc */
    int depth0 = (c0_depth_raw + ((c0_depth_raw >> 31) & 0xFFF)) >> 12;  /* 0x0045a6e1-0x0045a6eb */

    /* 0x0045a6f4: Depth clip checks */
    int farClipTimes8 = g_farClipDepth << 3;
    if (depth0 < 1) {
        return;                                  /* 0x0045a6f7 */
    }
    if (depth0 > farClipTimes8) {
        return;                      /* 0x0045a703 */
    }

    /* Corner 1: (+boundSize along row0, +halfExtent along row1) */
    int c1a = boundSize * r2c2_final;                        /* 0x0045a70f → ebp-0x80 */
    int c1_depth_raw = c1a + c0b + (vm_02s << 12);           /* 0x0045a717-0x0045a71a */
    int depth1 = (c1_depth_raw + ((c1_depth_raw >> 31) & 0xFFF)) >> 12;

    if (depth1 < 1) {
        return;                                  /* 0x0045a72f */
    }
    if (depth1 > farClipTimes8) {
        return;                      /* 0x0045a738 */
    }

    /* Corner 2: (negHalf along row0, -boundSize along row1) */
    /* 0x0045a74f: negHalf * r2c2_b */
    int c2a = negHalf * r2c2_b;                              /* 0x0045a752 → ebp-0x74 */
    int c2_depth_raw = c1a + c2a + (vm_02s << 12);           /* 0x0045a758-0x0045a75a */
    int depth2 = (c2_depth_raw + ((c2_depth_raw >> 31) & 0xFFF)) >> 12;

    if (depth2 < 1) {
        return;                                  /* 0x0045a772 */
    }
    if (depth2 > farClipTimes8) {
        return;                      /* 0x0045a77b */
    }

    /* Corner 3: (-boundSize along row0, negHalf along row1) */
    int c3_depth_raw = c0a + c2a + (vm_02s << 12);           /* 0x0045a787-0x0045a78d */
    int depth3 = (c3_depth_raw + ((c3_depth_raw >> 31) & 0xFFF)) >> 12;

    if (depth3 < 1) {
        return;                                  /* 0x0045a7a2 */
    }
    if (depth3 > farClipTimes8) {
        return;                      /* 0x0045a7ab */
    }

    /* Screen-space X/Y for each corner */

    /* Corner 0 screen: use row0/row1 of orientation × view */
    /* X component along mat row 0 */
    int vm_20_shift = vm20s << 12;                           /* 0x0045a7d2 → ebp-0xac */

    int r0_s0a = negBound * vm_00s;                          /* 0x0045a7c4 */
    int r0_s0b = halfExtent * vm_10s;                        /* 0x0045a7ba-0x0045a7be → ebp-0xa4 */
    int sx0_raw = r0_s0a + r0_s0b + vm_20_shift;            /* 0x0045a7cd-0x0045a7d5 */
    int sx0 = (sx0_raw + ((sx0_raw >> 31) & 0xFFF)) >> 12;  /* 0x0045a7dd-0x0045a7e7 → ebp-0x9c */

    /* Y component */
    int vm_21_shift = vm21s << 12;                           /* 0x0045a7ff → edi */
    int r0_s1a = halfExtent * vm_11s;                        /* 0x0045a7ed → ebp-0xa8 */
    int r0_s1b = negBound * vm_01s;                          /* 0x0045a7f9 */
    int sy0_raw = r0_s1b + r0_s1a + vm_21_shift;            /* 0x0045a802-0x0045a80a */
    int sy0 = (sy0_raw + ((sy0_raw >> 31) & 0xFFF)) >> 12;  /* 0x0045a80d-0x0045a817 → ebp-0x90 */

    /* 0x0045a826-0x0045a846: Screen projection for corner 0 */
    int scrX0 = g_screenCenterX + (sx0 * g_projScaleXCurrent) / depth0;   /* 0x0045a839-0x0045a841 → ebp-0x118 */
    int scrY0 = g_screenCenterY - (sy0 * g_projScaleY) / depth0;          /* 0x0045a852-0x0045a866 → ebp-0x108 */

    /* Corner 1 screen */
    int c1_s0 = boundSize * vm_00s;                          /* 0x0045a874 → ebp-0xa0 */
    int sx1_raw = c1_s0 + r0_s0b + vm_20_shift;             /* 0x0045a87d-0x0045a88b */
    int sx1 = (sx1_raw + ((sx1_raw >> 31) & 0xFFF)) >> 12;  /* → ebp-0x98 */

    int c1_s1 = boundSize * vm_01s;                          /* 0x0045a8a3 → ebp-0xa4 */
    int sy1_raw = c1_s1 + r0_s1a + vm_21_shift;             /* 0x0045a8ac-0x0045a8b4 */
    int sy1 = (sy1_raw + ((sy1_raw >> 31) & 0xFFF)) >> 12;  /* → ebp-0x88 */

    int scrX1 = g_screenCenterX + (sx1 * g_projScaleXCurrent) / depth1;   /* → ebp-0x114 */
    int scrY1 = g_screenCenterY - (sy1 * g_projScaleY) / depth1;          /* → ebp-0x104 */

    /* Corner 2 screen */
    int sx2_raw = negHalf * vm_10s + c1_s0 + vm_20_shift;   /* 0x0045a91d-0x0045a92c */
    int sx2 = (sx2_raw + ((sx2_raw >> 31) & 0xFFF)) >> 12;  /* → ebp-0xbc */

    int sy2_raw = negHalf * vm_11s + c1_s1 + vm_21_shift;   /* 0x0045a941-0x0045a94d */
    int sy2 = (sy2_raw + ((sy2_raw >> 31) & 0xFFF)) >> 12;

    int scrX2 = g_screenCenterX + (sx2 * g_projScaleXCurrent) / depth2;   /* → ebp-0x110 */
    int scrY2 = g_screenCenterY - (sy2 * g_projScaleY) / depth2;          /* → ebp-0x100 */

    /* Corner 3 screen (remaining corner) */
    int sx3_raw;
    int neg_half_2 = -halfExtent;                        /* 0x0045aa09-0x0045aa0f */
    int sx3a = vm_10s * neg_half_2;                      /* 0x0045aa11 */
    int neg_bound_2 = -boundSize;                        /* 0x0045aa1d */
    int sx3b = neg_bound_2 * vm_00s;                     /* 0x0045aa25 */
    int sx3_r = sx3b + sx3a + (vm20s << 12);             /* 0x0045aa29-0x0045aa3d */
    sx3_raw = (sx3_r + ((sx3_r >> 31) & 0xFFF)) >> 12;
    int sx3 = sx3_raw;                                       /* ebp-0x8c */

    int sy3_raw;
    neg_half_2 = -halfExtent;
    int sy3a = vm_11s * neg_half_2;                      /* 0x0045aa4c */
    int sy3b = (-boundSize) * vm_01s;                    /* 0x0045aa4f */
    int sy3_r = sy3a + sy3b + (vm21s << 12);             /* 0x0045aa59-0x0045aa5e */
    sy3_raw = (sy3_r + ((sy3_r >> 31) & 0xFFF)) >> 12;
    int sy3 = sy3_raw;

    int scrX3 = g_screenCenterX + (sx3 * g_projScaleXCurrent) / depth3;   /* → ebp-0x10c */
    int scrY3 = g_screenCenterY - (sy3 * g_projScaleY) / depth3;          /* → ebp-0xfc */

    /* Depth adjustment: subtract 0x18 from all depths */
    /* 0x0045aabb-0x0045ab08 */
    depth0 -= 0x18;                                          /* 0x0045aabb */
    if (depth0 < 1) {
        return;                                  /* 0x0045aacc */
    }
    depth1 -= 0x18;                                          /* 0x0045aad5 */
    if (depth1 < 1) {
        return;                                  /* 0x0045aae4 */
    }
    depth2 -= 0x18;                                          /* 0x0045aaed */
    if (depth2 < 1) {
        return;                                  /* 0x0045aafc */
    }
    depth3 -= 0x18;                                          /* 0x0045ab05 */
    if (depth3 < 1) {
        return;                                  /* 0x0045ab0e */
    }

    /* Screen clip tests against viewport bounds */
    /* 0x0045ab17-0x0045ab3d: All 4 X coords vs g_clipLeft */
    if (scrX0 < g_clipLeft && g_clipLeft > scrX1 &&
        g_clipLeft > scrX2 && g_clipLeft > scrX3)
    {
        return;
    }

    /* 0x0045ab43-0x0045ab69: All 4 X coords vs g_clipRight */
    if (scrX0 > g_clipRight && g_clipRight < scrX1 &&
        g_clipRight < scrX2 && g_clipRight < scrX3)
    {
        return;
    }

    /* 0x0045ab6f-0x0045ab95: All 4 Y coords vs g_clipTop */
    if (scrY0 < g_clipTop && g_clipTop > scrY1 &&
        g_clipTop > scrY2 && g_clipTop > scrY3)
    {
        return;
    }

    /* 0x0045ab9b-0x0045abc1: All 4 Y coords vs g_clipBottom */
    if (scrY0 > g_clipBottom && g_clipBottom < scrY1 &&
        g_clipBottom < scrY2 && g_clipBottom < scrY3)
    {
        return;
    }

    /* Backface cull */
    /* 0x0045a9a4-0x0045aa03: Cross product of screen edges for winding order.
       Binary uses edges from vertex 1:
         edx = (scrY0-scrY1) * (scrX2-scrX1)    = dy01 * dx21
         eax = (scrY2-scrY1) * (scrX0-scrX1)    = dy21 * dx01
       Then checks sign vs g_mirrorMode [0x6e9920]. */
    int dy01 = scrY0 - scrY1;                            /* 0x0045a9b0-0x0045a9bc */
    int dx21 = scrX2 - scrX1;                            /* 0x0045a9be-0x0045a9c4 */
    int dx01 = scrX0 - scrX1;                            /* 0x0045a9c9-0x0045a9d5 */
    int dy21 = scrY2 - scrY1;                            /* 0x0045a9db-0x0045a9e1 */
    int cross_edx = dy01 * dx21;                         /* 0x0045a9c6 */
    int cross_eax = dy21 * dx01;                         /* 0x0045a9e9 */
    if (g_mirrorMode != 0) {
        if ((cross_eax - cross_edx) < 0) {
            return;        /* 0x0045a9f9: sub eax,edx; jl exit */
        }
    }
    else {
        if ((cross_edx - cross_eax) < 0) {
            return;        /* 0x0045a9ff: sub edx,eax; jl exit */
        }
    }

    /* Vertex lighting / shadow color */

    /* 0x0045abc7-0x0045ac2b: Determine shadow color based on player state */
    float shadowU;
    float shadowV;  /* UV/color params for the shadow quad */

    if (player->yOffset < 0x30000) {
        /* 0x0045abd0: Character-specific shadow brightness */
        int charId = (int)player->charId;
        if (charId == CHAR_AMY || charId == CHAR_EGG_ROBO) {
            /* 0x0045abe8-0x0045abf5: Brighter shadow for characters 3 and 8 */
            shadowV = 0.6875f;   /* 0x3F300000 → ebp-0x30 */
            shadowU = 0.875f;    /* 0x3F600000 → ebp-0x3c */
        }
        else {
            /* 0x0045abfa-0x0045ac04: Normal shadow */
            shadowV = 0.1875f;   /* 0x3E400000 → ebp-0x30 */
            shadowU = 0.0f;      /* 0 → ebp-0x3c */
        }
    }
    else {
        /* 0x0045ac09-0x0045ac2b: Dynamic shadow based on g_emeraldAnimFrame1 */
        int phase = g_emeraldAnimFrame1 << 5;                  /* 0x0045ac0e */
        shadowU = (float)phase * (float)ROM_INV_256;         /* 0x0045ac1c-0x0045ac2b */
        shadowV = 0.0625f;  /* 0x3D800000 → ebp-0x30 */
    }

    /* Build shadow quad via immediate mode */
    float depthDiv = g_farClipFloat + ROM_FLOAT_NEG24;       /* g_farClipFloat - 24.0 */

    /* Per-vertex: screen pos, depth, UV, fog alpha */
    int depths[4] = { depth0, depth1, depth2, depth3 };
    int scrXs[4] = { scrX0, scrX1, scrX2, scrX3 };
    int scrYs[4] = { scrY0, scrY1, scrY2, scrY3 };
    float uvUs[4] = { shadowU, shadowU + (float)ROM_UV_OFFSET,
                      shadowU + (float)ROM_UV_OFFSET, shadowU };
    float uvVs[4] = { shadowV, shadowV,
                      shadowV + (float)ROM_UV_OFFSET, shadowV + (float)ROM_UV_OFFSET };

    /* Source order is TL,TR,BR,BL = 0,1,2,3; PVR strip order is 0,1,3,2.
     * Non-DC R_DrawQuad does the reorder; DC direct emit writes verts
     * straight into PVR strip order. */
#ifdef SONICR_DC
    static const int s_pvrStripIdx[4] = { 0, 1, 3, 2 };
    static const uint32_t s_pvrFlags[4] = {
        PVR_CMD_VERTEX, PVR_CMD_VERTEX, PVR_CMD_VERTEX, PVR_CMD_VERTEX_EOL
    };
#else
    RenderVertex shadowVerts[4];
#endif
    for (int sv = 0; sv < 4; sv++) {
#ifdef SONICR_DC
        int srcIdx = s_pvrStripIdx[sv];
#else
        int srcIdx = sv;
#endif
        float fD = (float)depths[srcIdx];
        float normZ = fD / depthDiv;

        int alpha;
        sr_double dv = (sr_double)normZ;
        if (dv > ROM_DEPTH_THRESH_HI) {
            alpha = 0;
        }
        else if (dv <= ROM_DEPTH_THRESH_LO) {
            alpha = 0xC0;
        }
        else {
            int raw = (int)sr_lrint((dv + ROM_DEPTH_OFFSET) * ROM_DEPTH_SCALE);
            alpha = (raw < 0) ? -raw : raw;
        }

        uint32_t argb = ((unsigned int)alpha << 24) | VERTEX_WHITE_RGB;

#ifdef SONICR_DC
        pvr_vertex_t *pv = &s_ptScratch[sv];
        pv->flags = s_pvrFlags[sv];
        pv->x = (float)scrXs[srcIdx];
        pv->y = (float)scrYs[srcIdx];
        pv->z = 1.0f / fD;
        pv->u = uvUs[srcIdx];
        pv->v = uvVs[srcIdx];
        pv->argb = argb;
        pv->oargb = 0;
#else
        shadowVerts[sv].sx = (float)scrXs[sv];
        shadowVerts[sv].sy = (float)scrYs[sv];
        shadowVerts[sv].sz = normZ;
        shadowVerts[sv].rhw = 1.0f / fD;
        shadowVerts[sv].color = argb;
        shadowVerts[sv].specular = 0;
        shadowVerts[sv].u = uvUs[sv];
        shadowVerts[sv].v = uvVs[sv];
#endif
    }

#if 0
ndef SONICR_DC
    /* DEBUG: shadow-vs-track depth separation and inherited draw state.
     * The shadow divides camera depth by (farClip - 24) while track polys
     * divide by farClip, so at the same depth the shadow should land BEHIND
     * the track by a fixed margin. Logs that margin in 32-bit depth units
     * alongside the state this unscoped draw inherits. */
    {
        static int s_dbgTick = 0;
        if ((s_dbgTick++ % 120) == 0) {
            float fD0 = (float)depths[0];
            float shadowSz = fD0 / depthDiv;
            float trackSz = (g_farClipFloat > 0.0f) ? (fD0 / g_farClipFloat) : 0.0f;
            float delta = shadowSz - trackSz;
            int dt = 0;
            int dw = 0;
            int df = 0;
            int bm = 0;
            int at = 0;
            float ar = 0.0f;
            R_DebugGetState(&dt, &dw, &df, &bm, &at, &ar);
            fprintf(stderr,
                    "[shadow] depth=%.1f farClip=%.1f sz=%.8f trackSz=%.8f "
                    "delta=%.8f (%.1f units of 2^32) | depthTest=%d depthWrite=%d "
                    "depthFunc=%d blend=%d alphaTest=%d ref=%.3f\n",
                    fD0, g_farClipFloat, shadowSz, trackSz, delta,
                    delta * 4294967296.0f, dt, dw, df, bm, at, ar);
        }
    }
#endif

    R_SetTexture(tpage);
    R_SetTexEnv(R_TEXENV_MODULATE);
#ifdef SONICR_DC
    R_DrawPvrQuad(s_ptScratch);
#else
    R_DrawQuad(shadowVerts);
#endif
}

/* =====================================================================
 * RenderAllCharacterModels — FUN_0045AFFC — 350 bytes
 * Iterates all players and renders each character's 3D model.
 * Also renders character-attached sprites (items, water effects).
 * EAX = number of players (Watcom fastcall).
 * ===================================================================== */
void RenderAllCharacterModels(int numPlayers)
{
    if (numPlayers <= 0) {
        return;
    }

    for (int i = 0; i < numPlayers; i++) {
        Player *player = &g_playerBase[i];
        { extern int NetPlayerGone(int); if (NetPlayerGone(i)) continue; }   /* left the online race */

        /* 0x45B02D: Shadow setup — call if player->yOffset == 0 or 0x60000 */
        int animField = player->yOffset;
        if ((animField == 0 || animField == 0x60000) &&
            SHADOWS_ENABLED(SPLIT_CHAR_SHADOW)) {
            SetupCharacterShadowQuad(player);
        }

        /* 0x45B042: Character special sprite (charId < 3 or == 9) */
        int charId = (int)player->charId;
        if (charId < CHAR_AMY || charId == CHAR_SUPER_SONIC) {
            int moveState = (int)player->animId;    /* P_INT(p,0x96)>>16 = short at 0x98 = animId */
            if (moveState == 1
                && player->renderEnabled != 0
                && g_raceOrder[3] != 0)
            {
                SubmitCharacterSprite(player, (int)0xFFFD7000, charId);
            }
        }

        /* 0x45B08E: Item rendering — skip in time attack (raceType==2) */
        if (g_raceType != RACE_TIMEATTACK) {
            /* 0x45B097: Item box sprite */
            if (player->itemEffectId != 0) {
                int itemModelId = player->itemHeightMod;
                if (itemModelId != 0x0FFFFFFF) {
                    int itemType = (int)player->itemEffectId;  /* P_INT(p,0x62)>>16 = short at 0x64 */
                    RenderItemSprite(player, itemModelId, itemType);
                }
            }

            /* 0x45B0BA: Player item effect — deferred to after model draw
             * so transparent shield renders over opaque character geometry. */

            /* 0x45B0E3: Air/water effects */
            if (player->_unk_0x78 != 0 || player->_unk_0x7A != 0) {
                RenderAirWaterEffect(i);
            }
        }

        /* 0x45B0F8: Ghost skip — in single-player TA with ghost, skip player 1's
         * model on odd frames. 0x45B107 reads [0x902078] = g_raceOrder[2], the
         * per-frame 0/1 toggle written by UpdateLapCounter at 0x482028. */
        if (i == 1 && i == g_ghostToggle) {
            if (g_raceOrder[2] != 0
                && g_raceType == RACE_TIMEATTACK
                && g_raceType > g_raceSubMode         /* 0x45B118: cmp eax, [0x8FB954] */
                && g_ghostDataExists != 0)            /* 0x45B120: [0x8FB960] */
            {
                goto next_player;                        /* 0x45B127: skip model */
            }
        }

        /* 0x45B12D: Render the 3D character model.
         * Skip when submerged (yOffset == MAX_JUMP_HEIGHT) — the original
         * hides the model through an unknown mechanism; in water loopMode
         * is actually 0, not 1. */
        if (player->yOffset != 0x60000) {
            RenderCharacterD3D(player);
        }

        /* 0x45B0BA: Player item effect — drawn after model so transparent
         * shield/boost overlays render on top of opaque character geometry. */
        if (g_raceType != RACE_TIMEATTACK) {
            int effectState = (int)player->itemEffectState;  /* short at 0x82 */
            if (effectState != 0) {
                int effectModelId = player->effectYMod;
                if (effectModelId != 0x0FFFFFFF) {
                    RenderPlayerItemEffect(player, (int)0xFFFD7000, effectState, effectModelId);
                }
            }
        }

    next_player:
        /* */ ;
    }

    /* 0x45B139: Network player rendering */
    if (g_netSessionActive != 0 || g_isNetworkGame != 0) {
        RenderNetworkPlayers();
    }
}
