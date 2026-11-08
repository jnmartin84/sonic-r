/**
 * render_track_d3d.c — D3D track/object polygon renderer
 *
 * Iterates all objects in g_objectStructArray, transforms vertices
 * through the integer camera view matrix, projects to screen space,
 * then submits polygons to the per-tpage batch system.
 *
 * The original used D3D 2.0 execute buffers for submission; we use
 * our tpage vertex/index batch system instead.
 *
 * Object struct (stride 0x44 = 17 ints):
 *   [0]-[2]:  world position (X, Y, Z)
 *   +0x16:    pitch (>>16, mode 2 decorations)
 *   +0x18:    yaw (>>16, mode 2)
 *   +0x1A:    roll (>>16, mode 2)
 *   [8]-[10]: pivot position (mode 2)
 *   +0x2A:    bounding sphere radius (>>16, -1 = hidden)
 *   +0x2C:    mode (>>16): 0=track part, 2=decoration
 *   +0x30:    polygon start index (short)
 *   +0x32:    polygon count (short)
 *   +0x38:    vertex start index (short)
 *   +0x3A:    vertex count (short)
 *
 * Vertex format (stride 0x40 = 16 ints):
 *   [0]:    screen X (written by transform)
 *   [1]:    screen Y (written by transform)
 *   [2]:    R (13-bit fixed)
 *   [3]:    G
 *   [4]:    B
 *   [5]:    world X (+0x14)
 *   [6]:    world Y (+0x18)
 *   [7]:    world Z (+0x1C)
 *   [0xA]:  cam-space X (+0x28, written by transform)
 *   [0xB]:  cam-space Y (+0x2C, written by transform)
 *   [0xC]:  cam-space Z / depth (+0x30, written by transform)
 *
 * Polygon format (stride 0x30 = 12 ints):
 *   int[0..7]: UV pairs (16.16 fixed, 4 corners)
 *   +0x20..+0x26: 4 vertex indices (shorts)
 *   +0x28: tpage (byte)
 *   +0x2E: flags (bit 0 = quad, bit 2 = double-sided, bit 1 = alt winding)
 *
 * UV-vertex pairing (from LoadTrack3 analysis):
 *   QUADS: polyI[0,1]→+0x20, polyI[2,3]→+0x22, polyI[4,5]→+0x24, polyI[6,7]→+0x26
 *   TRIS:  polyI[0,1]→+0x24, polyI[2,3]→+0x22, polyI[4,5]→+0x20 (reversed)
 */

#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "sonicr_functions.h"
#include <math.h>
#include "r_types.h"
#include "r_state.h"
#include "r_draw.h"

#ifdef SONICR_DC
extern int R_EmitHeader(int pvr_list);
extern pvr_vertex_t *R_TrVertbufTail(void);
extern void R_TrVertbufWritten(size_t bytes);
extern void R_PtSubmit(pvr_vertex_t *v, size_t bytes);
#endif

extern void RenderHiddenSubEntry(int worldX, int worldZ, int worldY);

/* Compile-time gate between the two water-band animations:
 *   0 = FUN_0045ca28, the faithful retail D3D renderer (multi-octave
 *       vertex noise driven by g_waterScrollA-E).
 *   1 = RenderWaterBandSWRipple, prototype that keeps FUN_0045ca28's
 *       band/strip/substrip walk and UV layout but replaces the animation
 *       with the software renderer's ripple (0x4A84C8) — the look of the
 *       retail software mode. See DESIGN_water_ripple.md. */
#ifdef SONICR_XBOX
/* Xbox: use the faithful retail D3D water (what the PC version shows: the
 * mirrored sky/sun panorama rippled by the multi-octave noise). */
#define WATER_RIPPLE_SW 0
#else
#define WATER_RIPPLE_SW 1
#endif

#define recip256 0.00390625f
#define recip256_over_2 0.00195312f

/* Near-plane triangle clipper */
#include "nearclip.h"
#include "render_track_internal.h"

/* ROM double constants from binary */
#define UV_SCALE_D   5.960464477539063e-08   /* 0x52C278: 1/(256*65536) — 16.16 fixed UV to 0..1 */
#define DEPTH_SCALE  0.0001220703125          /* 0x52C280: 1/8192 */


/* Four-edge screen viewport reject — binary 0x453EED (quad) / 0x4546E9 (tri).
 * Rejects the polygon when every vertex falls outside the same clip edge.
 * Only the fast path does this; the clip path has no equivalent. */
int TrackViewportReject(SrcVertex *const *pv, int n)
{
    int outside;

    outside = 1;
    for (int i = 0; i < n; i++) {
        if (g_clipLeft <= pv[i]->screenX) {
            outside = 0;
            break;
        }
    }
    if (outside) {
        return 1;
    }

    outside = 1;
    for (int i = 0; i < n; i++) {
        if (g_clipTop <= pv[i]->screenY) {
            outside = 0;
            break;
        }
    }
    if (outside) {
        return 1;
    }

    outside = 1;
    for (int i = 0; i < n; i++) {
        if (g_clipRight >= pv[i]->screenX) {
            outside = 0;
            break;
        }
    }
    if (outside) {
        return 1;
    }

    outside = 1;
    for (int i = 0; i < n; i++) {
        if (g_clipBottom >= pv[i]->screenY) {
            outside = 0;
            break;
        }
    }
    return outside;
}


/**
 * UpdateTrackVertexColors — extracted from FUN_00442464 (software renderer)
 *
 * The software renderer (FUN_00442464, 0x4429A5-0x4429FD) computes per-vertex
 * colors from sin table lookups on the vertex position fields (+0x14/+0x18/+0x1C)
 * offset by per-frame phase globals (g_emeraldSineOff{X,Y,Z}).
 *
 * SOFTWARE-PATH FORM ONLY — currently has no callers.
 *
 * The D3D path does NOT work this way. RenderTrackD3D recolours per object,
 * inline between an object's transform loop and its poly loop (0x453B47), over
 * that object's vertices only and skipping any the transform left behind the
 * camera. That is TrackEmeraldRecolourObject in render_track_internal.h, and it
 * is what both RenderTrackD3D bodies call.
 *
 * The comment here used to justify the whole-array sweep by citing "the
 * standalone function at 0x430FEE which does the same thing for all track
 * vertices". 0x430FEE has ZERO references of any kind in the image — no call,
 * no data reference. That justification was for unreachable code.
 *
 * Kept, not deleted: it is a real fragment of the untranslated software
 * renderer, and belongs with it if that path is ever brought over.
 */
void UpdateTrackVertexColors(void)
{
    int vertCount = g_vertexIndexRunning;
    if (vertCount <= 0 || g_vertexArrayBase == NULL) {
        return;
    }

    SrcVertex *vtx = g_vertexArrayBase;
    for (int i = 0; i < vertCount; i++) {
        vtx->colorR = (g_sinTable[(vtx->posX + g_emeraldSineOffX) & 0xFFF] << 6) + 0x100020;
        vtx->colorG = (g_sinTable[(vtx->posY + g_emeraldSineOffY) & 0xFFF] << 6) + 0x100020;
        vtx->colorB = (g_sinTable[(vtx->posZ + g_emeraldSineOffZ) & 0xFFF] << 6) + 0x100020;
        vtx++;
    }
}


#ifdef SONICR_DC
/* Sutherland-Hodgman clip a sky/water quad against the active scissor edge,
 * then emit the survivor as a triangle fan. On SDL or full-screen DC the
 * helper is a one-line passthrough to R_DrawQuad. Used to keep sky/water
 * rendering inside the active viewport when DC's R_SetScissor (a no-op at
 * the PVR backend level) can't enforce it itself. */
static void RenderVertexLerpClip(const RenderVertex *a, const RenderVertex *b,
                                 RenderVertex *out, float t)
{
    out->sx = a->sx + (b->sx - a->sx) * t;
    out->sy = a->sy + (b->sy - a->sy) * t;
    out->sz = a->sz + (b->sz - a->sz) * t;
    out->rhw = a->rhw + (b->rhw - a->rhw) * t;
    out->u = a->u + (b->u - a->u) * t;
    out->v = a->v + (b->v - a->v) * t;
    out->specular = 0;
    unsigned int ca = a->color, cb = b->color;
    int aa = (ca >> 24) & 0xFF, ar = (ca >> 16) & 0xFF;
    int ag = (ca >>  8) & 0xFF, ab =  ca        & 0xFF;
    int ba = (cb >> 24) & 0xFF, br = (cb >> 16) & 0xFF;
    int bg = (cb >>  8) & 0xFF, bb =  cb        & 0xFF;
    int oa = aa + (int)((ba - aa) * t);
    int orr = ar + (int)((br - ar) * t);
    int og = ag + (int)((bg - ag) * t);
    int ob = ab + (int)((bb - ab) * t);
    out->color = ((unsigned)oa << 24) | ((unsigned)orr << 16)
               | ((unsigned)og  <<  8) |  (unsigned)ob;
}
#endif

static void EmitSkyWaterQuad(const RenderVertex q[4])
{
    /* No CPU scissor: on DC the PVR user tile clip owns the viewport edges,
     * and on SDL the GL scissor does. See PLAN_DC_HW_TILE_CLIP.md. */
    R_DrawQuad(q);
}

/**
 * RenderParallaxStripsD3D — 0x0045C444 — 1508 bytes
 * Dispatches to FUN_0045be88 (the D3D sky/parallax strip renderer).
 *
 * Original: in_EAX = per-viewport struct (0x68b210 + vp * 0x1C).
 *   +0x14 = horizon Y (from ComputeParallaxAndPlayfieldState → g_parallaxState[0])
 *   +0x18 = parallax V scroll (from ComputeParallaxAndPlayfieldState → g_parallaxState[1])
 * DAT_004fc23c == 4 always, so FUN_0045be88 is always called.
 *
 * FUN_0045be88 — 0x0045be88 — 1465 bytes
 * Renders the sky/parallax background as textured vertical strips in D3D mode.
 *
 * Pass 1: Solid-color sky quad from clipTop to (horizonY - bandHeight).
 *   Color 0xffe0e0e0u, Z=0.99999, at far depth.
 *
 * Pass 2: Textured strips from left to right, each spanning vertically
 *   from (horizonY - bandHeight) to horizonY. UVs from ROM tables
 *   cycle through 7 patterns mapping the parallax texture.
 *
 * ROM double constants (verified from DGROUP):
 *   0x52c3d8 = 1/256, 0x52c3e0 = 128.0, 0x52c3e8 = 256.0,
 *   0x52c3f0 = 0.5, 0x52c3f8 = 0.25, 0x52c400 = -1/256.
 *
 * ROM float tables:
 *   0x4fc4ec: float[14] — UV U pairs per strip (left U, right U) × 7
 *   0x4fc524: float[7]  — V base offset per strip
 */
void RenderParallaxStripsD3D(int vpIdx)
{
    /* ROM UV tables — verified from 0x004fc4ec and 0x004fc524 */
    static const float uvPairs[14] = {
        0.0f, 0.5f,  0.5f, 1.0f,  0.0f, 0.5f,  0.5f, 1.0f,
        0.0f, 0.5f,  0.5f, 1.0f,  0.0f, 0.25f
    };
    static const float vBase[7] = {
        0.0f, 0.0f, 0.25f, 0.25f, 0.5f, 0.5f, 0.75f
    };

    /* Per-viewport state, indexed exactly as the binary does: vp_struct
     * stride 0x1C (= 7 ints), +0x14=horizonY, +0x18=scrollVal. The binary
     * (FUN_0045C444) takes the vp_struct pointer as a parameter; our caller
     * just sets g_viewportIndex, so we index off that. */
    int stateOff = vpIdx * 7;
    int horizonYi = g_parallaxState[stateOff + 0];
    int scrollVal = g_parallaxState[stateOff + 1];

    /* Outer check: horizon must be below clip top (line 23810) */
#ifndef SONICR_DC
    if (g_clipTop > horizonYi) {
        return;
    }
#endif

    /* Rendering parameters (verified from disassembly) */
    int tpage = g_tpageCount;

    /* projScaleXCurrent * (scrollVal & 0xFF) — sub-pixel X offset */
    int projXtimesLow = g_projScaleXCurrent * (scrollVal & 0xFF);
    /* scrollVal / 256 — integer part selects starting strip index */
    int stripStart = scrollVal / 256;   /* signed div, matches sar+sbb+sar */

    float bandHeight = (float)((sr_double)g_projScaleY * 0.5f); // 128.0 / 256.0);
    float bandWidth = (float)((sr_double)g_projScaleXCurrent); // * 256.0 / 256.0);
    float halfBand = bandWidth * 0.5f;

    float horizonY = (float)horizonYi;
    float startX = (float)g_clipLeft - (float)projXtimesLow * recip256; // (1.0f / 256.0f);
    float curX = startX;

    /* Per-strip widths — band 6 is half-width to match its U=0..0.25 range. */
    float bandWidths[7] = { bandWidth, bandWidth, bandWidth, bandWidth,
                            bandWidth, bandWidth, halfBand };

    /* Z/RHW: binary uses the same far-depth pair for both Pass 1 and Pass 2
     * (0x45bf2c / 0x45bf51): Z=0x3F7FFF58, RHW=0x38D1B7A1. */
    #define SKY_Z   0.999988675117f   /* 0x3F7FFF58 */
#ifdef SONICR_DC
    /* PVR depth slot IS rhw, band is depth-write-off. With pvr_z_clip=0 we push
     * the backdrop ~5x farther (rhw *= 0.2, ~2e-5, just above PVR's ~1e-5 render floor) so it sits BEHIND the full-LOD
     * grid's far extent instead of slicing through it. Uniform scale on all
     * backdrop layers preserves their relative ordering. SDL unchanged. */
    #define SKY_RHW (0.0000999450f * 0.2f)   /* 0x38D1B7A1 pushed behind grid */
#else
    #define SKY_RHW 0.0000999450f     /* 0x38D1B7A1 */
#endif

    R_PushState();
    R_SetDepthWrite(0);
    R_SetBlendMode(R_BLEND_NONE);
    /* Self-contained: this used to inherit the tex-env. Push/Pop snapshots and
     * restores the whole state struct, so it protects the CALLER from us, not
     * us from the caller — and the sky is the first thing each viewport draws,
     * after the previous viewport's track and characters have already latched
     * R_TEXENV_ADD_SIGNED and never restored MODULATE. Viewport 0 got the frame
     * -start value and every later viewport got add-signed, which on the sky's
     * VERTEX_WHITE is a flat +0x49 additive lift instead of a 0.88 modulate.
     * Visible as a washed-out backdrop in the lower split-screen viewports —
     * loudest on Factory, whose night sky is dark enough to show it. The binary
     * does not apply the brightening to the sky. */
    R_SetTexEnv(R_TEXENV_MODULATE);

    /* Sky quad */
    float stripTop = horizonY - bandHeight;
    if ((float)g_clipTop <= stripTop &&
        (g_tpageStateArray[tpage] == 4 || g_tpageStateArray[tpage] == 6))
    {
        float skyL = (float)(g_clipLeft - 1);
        float skyT = (float)(g_clipTop - 1);
        float skyR = (float)(g_clipRight + 1);

        RenderVertex quad[4] = {
            { skyL, skyT,     SKY_Z, SKY_RHW, VERTEX_WHITE, 0, 0.0f,   0.0f   },
            { skyR, skyT,     SKY_Z, SKY_RHW, VERTEX_WHITE, 0, 0.001f, 0.0f   },
            { skyR, horizonY, SKY_Z, SKY_RHW, VERTEX_WHITE, 0, 0.001f, 0.001f },
            { skyL, horizonY, SKY_Z, SKY_RHW, VERTEX_WHITE, 0, 0.0f,   0.001f },
        };
        R_SetTexture(tpage);
        R_SetFilter(R_FILTER_NEAREST);
        EmitSkyWaterQuad(quad);
    }

    /* Pass 2: Textured parallax strips (lines 23658-23737) */
    if (stripTop <= (float)g_clipBottom &&
        (g_tpageStateArray[tpage] == 4 || g_tpageStateArray[tpage] == 6))
    {
        float curY = stripTop;          /* top Y of ground strips */
        int si = stripStart;            /* strip pattern index */
        curX = startX;

        /* Set the filter here too, not just in the sky quad above. The two
         * passes have independent guards — pass 1 needs g_clipTop <= stripTop,
         * this one needs stripTop <= g_clipBottom — so when the band top sits
         * above the viewport top, pass 1 is skipped and this pass used to draw
         * with whatever filter it inherited. Short split-screen viewports make
         * that ordering common. The parallax art is a folded atlas and must be
         * point-sampled; see project_dc_parallax_revert_intent. */
        R_SetTexture(tpage);
        R_SetFilter(R_FILTER_NEAREST);

        do {
            float rightX = curX + bandWidths[si];

            float uL = uvPairs[si * 2];
            float uR = uvPairs[si * 2 + 1];
            float vTop    = vBase[si] + 0.25f + -recip256; // (-1.0f / 256.0f);
            float vBottom = vBase[si] + recip256; // (1.0f / 256.0f);

            RenderVertex quad[4] = {
                { curX,   horizonY, SKY_Z, SKY_RHW, VERTEX_WHITE, 0, uL, vTop    },
                { curX,   curY,     SKY_Z, SKY_RHW, VERTEX_WHITE, 0, uL, vBottom },
                { rightX, curY,     SKY_Z, SKY_RHW, VERTEX_WHITE, 0, uR, vBottom },
                { rightX, horizonY, SKY_Z, SKY_RHW, VERTEX_WHITE, 0, uR, vTop    },
            };
            EmitSkyWaterQuad(quad);

            curX += bandWidths[si];
            si++;
            if (si == 7) {
                si = 0;
            }
        } while (curX <= (float)g_clipRight);
    }

    R_PopState();
    #undef SKY_Z
    #undef SKY_RHW
}

/* Water scroll phase counters — 0x4FC594..0x4FC5A4 */
extern int g_waterScrollA;
extern int g_waterScrollB;
extern int g_waterScrollC;
extern int g_waterScrollD;
extern int g_waterScrollE;

#if !WATER_RIPPLE_SW
/* Water reflection color gradient — ROM at 0x4FC5A8 (9 ARGB entries) */
static const unsigned int s_waterColors[9] = {
    0xFF3F5F7Fu, 0xFF4F6F8Fu, 0xFF5F7F9Fu, 0xFF6F8FAFu,
    0xFF7F9FBFu, 0xFF8FAFCFu, 0xFF9FBFDFu, 0xFFAFCFEFu,
    0xFFBFDFFFu
};
#endif

/* Final Z/RHW constants the binary stamps into every water-reflection
 * vertex (single-tpage path). Disasm: 0x3F7FFEB0 → 0.999979972839f and
 * 0x38D1B82A → 0.0001000019f. The bottom-edge fill uses slightly
 * different constants — see the BOTTOM_FILL_* defines below. */
#define WATER_VERT_Z          0.999979972839f   /* 0x3F7FFEB0 */
#define BOTTOM_FILL_Z         0.999988675117f   /* 0x3F7FFF58 */
#ifdef SONICR_DC
/* PVR depth slot IS rhw, reflection is depth-write-off. With pvr_z_clip=0 push
 * the water backdrop ~5x farther (rhw *= 0.2, ~2e-5, just above PVR's ~1e-5 render floor) so it sits BEHIND the full-LOD
 * grid instead of slicing through it. Uniform scale keeps band-in-front-of-fill
 * ordering (band 1.000019e-4 > fill 0.999450e-4). SDL unchanged. */
#define WATER_VERT_RHW        (0.0001000019f * 0.2f)   /* 0x38D1B82A pushed behind grid */
#define BOTTOM_FILL_RHW       (0.0000999450f * 0.2f)   /* 0x38D1B7A1 pushed behind grid */
#else
#define WATER_VERT_RHW        0.0001000019f      /* 0x38D1B82A */
#define BOTTOM_FILL_RHW       0.0000999450f      /* 0x38D1B7A1 */
#endif

/* WATER_RIPPLE_SW only: extra vertical squash on top of the faithful
 * software mapping (one screen line per reflection-buffer row, hScale rows
 * total — 256 lines at 480p). 1.0 = faithful to the 1998 EXE. */
#define WR_REFL_COMPRESS 1.0f

#if !WATER_RIPPLE_SW
/**
 * FUN_0045ca28 — 0x0045CA28 — 2987 bytes
 * Renders water reflection as textured quad strips below the horizon.
 * Uses multi-octave sine-wave noise for ripple displacement and a
 * blue vertex color gradient. Called when g_d3dSurfaceMode == 4 (always).
 *
 * ROM UV tables (from DGROUP):
 *   0x4FC5CC: float[7]  — U coordinates per strip pattern
 *   0x4FC5E8: float[7]  — V base offset per strip pattern
 *   0x4FC604: float[9]  — V height per sub-strip (U step = 0..0.5)
 *   0x4FC628: float[9]  — V half-height per row (for reflected V)
 *
 * ROM double constants: 0x52C430..0x52C478 (identical to 0x52C480..0x52C4C4)
 */
static void FUN_0045ca28(int vpIdx)
{
    /* ROM UV tables — copied to stack in the binary at 0x45ca3c-0x45ca90.
     * uvU and vBase are indexed by a per-row "band" that walks 0..6.
     * vHeight steps the U axis across 8 substrips per band; vHalfHeight
     * steps the V axis across the 8 vertical strips. */
    static const float uvU[7]  = { 0.0f, 0.5f, 0.0f, 0.5f, 0.0f, 0.5f, 0.0f }; /* 0x4FC5CC */
    static const float vBase[7] = { 0.0f, 0.0f, 0.25f, 0.25f, 0.5f, 0.5f, 0.75f }; /* 0x4FC5E8 */
    static const float vHeight[9] = { 0.0f, 0.0625f, 0.125f, 0.1875f, 0.25f,
                                          0.3125f, 0.375f, 0.4375f, 0.5f };          /* 0x4FC604 */
    static const float vHalfHeight[9] = { 0.0f, 0.03125f, 0.0625f, 0.09375f, 0.125f,
                                          0.15625f, 0.1875f, 0.21875f, 0.25f };      /* 0x4FC628 */


    int tpage = g_tpageCount;                                       /* 0x45ca72 */

    /* Early exit if tpage not ready (0x45ca8a-95) */
    if (g_tpageStateArray[tpage] != 4) {
        return;
    }

    /* Screen scale — adjusts ripple amplitude for viewport size (0x45ca9b-cc7) */
    float screenScale;
    if (g_numHumans == 2 && g_viewportIndex == 1) {                           /* 0x45caa4 */
        screenScale = (float)((sr_double)g_dispHalfHeight * (1.0 / 480.0));  /* 0x45caad fild [0x6e9884] */    /* 0x52C438 */
    }
    else {
        screenScale = (float)((sr_double)g_screenWidthFull * (1.0 / 640.0));  /* 0x52C430 */
    }

    /* Update scroll counters (skip if paused or snow) — 0x45cacd-b6e */
    if (g_isPaused == 0 && g_weatherType != WEATHER_SNOW) {
#ifdef SONICR_XBOX
        /* The PC runs this once per rendered frame, and its D3D renderer
         * draws faster than the 30 fps game logic, so the ripples move about
         * twice as fast there. Step twice per frame to match. */
        for (int rep = 0; rep < 2; rep++)
#endif
        {
        g_waterScrollA = (g_waterScrollA + 0x13)  & 0xFFF;
        g_waterScrollB = (g_waterScrollB - 0x22)  & 0xFFF;
        g_waterScrollC = (g_waterScrollC + 0x19)  & 0xFFF;
        g_waterScrollD = (g_waterScrollD - 0x2A)  & 0xFFF;
        g_waterScrollE = (g_waterScrollE + 0x1B)  & 0xFFF;
        }
    }

    int sA = g_waterScrollA;
    int sB = g_waterScrollB;
    int sC = g_waterScrollC;
    int sD = g_waterScrollD;
    int sE = g_waterScrollE;

    /* Noise displacement tables — 0x45cb96-cd04.
     * Binary writes 8 freq × 4 sp = 32 stores at indices 5..36 (off-by-one
     * relative to a "natural" 0..31 layout — the inner loop pre-increments
     * the byte offset before the first store). Indices 0..3 are explicitly
     * zeroed. Index 4 is never written (the binary leaves it as whatever
     * stack value happened to be there; effectively zero on first call).
     * Index 36 is the OOB store the binary makes silently — we just give
     * the array room for it.
     *
     * Consumer at the inner-loop body indexes via `1 + strip*4 + (sub&3)`
     * where strip=0..7 and sub=0..7, so it reads indices 1..32. */
    float noiseY[37], noiseX[37];
    for (int i = 0; i < 4; i++) {
        noiseX[i] = 0.0f;
        noiseY[i] = 0.0f;
    }
    noiseX[4] = 0.0f;
    noiseY[4] = 0.0f;   /* binary leaves uninitialized; we zero for determinism */

    float curScale = 0.2f;                                      /* 0x3e4ccccd */
    for (int freq = 0; freq < 8; freq++) {
        float amplitude = screenScale * curScale;
        for (int sp = 0; sp < 4; sp++) {
            sA = (sA + 0x190) & 0xFFF;                          /* 0x45cc30 */
            sB = (sB - 0x385) & 0xFFF;                          /* 0x45cc35 */
            sC = (sC + 0x58D) & 0xFFF;                          /* 0x45cc56 */
            int hash1 = ((sA << 2) + (sB << 1) + sC * 3) & 0xFFF;
            int sinVal1 = g_sinTable[hash1] / 2048;

            sD = (sD - 0x4B)  & 0xFFF;                          /* 0x45cc94 */
            sE = (sE + 0x1EA) & 0xFFF;                          /* 0x45cc8e */
            int hash2 = ((sE << 2) + sD) & 0xFFF;
            int sinVal2 = g_sinTable[hash2] / 2048;

            /* Binary off-by-one: stores at byte offset (freq+1)*16 + (sp+1)*4
             * → float index (freq+1)*4 + sp + 1. */
            int idx = (freq + 1) * 4 + sp + 1;
            noiseY[idx] = (float)sinVal1 * amplitude;
            noiseX[idx] = (float)sinVal2 * amplitude;
        }
        curScale += 0.1f;                                       /* 0x52C46C */
    }

    /* Per-viewport state from ComputeParallaxAndPlayfieldState — 0x45cd09-15 */
    int stateOff = vpIdx * 7;
    int horizonYi = g_parallaxState[stateOff + 0];
    int scrollVal = g_parallaxState[stateOff + 1];

    if (g_clipBottom < horizonYi) {                                 /* 0x45cd18 */
        return;
    }

    /* Setup parameters — 0x45cd1e-ce35 */
    int projXtimesLow = g_projScaleXCurrent * (scrollVal & 0xFF);   /* 0x45cd27-32 */
    int stripStart = scrollVal / 256;                            /* 0x45cd4d-55: signed div */

    float bandWidth = (float)g_projScaleXCurrent;                /* 0x45cd58-7a */
    float halfBand = bandWidth * 0.5f;                          /* 0x52C458 */
    float stripHeight = (float)g_projScaleY * 0.0625f; // (1.0f / 16.0f);    /* projScaleY/16 */
    float subStripWidth = (float)g_projScaleXCurrent * 0.125f; // (1.0f / 8.0f); /* projScaleX/8 */
    float startX = (float)g_clipLeft - (float)((sr_double)projXtimesLow * recip256); // (1.0 / 256.0));
    float baseY = (float)horizonYi;

    float bandWidths[7] = { bandWidth, bandWidth, bandWidth, bandWidth,
                             bandWidth, bandWidth, halfBand };       /* 0x45cdf1-e2f */

    int bandIdx = stripStart;                                      /* [ebp-0xcc] */
    float curX    = startX;                                          /* [ebp-0x68] */

    /* Bind the folded panorama tpage (g_tpageCount), built by
     * S3D_LoadAndScaleParallax as a 512×512 fold of the 1664×128 source.
     * The binary's vBase[7] / vHalfHeight[9] V tables are designed against
     * this exact layout. */
    R_SetTexture(tpage);
    R_SetFilter(R_FILTER_NEAREST);

    float clipRightF = (float)g_clipRight;
    float halfSubStrip = subStripWidth * 0.5f;

    /* OUTER ROW LOOP — advances horizontally through the panorama via
     * bandIdx wrapping 0..6. Exits when curX runs past clipRight + half a
     * substrip width (binary: 0x45d2cc jae continues, otherwise exits). */
    for (;;) {
        /* INNER STRIP LOOP — 8 vertical strips per row, indexed by `strip`
         * 0..7. Each strip spans stripHeight pixels in Y. */
        for (int strip = 0; strip < 8; strip++) {
            /* Strip's Y bounds (0x45ce86-cc1) */
            float stripTopY = baseY + (float)strip * stripHeight;
            float stripBotY = stripTopY + stripHeight;

            /* Strip-edge V markers — 0x45ceda / 0x45d282.
             * The first strip nudges its top V by 1/256, and strip 7 nudges
             * its bottom V by the same amount, to hide texel-edge bleed at
             * the horizon and at the bottom of the V-mirrored panorama. */
            float topVMarker = (strip == 0) ? recip256 /* (1.0f / 256.0f) */ : 0.0f;
            float botVMarker = (strip == 7) ? recip256 /* (1.0f / 256.0f) */ : 0.0f;

            unsigned int colTop = s_waterColors[strip < 8 ? strip : 8];
            unsigned int colBot = s_waterColors[(strip + 1) < 8 ? (strip + 1) : 8];

            /* Per-strip texture-row V (0x45d244-77):
             *   uTop = uvU[bandIdx] + vHeight[strip]   — note: this is the
             *          binary's "U" axis, but the panorama is laid out so
             *          that vHeight[] indexes substrip horizontal positions
             *          within the band (the V coordinate of the texture);
             *          uvU is the per-band base U.
             *   vTop = vBase[bandIdx] + vHalfHeight[8 - strip]
             *
             * The inner loop adds vHeight[subStrip+1] - vHeight[subStrip]
             * per substrip and the bottom-Y vertices use vHalfHeight[8 -
             * (strip+1)] for V. We compute these inline below. */
            float uBase = uvU[bandIdx];
            float vRowTop = vBase[bandIdx] + vHalfHeight[8 - strip];
            float vRowBot = vBase[bandIdx] + vHalfHeight[8 - (strip + 1)];

            /* INNER SUBSTRIP LOOP — 8 horizontal substrips per strip,
             * indexed by `sub` 0..7. Each emits one quad. The half-band
             * (bandIdx == 6) only emits the first 4 substrips. */
            for (int sub = 0; sub < 8; sub++) {
                if (bandIdx == 6 && sub > 3) {
                    continue;             /* 0x45d23b-42 */
                }

                /* Noise indices follow the binary: 1 + strip*4 + (sub&3),
                 * i.e. 4 noise samples per strip wrapping at the strip
                 * boundary. Strip 0 reads index range 1..4 (zeros) → no
                 * displacement on the horizon row; later strips read
                 * populated entries. */
                int nIdxL = 1 + strip * 4 + (sub & 3);
                int nIdxR = 1 + strip * 4 + ((sub + 1) & 3);
                int nIdxBL = 1 + (strip + 1 < 8 ? (strip + 1) : 7) * 4 + (sub & 3);
                int nIdxBR = 1 + (strip + 1 < 8 ? (strip + 1) : 7) * 4 + ((sub + 1) & 3);

                /* X positions along the substrip — 0x45cee8-cf67.
                 * Base X is sub*subStripWidth + curX; ripple comes from the
                 * Y noise table (binary uses noiseY for X displacement and
                 * noiseX for Y displacement — names are arbitrary). */
                float baseLX = (float)sub * subStripWidth + curX;
                float baseRX = baseLX + subStripWidth;

                float xTL = baseLX + noiseY[nIdxL];
                float xTR = baseRX + noiseY[nIdxR];
                float xBL = baseLX + noiseY[nIdxBL];
                float xBR = baseRX + noiseY[nIdxBR];

                /* Off-screen substrip cull — 0x45cf6f-cfa3. Skip if quad
                 * lies entirely past clipRight or entirely before clipLeft. */
                if (xTL > clipRightF && xBR > clipRightF) {
                    continue;
                }
                if (xTR < (float)g_clipLeft && xBL < (float)g_clipLeft) {
                    continue;
                }

                /* Y positions — 0x45d0cd / 0x45d15c / 0x45d1be / 0x45d207. */
                float yTL = stripTopY + noiseX[nIdxL];
                float yTR = stripTopY + noiseX[nIdxR];
                float yBR = stripBotY + noiseX[nIdxBR];
                float yBL = stripBotY + noiseX[nIdxBL];

                /* UVs — 0x45d244-d27 + per-vertex offsets from 0x52C470
                 * (vHeight-step) and 0x52C478 (vHalfHeight-step). */
                float uL = uBase + vHeight[sub];
                float uR = uBase + vHeight[sub + 1];
                float vT = vRowTop - topVMarker;
                float vB = vRowBot + botVMarker;

                RenderVertex quad[4] = {
                    { xTL, yTL, WATER_VERT_Z, WATER_VERT_RHW, colTop, 0, uL, vT },
                    { xTR, yTR, WATER_VERT_Z, WATER_VERT_RHW, colTop, 0, uR, vT },
                    { xBR, yBR, WATER_VERT_Z, WATER_VERT_RHW, colBot, 0, uR, vB },
                    { xBL, yBL, WATER_VERT_Z, WATER_VERT_RHW, colBot, 0, uL, vB },
                };
                EmitSkyWaterQuad(quad);
            }
        }

        /* Advance to next band — 0x45d28e-2cc */
        curX += bandWidths[bandIdx];
        bandIdx++;
        if (bandIdx == 7) {
            bandIdx = 0;
        }

        /* Exit when curX has moved past the right clip plus half a
         * substrip — i.e. the next row of strips would be entirely
         * off-screen to the right. */
        if (curX - halfSubStrip > clipRightF) {
            break;
        }
    }

    /* Bottom-edge fill (0x45d378-d5c6).
     * fillTop = baseY + 8*stripHeight + min(noiseX[29..32]) — the binary
     * (0x45d2d2..0x45d363) walks strip 7's four bottom-corner Y-noise
     * samples and tracks the minimum, then adds it to (baseY+8*stripHeight).
     * The most-negative noise pulls fillTop *up* to cover the highest
     * point any strip-7 bottom corner can reach, eliminating the
     * triangular gaps where displaced strip ends don't meet a rigid Y8.
     * Color = s_waterColors[8] = 0xFFBFDFFF. */
    unsigned int fillCol = s_waterColors[8];                   /* 0x4FC5C8 */

    float left   = (float)(g_clipLeft - 1);
    float right  = (float)(g_clipRight + 1);
    float bottom = (float)(g_clipBottom + 1);

    float minBotNoise = noiseX[29];
    if (noiseX[30] < minBotNoise) {
        minBotNoise = noiseX[30];
    }
    if (noiseX[31] < minBotNoise) {
        minBotNoise = noiseX[31];
    }
    if (noiseX[32] < minBotNoise) {
        minBotNoise = noiseX[32];
    }

    float fillTop = baseY + 8.0f * stripHeight + minBotNoise;
    float third   = (bottom - fillTop) * 0.33333333f; // (1.0f / 3.0f);

    for (int q = 0; q < 3; q++) {
        float y0 = fillTop + (float)q * third;
        float y1 = (q == 2) ? bottom : (fillTop + (float)(q + 1) * third);
        RenderVertex quad[4] = {
            { left,  y0, BOTTOM_FILL_Z, BOTTOM_FILL_RHW, fillCol, 0, 0.0f,   0.0f   },
            { right, y0, BOTTOM_FILL_Z, BOTTOM_FILL_RHW, fillCol, 0, 0.001f, 0.0f   },
            { right, y1, BOTTOM_FILL_Z, BOTTOM_FILL_RHW, fillCol, 0, 0.001f, 0.001f },
            { left,  y1, BOTTOM_FILL_Z, BOTTOM_FILL_RHW, fillCol, 0, 0.0f,   0.001f },
        };
        EmitSkyWaterQuad(quad);
    }
}
#endif /* !WATER_RIPPLE_SW */

#if WATER_RIPPLE_SW
/**
 * RenderWaterBandSWRipple — PROTOTYPE, not a binary translation.
 *
 * Same band/strip/substrip walk, panorama UV layout, colors, Z/RHW and
 * bottom fill as FUN_0045ca28 (0x45CA28), but the animation is the retail
 * SOFTWARE renderer's water ripple (0x4A84C8) reconstructed in mesh space:
 *
 *   phase    = snow ? 0x4D2 (frozen = "ice")
 *                   : (0x7F - (frame & 0x7F)) << 5      frame = g_totalFrames
 *   angle(y) = (phase + linesBelowHorizon * 0x2000/hScale) & 0xFFF
 *   depth(y) = linesBelowHorizon * 0x800/hScale
 *   dx(y)    = ((sin[angle] >> 10) * (depth >> 6)) >> 4      horizontal
 *   rowOff(y)= sin[(angle*3 + 0x159) & 0xFFF] >> 11          row shimmer
 *
 * hScale is the software-mode value of 0x8FB628 ((vpH<<8)/480 capped
 * 0x100, InitTrackTextures software branch) — computed locally because our
 * port runs render-mode D3D, where g_parallaxExtraX holds the parallax
 * texture height (0x80) instead.
 *
 * Mapping choices (screen blit → geometry, judged vs reference videos):
 *   dx     → per-row horizontal VERTEX displacement (rows shear, texture
 *            stays continuous across band seams). Amplitude is zero at the
 *            horizon row, so the horizon edge stays pinned.
 *   rowOff → per-row V offset (source-row shift = texture scroll), clamped
 *            to the band's V quarter to avoid sampling the adjacent fold.
 * Both are per-row uniform, so shared strip edges stay watertight.
 */

#ifdef SONICR_DC
/* Teal depth ramp as a PVR offset color (oargb, packed 0x00RRGGBB). f runs
 * 0 at the horizon to 1 at the waterline, giving #000C18 -> #183C78. The
 * builder (0x46F6B4) never fades to zero: its accumulators (0x3FFFFF/
 * 0x7FFFFF/0xFFFFFF minus 0x30FFFF/0x60FFFF/0xC0FFFF over the art) land on
 * 565 (0,3,3) = #000C18 at the horizon row, so the water keeps a blue floor
 * even where the halved sky isn't blue. On DC this ADDS on top of the
 * band's 0x808080 modulate (specular=1 in every header), reproducing the
 * builder's (src>>1)+ramp on the opaque PT prim — no additive/TR pass. On
 * SDL the field is unused, so this is a no-op there and the GL path keeps
 * its own tint. */
static inline unsigned int WaterTealOffset(float f)
{
    static int count = 0;

    int r = (int)(/*24.0f*/18.0f * f);                  /* 0x00 -> 0x18 */
    int g = 12 + (int)(/*48.0f*/36.0f * f);             /* 0x0C -> 0x3C */
    int b = 24 + (int)(/*96.0f*/84.0f * f);             /* 0x18 -> 0x78 */
    /* 1.5x = the +25% lift fill_vertex applies to base colors (keeps the
     * tint-to-scene ratio retail-faithful under the DC brightness boost;
     * oargb bypasses fill_vertex's per-channel lift) plus a ~1.2x taste
     * boost on top. */
    r += r >> 1;
    g += g >> 1;
    b += b >> 1;
    return ((unsigned)r << 16) | ((unsigned)g << 8) | (unsigned)b;
}
#endif

static void RenderWaterBandSWRipple(int vpIdx)
{
    static const float uvU[7] = { 0.0f, 0.5f, 0.0f, 0.5f, 0.0f, 0.5f, 0.0f }; /* 0x4FC5CC */
    static const float vBase[7] = { 0.0f, 0.0f, 0.25f, 0.25f, 0.5f, 0.5f, 0.75f }; /* 0x4FC5E8 */
    static const float vHeight[9] = { 0.0f, 0.0625f, 0.125f, 0.1875f, 0.25f,
                                          0.3125f, 0.375f, 0.4375f, 0.5f };          /* 0x4FC604 */
    static const float __attribute__((unused)) vHalfHeight[9] = { 0.0f, 0.03125f, 0.0625f, 0.09375f, 0.125f,
                                          0.15625f, 0.1875f, 0.21875f, 0.25f };      /* 0x4FC628 */

    int tpage = g_tpageCount;

    if (g_tpageStateArray[tpage] != 4) {
        return;
    }
        
    /* Screen scale — same rule as FUN_0045ca28 (0x45ca9b-cc7); dx/rowOff
     * are 640x480-reference pixels and scale with the framebuffer. */
    float screenScale;
    if (g_numHumans == 2 && g_viewportIndex == 1) {
        screenScale = (float)((sr_double)g_dispHalfHeight * (1.0 / 480.0));  /* 0x45caad fild [0x6e9884] */
    }
    else {
        screenScale = (float)((sr_double)g_screenWidthFull * (1.0 / 640.0));
    }

    int stateOff = vpIdx * 7;
    int horizonYi = g_parallaxState[stateOff + 0];
    int scrollVal = g_parallaxState[stateOff + 1];

    if (g_clipBottom < horizonYi) {
        return;
    }

    /* Software-mode height scale for the per-line increments. */
    int vpH = g_viewportHeight;
    if (g_numHumans == 2 && g_viewportIndex == 0) {
        vpH = g_screenHeight;
    }
    int hScale = (vpH << 8) / 0x1E0;
    if (hScale > 0x100) {
        hScale = 0x100;
    }
    int anglePerLine = 0x2000 / hScale;
    int depthPerLine = 0x800 / hScale;

    int phaseBase;
    if (g_weatherType == WEATHER_SNOW) {
        phaseBase = 0x4D2;
    }
    else {
        phaseBase = (0x7F - (g_totalFrames & 0x7F)) << 5;
    }

    int projXtimesLow = g_projScaleXCurrent * (scrollVal & 0xFF);
    int stripStart = scrollVal / 256;

    float bandWidth = (float)g_projScaleXCurrent;
    float halfBand = bandWidth * 0.5f;
    /* The sky band spans projScaleY/2 px for the full parallax image; the
     * reflection packs the same image into WR_REFL_COMPRESS× fewer lines. */
    float reflHeight = (float)g_projScaleY * 0.5f / WR_REFL_COMPRESS;
    float subStripWidth = (float)g_projScaleXCurrent * 0.125f;
    float startX = (float)g_clipLeft - (float)((sr_double)projXtimesLow * recip256);
    float baseY = (float)horizonYi;

    float bandWidths[7] = { bandWidth, bandWidth, bandWidth, bandWidth,
                            bandWidth, bandWidth, halfBand };

    /* The band is split into 8*subRows sample rows: the row shimmer runs
     * a full period every ~85 scanlines, so the 9 strip edges of the retail
     * D3D grid alias it away entirely. 32 rows ≈ 12 samples/period.
     *
     * WR_SUBROWS is the MAXIMUM (it sizes the arrays below); the live count
     * is subRows. In split-screen we drop to WR_SUBROWS_SPLIT, because this
     * band is emitted once PER VIEWPORT and the row count multiplies
     * straight into submitted quads — ~320/viewport at 4 subrows, roughly
     * 1300/frame across four quadrants.
     *
     * Note which direction that moves us: 8 rows IS the retail D3D row
     * count. The 4x subdivision is a port addition (this whole function is
     * a prototype, not a translation), bought purely to stop the shimmer
     * aliasing. Spending it back in the split views where it reads least
     * converges on the original rather than diverging from it. */
    #define WR_SUBROWS       4      /* single-player / array bound */
    #define WR_SUBROWS_SPLIT 1      /* split-screen — retail's 8 rows */

    /* DC with 3 or 4 viewports: stop animating the water altogether and draw
     * it as ONE static quad per panorama band.
     *
     * The band is only subdivided at all to support motion. The 8 substrips
     * exist because each row shears horizontally by its own xDisp, and the
     * rows exist to sample the shimmer — but the substrips' U ranges tile
     * uBase..uBase+0.5 contiguously (vHeight[0..8]), so with no shear the
     * whole band is one rectangle with one continuous UV range. Texturing is
     * therefore IDENTICAL; the only thing lost is the movement.
     *
     * The teal depth ramp survives too: tintTop/tintBot are still evaluated
     * at the horizon and waterline ends of the single row, so the gradient
     * just interpolates across one tall quad instead of many short ones. */
#ifdef SONICR_DC
    const int staticWater = (g_numViewports > 2);
#else
    const int staticWater = 0;
#endif

    const int subRows = (g_scissorEdge != SCISSOR_NONE)
                      ? WR_SUBROWS_SPLIT : WR_SUBROWS;
    const int rowCount = staticWater ? 1 : (8 * subRows);
    float subRowHeight = reflHeight / (float)rowCount;

    /* Per-row ripple samples for the 8*subRows+1 row edges. The binary
     * maps dest line -> source row as lineIdx + 6 + rowOff (0x4a8622 adds
     * the +6 bias once before the loop), so the bias rides along in vDisp.
     * dx displaces in SCREEN pixels; rowOff is in SOURCE rows (hScale rows
     * = the whole image = 0.25 V of the fold, walked bottom-up = V falling),
     * so its conversion is independent of the on-screen squash.
     * Sized for WR_SUBROWS so the arrays are fixed regardless of subRows. */
    float xDisp[8 * WR_SUBROWS + 1], vDisp[8 * WR_SUBROWS + 1];
    for (int r = 0; r <= rowCount; r++) {
        if (staticWater) {
            /* No shear, no shimmer — but keep the constant +6 source-row
             * bias the retail blit applies (0x4a8622), so the static water
             * samples the same rows the animated water averages around. */
            xDisp[r] = 0.0f;
            vDisp[r] = 6.0f * (-0.25f / (float)hScale);
            continue;
        }
        int rows = (int)((float)r * subRowHeight);
        int angle = (phaseBase + rows * anglePerLine) & 0xFFF;
        int depth = rows * depthPerLine;
        int dx = ((g_sinTable[angle] >> 10) * (depth >> 6)) >> 4;
        int rowOff = (g_sinTable[(angle * 3 + 0x159) & 0xFFF] >> 11) + 6;
        xDisp[r] = (float)dx * screenScale;
        vDisp[r] = (float)rowOff * (-0.25f / (float)hScale);
    }

    int bandIdx = stripStart;
    float curX    = startX;

    /* Rows shift horizontally by up to ~52 reference px, so extend the
     * emitted range by a margin band on the left and widen the right-side
     * exit test — otherwise a shifted row exposes a gap at the clip edge. */
    float maxDx = 64.0f * screenScale;
    float clipLeftF  = (float)g_clipLeft;
    float clipRightF = (float)g_clipRight;
    while (curX > clipLeftF - maxDx) {
        bandIdx = (bandIdx + 6) % 7;
        curX -= bandWidths[bandIdx];
    }

#ifdef SONICR_DC
    /* Pin opaque-modulate state for the band + fill. The binary rendered
     * these opaque; without this the ~700 band quads inherit whatever
     * blend/texEnv the frame left behind (e.g. additive from an earlier
     * effect) and flood the DC TR staging buffer, dropping polys past its
     * per-frame limit — holes that vary with view.
     *
     * The MODULATE below is LOAD-BEARING, and not for brightness. It keeps
     * submission on the fill_vertex path, which does `pv->oargb = v->specular`
     * and so carries the per-row teal depth ramp built further down. The
     * add-signed path instead calls split_add_signed_color, which derives
     * oargb from the vertex colour and NEVER READS v->specular at all — so
     * inheriting add-signed here would not merely brighten the water, it would
     * silently delete the entire depth ramp. See project_dc_add_signed.
     *
     * PopState restores the ambient state after the fill. */
    R_PushState();
#endif
    R_SetTexture(tpage);
    R_SetFilter(R_FILTER_NEAREST);
#ifdef SONICR_DC
    R_SetDepthWrite(0);
    R_SetBlendMode(R_BLEND_NONE);
    R_SetTexEnv(R_TEXENV_MODULATE);
#endif

    float halfSubStrip = subStripWidth * 0.5f;

    /* The reflection buffer builder (0x46F6B4) halves every source pixel
     * ((pix>>1)&0x7BEF) and adds a teal ramp per row. Pass 1 does the
     * halving via 0x808080 modulate; the additive gradient pass below
     * restores the ramp. This tint is what makes the mirrored cloud wisps
     * read as ripple glints on dark water instead of raw sky. */
#ifdef SONICR_DC
    /* Blue bias in the MULTIPLICATIVE base color (scales with texture
     * brightness, unlike the additive tint): the 0x80 halving modulate
     * tilted toward blue — red 0x60, green 0x68, blue 0xB0. fill_vertex's
     * +25% lift takes this to an effective (0x78,0x82,0xDC) multiply.
     * Knob: widen/narrow the spread to taste; 0xFF808080 = neutral retail
     * halving (pre-lift). */
    const unsigned int colWhite = 0xFF808080u; //6068B0u;
#else
    /* Same effective multiply as DC's post-lift value, applied directly
     * (GL has no vertex-color lift). 0xFF808080 = neutral retail halving. */
    const unsigned int colWhite = 0xFF7882DCu;
#endif
    for (;;) {
        float uBase = uvU[bandIdx];
        /* V is linear from vBase+0.25 (horizon) down to vBase across the
         * band — the per-strip vHalfHeight[8-strip] table is exactly this
         * line sampled at the 9 strip edges. */
        float vMin = vBase[bandIdx] + recip256;
        float vMax = vBase[bandIdx] + 0.25f - recip256;

        for (int row = 0; row < rowCount; row++) {
            float rowTopY = baseY + (float)row * subRowHeight;
            float rowBotY = rowTopY + subRowHeight;

            float topVMarker = (row == 0) ? recip256 : 0.0f;
            float botVMarker = (row == rowCount - 1) ? recip256 : 0.0f;

            float vRowTop = vBase[bandIdx] + 0.25f * (1.0f - (float)row / (float)rowCount);
            float vRowBot = vBase[bandIdx] + 0.25f * (1.0f - (float)(row + 1) / (float)rowCount);

            /* Clamp shimmered V into the band's quarter of the fold. */
            float vT = vRowTop - topVMarker + vDisp[row];
            float vB = vRowBot + botVMarker + vDisp[row + 1];
            if (vT < vMin) {
                vT = vMin;
            }
            if (vT > vMax) {
                vT = vMax;
            }
            if (vB < vMin) {
                vB = vMin;
            }
            if (vB > vMax) {
                vB = vMax;
            }

#ifdef SONICR_DC
            /* Per-row teal depth ramp baked as offset color (adds on top of
             * the 0x808080 modulate = (src>>1)+ramp). Zero at the horizon
             * (row 0), full #183C78 at the waterline (last row). */
            unsigned int tintTop = WaterTealOffset((float)row       / (float)rowCount);
            unsigned int tintBot = WaterTealOffset((float)(row + 1) / (float)rowCount);
#else
            unsigned int tintTop = 0, tintBot = 0;
#endif

            /* Band 6 is the half-width fold — only 4 of its 8 substrips
             * exist, so it spans U uBase..uBase+0.25. */
            const int bandSubs = (bandIdx == 6) ? 4 : 8;
            const int subCount = staticWater ? 1 : 8;

            for (int sub = 0; sub < subCount; sub++) {
                if (!staticWater && bandIdx == 6 && sub > 3) {
                    continue;
                }

                /* Static: one quad spanning the whole band. Animated: one
                 * substrip. Both cover the same geometry and the same U. */
                float baseLX, baseRX, uL, uR;
                if (staticWater) {
                    baseLX = curX;
                    baseRX = curX + (float)bandSubs * subStripWidth;
                    uL = uBase + vHeight[0];
                    uR = uBase + vHeight[bandSubs];
                }
                else {
                    baseLX = (float)sub * subStripWidth + curX;
                    baseRX = baseLX + subStripWidth;
                    uL = uBase + vHeight[sub];
                    uR = uBase + vHeight[sub + 1];
                }

                float xTL = baseLX + xDisp[row];
                float xTR = baseRX + xDisp[row];
                float xBL = baseLX + xDisp[row + 1];
                float xBR = baseRX + xDisp[row + 1];

                /* Cull only when BOTH rows are past the edge — the original's
                 * diagonal-corner test (xTL/xBR) misculls partially visible
                 * quads once the rows shear apart, leaving triangular holes
                 * at the clip edges. Left x of each row is its minimum, right
                 * x its maximum, so two corners per side suffice. */
                if (xTL > clipRightF && xBL > clipRightF) {
                    continue;
                }
                if (xTR < clipLeftF && xBR < clipLeftF) {
                    continue;
                }

                RenderVertex quad[4] = {
                    { xTL, rowTopY, WATER_VERT_Z, WATER_VERT_RHW, colWhite, tintTop, uL, vT },
                    { xTR, rowTopY, WATER_VERT_Z, WATER_VERT_RHW, colWhite, tintTop, uR, vT },
                    { xBR, rowBotY, WATER_VERT_Z, WATER_VERT_RHW, colWhite, tintBot, uR, vB },
                    { xBL, rowBotY, WATER_VERT_Z, WATER_VERT_RHW, colWhite, tintBot, uL, vB },
                };
                EmitSkyWaterQuad(quad);
            }
        }

        curX += bandWidths[bandIdx];
        bandIdx++;
        if (bandIdx == 7) {
            bandIdx = 0;
        }

        if (curX - halfSubStrip - maxDx > clipRightF) {
            break;
        }
    }

    /* Bottom-edge fill — drawn BEFORE the tint pass. The quads keep the
     * retail corner-texel trick (UV 0..0.001 samples the panorama's (0,0)
     * texel = the art's top-left pixel) and modulate by the same 0x808080
     * as the reflection rows, so the fill equals "art top pixel, halved" —
     * exactly what the deepest band rows show. The tint pass below then
     * covers band + fill alike, making the seam continuous by construction
     * (the software renderer got this for free: its fill color was the
     * tinted buffer's first pixel). */
    float left   = (float)(g_clipLeft - 1);
    float right  = (float)(g_clipRight + 1);
    float bottom = (float)(g_clipBottom + 1);

    float fillTop = baseY + reflHeight;
    float third   = (bottom - fillTop) * 0.33333333f;
#ifdef SONICR_DC
    /* Fill sits below the waterline, so it carries the ramp's constant
     * max (#183C78) — same offset the deepest band row shows. */
    unsigned int tintMax = WaterTealOffset(1.0f);
#else
    unsigned int tintMax = 0;
#endif

    for (int q = 0; q < 3; q++) {
        float y0 = fillTop + (float)q * third;
        float y1 = (q == 2) ? bottom : (fillTop + (float)(q + 1) * third);
        RenderVertex quad[4] = {
            { left,  y0, BOTTOM_FILL_Z, BOTTOM_FILL_RHW, colWhite, tintMax, 0.0f,   0.0f   },
            { right, y0, BOTTOM_FILL_Z, BOTTOM_FILL_RHW, colWhite, tintMax, 0.001f, 0.0f   },
            { right, y1, BOTTOM_FILL_Z, BOTTOM_FILL_RHW, colWhite, tintMax, 0.001f, 0.001f },
            { left,  y1, BOTTOM_FILL_Z, BOTTOM_FILL_RHW, colWhite, tintMax, 0.0f,   0.001f },
        };
        EmitSkyWaterQuad(quad);
    }
    

#ifdef SONICR_DC
    /* DC bakes the teal ramp into the band+fill oargb offset above, so it
     * needs no additive overlay (which would force the water onto the TR
     * list). Just restore the ambient state pinned at the top. */
    R_PopState();
#else
    /* Depth tint — the buffer builder's per-row teal ramp (0x46F6B4):
     * (R0,G3,B3)/565 = #000C18 at the art's bottom row (horizon — the
     * ramp never reaches zero), rising to (R3,G15,B15)/565 = #183C78 at
     * the art's top row, then CONSTANT max over the fill region below
     * (the software fill color included the full tint).
     * Linear in screen Y → one gradient quad + one constant quad.
     * Scaled 1.5x to match the DC oargb path (WaterTealOffset): floor
     * #000C18 -> #001224, max #183C78 -> #245AB4.
     * GL/SDL only — DC uses the oargb path above. */
    R_PushState();
    R_SetTexture(-1);
    R_SetBlendMode(R_BLEND_ADDITIVE);
    R_SetDepthWrite(0);

    left   = (float)(g_clipLeft - 1);
    right  = (float)(g_clipRight + 1);
    float top    = baseY;
    float botY   = baseY + reflHeight;
    bottom = (float)(g_clipBottom + 1);

    RenderVertex quad[4] = {
        { left,  top,  WATER_VERT_Z, WATER_VERT_RHW, 0xFF001224u, 0, 0.0f, 0.0f },
        { right, top,  WATER_VERT_Z, WATER_VERT_RHW, 0xFF001224u, 0, 0.0f, 0.0f },
        { right, botY, WATER_VERT_Z, WATER_VERT_RHW, 0xFF245AB4u, 0, 0.0f, 0.0f },
        { left,  botY, WATER_VERT_Z, WATER_VERT_RHW, 0xFF245AB4u, 0, 0.0f, 0.0f },
    };
    EmitSkyWaterQuad(quad);

    if (bottom > botY) {
        RenderVertex quad2[4] = {
            { left,  botY,   WATER_VERT_Z, WATER_VERT_RHW, 0xFF245AB4u, 0, 0.0f, 0.0f },
            { right, botY,   WATER_VERT_Z, WATER_VERT_RHW, 0xFF245AB4u, 0, 0.0f, 0.0f },
            { right, bottom, WATER_VERT_Z, WATER_VERT_RHW, 0xFF245AB4u, 0, 0.0f, 0.0f },
            { left,  bottom, WATER_VERT_Z, WATER_VERT_RHW, 0xFF245AB4u, 0, 0.0f, 0.0f },
        };
        EmitSkyWaterQuad(quad2);
    }

    R_PopState();
#endif /* SONICR_DC */
}
#endif /* WATER_RIPPLE_SW */

/**
 * RenderWaterReflectionD3D — 0x0045D5D4 — 3048 bytes
 * Renders the below-horizon water/reflection surface using the parallax
 * panorama texture. Dispatches to FUN_0045ca28 when g_d3dSurfaceMode == 4
 * (which is always the case). The fallback path (mode != 4) is dead code.
 *
 * Original: EAX = per-viewport struct (0x68B210 + vp * 0x1C).
 *   +0x14 = horizon Y (from ComputeParallaxAndPlayfieldState → g_parallaxState[vp*7+0])
 *   +0x18 = parallax V scroll (→ g_parallaxState[vp*7+1])
 */
void RenderWaterReflectionD3D(int vpIdx)
{
    /* ROM tables for fallback path (not used when mode == 4):
     * 0x4FC64C: int[7]  — tpage offsets {0,0,1,1,2,2,3}
     * 0x4FC6A0: float[9] — V heights {0..1 in 1/8 steps}
     * 0x4FC6C4: float[9] — V half-heights {0..0.5 in 1/16 steps}
     * These are only needed by the dead fallback path. */

    if (g_d3dSurfaceMode == 4) {                                    /* 0x45d624 */
#if WATER_RIPPLE_SW
        RenderWaterBandSWRipple(vpIdx);
#else
        FUN_0045ca28(vpIdx);
#endif
        return;                                                     /* 0x45d62e: jmp epilogue */
    }

    /* Fallback path (0x45d633..0x45e1b2): dead code — g_d3dSurfaceMode is always 4.
     * Uses the same algorithm as FUN_0045ca28 but with different ROM tables
     * (0x4FC64C for multi-tpage offsets instead of single tpage) and
     * different constant addresses (0x52C480..0x52C4C4). */
}

static int Obj3D_FogAlpha(sr_double z)
{
    if (z > 0.9f) {
        return 0;
    }
    if (z <= 0.7f) {
        return 0xFF;
    }
    int raw = (int)((z + (-0.9f)) * 1275.0f);
    return (raw < 0) ? -raw : raw;
}

/*================================================================
 * RenderBalloonModelForRace — 0x0045FAFC — 4321 bytes
 * Renders the in-race balloon (s_balloonModelVtx) with Euler rotation
 * (pitch/yaw/roll). Builds combined rotation×view matrix, transforms 52
 * vertices, applies per-vertex lighting from normals, submits 34 quads to the
 * tpage batch. Sole caller is AnimateBalloons (0x46149C), which only
 * runs in Balloon submode — so this never draws anything but balloons.
 * Rings are 2D sprites and do not come through here.
 *
 * Watcom: EAX=posX, EDX=posY, EBX=posZ, ECX=pitchAngle
 *   stack: yawAngle, rollAngle, lightX, lightY, lightZ (ret 0x14)
 *================================================================ */
void RenderBalloonModelForRace(int posX, int posY, int posZ, int pitchAngle,
                         int yawAngle, int rollAngle,
                         int lightX, int lightY, int lightZ)
{
    /* Camera-relative position */
    int dx = posX - g_camIntX;
    int dy = posY - g_camIntY;
    int dz = posZ - g_camIntZ;

    /* Camera-space depth (row 2: indices 2,6,10 with stride-4) */
    int cz = (dx*g_viewMtx[2] + dy*g_viewMtx[6] + dz*g_viewMtx[10]) / 4096;
    if (((cz - 0x5F) >> 3) > g_farClipDepth) {
        return;
    }
    if (cz < -0x5F) {
        return;
    }
    if (cz < 1) {
        cz = 1;
    }

    /* Camera-space X + frustum cull (row 0: indices 0,4,8 with stride-4) */
    int cx = (dx*g_viewMtx[0] + dy*g_viewMtx[4] + dz*g_viewMtx[8]) / 4096;
    int scrCX = g_screenCenterX + (g_projScaleXCurrent * cx) / cz;
    int projRad = (0x5F * g_projScaleXCurrent) / cz;
    if (scrCX - projRad > g_clipRight || scrCX + projRad < g_clipLeft) {
        return;
    }

    /* Camera-space Y + frustum cull (row 1: indices 1,5,9 with stride-4) */
    int cy = (dx*g_viewMtx[1] + dy*g_viewMtx[5] + dz*g_viewMtx[9]) / 4096;
    int scrCY = g_screenCenterY - (g_projScaleY * cy) / cz;
    if (scrCY - projRad > g_clipBottom || scrCY + projRad < g_clipTop) {
        return;
    }

    /* Build local rotation matrix from Euler angles */
    int sinY = g_sinTable[yawAngle] >> 2;
    int cosY = g_cosTable[yawAngle] >> 2;
    int sinR = g_sinTable[rollAngle] >> 2;
    int cosR = g_cosTable[rollAngle] >> 2;
    int sinP = g_sinTable[pitchAngle] >> 2;
    int cosP = g_cosTable[pitchAngle] >> 2;

    int L00 = (cosY * cosR) >> 12;
    int L01 = (cosY * sinR) >> 12;
    int L02 = -(sinR << 12) >> 12;   /* = -sinR */
    int L10 = (cosR << 12) >> 12;    /* = cosR  */
    int L20 = (cosR * sinY) >> 12;
    int L21 = (sinR * sinY) >> 12;

    /* Apply pitch */
    int R10 = (L01 * cosP - (-sinY) * sinP) >> 12;
    int R11 = (L01 * sinP + (-sinY) * cosP) >> 12;
    int R20 = (L10 * cosP) >> 12;
    int R21 = (L10 * sinP) >> 12;
    int R30 = (L21 * cosP - cosY * sinP) >> 12;
    int R31 = (sinP * L21 + cosP * cosY) >> 12;

    /* Combined matrix F = ViewMatrix × LocalRotation (stride-4 indices) */
    int f00 = (L00*g_viewMtx[0] + R10*g_viewMtx[4] + R11*g_viewMtx[8]) >> 12;
    int f01 = (L00*g_viewMtx[1] + R10*g_viewMtx[5] + R11*g_viewMtx[9]) >> 12;
    int f02 = (L00*g_viewMtx[2] + R10*g_viewMtx[6] + R11*g_viewMtx[10]) >> 12;
    int f10 = (L02*g_viewMtx[0] + R20*g_viewMtx[4] + R21*g_viewMtx[8]) >> 12;
    int f11 = (L02*g_viewMtx[1] + R20*g_viewMtx[5] + R21*g_viewMtx[9]) >> 12;
    int f12 = (L02*g_viewMtx[2] + R20*g_viewMtx[6] + R21*g_viewMtx[10]) >> 12;
    int f20 = (L20*g_viewMtx[0] + R30*g_viewMtx[4] + R31*g_viewMtx[8]) >> 12;
    int f21 = (L20*g_viewMtx[1] + R30*g_viewMtx[5] + R31*g_viewMtx[9]) >> 12;
    int f22 = (L20*g_viewMtx[2] + R30*g_viewMtx[6] + R31*g_viewMtx[10]) >> 12;

    /* Translation (stride-4 indices) */
    int tx = ((dx*g_viewMtx[0] + dy*g_viewMtx[4] + dz*g_viewMtx[8]) >> 12) << 12;
    int ty = ((dx*g_viewMtx[1] + dy*g_viewMtx[5] + dz*g_viewMtx[9]) >> 12) << 12;
    int tz = ((dx*g_viewMtx[2] + dy*g_viewMtx[6] + dz*g_viewMtx[10]) >> 12) << 12;

    float farClipAdj = g_farClipFloat + 32.0f;

    /* Vertex transform loop: 52 vertices */
    int *vtx = s_balloonModelVtx;
    const int *nrm = s_balloonModelNormal;
    for (int vi = 0; vi < 52; vi++, vtx += 16, nrm++) {
        int vx = vtx[5], vy = vtx[6], vz = vtx[7];
        int depth = (vx*f02 + vy*f12 + vz*f22 + tz) / 4096;
        vtx[12] = depth;
        if (depth <= 0) {
            continue;
        }

        int camXv = (vx*f00 + vy*f10 + vz*f20 + tx) / 4096;
        int camYv = (vx*f01 + vy*f11 + vz*f21 + ty) / 4096;
        vtx[0] = g_screenCenterX + (camXv * g_projScaleXCurrent) / depth;
        vtx[1] = g_screenCenterY - (camYv * g_projScaleY) / depth;

        int n = *nrm;
        int r = (lightX * n) / 256 + 0x80000;
        if (r > 0x1FFFFF) {
            r = 0x1FFFFF;
        }
        vtx[2] = r;
        int g = (lightY * n) / 256 + 0x80000;
        if (g > 0x1FFFFF) {
            g = 0x1FFFFF;
        }
        vtx[3] = g;
        int b = (lightZ * n) / 256 + 0x80000;
        if (b > 0x1FFFFF) {
            b = 0x1FFFFF;
        }
        vtx[4] = b;

        vtx[12] -= 0x20;   /* depth bias */
    }

    /* Polygon loop: 34 quads */
    int *poly = s_balloonModelPoly;
    int *vBase = s_balloonModelVtx;
    for (int pi = 0; pi < 34; pi++, poly += 12) {
        int *vA = (int *)((char *)vBase + ((int)*(unsigned short *)((char *)poly + 0x20) << 6));
        int *vB = (int *)((char *)vBase + ((int)*(unsigned short *)((char *)poly + 0x22) << 6));
        int *vC = (int *)((char *)vBase + ((int)*(unsigned short *)((char *)poly + 0x24) << 6));
        int *vD = (int *)((char *)vBase + ((int)*(unsigned short *)((char *)poly + 0x26) << 6));

        if (vA[12] < 1 || (float)vA[12] > farClipAdj) {
            continue;
        }
        if (vB[12] < 1 || (float)vB[12] > farClipAdj) {
            continue;
        }
        if (vC[12] < 1 || (float)vC[12] > farClipAdj) {
            continue;
        }
        if (vD[12] < 1 || (float)vD[12] > farClipAdj) {
            continue;
        }
        unsigned char flags = *(unsigned char *)((char *)poly + 0x2E);
        if (!(flags & 4)) {
            int cross = (vA[1]-vB[1])*(vC[0]-vB[0]) - (vC[1]-vB[1])*(vA[0]-vB[0]);
            if (g_mirrorMode != 0) {
                cross = -cross;
            }
            if (cross < 0) {
                if (!(flags & 2)) {
                    continue;
                }
                int cross2 = (vA[1]-vC[1])*(vD[0]-vC[0]) - (vD[1]-vC[1])*(vA[0]-vC[0]);
                if (g_mirrorMode != 0) {
                    cross2 = -cross2;
                }
                if (cross2 < 0) {
                    continue;
                }
            }
        }

        if (g_clipLeft>vA[0] && g_clipLeft>vB[0] && g_clipLeft>vC[0] && g_clipLeft>vD[0]) {
            continue;
        }
        if (g_clipTop>vA[1] && g_clipTop>vB[1] && g_clipTop>vC[1] && g_clipTop>vD[1]) {
            continue;
        }
        if (g_clipRight<vA[0] && g_clipRight<vB[0] && g_clipRight<vC[0] && g_clipRight<vD[0]) {
            continue;
        }
        if (g_clipBottom<vA[1] && g_clipBottom<vB[1] && g_clipBottom<vC[1] && g_clipBottom<vD[1]) {
            continue;
        }

        unsigned int tpage = *(unsigned char *)((char *)poly + 0x28);

        /* Emit 4 vertices: screenXY, depth, fog+lit color, UV */
        int *vv[4] = { vA, vB, vC, vD };
        RenderVertex quad[4];
        for (int kk = 0; kk < 4; kk++) {
            int *v = vv[kk];
            float dep = (float)v[12];
            float nz = dep * reciprocal(farClipAdj);
            quad[kk].sx = (float)v[0];
            quad[kk].sy = (float)v[1];
            quad[kk].sz = nz;
            quad[kk].rhw = reciprocal(dep);
            int fog = Obj3D_FogAlpha((sr_double)nz);
            int rv = v[2] >> 13;
            int gv = v[3] >> 13;
            int bv = v[4] >> 13;
            if (g_colorTintEnable != 0) {
                rv += g_gridTintR;
                if (rv > 255) {
                    rv = 255;
                }
                gv += g_gridTintG;
                if (gv > 255) {
                    gv = 255;
                }
                bv += g_gridTintB;
                if (bv > 255) {
                    bv = 255;
                }
            }
            quad[kk].color = ((unsigned int)fog<<24)|((unsigned int)rv<<16)|((unsigned int)gv<<8)|(unsigned int)bv;
            quad[kk].specular = 0;
            quad[kk].u = g_uvLUT256[poly[kk*2] >> 16];
            quad[kk].v = g_uvLUT256[poly[kk*2+1] >> 16];
        }
        R_SetTexture(tpage);
        R_SetTexEnv(R_TEXENV_ADD_SIGNED);
        R_DrawQuad(quad);
    }
}

/*================================================================
 * SubmitGroundShadow — 0x00460BE0 — 2234 bytes
 *
 * Submits one ground-shadow quad: transforms its 4 world-space corners
 * through the view matrix, projects to screen, clips and backface-culls,
 * computes the per-vertex distance fade, and appends to the tpage batch.
 * The quad itself is built by BuildGroundShadowQuad; the sprite is the
 * 32x32 black disc at (0,48) on g_tpageCharBase.
 *
 * Two callers, two object systems: AnimateBalloons (one, over
 * g_balloonArray) and DrawPickupShadows (36, over g_objectStructArray).
 * EAX = pointer to 12 ints (4 vertices × 3 coords: X, Y, Z).
 *================================================================ */
void SubmitGroundShadow(intptr_t param)  /* EAX = vertex data pointer */
{
    int *verts = (int *)param;                                 /* 0x460BEB */
    int tpage = g_tpageCharBase;                                /* 0x460BED */
    if (g_tpageStateArray[tpage] != 4) {
        return;                  /* 0x460BF2 */
    }

    /* Transform all 4 vertices: world → camera Z (depth, stride-4) */
    int dx0 = verts[0] - g_camIntX, dy0 = verts[1] - g_camIntY, dz0 = verts[2] - g_camIntZ;
    int camZ0 = (g_viewMtx[2]*dx0 + g_viewMtx[6]*dy0 + g_viewMtx[10]*dz0) / 4096;
    if (camZ0 < 1) {
        return;
    }

    int dx2 = verts[6] - g_camIntX, dy2 = verts[7] - g_camIntY, dz2 = verts[8] - g_camIntZ;
    int camZ2 = (g_viewMtx[2]*dx2 + g_viewMtx[6]*dy2 + g_viewMtx[10]*dz2) / 4096;
    if (camZ2 < 1) {
        return;
    }

    if (((camZ0 + camZ2) >> 4) + 12 > g_farClipDepth) {
        return;  /* 0x460CBE */
    }

    int dx1 = verts[3] - g_camIntX, dy1 = verts[4] - g_camIntY, dz1 = verts[5] - g_camIntZ;
    int camZ1 = (g_viewMtx[2]*dx1 + g_viewMtx[6]*dy1 + g_viewMtx[10]*dz1) / 4096;
    if (camZ1 < 1) {
        return;
    }

    int dx3 = verts[9] - g_camIntX, dy3 = verts[10] - g_camIntY, dz3 = verts[11] - g_camIntZ;
    int camZ3 = (g_viewMtx[2]*dx3 + g_viewMtx[6]*dy3 + g_viewMtx[10]*dz3) / 4096;
    if (camZ3 < 1) {
        return;
    }

    /* Camera X/Y + screen projection for all 4 vertices (stride-4) */
    int camX0 = (g_viewMtx[0]*dx0 + g_viewMtx[4]*dy0 + g_viewMtx[8]*dz0) / 4096;
    int camY0 = (g_viewMtx[1]*dx0 + g_viewMtx[5]*dy0 + g_viewMtx[9]*dz0) / 4096;
    int sx0 = g_screenCenterX + (g_projScaleXCurrent * camX0) / camZ0;
    int sy0 = g_screenCenterY - (g_projScaleY * camY0) / camZ0;

    int camX1 = (g_viewMtx[0]*dx1 + g_viewMtx[4]*dy1 + g_viewMtx[8]*dz1) / 4096;
    int camY1 = (g_viewMtx[1]*dx1 + g_viewMtx[5]*dy1 + g_viewMtx[9]*dz1) / 4096;
    int sx1 = g_screenCenterX + (g_projScaleXCurrent * camX1) / camZ1;
    int sy1 = g_screenCenterY - (g_projScaleY * camY1) / camZ1;

    int camX2 = (g_viewMtx[0]*dx2 + g_viewMtx[4]*dy2 + g_viewMtx[8]*dz2) / 4096;
    int camY2 = (g_viewMtx[1]*dx2 + g_viewMtx[5]*dy2 + g_viewMtx[9]*dz2) / 4096;
    int sx2 = g_screenCenterX + (g_projScaleXCurrent * camX2) / camZ2;
    int sy2 = g_screenCenterY - (g_projScaleY * camY2) / camZ2;

    int camX3 = (g_viewMtx[0]*dx3 + g_viewMtx[4]*dy3 + g_viewMtx[8]*dz3) / 4096;
    int camY3 = (g_viewMtx[1]*dx3 + g_viewMtx[5]*dy3 + g_viewMtx[9]*dz3) / 4096;
    int sx3 = g_screenCenterX + (g_projScaleXCurrent * camX3) / camZ3;
    int sy3 = g_screenCenterY - (g_projScaleY * camY3) / camZ3;

    /* Screen clip: reject if ALL 4 verts outside any edge */
    if (sx0 < g_clipLeft && sx1 < g_clipLeft && sx2 < g_clipLeft && sx3 < g_clipLeft) {
        return;
    }
    if (sy0 < g_clipTop  && sy1 < g_clipTop  && sy2 < g_clipTop  && sy3 < g_clipTop)  {
        return;
    }
    if (sx0 > g_clipRight  && sx1 > g_clipRight  && sx2 > g_clipRight  && sx3 > g_clipRight)  {
        return;
    }
    if (sy0 > g_clipBottom && sy1 > g_clipBottom && sy2 > g_clipBottom && sy3 > g_clipBottom) {
        return;
    }

    /* Backface cull (with mirror mode) */
    int mcross1 = (sy0 - sy1) * (sx2 - sx1);               /* 0x4610AE */
    int mcross2 = (sy2 - sy1) * (sx0 - sx1);
    if (g_mirrorMode != 0) {
        if ((mcross2 - mcross1) < 0) {
            return;
        }
    }
    else {
        if ((mcross1 - mcross2) < 0) {
            return;
        }
    }

    /* Depth adjustment — 0x4610F0: fld [0x8fb364]; fadd [0x52c54c],
     * where the ROM float at 0x52c54c is -20.0. */
    float depthScale = g_farClipFloat - 20.0f;
    camZ0 -= 20;
    if (camZ0 < 1) {
        return;
    }
    camZ1 -= 20;
    if (camZ1 < 1) {
        return;
    }
    camZ2 -= 20;
    if (camZ2 < 1) {
        return;
    }
    camZ3 -= 20;
    if (camZ3 < 1) {
        return;
    }

    /* Per-vertex: depth, fog alpha, build RenderVertex */
    static const float uvs[4][2] = {
        {0.0f, 0.1875f}, {0.125f, 0.1875f},
        {0.125f, 0.3125f}, {0.0f, 0.3125f}
    };
    int sxArr[4] = {sx0, sx1, sx2, sx3};
    int syArr[4] = {sy0, sy1, sy2, sy3};
    int czArr[4] = {camZ0, camZ1, camZ2, camZ3};
    RenderVertex quad[4];
    int allFogged = 1;      /* every vertex alpha 0 -> whole shadow invisible */
    for (int v = 0; v < 4; v++) {
        float fz = (float)czArr[v];
        float depth = fz * reciprocal(depthScale);
        float invZ = reciprocal(fz);
        int alpha;
        if ((sr_double)depth > 0.9f) {
            alpha = 0;
        }
        else if ((sr_double)depth <= 0.7f) {
            alpha = 0x80;
        }
        else {
            /* 0x4613E3/E9: fadd [0x52c560] (-0.9), fmul [0x52c568] (1275.0) */
            int raw = (int)(((sr_double)depth - 0.9f) * 1275.0f);
            alpha = raw < 0 ? -raw : raw;
        }
        quad[v].sx = (float)sxArr[v];
        quad[v].sy = (float)syArr[v];
        quad[v].sz = depth;
        quad[v].rhw = invZ;
        quad[v].color = ((unsigned int)alpha << 24) | VERTEX_WHITE_RGB;
        quad[v].specular = 0;
        quad[v].u = uvs[v][0];
        quad[v].v = uvs[v][1];

        if (alpha != 0) {
            allFogged = 0;
        }
    }

    /* Every corner fully fogged — the quad contributes nothing. The depth
     * rejects above stop at the far plane; fog reaches 0 at 0.9x of it. */
    if (allFogged) {
        return;
    }

    R_SetTexture(tpage);
    R_SetTexEnv(R_TEXENV_MODULATE);
    R_DrawQuad(quad);
}
