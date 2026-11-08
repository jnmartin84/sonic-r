/**
 * render_grid_d3d.c — D3D playfield tile grid renderer
 *
 * RenderPlayfieldGridD3D — translated from 0x0045E1BC (2511 bytes)
 *
 * Renders the playfield as a 128×128 grid of textured D3D quads.
 * Each cell is looked up through a tile map and per-track visibility
 * table. Visible cells are transformed through the float view matrix,
 * projected to screen, fog-blended, and submitted to the tpage batch
 * system.
 *
 * Original call convention (Watcom fastcall):
 *   EAX = baseVerts  — &g_playfieldVertices[vpIdx * VP_STRIDE]
 *   EDX = upperVerts — &g_playfieldVertices[vpIdx * VP_STRIDE + 24]
 *   EBX = vpConfig   — g_viewportConfigArray + vpIdx * 0xC8
 */

#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "sonicr_functions.h"
#include "r_types.h"
#include "r_state.h"
#include "r_draw.h"
#include "nearclip.h"   /* g_scissorEdge for split-screen fog suppression */
extern float g_farClipFullScreenF;  /* hud_full.c — far plane BEFORE the
                                     * split-screen reduction */
#include <math.h>
#include <stdlib.h>
#ifdef SONICR_DC
#include <kos.h>        /* pvr_vertex_t for grid scissor strip clipper */
#endif

/* Split-screen-only LOD toggles. All run only when g_scissorEdge != NONE.
 * GRID_SS_FOG_CULL: skip tiles whose 4 verts are all past fog-far depth
 *   (would render alpha=0 anyway) — pure win, no visual change.
 * GRID_SS_LOD_QUAD: rows past half-far-plane depth replaced by a single
 *   textured LOD quad sampled from the per-track averaged-tile texture.
 *   Smooth distance fade, ~1 quad instead of N tiles. Default on.
 */
#define GRID_SS_FOG_CULL  0
#ifdef SONICR_DC
#define GRID_SS_LOD_QUAD  0
#else
#define GRID_SS_LOD_QUAD  0
#endif

/* Tpage slot holding the coarse grid LOD texture. File scope, and outside the
 * SONICR_DC guard, because the fineRadius selection below reads
 * g_tpageStateArray[GRID_LOD_TPAGE] on both platforms; the builder that fills
 * it is DC-only. Must stay distinct from TPAGE_PLATFORM_ICONS (51) — see
 * BuildGridLODTex for that history. */
#define GRID_LOD_TPAGE 50

/* ROM double constants from binary .rdata section (0x52C4xx) — extracted from SONICR.EXE */
#define GRID_STEP_SCALE     0.0078125            /* 0x52C4CC: 1/128 (double) — grid step */
#define GRID_FAR_OFFSET     64.0                 /* 0x52C4D4: added to farClipFloat for maxZ (double) */
#define GRID_FOG_SCALE      1275.0               /* 0x52C4DC: fog alpha multiplier (double) */
#ifdef SONICR_DC
/* DC: smaller bias so projection and rhw can both use post-bias z without
 * visibly shrinking the grid. PVR's depth resolution is fine enough that
 * 0.5 still pushes grid behind the track. SDL/D3D keeps the binary's
 * original 64.0f because D3D's interpolators are precise enough that the
 * pre/post-bias mismatch isn't visible. */
#define GRID_DEPTH_BIAS     12.0f
//0.5f
#else
#define GRID_DEPTH_BIAS     64.0f                /* 0x52C4E4: depth bias added before Z-buffer calc (float) */
#endif
#define GRID_FOG_FAR        0.9                  /* 0x52C4E8: Z-buffer above this = fully fogged (double) */
#define GRID_FOG_NEAR       0.7                  /* 0x52C4F0: Z-buffer below this = no fog (double) */
#define GRID_FOG_OFFSET     (-0.9)               /* 0x52C4F8: offset before fog scale multiply (double) */

/* Grid color tint offsets at 0x94BD00-08 (separate from track tint at 0x94BD0C-14).
 * In the original binary, these are set by weather/lighting code per-track.
 * Default 0 = no extra tint, base color 0xD0 for all channels. */
int g_gridTintR;                                 /* 0x0094BD00 */
int g_gridTintG;                                 /* 0x0094BD04 */
int g_gridTintB;                                 /* 0x0094BD08 */

/* Per-track grid visibility table — ROM data extracted from SONICR.EXE at 0x004FC6E8.
 * 5 rows × 120 bytes, indexed as g_gridColorTable[(g_trackId - 1) * 120 + tile].
 * Entry != 0 means tile is visible for that track.
 * Rows 2/3 are stored Ruin-then-Factory to match our C-side g_trackId order
 * (binary's ROM table has these swapped — see project_trackid_swap memory). */
unsigned char g_gridColorTable[5 * 120] = {      /* 0x004FC6E8 */
    /* Row 0 — g_trackId=1 (Resort Island) */
    0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01, 0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
    0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01, 0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
    0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x00, 0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
    0x01,0x01,0x01,0x01,0x00,0x00,0x00,0x01, 0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
    0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01, 0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
    0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01, 0x01,0x01,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    /* Row 1 — g_trackId=2 (Radical City) */
    0x00,0x01,0x01,0x01,0x00,0x00,0x00,0x00, 0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
    0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01, 0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
    0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01, 0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
    0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01, 0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
    0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01, 0x01,0x01,0x01,0x01,0x01,0x00,0x00,0x01,
    0x01,0x01,0x01,0x01,0x01,0x01,0x00,0x01, 0x00,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
    0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01, 0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
    0x01,0x01,0x01,0x01,0x01,0x01,0x00,0x00,
    /* Row 2 — g_trackId=3 (Regal Ruin) */
    0x01,0x01,0x00,0x00,0x00,0x00,0x01,0x01, 0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
    0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01, 0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
    0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01, 0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
    0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01, 0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
    0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01, 0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
    0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01, 0x01,0x01,0x01,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    /* Row 3 — g_trackId=4 (Reactive Factory) */
    0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01, 0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
    0x01,0x01,0x01,0x01,0x01,0x01,0x00,0x00, 0x00,0x00,0x01,0x01,0x01,0x01,0x01,0x01,
    0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01, 0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
    0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01, 0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
    0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01, 0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
    0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01, 0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
    0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01, 0x01,0x01,0x01,0x01,0x01,0x01,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    /* Row 4 — g_trackId=5 (Radiant Emerald — caller skips, never used) */
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x02,0x00,0x00,0x00,0x00,0x00,0x00, 0x80,0x02,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00, 0x00,0x01,0x00,0x00,0x00,0x01,0x00,0x00,
    0x00,0x02,0x00,0x00,0x00,0x01,0x00,0x00, 0x80,0x02,0x00,0x00,0x00,0x01,0x00,0x00,
    0x00,0x00,0x00,0x00,0xE0,0x01,0x00,0x00, 0x00,0x01,0x00,0x00,0xE0,0x01,0x00,0x00,
    0x00,0x02,0x00,0x00,0xE0,0x01,0x00,0x00, 0x80,0x02,0x00,0x00,0xE0,0x01,0x00,0x00,
    0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00, 0x05,0x00,0x00,0x00,0x04,0x00,0x00,0x00,
    0x01,0x00,0x00,0x00,0x02,0x00,0x00,0x00,
};

/* Per-tile UV coordinate buffer at 0x006400DC.
 * 128 entries × 8 floats (32 bytes) — 4 UV pairs per quad.
 * Populated by BuildGridUVTable() at startup. */
float g_gridUVBuffer[128 * 8];                   /* 0x006400DC */

/* ROM constants for BuildGridUVTable — extracted from SONICR.EXE */
#define GRID_UV_SCALE       0.125                /* 0x52F8C2: 1/8 (double) — 8 tiles per texture page */

#ifdef BLURRY
//#ifdef SONICR_DC
/* The tile pages are bilinear-filtered on DC (R_SetTpageFilter in
 * D3D_LoadPlayfieldTilesRGB), and they are ATLASES — 8x8 tiles of 32x32 in a
 * 256x256 page. The ROM's 0.001 inset is ~0.26 texel, which is enough to keep
 * a point sample off the boundary but NOT enough to keep a bilinear 2x2 kernel
 * inside the tile. When the kernel reaches the neighbouring atlas tile you get
 * a colour seam; where that neighbour is colour-keyed, the filtered 1-bit
 * alpha drops under the punch-through test and the tile edge is discarded
 * outright — a gap showing the water behind the grid.
 *
 * Half a texel on a 256px page is the minimum that contains the kernel. Costs
 * each tile ~1 of its 32 texels of sampled width. */
#define GRID_UV_BIAS        (0.5f / 256.0f)
#define GRID_UV_BIAS2      (-0.5f / 256.0f)
#else
#define GRID_UV_BIAS        0.001f               /* 0x52F8CA: seam inset (float) */
#define GRID_UV_BIAS2      (-0.001f)             /* 0x52F8CE: seam inset opposite edge (float) */
#endif

/**
 * BuildGridUVTable — FUN_004767f8 — 302 bytes
 * Generates UV coordinates for the 128-entry playfield tile grid.
 * Each tile occupies a 1/8 × 1/8 region of the texture, arranged
 * in an 8×8 pattern. UV pairs are inset by a small bias to avoid seams.
 * Called once at startup from the main init function.
 */
void BuildGridUVTable(void)
{
    float scale = (float)GRID_UV_SCALE;                               /* 0x52F8C2 */
    float bias  = GRID_UV_BIAS;                                       /* 0x52F8CA */
    float bias2 = GRID_UV_BIAS2;                                     /* 0x52F8CE */

    for (int i = 0; i < 128; i++) {                                      /* 0x47690F: cmp ecx, 0x80 */
        float u_base = (float)(i & 7) * scale;                      /* 0x476816: and edx, 7 */
        float v_base = (float)((i & 0x3F) >> 3) * scale;            /* 0x47681E: and edx, 0x3f; sar 3 */

        float *p = &g_gridUVBuffer[i * 8];

        /* Vertex 0 (top-left) — U inset right, V inset down */
        p[0] = u_base + bias;                                        /* 0x47686F */
        p[1] = v_base + bias;                                        /* 0x4768DB */

        /* Vertex 1 (top-right) — U at right edge + bias2, V inset down */
        p[2] = u_base + scale + bias2;                               /* 0x47689B */
        p[3] = v_base + bias;                                        /* 0x4768FB */

        /* Vertex 2 (bottom-right) — U at right edge + bias2, V at bottom + bias2 */
        p[4] = u_base + scale + bias2;                               /* 0x4768A1 */
        p[5] = v_base + scale + bias2;                               /* 0x476903 */

        /* Vertex 3 (bottom-left) — U inset right, V at bottom + bias2 */
        p[6] = u_base + bias;                                        /* 0x47688B */
        p[7] = v_base + scale + bias2;                               /* 0x476909 */
    }
}

/* Externs */

/* Compute fog alpha for a given Z-buffer depth value.
 * Returns 0 (fully fogged) to 255 (no fog).
 * Binary pattern: compare thresholds, then abs((int)((depth + offset) * scale)).
 * 0x4E08CA = __CHP (Watcom float truncation) = C (int) cast. */
#ifdef SONICR_DC
/* ============================================================
 * Grid scissor clip (split-screen only). In-place strip clipper
 * modeled on TrackClipAndEmitQuad_DC's vismask switch — same
 * topology cases (a single half-plane crosses a convex quad at
 * most twice → 3/4/5-vert PVR strip output via one R_DrawPvrStrip
 * call). Caller pre-loads s_ptScratch[0..3] in PVR strip order
 * (TL=src0 → slot 0, TR=src1 → slot 1, BL=src3 → slot 2,
 * BR=src2 → slot 3); slot 0..3 are mutated in place, slot 4 is
 * scratch for the 5-vert cases.
 * ============================================================ */
extern __attribute__((aligned(32))) pvr_vertex_t s_ptScratch[16];
extern void R_DrawPvrStrip(pvr_vertex_t *v, int count);
extern int R_EmitHeader(int pvr_list);
extern pvr_vertex_t *R_TrVertbufTail(void);
extern void R_TrVertbufWritten(size_t bytes);
extern void R_PtSubmit(pvr_vertex_t *v, size_t bytes);

/* Scale (if 240p) and submit a finished strip. Returns triangle count. */
static int grid_strip_submit(pvr_vertex_t *buf, int sendverts, int is_tr)
{
    size_t bytes = (size_t)sendverts * sizeof(pvr_vertex_t);
#if SONICR_DC_240P
    for (int i = 0; i < sendverts; i++) {
        buf[i].x *= 0.5f;
        buf[i].y *= 0.5f;
    }
#endif
    if (is_tr) {
        R_TrVertbufWritten(bytes);
    }
    else {
        R_PtSubmit(buf, bytes);
    }
    return sendverts - 2;
}

/* Public wrapper: convert a perimeter-order RenderVertex quad
 * (TL, TR, BR, BL) into PVR strip slot order in s_ptScratch[0..3]
 * (slot 0=TL=src0, slot 1=TR=src1, slot 2=BL=src3, slot 3=BR=src2),
 * then run the grid scissor clipper. Caller is responsible for
 * R_SetTexture / R_SetTexEnv / R_SetDepthWrite state before calling.
 * Returns triangle count submitted (0 if fully clipped). */
int BillboardScissorClipEmitStrip(const RenderVertex *quad)
{
    static const int srcFromSlot[4] = { 0, 1, 3, 2 };

    for (int s = 0; s < 4; s++) {
        const RenderVertex *rv = &quad[srcFromSlot[s]];
        pvr_vertex_t *pv = &s_ptScratch[s];
        pv->flags = PVR_CMD_VERTEX;
        pv->x = rv->sx;
        pv->y = rv->sy;
        pv->z = rv->rhw;     /* PVR's z slot holds rhw (1/w). */
        pv->u = rv->u;
        pv->v = rv->v;
        pv->argb = rv->color;
        pv->oargb = 0;
    }

    s_ptScratch[3].flags = PVR_CMD_VERTEX_EOL;
    return grid_strip_submit(s_ptScratch, 4, 0);
}
#endif /* SONICR_DC */

static int GridFogAlpha(sr_double depth)
{
    if (depth > GRID_FOG_FAR) {                                       /* 0x45E407/4A6/551/5F5 */
        return 0;
    }
    if (depth <= GRID_FOG_NEAR) {                                     /* 0x45E410/4B5/55A/5FE */
        return 255;
    }
    int alpha = (int)((depth + GRID_FOG_OFFSET) * GRID_FOG_SCALE);    /* 0x45E418-424 */
    return abs(alpha);                                                /* cdq; xor; sub */
}

/* Near-plane clipping for tile grid quads */

typedef struct {
    float camX, camY, z;   /* view-space position (unbiased) */
    float u, v;            /* texture coords */
} GridClipVert;

typedef struct {
    float maxZ;
    float projScaleXf;
    float projScaleYf;
    float centerXf;
    float centerYf;
    uint32_t baseColor;
} GridProjection;

static void GridClipLerp(const GridClipVert *behind, const GridClipVert *front, GridClipVert *out) {
    float t = (1.0f - behind->z) * reciprocal(front->z - behind->z);
    out->camX = behind->camX + (front->camX - behind->camX) * t;
    out->camY = behind->camY + (front->camY - behind->camY) * t;
    out->z = 1.0f;
    out->u = behind->u + (front->u - behind->u) * t;
    out->v = behind->v + (front->v - behind->v) * t;
}

static RenderVertex BuildGridClipVertex(const GridClipVert *v, const GridProjection *gp) {
    RenderVertex rv;
    float z = v->z;
    float zBiased = z + GRID_DEPTH_BIAS;
#ifdef SONICR_DC
    float invZ = reciprocal(zBiased);   /* projection W and PVR rhw share the bias */
#else
    float invZ = reciprocal(z);         /* binary adds the bias AFTER projecting (0x45E9E6) */
#endif
    rv.sx = gp->centerXf + v->camX * gp->projScaleXf * invZ;
    rv.sy = gp->centerYf - v->camY * gp->projScaleYf * invZ;
    rv.sz = zBiased * reciprocal(gp->maxZ);
    /* rhw must match the W the screen X/Y were projected with (invZ), NOT the
     * biased depth. On DC invZ == 1/zBiased (projection is biased); on SDL
     * invZ == 1/z (unbiased) — using zBiased here caused grid texture swim. */
    rv.rhw = invZ;
    int fog = GridFogAlpha((sr_double)rv.sz);
    rv.color = gp->baseColor | ((unsigned int)fog << 24);
    rv.specular = 0;
    rv.u = v->u;
    rv.v = v->v;
    return rv;
}

/* Clip a 4-vertex convex quad against the near plane (z=1) as a single
 * polygon and emit the result. Sutherland-Hodgman against one plane
 * produces 0, 3, 4, or 5 output vertices for a convex input. Compared
 * to splitting into two triangles up front, this avoids re-clipping the
 * shared diagonal edge and skips the redundant draw call when only part
 * of the quad needs clipping. */
/* Lerp two RenderVertex (already screen-projected) at parameter t. argb
 * is lerped componentwise so per-vert fog/lighting variations survive
 * the scissor clip. */
static inline void grid_screen_lerp(const RenderVertex *a, const RenderVertex *b,
                                    RenderVertex *out, float t)
{
    out->sx = a->sx + (b->sx - a->sx) * t;
    out->sy = a->sy + (b->sy - a->sy) * t;
    out->sz = a->sz + (b->sz - a->sz) * t;

    /* PERSPECTIVE-CORRECT u/v. These vertices are already projected, so u and
     * v are NOT linear in screen space — only u*rhw and v*rhw are. Lerping u
     * directly is exact only while w is constant along the edge.
     *
     * A fine grid tile spans one cell, so w barely moves and the error is
     * invisible; a LOD block spans 8x8 tiles, so w varies hugely along an edge
     * and the clip seam warps badly. That is why this shows up on the LOD grid
     * and not the fine grid.
     *
     * The near-plane clip does not need this — GridClipLerp runs in VIEW space,
     * where linear interpolation is already correct. Only this post-projection
     * scissor pass does. */
    {
        float aw = a->rhw;
        float bw = b->rhw;
        float w  = aw + (bw - aw) * t;
        float uw = a->u * aw + (b->u * bw - a->u * aw) * t;
        float vw = a->v * aw + (b->v * bw - a->v * aw) * t;
        float invW = reciprocal(w);
        out->rhw = w;
        out->u = uw * invW;
        out->v = vw * invW;
    }
#if 0
    int aA = (int)((a->color >> 24) & 0xFFu);
    int aR = (int)((a->color >> 16) & 0xFFu);
    int aG = (int)((a->color >>  8) & 0xFFu);
    int aB = (int)( a->color        & 0xFFu);

    int bA = (int)((b->color >> 24) & 0xFFu);
    int bR = (int)((b->color >> 16) & 0xFFu);
    int bG = (int)((b->color >>  8) & 0xFFu);
    int bB = (int)( b->color        & 0xFFu);

    int rA = aA + (int)((float)(bA - aA) * t);
    int rR = aR + (int)((float)(bR - aR) * t);
    int rG = aG + (int)((float)(bG - aG) * t);
    int rB = aB + (int)((float)(bB - aB) * t);

    if (rA < 0) {
        rA = 0;
    }
    else if (rA > 255) {
        rA = 255;
    }
    if (rR < 0) {
        rR = 0;
    }
    else if (rR > 255) {
        rR = 255;
    }
    if (rG < 0) {
        rG = 0;
    }
    else if (rG > 255) {
        rG = 255;
    }
    if (rB < 0) {
        rB = 0;
    }
    else if (rB > 255) {
        rB = 255;
    }

    out->color = ((unsigned int)rA << 24) | ((unsigned int)rR << 16) |
                 ((unsigned int)rG <<  8) |  (unsigned int)rB;
#endif
    out->color = a->color;
    out->specular = 0;
}

/* Sutherland-Hodgman screen-space scissor clip on a polygon of RenderVertex
 * (post near-plane projection) against ONE edge. Returns out vert count. */
static int grid_scissor_clip_renderv_one_edge(const RenderVertex *in, int inCount,
                                              RenderVertex *out, int edge)
{
    int outCount = 0;
    float boundary;
    switch (edge) {
        case SCISSOR_TOP:
            boundary = (float)g_clipTop;
            break;
        case SCISSOR_BOTTOM:
            boundary = (float)g_clipBottom;
            break;
        case SCISSOR_LEFT:
            boundary = (float)g_clipLeft;
            break;
        case SCISSOR_RIGHT:
            boundary = (float)g_clipRight;
            break;
        default:
            return 0;
    }
    for (int i = 0; i < inCount; i++) {
        const RenderVertex *cur = &in[i];
        const RenderVertex *nxt = &in[(i + 1) % inCount];
        float curCoord, nxtCoord;
        int curIn, nxtIn;
        switch (edge) {
            case SCISSOR_TOP:
                curCoord = cur->sy;
                nxtCoord = nxt->sy;
                curIn = (curCoord >= boundary);
                nxtIn = (nxtCoord >= boundary);
                break;
            case SCISSOR_BOTTOM:
                curCoord = cur->sy;
                nxtCoord = nxt->sy;
                curIn = (curCoord <= boundary);
                nxtIn = (nxtCoord <= boundary);
                break;
            case SCISSOR_LEFT:
                curCoord = cur->sx;
                nxtCoord = nxt->sx;
                curIn = (curCoord >= boundary);
                nxtIn = (nxtCoord >= boundary);
                break;
            case SCISSOR_RIGHT:
                curCoord = cur->sx;
                nxtCoord = nxt->sx;
                curIn = (curCoord <= boundary);
                nxtIn = (nxtCoord <= boundary);
                break;
            default:
                return 0;
        }
        if (curIn) {
            out[outCount++] = *cur;
            if (!nxtIn) {
                float t = (boundary - curCoord) / (nxtCoord - curCoord);
                grid_screen_lerp(cur, nxt, &out[outCount++], t);
            }
        }
        else if (nxtIn) {
            float t = (boundary - curCoord) / (nxtCoord - curCoord);
            grid_screen_lerp(cur, nxt, &out[outCount++], t);
        }
    }
    return outCount;
}

/* Clip against every active scissor edge. Reads g_scissorEdge — caller checks
 * != SCISSOR_NONE before invoking. out[] needs room for inCount + one vertex
 * per active edge. Returns out vert count. */
static int grid_scissor_clip_renderv(const RenderVertex *in, int inCount, RenderVertex *out)
{
    int remaining = g_cpuClipEdge & SCISSOR_EDGE_MASK;
    int edge;

    if (remaining == 0) {
        return 0;
    }

    edge = remaining & (-remaining);
    remaining &= remaining - 1;

    /* Single edge (1P/2P): straight into out[], same work as before. */
    if (remaining == 0) {
        return grid_scissor_clip_renderv_one_edge(in, inCount, out, edge);
    }

    /* Quadrant: clip against each plane in turn, ping-ponging buffers. */
    {
        RenderVertex scratch[8];
        RenderVertex *dst = scratch;
        RenderVertex *alt = out;
        int count = grid_scissor_clip_renderv_one_edge(in, inCount, dst, edge);

        while (remaining != 0 && count > 0) {
            edge = remaining & (-remaining);
            remaining &= remaining - 1;
            count = grid_scissor_clip_renderv_one_edge(dst, count, alt, edge);
            {
                RenderVertex *swap = dst;
                dst = alt;
                alt = swap;
            }
        }
        if (count > 0 && dst != out) {
            for (int i = 0; i < count; i++) {
                out[i] = dst[i];
            }
        }
        return count;
    }
}

/* File-static perimeter-order temp for the scissor pass. */
static RenderVertex s_gridScissorClipBuf[8];

#ifdef SONICR_DC
extern void GL_KeepPixels(int tpage);
extern void R_MarkTextureDirty(int tpage);

static inline void modify_grid_color(uint32_t c, uint32_t *bc, uint32_t *oc) {
    uint32_t cr = 0xFF;
    uint32_t cg = 0xFF;
    uint32_t cb = 0xFF;

    uint32_t ocr = 10;
    uint32_t ocg = 10;
    uint32_t ocb = 10;
    if (g_colorTintEnable) {
        ocr = g_gridTintR;
        ocg = g_gridTintG;
        ocb = g_gridTintB;
        if (ocr > 255) {
            ocr = 255;
        }
        if (ocg > 255) {
            ocg = 255;
        }
        if (ocb > 255) {
            ocb = 255;
        }
    }

    *bc = (c & 0xff000000) | (cr << 16) | (cg << 8) | cb;
    *oc = (ocr << 16) | (ocg << 8) | ocb;
    return;

#if 0
    cr = cr + 0x40;
    cg = cg + 0x40;
    cb = cb + 0x40;

    if (cr > 255) {
        cr = 255;
    }
    if (cg > 255) {
        cg = 255;
    }
    if (cb > 255) {
        cb = 255;
    }

    *bc = (c & 0xff000000) | (cr << 16) | (cg << 8) | cb;
    *oc = 0;
#endif
}

/* Strip-emit a perimeter-ordered RenderVertex polygon via zig-zag. Grid
 * input is already screen-projected; we only need to copy fields into
 * pvr_vertex_t in strip slot order and submit a single R_DrawPvrStrip. */
static void grid_strip_emit_pvr(const RenderVertex *poly, int n)
{
    for (int k = 0; k < n; k++) {
        int srcIdx;

        if (k < 2) {
            srcIdx = k;
        }
        else if (k & 1) {
            srcIdx = (k + 1) >> 1;
        }
        else {
            srcIdx = n - (k >> 1);
        }

        const RenderVertex *src = &poly[srcIdx];
        pvr_vertex_t *pv = &s_ptScratch[k];
        pv->flags = (k == n - 1) ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX;
        pv->x    = src->sx;
        pv->y    = src->sy;
        pv->z    = src->rhw;
        pv->u    = src->u;
        pv->v    = src->v;

        modify_grid_color(src->color, &pv->argb, &pv->oargb);
    }

    R_DrawPvrStrip(s_ptScratch, n);
}

/* ============================================================
 * Grid LOD texture (split-screen-only, DC-focused).
 *
 * 256x256: 2x2 LOD pixels per world tile. Each LOD pixel is the
 * averaged color of one 16x16 quadrant of its source tile texture,
 * excluding green-keyed transparent texels. Mostly-transparent
 * quadrants get encoded AS the green-key color so the existing 16bpp
 * tpage upload path's green-key detection writes alpha=0 to ARGB1555
 * VRAM for them — no custom polygon header or VRAM pointer needed.
 *
 * UV mapping: world tile (tX, tZ) → LOD UV (tX/128, tZ/128). With
 * 256x256 the per-tile region is 2x2 pixels, so a 0..1 UV span over
 * 128 tiles still indexes correctly (tX/128 == tX*2/256).
 *
 * Built per track via BuildGridLODTex(), called from each per-track
 * init in track_per_level.c after both D3D_LoadPlayfieldTilesRGB
 * (source tile texels) and LoadResource64K (g_tileMap) have run.
 *
 * Lives in tpage slot GRID_LOD_TPAGE. This MUST stay distinct from
 * TPAGE_PLATFORM_ICONS (slot 51 — NET01.RAW network platform icons): the two
 * features both originally claimed slot 51, so BuildGridLODTex clobbered the
 * frozen icon texture on every track load (garbage icons in network races).
 * Slot 50 is free — normal track/menu tpages top out ~18, emerald reaches 19,
 * leaving 20–50 unused. GL_KeepPixels keeps the CPU buffer persistent so
 * subsequent track loads can rebuild without reallocating. Memory:
 * 256*256*2 = 128KB. */
/* GRID_LOD_TPAGE is defined at file scope above — the grid viewport reject
 * needs it on both platforms. */

static void grid_lod_avg_quadrant(const unsigned short *src,
                                  int srcTileX, int srcTileY,
                                  int qx, int qy,
                                  unsigned short *outPixel)
{
    /* qx, qy in 0..1 select which 16x16 quadrant of the source tile. */
    int x0 = srcTileX + qx * 16;
    int y0 = srcTileY + qy * 16;
    int sumR = 0;
    int sumG = 0;
    int sumB = 0;
    int opaque = 0;
    for (int ty = 0; ty < 16; ty++) {
        const unsigned short *row = src + (y0 + ty) * 256 + x0;
        for (int tx = 0; tx < 16; tx++) {
            unsigned short p = row[tx];
            int r5 = (p >> 11) & 0x1F;
            int g5 = (p >> 6)  & 0x1F;
            int b5 = p & 0x1F;
            if (IS_COLOR_KEY_RGB5(r5, g5, b5)) {
                continue;  /* green-key (exact 0,255,0) */
            }
            sumR += r5;
            sumG += g5;
            sumB += b5;
            opaque++;
        }
    }
    if (opaque == 0 || opaque < (16 * 16 / 2)) {
        /* Mostly transparent — encode AS green-key. */
        *outPixel = (unsigned short)((0 << 11) | (31 << 6) | 0);
    } else {
        int avgR = sumR / opaque;
        int avgG = sumG / opaque;
        int avgB = sumB / opaque;
        *outPixel = (unsigned short)((avgR << 11) | (avgG << 6) | avgB);
    }
}

void BuildGridLODTex(void)
{
#if !GRID_SS_LOD_QUAD
    /* Nothing draws the LOD quad in this build, so skip building its texture.
     * Otherwise every track load spends time averaging a 256x256 image and
     * pins 128KB of VRAM in slot GRID_LOD_TPAGE that nothing ever samples.
     * Callers in track_per_level.c are unconditional by design — the decision
     * belongs here, next to the feature. */
    return;
#else
    /* The grid LOD is only ever *drawn* on DC. */
    int tpageA = g_tpageUIAlt;
    int tpageB = g_tpageUIAlt + 1;
    const unsigned short *srcA = (const unsigned short *)g_tpagePixelBuf[tpageA];
    const unsigned short *srcB = (const unsigned short *)g_tpagePixelBuf[tpageB];
    if (srcA == NULL || srcB == NULL) {
        return;
    }

    if (g_tpagePixelBuf[GRID_LOD_TPAGE] == NULL) {
        g_tpagePixelBuf[GRID_LOD_TPAGE] = malloc(256 * 256 * 2);
        if (g_tpagePixelBuf[GRID_LOD_TPAGE] == NULL) {
            return;
        }
    }
    g_tpageWidth[GRID_LOD_TPAGE]  = 256;
    g_tpageHeight[GRID_LOD_TPAGE] = 256;
    GL_KeepPixels(GRID_LOD_TPAGE);

#ifdef BLURRY
//SONICR_DC
    /* Match the playfield tile pages (texture.c D3D_LoadPlayfieldTilesRGB) so
     * LOD blocks and per-tile grid filter the same way. This one is a true
     * 256x256 image rather than an atlas, so it has no edge-bleed problem. */
    R_SetTpageFilter(GRID_LOD_TPAGE, R_FILTER_LINEAR);
#endif

    unsigned short *dst = (unsigned short *)g_tpagePixelBuf[GRID_LOD_TPAGE];

    for (int tileZ = 0; tileZ < 128; tileZ++) {
        for (int tileX = 0; tileX < 128; tileX++) {
            unsigned char tileVal = g_tileMap[tileZ * 256 + tileX];
            const unsigned short *src = (tileVal < 64) ? srcA : srcB;
            int localTile = tileVal & 0x3F;
            int srcTileX = (localTile & 7) * 32;
            int srcTileY = (localTile >> 3) * 32;

            for (int qy = 0; qy < 2; qy++) {
                for (int qx = 0; qx < 2; qx++) {
                    int dstX = tileX * 2 + qx;
                    int dstY = tileZ * 2 + qy;
                    grid_lod_avg_quadrant(src, srcTileX, srcTileY, qx, qy,
                                          &dst[dstY * 256 + dstX]);
                }
            }
        }
    }

    g_tpageStateArray[GRID_LOD_TPAGE] = 4;
    R_MarkTextureDirty(GRID_LOD_TPAGE);
#endif /* GRID_SS_LOD_QUAD */
}
#endif

/* Four-edge viewport reject for a projected grid polygon — the grid's
 * equivalent of TrackViewportReject (render_track_d3d.c:89) and
 * ScissorAllOutside (nearclip.h). Rejects when every vertex falls outside
 * the same clip edge.
 *
 * This has to be unconditional, and it has to test g_clipLeft/Top/Right/
 * Bottom rather than any scissor-edge flag. On DC g_cpuClipEdge is pinned
 * to SCISSOR_NONE because the PVR user tile clip owns the viewport edges —
 * but hardware tile clipping happens at RASTERISATION, not at binning. A
 * grid quad landing in another viewport is invisible yet still takes an
 * object-pointer entry in every tile it covers, so without this the grid
 * costs full binning pressure in all four split-screen passes. Clipping is
 * not culling; removing the CPU scissor removed the trivial rejection that
 * used to come with it.
 *
 * Reject only: compare per vertex, no lerp, no output buffer. */
static int GridViewportReject(const RenderVertex *rv, int n)
{
    int outside;

    outside = 1;
    for (int i = 0; i < n; i++) {
        if (rv[i].sx >= (float)g_clipLeft) {
            outside = 0;
            break;
        }
    }
    if (outside) {
        return 1;
    }

    outside = 1;
    for (int i = 0; i < n; i++) {
        if (rv[i].sx <= (float)g_clipRight) {
            outside = 0;
            break;
        }
    }
    if (outside) {
        return 1;
    }

    outside = 1;
    for (int i = 0; i < n; i++) {
        if (rv[i].sy >= (float)g_clipTop) {
            outside = 0;
            break;
        }
    }
    if (outside) {
        return 1;
    }

    outside = 1;
    for (int i = 0; i < n; i++) {
        if (rv[i].sy <= (float)g_clipBottom) {
            outside = 0;
            break;
        }
    }
    if (outside) {
        return 1;
    }

    return 0;
}

static int GridClipAndEmitQuad(const GridClipVert v[4], int tpage,
                              const GridProjection *gp) {
    /* Up to 6 outputs for an in-out-in-out vertex pattern. */
    GridClipVert out[6];
    int outCount = 0;

    for (int i = 0; i < 4; i++) {
        const GridClipVert *cur = &v[i];
        const GridClipVert *nxt = &v[(i + 1) & 3];
        int curIn = (cur->z >= 1.0f);
        int nxtIn = (nxt->z >= 1.0f);

        if (curIn) {
            out[outCount++] = *cur;
            if (!nxtIn) {
                /* leaving: emit intersection */
                GridClipLerp(nxt, cur, &out[outCount++]);
            }
        }
        else if (nxtIn) {
            /* entering: emit intersection (cur itself stays out) */
            GridClipLerp(cur, nxt, &out[outCount++]);
        }
    }

    if (outCount < 3) {
        return 0;
    }

    RenderVertex rv[7];   /* +1 vs near-plane-only path: scissor adds at most 1 */
    for (int i = 0; i < outCount; i++) {
        rv[i] = BuildGridClipVertex(&out[i], gp);
    }

    /* Reject before touching render state — a rejected quad should not
     * dirty the texture/tex-env and provoke a header re-emit. */
    if (GridViewportReject(rv, outCount)) {
        return 0;
    }

    R_SetTexture(tpage);
    if (g_colorTintEnable) {
        R_SetTexEnv(R_TEXENV_ADD_SIGNED);
    }
    else {
        R_SetTexEnv(R_TEXENV_MODULATE);
    }

    /* Split-screen second pass: SH-clip the screen-projected polygon
     * against the inward viewport edge. DC: zig-zag strip emit (single
     * R_DrawPvrStrip). Non-DC: fan emit (hardware glScissor would have
     * handled this anyway, but the path runs uniformly).
     *
     * Fast path: if all post-near-plane verts are already inside the
     * scissor edge, the SH-clip is a no-op — emit the existing rv[]
     * directly without copying through the scissor buffer. */
    if (g_cpuClipEdge != SCISSOR_NONE) {
        int edges = g_cpuClipEdge & SCISSOR_EDGE_MASK;
        int allIn = 1;
        for (int i = 0; i < outCount; i++) {
            float sx = rv[i].sx;
            float sy = rv[i].sy;
            if (((edges & SCISSOR_TOP)    && sy < (float)g_clipTop)    ||
                ((edges & SCISSOR_BOTTOM) && sy > (float)g_clipBottom) ||
                ((edges & SCISSOR_LEFT)   && sx < (float)g_clipLeft)   ||
                ((edges & SCISSOR_RIGHT)  && sx > (float)g_clipRight)) {
                allIn = 0;
                break;
            }
        }
        if (!allIn) {
            int n2 = grid_scissor_clip_renderv(rv, outCount, s_gridScissorClipBuf);
            if (n2 < 3) {
                return 0;
            }
#ifdef SONICR_DC
            grid_strip_emit_pvr(s_gridScissorClipBuf, n2);
#else
            R_DrawTriFan(s_gridScissorClipBuf, n2);
#endif
            return n2 - 2;
        }
        /* Fall through: all-inside, emit rv[] directly. */
    }

#ifdef SONICR_DC
    /* Use strip emit on DC regardless of scissor state — submitting the
     * SH output as a single strip primitive keeps both triangles in one
     * PVR command, which avoids the path where the second separate
     * primitive would be silently dropped (extreme rhw/coords on lerped
     * intersection vertices). */
    grid_strip_emit_pvr(rv, outCount);
    return outCount - 2;
#else
    R_DrawTriFan(rv, outCount);
    return outCount - 2;
#endif
}

#ifdef SONICR_DC
#include <kos.h>
#endif

#if GRID_SS_LOD_QUAD && defined(SONICR_DC)
/* Camera-space grid basis — the locals RenderPlayfieldGridD3D derives from
 * vpConfig at 0x45E242-0x45E295, bundled so the coarse LOD pass can be lifted
 * out of that function's body. */
typedef struct {
    float baseX, baseZ;
    float dxStep, dzStep;
    float camX, camZ;
    float camTransX, camTransY, camTransZ;
    const float *fMtx;
    int camTileCol, camTileRow;
} GridBasis;

/* Coarse LOD pass (DC split-screen). The 128x128 tile grid is covered by
 * 16x16 blocks of 8x8 tiles; every block lying entirely outside lodRadSq is
 * drawn as ONE textured quad sampled from the per-track averaged-tile texture
 * instead of 64 tiles. Runs once per frame, ahead of the per-tile loop, which
 * then skips the same blocks. Not part of the binary — a DC perf measure. */
static void GridEmitLODBlocks(const GridBasis *gb, float maxZ,
                              unsigned int baseColor, float lodRadSq)
{
    /* The coarse LOD quad is meant to reach the ORIGINAL far plane even though
     * per-tile rendering uses the reduced split-screen one — otherwise the
     * detailed tiles and the coarse ground stop at the same distance and there
     * is simply nothing past it. hud_full.c captures the pre-reduction value in
     * g_farClipFullScreenF for exactly this, but nothing ever read it, so the
     * blocks were being culled at the reduced distance and the horizon went
     * missing. Worse the more draw distance is cut.
     *
     * Falls back to the reduced value when the capture is unset (SDL, or before
     * the first viewport pass). */
    const float lodFarF = (g_farClipFullScreenF > 0.0f)
                        ? g_farClipFullScreenF : g_farClipFloat;

    /* Depth must normalise against the same far plane the blocks now reach,
     * or anything past the reduced one projects with sz > 1. */
    const float lodMaxZ = (lodFarF > g_farClipFloat)
                        ? (float)((sr_double)lodFarF + GRID_FAR_OFFSET) : maxZ;

    if (g_tpageStateArray[GRID_LOD_TPAGE] == 4) {
        GridProjection lodGP = {
            lodMaxZ,
            (float)g_projScaleXCurrent,
            (float)g_projScaleY,
            (float)g_screenCenterX,
            (float)g_screenCenterY,
            baseColor,
        };

        for (int cr = 0; cr < 16; cr++) {
            float cr0 = (float)(cr * 8) - (float)gb->camTileRow;
            float cr1 = (float)(cr * 8 + 8) - (float)gb->camTileRow;
            for (int cc = 0; cc < 16; cc++) {
                float cc0 = (float)(cc * 8) - (float)gb->camTileCol;
                float cc1 = (float)(cc * 8 + 8) - (float)gb->camTileCol;
                if ((cc0 * cc0 + cr0 * cr0) < lodRadSq &&
                    (cc1 * cc1 + cr0 * cr0) < lodRadSq &&
                    (cc0 * cc0 + cr1 * cr1) < lodRadSq &&
                    (cc1 * cc1 + cr1 * cr1) < lodRadSq)
                {
                    continue;
                }

                float worldZ_N = gb->baseZ + (float)(cr * 8) * gb->dzStep;
                float worldZ_F = gb->baseZ + (float)(cr * 8 + 8) * gb->dzStep;
                float zN = worldZ_N - gb->camZ;
                float zF = worldZ_F - gb->camZ;

                float vTX_N = zN * gb->fMtx[8]  + gb->camTransX;
                float vTY_N = zN * gb->fMtx[9]  + gb->camTransY;
                float vTZ_N = zN * gb->fMtx[10] + gb->camTransZ;
                float vTX_F = zF * gb->fMtx[8]  + gb->camTransX;
                float vTY_F = zF * gb->fMtx[9]  + gb->camTransY;
                float vTZ_F = zF * gb->fMtx[10] + gb->camTransZ;

                float worldX_L = gb->baseX + (float)(cc * 8) * gb->dxStep;
                float worldX_R = gb->baseX + (float)(cc * 8 + 8) * gb->dxStep;
                float xL = worldX_L - gb->camX;
                float xR = worldX_R - gb->camX;

                float xL_camX  = xL * gb->fMtx[0];
                float xL_camY  = xL * gb->fMtx[1];
                float xL_projZ = xL * gb->fMtx[2];
                float xR_camX  = xR * gb->fMtx[0];
                float xR_camY  = xR * gb->fMtx[1];
                float xR_projZ = xR * gb->fMtx[2];

                float z_TL = xL_projZ + vTZ_N + 2.0f;
                float z_TR = xR_projZ + vTZ_N + 2.0f;
                float z_BR = xR_projZ + vTZ_F + 2.0f;
                float z_BL = xL_projZ + vTZ_F + 2.0f;

                /* LOD block entirely past the far plane — nothing to draw. */
                if (z_TL > lodFarF && z_TR > lodFarF &&
                    z_BR > lodFarF && z_BL > lodFarF)
                {
                    continue;
                }
                if (z_TL < 1.0f && z_TR < 1.0f &&
                    z_BR < 1.0f && z_BL < 1.0f)
                {
                    continue;
                }

                float u0 = (float)(cc * 8) * 0.0078125f; // / 128.0f;
                float u1 = (float)(cc * 8 + 8) * 0.0078125f; //  / 128.0f;
                float v0 = (float)(cr * 8) * 0.0078125f; //  / 128.0f;
                float v1 = (float)(cr * 8 + 8) * 0.0078125f; //  / 128.0f;

                GridClipVert lodCorners[4] = {
                    { xL_camX + vTX_N, xL_camY + vTY_N, z_TL, u0, v0 },
                    { xR_camX + vTX_N, xR_camY + vTY_N, z_TR, u1, v0 },
                    { xR_camX + vTX_F, xR_camY + vTY_F, z_BR, u1, v1 },
                    { xL_camX + vTX_F, xL_camY + vTY_F, z_BL, u0, v1 },
                };

                GridClipAndEmitQuad(lodCorners, GRID_LOD_TPAGE, &lodGP);
            }
        }
    }
}
#endif

/**
 * RenderPlayfieldGridD3D — 0x0045E1BC — 2511 bytes
 *
 * Parameters:
 *   baseVerts  — float[2]: world X,Z of playfield grid corner 0 (top-left)
 *   upperVerts — float[2]: world X,Z of playfield grid corner 2 (bottom-right)
 *   vpConfig   — viewport config pointer (stride 0xC8 per viewport)
 */
#ifdef SONICR_DC

#include <stdint.h>
#include <math.h>
#include <dc/matrix.h>

/*
 * KOS matrix_t storage for the selector transform:
 *
 *     out0 = in0 + in2
 *     out1 = in1 + in2
 *     out2 = in1 + in3
 *     out3 = in0 + in3
 *
 * mat_trans_nodiv() maps these four results back into its four arguments.
 */
static const matrix_t s_gridSelectorMtx __attribute__((aligned(32))) = {
    { 1.0f, 0.0f, 0.0f, 1.0f },
    { 0.0f, 1.0f, 1.0f, 0.0f },
    { 1.0f, 1.0f, 0.0f, 0.0f },
    { 0.0f, 0.0f, 1.0f, 1.0f },
};

/*
 * Set this to 1 only if something called from this renderer modifies XMTRX.
 * Leaving it at 0 is the fast path: load the selector once for the whole grid.
 */
#ifndef GRID_FTRV_RELOAD_EACH_TILE
#define GRID_FTRV_RELOAD_EACH_TILE 0
#endif

/*
 * Optional behavioral optimization: if all four vertices are beyond
 * GRID_FOG_FAR, GridFogAlpha() would return zero for the entire quad.
 * Leave this off by default until you have verified that omitting a fully
 * transparent TR polygon cannot affect your depth/list behavior.
 */
#ifndef GRID_FULL_FOG_CULL
#define GRID_FULL_FOG_CULL 1
#endif

#if GRID_FTRV_RELOAD_EACH_TILE
#define GRID_FTRV_PREPARE() mat_load(&s_gridSelectorMtx)
#else
#define GRID_FTRV_PREPARE() ((void)0)
#endif

void RenderPlayfieldGridD3D(float *baseVerts, float *upperVerts, int *vpConfig)
{
    uint8_t *g_CurrentGridColorTable =
        &g_gridColorTable[((g_trackId - 1) * 120)];

    /* DC path always used white here before modify_grid_color(). */
    uint32_t baseColor = 0x00FFFFFFu;
    uint32_t offsetColor;
    modify_grid_color(baseColor, &baseColor, &offsetColor);

    const float baseX = baseVerts[0];
    const float baseZ = baseVerts[1];
    const float dxStep =
        (float)((sr_double)(upperVerts[0] - baseX) * GRID_STEP_SCALE);
    const float dzStep =
        (float)((sr_double)(upperVerts[1] - baseZ) * GRID_STEP_SCALE);

    const float camFloatX = g_camFloatX;
    const float camFloatY = g_camFloatY;
    const float camFloatZ = g_camFloatZ;

    float *fMtx = (float *)((char *)vpConfig + 0x88);

    const float negCamY = -camFloatY;
    const float camTransX = negCamY * fMtx[4];
    const float camTransY = negCamY * fMtx[5];
    const float camTransZ = negCamY * fMtx[6];

    const float farClipF = g_farClipFloat;
    const float maxZ = (float)((sr_double)farClipF + GRID_FAR_OFFSET);
    const float recipMaxZ = reciprocal(maxZ);

    /*
     * GridFogAlpha() receives biasedZ / maxZ. Convert its normalized fog
     * thresholds back into biased camera-space Z once so whole-quad fog
     * classification needs no zBuf multiplies or function calls.
     */
    const float fogNearBiasedZ = maxZ * (float)GRID_FOG_NEAR;
#if GRID_FULL_FOG_CULL
    const float fogFarBiasedZ  = maxZ * (float)GRID_FOG_FAR;
#endif

    const float projScaleXf = (float)g_projScaleXCurrent;
    const float projScaleYf = (float)g_projScaleY;
    const float centerXf = (float)g_screenCenterX;
    const float centerYf = (float)g_screenCenterY;
    const float clipLeftF = (float)g_clipLeft;
    const float clipRightF = (float)g_clipRight;
    const float clipTopF = (float)g_clipTop;
    const float clipBtmF = (float)g_clipBottom;
    const float depthBias = (float)GRID_DEPTH_BIAS;

#if GRID_SS_FOG_CULL
    /* Preserve the pre-existing GRID_SS_FOG_CULL comparison exactly. */
    const float fogFarUnbiasedZ = maxZ * (float)GRID_FOG_FAR;
#endif

    const int scissorEdge = g_scissorEdge;
    const int tpageBase = g_tpageUIAlt;

    /* Camera position in tile coordinates. */
    const int camTileCol = (int)((camFloatX - baseX) / dxStep);
    const int camTileRow = (int)((camFloatZ - baseZ) / dzStep);

    const float absDx = fabsf(dxStep);
    const float absDz = fabsf(dzStep);
    const float tileSizeMin = absDx < absDz ? absDx : absDz;
    int tileRadius = (int)(farClipF / tileSizeMin) + 2;
    if (tileRadius > 64) {
        tileRadius = 64;
    }

#if GRID_SS_LOD_QUAD
    /* Fraction of tileRadius that stays full-detail; beyond it, tiles collapse
     * to the coarse LOD quad. Lower = cheaper and blockier.
     *
     * Three buckets so 3/4-player can be tuned without disturbing 1P or 2P.
     * The 1P case is the only one with no scissor, so it is selected by
     * scissorEdge; the split cases are separated by viewport count. */
    #define GRID_LOD_PCT_1P    55   /* single viewport */
    #define GRID_LOD_PCT_2P    70   /* two viewports */
    #define GRID_LOD_PCT_MULTI 30   /* three or four — tune independently */

    const int lodPct = (g_numViewports > 2)          ? GRID_LOD_PCT_MULTI
                     : (g_numViewports == 2)  ? GRID_LOD_PCT_2P
                                                     : GRID_LOD_PCT_1P;
    const int lodRadius = tileRadius * lodPct / 100;
    const float lodRadSq = (float)(lodRadius * lodRadius);

    {
        const GridBasis gb = {
            baseX, baseZ, dxStep, dzStep, camFloatX, camFloatZ,
            camTransX, camTransY, camTransZ, fMtx,
            camTileCol, camTileRow,
        };
        GridEmitLODBlocks(&gb, maxZ, baseColor, lodRadSq);
    }

    /*
     * If the LOD tpage is usable, the original inner loop rejected every
     * fine tile outside lodRadius anyway. Shrink the iteration rectangle too.
     */
    const int fineRadius =
        (g_tpageStateArray[GRID_LOD_TPAGE] == 4) ? lodRadius : tileRadius;
#else
    const int fineRadius = tileRadius;
#endif

    const int fineRadSq = fineRadius * fineRadius;

    int rowMin = camTileRow - fineRadius;
    int rowMax = camTileRow + fineRadius;
    if (rowMin < 0) {
        rowMin = 0;
    }
    if (rowMax > 127) {
        rowMax = 127;
    }

    int colMin = camTileCol - fineRadius;
    int colMax = camTileCol + fineRadius;
    if (colMin < 0) {
        colMin = 0;
    }
    if (colMax > 127) {
        colMax = 127;
    }

    float curZ = baseZ + (float)rowMin * dzStep;

    /* These are invariant across rows. */
    const float dxBase = baseX - camFloatX;
    const float xStart = dxBase + (float)colMin * dxStep;

    /*
     * Fold camera X/Y, projection scale, screen center, biased Z, and the
     * row-dependent Z contribution into two projection numerators:
     *
     *   scrX = numeratorX / biasedZ
     *   scrY = numeratorY / biasedZ
     *
     * For X position p and row-relative dz:
     *
     *   numeratorX = p * projX_XMul + dz * projX_ZMul + projX_Const
     *   numeratorY = p * projY_XMul + dz * projY_ZMul + projY_Const
     */
    const float projX_XMul = centerXf * fMtx[2] + projScaleXf * fMtx[0];
    const float projY_XMul = centerYf * fMtx[2] - projScaleYf * fMtx[1];

    const float projX_ZMul = centerXf * fMtx[10] + projScaleXf * fMtx[8];
    const float projY_ZMul = centerYf * fMtx[10] - projScaleYf * fMtx[9];

    const float projX_Const =
        centerXf * (camTransZ + depthBias) + projScaleXf * camTransX;
    const float projY_Const =
        centerYf * (camTransZ + depthBias) - projScaleYf * camTransY;

    /* Constant regular-grid deltas used by the row/column recurrences. */
    const float zDx = dxStep * fMtx[2];
    const float zDz = dzStep * fMtx[10];

    const float projXRowStep = dzStep * projX_ZMul;
    const float projYRowStep = dzStep * projY_ZMul;
    const float projXColStep = dxStep * projX_XMul;
    const float projYColStep = dxStep * projY_XMul;

    const float clipXRowStep = dzStep * fMtx[8];
    const float clipYRowStep = dzStep * fMtx[9];

    /* Starting horizontal values are invariant across rows. */
    const float projZStart = xStart * fMtx[2];
    const float projXStart = xStart * projX_XMul;
    const float projYStart = xStart * projY_XMul;

    /*
     * If z0 is the upper-left depth, the other corners are:
     *   z1 = z0 + zDx
     *   z3 = z0 + zDz
     *   z2 = z0 + zDx + zDz
     * so these offsets classify the whole quad without first constructing
     * all four depths.
     */
    const float zMinOffset =
        (zDx < 0.0f ? zDx : 0.0f) +
        (zDz < 0.0f ? zDz : 0.0f);
    const float zMaxOffset =
        (zDx > 0.0f ? zDx : 0.0f) +
        (zDz > 0.0f ? zDz : 0.0f);

    GridProjection clipGP = {
        maxZ,
        projScaleXf,
        projScaleYf,
        centerXf,
        centerYf,
        0x00FFFFFFu,
    };

    /*
     * Preserve the caller's SH-4 matrix state. Load our selector only after
     * GridEmitLODBlocks(), so that helper sees exactly the state it saw before.
     */
    matrix_t savedXMtrx __attribute__((aligned(32)));
    mat_store(&savedXMtrx);
    mat_load(&s_gridSelectorMtx);

    /*
     * Initialize row state once. After this, every row transition is additive.
     */
    float dzCur = curZ - camFloatZ;
    float viewTransZCur = dzCur * fMtx[10] + camTransZ;
    float projXRowCur = dzCur * projX_ZMul + projX_Const;
    float projYRowCur = dzCur * projY_ZMul + projY_Const;

    /* Kept as recurrences too, although only the near-clip path consumes them. */
    float viewTransXCur = dzCur * fMtx[8] + camTransX;
    float viewTransYCur = dzCur * fMtx[9] + camTransY;

    for (int outerIdx = rowMin; outerIdx <= rowMax; ++outerIdx) {
        const float dzNext = dzCur + dzStep;
        const float viewTransZNext = viewTransZCur + zDz;
        const float projXRowNext = projXRowCur + projXRowStep;
        const float projYRowNext = projYRowCur + projYRowStep;
        const float viewTransXNext = viewTransXCur + clipXRowStep;
        const float viewTransYNext = viewTransYCur + clipYRowStep;

        const int jRowBase = outerIdx << 8;
        const int jStart = jRowBase + colMin;
        const int jLimit = jRowBase + colMax + 1;

        float xPos = xStart;

        /* Horizontal affine recurrences for Z and the two FTRV inputs. */
        float projZLeft = projZStart;
        float projXLeft = projXStart;
        float projYLeft = projYStart;

        /* Exact integer tile-distance recurrence; no per-tile float squares. */
        const int rowD = outerIdx - camTileRow;
        const int rowSq = rowD * rowD;
        const int colD0 = colMin - camTileCol;
        int colSq = colD0 * colD0;
        int colSqDelta = (colD0 << 1) + 1;

        for (int j = jStart; j < jLimit; ++j) {
            const int distSq = colSq + rowSq;
            if (distSq > fineRadSq) {
                goto next_col;
            }

            const unsigned int tileVal = g_tileMap[j];
            if (g_CurrentGridColorTable[tileVal]) {
                /*
                 * Only construct the upper-left Z initially. The constant
                 * min/max offsets classify the whole quad exactly.
                 */
                float z0 = projZLeft + viewTransZCur;
                const float zMin = z0 + zMinOffset;
                const float zMax = z0 + zMaxOffset;

                /* Original far rule: reject if ANY corner exceeds far. */
                if (zMax > farClipF) {
                    goto next_col;
                }

#if GRID_SS_FOG_CULL
                /* Original fog rule: reject if ALL corners exceed fog-far. */
                if (scissorEdge != SCISSOR_NONE && zMin > fogFarUnbiasedZ) {
                    goto next_col;
                }
#endif

                /* All four behind iff the maximum corner is behind. */
                if (zMax < 1.0f) {
                    goto next_col;
                }

                /* Some corner is behind iff the minimum corner is behind. */
                const int needsNearClip = (zMin < 1.0f);

#if GRID_FULL_FOG_CULL
                /*
                 * Exact whole-quad transparent test for the normal fog path.
                 * GridFogAlpha() receives (z + depthBias) / maxZ and returns
                 * zero when that value is > GRID_FOG_FAR.
                 */
                if (zMin + depthBias > fogFarBiasedZ) {
                    goto next_col;
                }
#endif

                const int tpageIdx = tpageBase + ((int)tileVal >> 6);
                if (g_tpageStateArray[tpageIdx] != 4) {
                    goto next_col;
                }

                /* Materialize the other depths only after cheap rejection. */
                float z1 = z0 + zDx;
                float z3 = z0 + zDz;
                float z2 = z1 + zDz;

                if (needsNearClip) {
                    const float xPosNext = xPos + dxStep;

                    GRID_FTRV_PREPARE();

                    float camX0 = xPos     * fMtx[0];
                    float camX1 = xPosNext * fMtx[0];
                    float camX2 = viewTransXCur;
                    float camX3 = viewTransXNext;
                    mat_trans_nodiv(camX0, camX1, camX2, camX3);

                    float camY0 = xPos     * fMtx[1];
                    float camY1 = xPosNext * fMtx[1];
                    float camY2 = viewTransYCur;
                    float camY3 = viewTransYNext;
                    mat_trans_nodiv(camY0, camY1, camY2, camY3);

                    float *uvPtr = &g_gridUVBuffer[tileVal * 8];
                    GridClipVert v[4] = {
                        { camX0, camY0, z0, uvPtr[0], uvPtr[1] },
                        { camX1, camY1, z1, uvPtr[2], uvPtr[3] },
                        { camX2, camY2, z2, uvPtr[4], uvPtr[5] },
                        { camX3, camY3, z3, uvPtr[6], uvPtr[7] },
                    };
                    GridClipAndEmitQuad(v, tpageIdx, &clipGP);
                    goto next_col;
                }

                /*
                 * Common path. The two FTRVs produce the four complete
                 * X and Y projection numerators directly.
                 */
                GRID_FTRV_PREPARE();

                float numX0 = projXLeft;
                float numX1 = projXLeft + projXColStep;
                float numX2 = projXRowCur;
                float numX3 = projXRowNext;
                mat_trans_nodiv(numX0, numX1, numX2, numX3);

                float numY0 = projYLeft;
                float numY1 = projYLeft + projYColStep;
                float numY2 = projYRowCur;
                float numY3 = projYRowNext;
                mat_trans_nodiv(numY0, numY1, numY2, numY3);

                /* DC requires projection W and PVR rhw to use the same bias. */
                z0 += depthBias;
                z1 += depthBias;
                z2 += depthBias;
                z3 += depthBias;

                const float invZ0 = reciprocal(z0);
                const float invZ1 = reciprocal(z1);
                const float invZ2 = reciprocal(z2);
                const float invZ3 = reciprocal(z3);

                const float scrX0 = numX0 * invZ0;
                const float scrY0 = numY0 * invZ0;
                const float scrX1 = numX1 * invZ1;
                const float scrY1 = numY1 * invZ1;
                const float scrX2 = numX2 * invZ2;
                const float scrY2 = numY2 * invZ2;
                const float scrX3 = numX3 * invZ3;
                const float scrY3 = numY3 * invZ3;

                if (scrX0 < clipLeftF && scrX1 < clipLeftF &&
                    scrX2 < clipLeftF && scrX3 < clipLeftF)
                {
                    goto next_col;
                }
                if (scrX0 > clipRightF && scrX1 > clipRightF &&
                    scrX2 > clipRightF && scrX3 > clipRightF)
                {
                    goto next_col;
                }
                if (scrY0 < clipTopF && scrY1 < clipTopF &&
                    scrY2 < clipTopF && scrY3 < clipTopF)
                {
                    goto next_col;
                }
                if (scrY0 > clipBtmF && scrY1 > clipBtmF &&
                    scrY2 > clipBtmF && scrY3 > clipBtmF)
                {
                    goto next_col;
                }

                float *uvPtr = &g_gridUVBuffer[tileVal * 8];

                unsigned int fogA0, fogA1, fogA2, fogA3;
                int is_tr;

                {
                    /*
                     * z0..z3 are already biased at this point. zMax was
                     * measured before the bias, so add it once here.
                     * If the farthest corner is still at/before fog-near,
                     * all four GridFogAlpha() calls are guaranteed to return
                     * 255 and no zBuf values are needed at all.
                     */
                    const float biasedZMax = zMax + depthBias;

                    if (biasedZMax <= fogNearBiasedZ) {
                        fogA0 = fogA1 = fogA2 = fogA3 = 255u;
                        is_tr = 0;
                    }
                    else {
                        const float zBuf0 = z0 * recipMaxZ;
                        const float zBuf1 = z1 * recipMaxZ;
                        const float zBuf2 = z2 * recipMaxZ;
                        const float zBuf3 = z3 * recipMaxZ;

                        fogA0 = (unsigned int)GridFogAlpha((sr_double)zBuf0);
                        fogA1 = (unsigned int)GridFogAlpha((sr_double)zBuf1);
                        fogA2 = (unsigned int)GridFogAlpha((sr_double)zBuf2);
                        fogA3 = (unsigned int)GridFogAlpha((sr_double)zBuf3);

                        is_tr =
                            (fogA0 < 255u || fogA1 < 255u ||
                             fogA2 < 255u || fogA3 < 255u);
                    }
                }

                R_SetTexture(tpageIdx);
                R_SetTexEnv(R_TEXENV_MODULATE);

                const int list = is_tr ? PVR_LIST_TR_POLY : PVR_LIST_PT_POLY;
                if (!R_EmitHeader(list)) {
                    goto next_col;
                }

                pvr_vertex_t *dest;
                if (is_tr) {
                    dest = R_TrVertbufTail();
                    if (!dest) {
                        goto next_col;
                    }
                }
                else {
                    dest = s_ptScratch;
                }

                /* V1, V0, V2, V3: preserves the original shared diagonal. */
                pvr_vertex_t *v = dest;

                v->flags = PVR_CMD_VERTEX;
                v->x = scrX1; v->y = scrY1; v->z = invZ1;
                v->argb = baseColor | (fogA1 << 24);
                v->oargb = offsetColor;
                v->u = uvPtr[2];
                v++->v = uvPtr[3];

                v->flags = PVR_CMD_VERTEX;
                v->x = scrX0; v->y = scrY0; v->z = invZ0;
                v->argb = baseColor | (fogA0 << 24);
                v->oargb = offsetColor;
                v->u = uvPtr[0];
                v++->v = uvPtr[1];

                v->flags = PVR_CMD_VERTEX;
                v->x = scrX2; v->y = scrY2; v->z = invZ2;
                v->argb = baseColor | (fogA2 << 24);
                v->oargb = offsetColor;
                v->u = uvPtr[4];
                v++->v = uvPtr[5];

                v->flags = PVR_CMD_VERTEX_EOL;
                v->x = scrX3; v->y = scrY3; v->z = invZ3;
                v->argb = baseColor | (fogA3 << 24);
                v->oargb = offsetColor;
                v->u = uvPtr[6];
                v->v = uvPtr[7];

#if SONICR_DC_240P
                for (int pi = 0; pi < 4; ++pi) {
                    dest[pi].x *= 0.5f;
                    dest[pi].y *= 0.5f;
                }
#endif

                if (is_tr) {
                    R_TrVertbufWritten(4 * sizeof(pvr_vertex_t));
                }
                else {
                    R_PtSubmit(dest, 4 * sizeof(pvr_vertex_t));
                }

grid_dc_done:
                ;
            }

next_col:
            xPos += dxStep;
            projZLeft += zDx;
            projXLeft += projXColStep;
            projYLeft += projYColStep;

            /* Advance (colD)^2 to (colD + 1)^2 using additions only. */
            colSq += colSqDelta;
            colSqDelta += 2;
        }

        /* Roll the already-computed next-row state forward. */
        dzCur = dzNext;
        viewTransZCur = viewTransZNext;
        projXRowCur = projXRowNext;
        projYRowCur = projYRowNext;
        viewTransXCur = viewTransXNext;
        viewTransYCur = viewTransYNext;
    }

    mat_load(&savedXMtrx);
}

#undef GRID_FTRV_PREPARE

#else

void RenderPlayfieldGridD3D(float *baseVerts, float *upperVerts, int *vpConfig)
{
    /* "color" lookup table tile grid */
    uint8_t *g_CurrentGridColorTable = &g_gridColorTable[((g_trackId - 1) * 120)];

    /* Base tile color (0xD0 = 208) with optional tinting */
    int colorR = 0xE0, colorG = 0xE0, colorB = 0xE0;                  /* 0x45E1EA: mov edx, 0xd0 */

    if (g_colorTintEnable) {                                          /* 0x45E1FA: test edi, edi */
        colorR = g_gridTintR + 0xE0;
        if (colorR > 255) {
            colorR = 255; /* 0x45E1FE-20E */
        }
        colorG = 0xE0 +  g_gridTintG;
        if (colorG > 255) {
            colorG = 255; /* 0x45E213-221 */
        }
        colorB = 0xE0 +  g_gridTintB;
        if (colorB > 255) {
            colorB = 255; /* 0x45E226-23B */
        }
    }

    unsigned int colorR16 = (unsigned int)colorR << 16;               /* 0x45E2A1: shl ebx, 0x10 */
    unsigned int colorG8  = (unsigned int)colorG << 8;                /* 0x45E2A4: shl edx, 8 */
    uint32_t baseColor = (unsigned int)colorB | colorR16 | colorG8;

    /*  Grid vertex deltas (over 128x128 grid) */
    float baseX = baseVerts[0];                                       /* 0x45E242: mov eax, [ecx] */
    float baseZ = baseVerts[1];                                       /* 0x45E24C: mov eax, [ecx+4] */
    float dxStep = (float)((sr_double)(upperVerts[0] - baseX) * GRID_STEP_SCALE); /* 0x45E25A-276 */
    float dzStep = (float)((sr_double)(upperVerts[1] - baseZ) * GRID_STEP_SCALE);

    /* Camera Y transform through float view matrix
     * Binary reads float cam position from globals at 0x6E9CA8-0x6E9CB0,
     * which are set by SetViewportClipRect from camBlock+0x30..0x38.
     * These are worldPos * (1/4096), stored as IEEE 754 floats. */
    float camFloatX = g_camFloatX;                                    /* 0x45E370: fsub [0x6E9CA8] */
    float camFloatY = g_camFloatY;                                    /* 0x45E278: fld [0x6E9CAC] */
    float camFloatZ = g_camFloatZ;                                    /* 0x45E334: fld [0x6E9CB0] */

    /* Float view matrix: vpConfig+0x88 = 4×4 float matrix (3 rows used, stride 4 per row).
     * Row 0 (m00,m01,m02): offsets 0x88, 0x8C, 0x90  → fMtx[0], [1], [2]
     * Row 1 (m10,m11,m12): offsets 0x98, 0x9C, 0xA0  → fMtx[4], [5], [6]
     * Row 2 (m20,m21,m22): offsets 0xA8, 0xAC, 0xB0  → fMtx[8], [9], [10] */
    float *fMtx = (float *)((char *)vpConfig + 0x88);

    float negCamY = -camFloatY;                                       /* 0x45E27E: fchs */
    float camTransX = negCamY * fMtx[4];                              /* 0x45E285: fmul [eax+0x98] */
    float camTransY = negCamY * fMtx[5];                              /* 0x45E28D: fmul [eax+0x9c] */
    float camTransZ = negCamY * fMtx[6];                              /* 0x45E295: fmul [eax+0xa0] */

    /* Far clip + offset for Z-buffer division */
    float maxZ = (float)((sr_double)g_farClipFloat + GRID_FAR_OFFSET); /* 0x45E29B-2B7 */
    float recipMaxZ = reciprocal(maxZ);

    /* 128×128 grid loop */
    float curZ = baseZ;                                               /* [ebp-0x84]: starts at baseZ */
    int uvBase = 0x80;                                                /* [ebp-0x88]: UV lookup base */
    int rowMin = 0;
    int rowMax = 127;

    for (int outerIdx = rowMin; outerIdx <= rowMax; ) {
        /* Compute camera-relative dz for current row */
        float dz_cur  = curZ - camFloatZ;                             /* 0x45E329-33C */

        float dzStep_eff = dzStep;
        float dxStep_eff = dxStep;

        float dz_next = (curZ + dzStep_eff) - camFloatZ;
        float dx_base = baseX - camFloatX;                            /* 0x45E36A-370 */

        /* View transform contributions from dz (row 2) + camY (row 1) */
        float viewTransX_cur  = dz_cur  * fMtx[8]  + camTransX;     /* 0x45E340+38D */
        float viewTransY_cur  = dz_cur  * fMtx[9]  + camTransY;     /* 0x45E348+395 */
        float viewTransZ_cur  = dz_cur  * fMtx[10] + camTransZ;     /* 0x45E358+39D */
        float viewTransX_next = dz_next * fMtx[8]  + camTransX;     /* 0x45E364+3C1 */
        float viewTransY_next = dz_next * fMtx[9]  + camTransY;     /* 0x45E378+3C9 */
        float viewTransZ_next = dz_next * fMtx[10] + camTransZ;     /* 0x45E385+3D1 */

        /* Inner loop: iterate columns per row */
        int j_row_base = outerIdx << 8;
        int j_start = j_row_base;                                    /* 0x45E3DF: shl eax, 8 */
        int j_limit = uvBase;                                        /* 0x45E3F9 */
        float xPos = dx_base;                                       /* [ebp-0x28] */

        for (int j = j_start; j < j_limit; j++) {
            /* Tile visibility lookup (double indirection) */
            unsigned int tileVal = g_tileMap[j];                     /* 0x45E664: mov cl, [eax+0x68b2c0] */
            if (g_CurrentGridColorTable[tileVal]) {
                /* Compute all 4 Z values upfront */
                float projZ0 = xPos * fMtx[2];
                float z0 = projZ0 + viewTransZ_cur;
                float xPosNext = xPos + dxStep_eff;                  /* 0x45E6A6-6A9 (LOD-aware) */
                float projZ1 = xPosNext * fMtx[2];                   /* 0x45E6B2 */
                float z1 = projZ1 + viewTransZ_cur;                  /* 0x45E6BB */
                float z2 = projZ1 + viewTransZ_next;
                float z3 = projZ0 + viewTransZ_next;

                /* Far clip: reject whole quad if any vertex beyond far plane */
                if (z0 > g_farClipFloat || z1 > g_farClipFloat ||
                    z2 > g_farClipFloat || z3 > g_farClipFloat)
                {
                    goto next_col;
                }

                /* Near plane classification */
                int nb0 = (z0 < 1.0f);
                int nb1 = (z1 < 1.0f);
                int nb2 = (z2 < 1.0f);
                int nb3 = (z3 < 1.0f);
                int behindCount = nb0 + nb1 + nb2 + nb3;
                if (behindCount == 4) {
                    goto next_col;
                }

                /* Tpage lookup (needed for both paths) */
                int tpageIdx = g_tpageUIAlt + ((int)tileVal >> 6);   /* 0x45E9A1-9B6 */
                if (g_tpageStateArray[tpageIdx] != 4) {
                    goto next_col;
                }

                if (behindCount > 0) {
                    /* Near-plane clipping path
                     * Split quad into 2 triangles and clip each against z=1. */
                    float camX0 = xPos     * fMtx[0] + viewTransX_cur;
                    float camX1 = xPosNext * fMtx[0] + viewTransX_cur;
                    float camX2 = xPosNext * fMtx[0] + viewTransX_next;
                    float camX3 = xPos     * fMtx[0] + viewTransX_next;

                    float camY0 = xPos     * fMtx[1] + viewTransY_cur;
                    float camY1 = xPosNext * fMtx[1] + viewTransY_cur;
                    float camY2 = xPosNext * fMtx[1] + viewTransY_next;
                    float camY3 = xPos     * fMtx[1] + viewTransY_next;

                    float *uvPtr = &g_gridUVBuffer[tileVal * 8];

                    GridProjection clipGP = {
                        maxZ,
                        (float)g_projScaleXCurrent,
                        (float)g_projScaleY,
                        (float)g_screenCenterX,
                        (float)g_screenCenterY,
                        (unsigned int)colorB | colorR16 | colorG8,
                    };

                    GridClipVert v[4] = {
                        { camX0, camY0, z0, uvPtr[0], uvPtr[1] },
                        { camX1, camY1, z1, uvPtr[2], uvPtr[3] },
                        { camX2, camY2, z2, uvPtr[4], uvPtr[5] },
                        { camX3, camY3, z3, uvPtr[6], uvPtr[7] },
                    };
                    GridClipAndEmitQuad(v, tpageIdx, &clipGP);
                    goto next_col;
                }

                /* Original unclipped path (all 4 vertices in front) */

                /* Compute 4 screen X,Y positions
                 * screenX = centerX + (camSpaceX * projScaleX) / z
                 * screenY = centerY - (camSpaceY * projScaleY) / z */
                float projScaleXf = (float)g_projScaleXCurrent;      /* 0x45E75A */
                float projScaleYf = (float)g_projScaleY;             /* 0x45E7C4 */
                float centerXf = (float)g_screenCenterX;             /* 0x45E79A */
                float centerYf = (float)g_screenCenterY;             /* 0x45E7D9 */

                float camX0 = xPos     * fMtx[0] + viewTransX_cur;   /* 0x45E748+754 */
                float camX1 = xPosNext * fMtx[0] + viewTransX_cur;   /* 0x45E76C */
                float camX2 = xPosNext * fMtx[0] + viewTransX_next;  /* +viewTransX_next */
                float camX3 = xPos     * fMtx[0] + viewTransX_next;

                float camY0 = xPos     * fMtx[1] + viewTransY_cur;   /* 0x45E7B2+7BE */
                float camY1 = xPosNext * fMtx[1] + viewTransY_cur;
                float camY2 = xPosNext * fMtx[1] + viewTransY_next;
                float camY3 = xPos     * fMtx[1] + viewTransY_next;

                float invZ0 = reciprocal(z0);
                float invZ1 = reciprocal(z1);
                float invZ2 = reciprocal(z2);
                float invZ3 = reciprocal(z3);

                float scrX0 = centerXf + camX0 * projScaleXf * invZ0; /* 0x45E7A6-7AC */
                float scrY0 = centerYf - camY0 * projScaleYf * invZ0; /* 0x45E7D6-7E5 */
                float scrX1 = centerXf + camX1 * projScaleXf * invZ1; /* 0x45E81D */
                float scrY1 = centerYf - camY1 * projScaleYf * invZ1;
                float scrX2 = centerXf + camX2 * projScaleXf * invZ2;
                float scrY2 = centerYf - camY2 * projScaleYf * invZ2;
                float scrX3 = centerXf + camX3 * projScaleXf * invZ3;
                float scrY3 = centerYf - camY3 * projScaleYf * invZ3;

                /* Screen bounds clipping (AABB rejection) */
                float clipLeftF  = (float)g_clipLeft;                 /* 0x45E87A */
                float clipRightF = (float)g_clipRight;                /* 0x45E8EA */
                float clipTopF   = (float)g_clipTop;                  /* 0x45E923 */
                float clipBtmF   = (float)g_clipBottom;               /* 0x45E95C */

                /* All 4 X < clipLeft → skip */                       /* 0x45E8B7-8E1 */
                if (scrX0 < clipLeftF && scrX1 < clipLeftF &&
                    scrX2 < clipLeftF && scrX3 < clipLeftF)
                {
                    goto next_col;
                }
                /* All 4 X > clipRight → skip */                      /* 0x45E8E7-91A */
                if (scrX0 > clipRightF && scrX1 > clipRightF &&
                    scrX2 > clipRightF && scrX3 > clipRightF)
                {
                    goto next_col;
                }
                /* All 4 Y < clipTop → skip */                        /* 0x45E920-953 */
                if (scrY0 < clipTopF && scrY1 < clipTopF &&
                    scrY2 < clipTopF && scrY3 < clipTopF)
                {
                    goto next_col;
                }
                /* All 4 Y > clipBottom → skip */                     /* 0x45E959-99B */
                if (scrY0 > clipBtmF && scrY1 > clipBtmF &&
                    scrY2 > clipBtmF && scrY3 > clipBtmF)
                {
                    goto next_col;
                }

                /* SDL: bias z AFTER projection (binary's pattern). DC
                 * already biased before the projection so rhw matches
                 * the projection W. */
                z0 += GRID_DEPTH_BIAS;                                 /* 0x45E9E6-9EE */
                z1 += GRID_DEPTH_BIAS;                                 /* 0x45E9F2 */
                z2 += GRID_DEPTH_BIAS;                                 /* 0x45E9F6 */
                z3 += GRID_DEPTH_BIAS;                                 /* 0x45E9FA */

                /* Submit quad via immediate mode */
                float *uvPtr = &g_gridUVBuffer[tileVal * 8];           /* 0x45E9D4-9DE */

                float zBuf0 = z0 * recipMaxZ;
                float zBuf1 = z1 * recipMaxZ;
                float zBuf2 = z2 * recipMaxZ;
                float zBuf3 = z3 * recipMaxZ;
                /* SDL keeps invZ at its pre-bias value (1/z, unbiased) so rhw
                 * matches the W used to project scrX/scrY. Recomputing it here
                 * from the biased z made rhw (1/(z+64)) disagree with the
                 * projection, warping/swimming the grid textures. Depth still
                 * uses the biased z via zBuf0..3 below. */

                 baseColor =  (colorR << 16) | (colorG << 8) | colorB;

                RenderVertex gridQuad[4] = {
                    { scrX0, scrY0, zBuf0, invZ0,
                      baseColor | ((unsigned int)GridFogAlpha((sr_double)zBuf0) << 24),
                      0, uvPtr[0], uvPtr[1] },
                    { scrX1, scrY1, zBuf1, invZ1,
                      baseColor | ((unsigned int)GridFogAlpha((sr_double)zBuf1) << 24),
                      0, uvPtr[2], uvPtr[3] },
                    { scrX2, scrY2, zBuf2, invZ2,
                      baseColor | ((unsigned int)GridFogAlpha((sr_double)zBuf2) << 24),
                      0, uvPtr[4], uvPtr[5] },
                    { scrX3, scrY3, zBuf3, invZ3,
                      baseColor | ((unsigned int)GridFogAlpha((sr_double)zBuf3) << 24),
                      0, uvPtr[6], uvPtr[7] },
                };

                R_SetTexture(tpageIdx);
                if (g_colorTintEnable) {
                    R_SetTexEnv(R_TEXENV_ADD_SIGNED);
                }
                else {
                    R_SetTexEnv(R_TEXENV_MODULATE);
                }
                R_DrawQuad(gridQuad);
            }

        next_col:
            xPos += dxStep_eff;                                       /* 0x45E64D (LOD-aware) */
        }

        /* Advance to next row */
        curZ += dzStep_eff;                                           /* 0x45E2FE (LOD-aware) */
        uvBase += 0x100;                                  /* 0x45E304: add ebx, 0x100 */
        outerIdx++;
    }
}
#endif