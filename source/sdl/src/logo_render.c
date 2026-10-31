/**
 * logo_render.c — Logo screen rendering
 *
 * SetupD3DTexturesBegin — FUN_00438CA4 — 50 bytes
 * RenderLogoQuads       — FUN_0046154C — 727 bytes
 *
 * The Sega/TT logo is a 640×480 image split into a 4×3 vertex grid,
 * forming 6 quads (2 rows × 3 columns). Each quad maps to a separate
 * tpage starting at g_uiTexPage. The quads are submitted to D3D
 * vertex/index buffers for batched rendering.
 */

#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "sonicr_functions.h"
#include "r_types.h"
#include "r_state.h"
#include "r_draw.h"
#include "net_transport.h"

/* SetupD3DTexturesBegin — declared above */
extern void FinalizeMenuTexturesD3D(void);
extern void R_SetNoColorKey(int tpage);
extern void GL_KeepPixels(int tpage);
extern void R_MarkTextureDirty(int tpage);
extern void R_SetTpageGreen6(int tpage, int on);

/**
 * g_nextLoadTpage / g_nextLoadFilename — set by callers before invoking
 * LoadWallpaperSoftware. This replaces the Watcom EAX register convention.
 */
int g_nextLoadTpage = 0;
const char *g_nextLoadFilename = NULL;

#ifdef SONICR_XBOX
#define XMARK(s) fprintf(stderr, "  mark: %s\n", (s))
#else
#define XMARK(s) ((void)0)
#endif

static const char *s_currentTitleFile = NULL;

/* Display config */

/* Logo quad vertex grid — 0x4fc8c8
 * 12 vertices in a 4×3 grid (X, Y pairs). Extracted from EXE. */
static const int s_logoGridXY[][2] = {
    {   0,   0 }, { 256,   0 }, { 512,   0 }, { 640,   0 },  /* row 0 */
    {   0, 256 }, { 256, 256 }, { 512, 256 }, { 640, 256 },  /* row 1 */
    {   0, 480 }, { 256, 480 }, { 512, 480 }, { 640, 480 },  /* row 2 */
};

/* Logo quad index table — 0x4fc928
 * 6 quads, each defined by 4 vertex indices into s_logoGridXY. */
static const int s_logoQuadIndices[6][4] = {
    { 0, 1, 5, 4 },   /* top-left */
    { 1, 2, 6, 5 },   /* top-center */
    { 2, 3, 7, 6 },   /* top-right */
    { 4, 5, 9, 8 },   /* bottom-left */
    { 5, 6, 10, 9 },  /* bottom-center */
    { 6, 7, 11, 10 }, /* bottom-right */
};

/* UV tables — 0x4fc988, 0x4fc9a0
 * Per-quad U-max and V-max. Most are 1.0; rightmost column = 0.5 (128/256),
 * bottom row V = 0.875 (224/256). */
static const float s_logoUMax[6] = { 1.0f, 1.0f, 0.5f, 1.0f, 1.0f, 0.5f };
static const float s_logoVMax[6] = { 1.0f, 1.0f, 1.0f, 0.875f, 0.875f, 0.875f };

/* Constants from EXE .data */
static const sr_double LOGO_Z_DEPTH = 3000.0;       /* 0x52c570 */
static const sr_double LOGO_X_SCALE = 0.0015625;    /* 0x52c578 = 1/640 */
static const sr_double LOGO_Y_SCALE = 0.00208333333; /* 0x52c580 = 1/480 */

/**
 * SetupD3DTexturesBegin — 0x438CA4 — 50 bytes
 * Marks all 52 tpage slots as needing D3D texture creation (status 6);
 * ProcessTpageStates then advances the tpage state machine.
 */
void SetupD3DTexturesBegin(void)
{
    for (int i = 0; i < 0x34; i++) {
        if (i == TPAGE_PLATFORM_ICONS || i == TPAGE_DRAIN ||
            i == TPAGE_PAD448)
        {
            continue;
        }

        /* Drop every per-tpage filter pin — PORT ADDITION, not in the binary.
         *
         * A pin outlives the texture that asked for it. LoadTPageRGB clears
         * and re-applies the pin for the slot it loads, but a track that
         * never loads a given slot inherits the previous track's pin, and
         * select_header_pair() gives the override priority over the global
         * R_SetFilter — so the new owner cannot override it from the draw.
         *
         * Concretely: Island and Emerald set g_tpageUIAlt = 14, so
         * D3D_LoadPlayfieldTilesRGB pins 14 AND 15 bilinear. City's sky draws
         * from g_tpageCount = 15 and InitCity only loads 0..14, so after an
         * Island demo the City sky rendered bilinear no matter that
         * RenderParallaxStripsD3D asks for NEAREST. Normal play hid it:
         * InitOptionStuff reloads 15 with PLAYER01, and the filename hook
         * re-pins that to NEAREST. The attract loop never passes through it,
         * which is why it only ever showed up in demos.
         *
         * Clearing here rather than at each load: every track and menu init
         * calls this first, so each one starts with no pins and only the
         * loads it actually performs re-apply any. */
        R_ClearTpageFilter(i);

        /* Same shape, same reason: the chroma boost is keyed by slot index and
         * outlives the texture. ClaimTpage resets it on every load, so only a
         * slot this track never reloads can carry a stale one — which then
         * gets applied by any later upload that doesn't go through ClaimTpage
         * (a no-color-key toggle, a sub-rect update, a MarkTextureDirty).
         * Free to clear here: it's a plain array write with no upload side
         * effect, and "set before the tpage is uploaded" is exactly where we
         * are. NOT doing the same for no-color-key — R_ClearNoColorKey calls
         * PVR_UploadTpage, so blanket-clearing would re-upload stale pixels
         * for every flagged slot right before the real textures load. That
         * one is already covered by LoadTPageRGB and by the explicit
         * R_ClearNoColorKey(g_tpageCount) in InitLevel. */
        R_SetTpageSatBoost(i, 0);

        if (g_tpageStateArray[i] != 0) {
            g_tpageStateArray[i] = 6;
        }
    }
    ProcessTpageStates();
}


/* =====================================================================
 * Logo/menu texture management
 * ===================================================================== */

void FinalizeD3DTextures(void) {
    FinalizeMenuTexturesD3D();
}

/**
 * SetupMenuTexturesD3D — FUN_00438CD8 — 54 bytes
 * Marks tpages from g_uiTexPage onward as state 6 (pending reload).
 * PRESERVES tpages 0 through g_uiTexPage-1 (character model textures).
 */
void SetupMenuTexturesD3D(void)
{
    for (int tp = g_uiTexPage; tp < 52; tp++) {
        /* Skip all three reserved slots. This loop runs from g_uiTexPage
         * (15 or 17) up to 51, so unlike SetupD3DTexturesBegin's 0..51 sweep
         * it is easy to forget that it reaches them at all. Marking one state
         * 6 hands it to ProcessTpageStates → PVR_ClearAndReset, which frees
         * the VRAM and drops the state to 0, with nothing to put it back.
         *
         * TPAGE_PAD448 is loaded once at boot with no reload path.
         * TPAGE_PLATFORM_ICONS is loaded and frozen at screen_misc.c:4797 and
         * is already exempt from SetupD3DTexturesBegin and from the GL-side
         * loop at render_gl.c:737 — this was the one place the reserved+
         * frozen+reset-exempt template was not being honoured. */
        if (g_tpageStateArray[tp] != 0 &&
            tp != TPAGE_DRAIN && tp != TPAGE_PAD448 &&
            tp != TPAGE_PLATFORM_ICONS)
        {
            g_tpageStateArray[tp] = 6;
        }
    }
    ProcessTpageStates();
}

void LoadMenuBitmaps(void) {
    /* Original: EAX=g_nextLoadTpage, EDX=g_nextLoadFilename (set by caller) */
    LoadTPageRGB(g_nextLoadTpage, g_nextLoadFilename);
}

/**
 * LoadTitleTextureD3D — 0x0042C3EC — 657 bytes
 * Loads a 640x480 RAW image and splits it into 6 x 256x256 tpages.
 */
void SetTitleTextureFile(const char *path) {
    s_currentTitleFile = path;
}

void LoadTitleTextureD3D(void)
{
    DebugLog("D3D_LoadWallpaper\n");

    /* Allocate tpage surfaces for 6 tiles */
    unsigned int tbase = g_uiTexPage;

    /* g_uiTexPage's system-RAM pixel buffer is read at runtime by
     * ColorizeTpageHiColor (intensity-bucket recolor for menu/fade
     * tints). Mark it keep BEFORE the load triggers eager upload so
     * the buffer survives the post-upload free path. The other 5
     * tiles are static after load and don't need keep. */
#ifdef SONICR_XBOX
    { extern void xbox_log_mem(const char *); xbox_log_mem("wallpaper start"); }
#endif
    XMARK("before keep");
    GL_KeepPixels((int)tbase);
    XMARK("keep done");

    /* These wallpapers are fully opaque, so they upload as RGB565 rather than
     * color-keyed ARGB1555 — one more bit of green and no alpha test. Flag it
     * here, before the buffers below exist: R_SetNoColorKey triggers an upload,
     * and an upload with a live buffer releases it out from under the fill. */
    for (unsigned int t = tbase; t < tbase + 6; t++) {
        R_SetNoColorKey((int)t);
    }

    XMARK("nocolorkey done");
    for (unsigned int t = tbase; t < tbase + 6; t++) {
        if (g_tpagePixelBuf[t] == NULL) {
            g_tpagePixelBuf[t] = malloc(256 * 256 * 2);
        }
        if (g_tpagePixelBuf[t] != NULL) {
            memset(g_tpagePixelBuf[t], 0, 256 * 256 * 2);
        }
        ClaimTpage((int)t, 256, 256, 4);
        /* The tile fill below packs true 6-bit green at bits 10:5. Set after
         * ClaimTpage, which clears the flag along with the rest of the
         * previous occupant's format state. */
        R_SetTpageGreen6((int)t, 1);
    }

    XMARK("alloc done");
    /* Open the RAW file */
    const char *filename = s_currentTitleFile;
    if (filename == NULL) {
        return;
    }

    FILE *fp = fOpen(filename, "rb");
    if (fp == NULL) {
        DebugLog("  Failed to open: %s\n", filename);
        return;
    }

    XMARK("opened");
    /* Read entire 640x480x3 file */
    int rawSize = 640 * 480 * 3;
    unsigned char *rawBuf = malloc(rawSize);
    XMARK(rawBuf ? "rawBuf ok" : "rawBuf NULL");
    if (rawBuf == NULL) {
        /* Out of memory: skip this wallpaper instead of crashing in fRead. */
        fClose(fp);
        return;
    }
#ifdef SONICR_DC
    /* ~900KB is the largest single read in the game, and the DC fileio shim
     * (sr_fRead, save_vmu.c) holds io_lock across the whole underlying fread.
     * The music streaming thread refills through the same fRead / same io_lock
     * (sndwav.c), so one unbroken read locks it out for the full ~0.5s GD-ROM
     * transfer, starving the ADPCM stream — the credits-slide glitch. Split it
     * into chunks so io_lock is released between them (same idea as
     * LoadTPageRGB's quarter-split read).
     */
    if (rawBuf != NULL) {
        const int chunk = 32 * 1024;
        int off = 0;
        while (off < rawSize) {
            int want = rawSize - off;
            if (want > chunk) {
                want = chunk;
            }
            if (fRead(rawBuf + off, 1, (size_t)want, fp) != (size_t)want) {
                break;
            }
            thd_sleep(1);
            off += want;
        }
    }
#else
    fRead(rawBuf, 1, rawSize, fp);
#endif
    fClose(fp);
    XMARK("read done");

    /* Color key pass: bright green gets B+=8 */
    unsigned char *p = rawBuf;
    for (int i = 0; i < 640 * 480; i++) {
        if ((p[1] >> 3 == 0x1F) && (p[0] >> 3 == 0) && (p[2] >> 3 == 0)) {
            p[2] = p[2] + 8;
        }
        p += 3;
    }

    /* Split 640x480 into 6 tiles, converting RGB to 16bpp. */
    struct { int srcColBytes; int srcRowStart; int tileW; int tileH; } tiles[6] = {
        { 0,     0,       256, 256 },
        { 256*3, 0,       256, 256 },
        { 512*3, 0,       128, 256 },
        { 0,     256*1920, 256, 224 },
        { 256*3, 256*1920, 256, 224 },
        { 512*3, 256*1920, 128, 224 },
    };

    for (unsigned int t = 0; t < 6; t++) {
        unsigned short *dst = (unsigned short *)g_tpagePixelBuf[tbase + t];
        if (dst == NULL) {
            continue;
        }
        for (int row = 0; row < tiles[t].tileH; row++) {
            unsigned char *srcRow = rawBuf + tiles[t].srcRowStart + row * 1920 + tiles[t].srcColBytes;
            for (int col = 0; col < tiles[t].tileW; col++) {
                unsigned char r = srcRow[col * 3];
                unsigned char g = srcRow[col * 3 + 1];
                unsigned char b = srcRow[col * 3 + 2];
                dst[row * 256 + col] = (unsigned short)(((int)r >> 3) << 11) |
                                       (unsigned short)(((int)g >> 2) << 5) |
                                       (unsigned short)((int)b >> 3);
            }
        }
    }
    free(rawBuf);
    XMARK("converted");

    /* Mark each loaded tpage dirty so the renderer uploads to VRAM. The
     * DC backend's MarkDirty is eager, so this also performs the upload
     * synchronously here — no first-draw lazy fallback. */
    for (unsigned int t = tbase; t < tbase + 6; t++) {
        R_MarkTextureDirty(t);
    }
}

/**
 * RenderLogoQuads — 0x46154C — 727 bytes
 * Renders the logo image as 6 textured quads, one per tpage starting
 * at g_uiTexPage. Each quad is two triangles (6 indices, 4 vertices)
 * submitted to the per-tpage D3D vertex/index buffers.
 *
 * Vertex layout: [X, Y, Z, 1/Z, color, unused, U, V] (8 ints = 32 bytes).
 * Color = 0xffe0e0e0u (slightly dimmed white).
 */
void RenderLogoQuads(void)
{
    /* Original: z = 3000.0 / g_farClipFloat. Before InitFarClipAndFog runs,
     * g_farClipFloat is 0, producing inf. D3D ignores inf Z for pre-transformed
     * vertices. For OpenGL with ortho(-1,1), clamp to a valid depth. */
    float z = (g_farClipFloat > 0.0f) ? ((float)LOGO_Z_DEPTH / g_farClipFloat) : 0.5f;
    float invZ = 1.0f / (float)LOGO_Z_DEPTH;

    R_SetTexEnv(R_TEXENV_MODULATE);

    for (int q = 0; q < 6; q++) {
        int tpage = g_uiTexPage + q;

        if (g_tpageStateArray[tpage] != 0x04) {
            continue;
        }

        /* Quad corner positions from grid, scaled to screen */
        int topLeftIdx  = s_logoQuadIndices[q][0];
        int topRightIdx = s_logoQuadIndices[q][1];
        int botRightIdx = s_logoQuadIndices[q][2];

        float halfW = (float)g_dispHalfWidth;
        float halfH = (float)g_dispHalfHeight;
        float xScale = (float)LOGO_X_SCALE;
        float yScale = (float)LOGO_Y_SCALE;

        float x0 = (float)s_logoGridXY[topLeftIdx][0]  * halfW * xScale + (float)g_dispClipLeft;
        float y0 = (float)s_logoGridXY[topLeftIdx][1]  * halfH * yScale + (float)g_dispClipTop;
        float x1 = (float)s_logoGridXY[topRightIdx][0] * halfW * xScale + (float)g_dispClipLeft;
        float y1 = y0;  /* same row */
        float x2 = (float)s_logoGridXY[botRightIdx][0] * halfW * xScale + (float)g_dispClipLeft;
        float y2 = (float)s_logoGridXY[botRightIdx][1] * halfH * yScale + (float)g_dispClipTop;
        float x3 = x0;  /* same column as top-left */
        float y3 = y2;  /* same row as bottom-right */

        /* UV coordinates */
        float uMax = s_logoUMax[q];
        float vMax = s_logoVMax[q];

        R_SetTexture(tpage);

        RenderVertex verts[4] = {
            { x0, y0, z, invZ, VERTEX_WHITE, 0, 0.0f, 0.0f },
            { x1, y1, z, invZ, VERTEX_WHITE, 0, uMax, 0.0f },
            { x2, y2, z, invZ, VERTEX_WHITE, 0, uMax, vMax },
            { x3, y3, z, invZ, VERTEX_WHITE, 0, 0.0f, vMax },
        };
        R_DrawQuad(verts);
    }
}
