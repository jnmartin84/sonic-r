/**
 * render_pvr_glue.c — DC stand-ins for backend-config functions and globals
 * that the SDL build provides via render_gl.c.
 *
 * Goal: link, not pretty pixels. Most bodies here are minimum-viable so the
 * rest of the engine has well-defined values to read. Replace as the real
 * PVR renderer comes online.
 */

#include "sonicr_globals.h"
#include "r_state.h"
#include "r_draw.h"
#include "r_types.h"

extern int g_clipLeftDouble;
extern int g_vpClipLeft10;
extern int g_vpClipRight10;
extern int g_vpClipLeft16;
extern int g_vpClipRight16;
extern int g_vpParam0F;
extern int g_vpParam10;

/* Backend dimensions / viewport offsets — render_gl.c keeps these for
 * letterboxing under HiDPI. On DC we render at native 640×480 with no
 * letterbox, so these stay at full-screen defaults. */
int g_glBackingWidth = 640;
int g_glBackingHeight = 480;
int g_glViewportOffsetX = 0;
int g_glViewportOffsetY = 0;

/* SetViewportFromConfig — copy the per-viewport config block into the engine's
 * globals. The SDL build also calls glViewport here; PVR equivalent is set
 * in the per-frame BeginFrame/scene path, not here. */
void SetViewportFromConfig(int *config)
{
    if (config == NULL) {
        return;
    }

    g_clipLeft = config[0];
    g_clipLeftDouble = g_clipLeft * 2;
    g_clipTop = config[1];
    g_clipRight = config[2];
    g_clipBottom = config[3];
    g_projScaleX = config[4];
    g_projScaleXCurrent = config[5];
    g_projScaleY = config[6];
    g_screenCenterX = config[7];
    g_screenCenterY = config[8];
    g_screenWidthFull = config[9];
    g_screenHeightFull = config[10];
    g_vpClipLeft10 = config[0xb];
    g_vpClipRight10 = config[0xc];
    g_vpClipLeft16 = config[0xd];
    g_vpClipRight16 = config[0xe];
    g_vpParam0F = config[0xf];
    g_vpParam10 = config[0x10];
}

static const RenderVertex bg_v[4] = {
    { 0.0f,   0.0f,   0.00001f, 0.00001f, 0xFF000000, 0, 0.0f, 0.0f },
    { 640.0f, 0.0f,   0.00001f, 0.00001f, 0xFF000000, 0, 0.0f, 0.0f },
    { 640.0f, 480.0f, 0.00001f, 0.00001f, 0xFF000000, 0, 0.0f, 0.0f },
    { 0.0f,   480.0f, 0.00001f, 0.00001f, 0xFF000000, 0, 0.0f, 0.0f },
};

    /* D3DAppClearBackBuffer — full-screen black quad on PT, behind everything else. */
void D3DAppClearBackBuffer(void)
{
    R_SetTexture(-1);
    R_DrawQuad(bg_v);
}

struct WaveVert {
    int sx;
    int sy;
    int depth;
};

/* RenderWavingMenuBackground — animated wave applied to the menu wallpaper
 * texture. No-op = no wallpaper visible. Translates the
 * SDL render_gl.c version to R_DrawQuad submission. 8 strips × 7 verts,
 * decomposed into 7×6 quads. */
void RenderWavingMenuBackground(void)
{
    int tpage = g_uiTexPage;
    if (tpage < 0 || tpage >= 52) {
        return;
    }
    if (g_tpageStateArray[tpage] != 4) {
        return;
    }

    int phase = (g_totalFrames & 0x7F) << 5;

    struct WaveVert buf[8][7];

    int sinAngle = phase;
    int yWorld   = 0x483;
    for (int strip = 0; strip < 8; strip++) {
        int vertAngle = sinAngle;
        int xWorld    = -0x604;
        for (int v = 0; v < 7; v++) {
            int sinVal = g_sinTable[vertAngle & 0xFFF] >> 7;
            int depth  = 0x8CA - sinVal;
            float rdepth = reciprocal((float)depth);
            int sx = g_screenCenterX + (g_projScaleXCurrent * xWorld) * rdepth;
            int sy = g_screenCenterY - (g_projScaleY * yWorld) * rdepth;
            buf[strip][v].sx = sx;
            buf[strip][v].sy = sy;
            buf[strip][v].depth = depth;
            vertAngle = (vertAngle - 0x14D) & 0xFFF;
            xWorld += 0x1B8;
        }
        sinAngle = (sinAngle - 0xDE) & 0xFFF;
        yWorld -= 0x14A;
    }

    /* Binary's mesh is asymmetric; D3D viewport clipping fills the edges,
     * so do the same trick the SDL build uses — pin first/last column to
     * the screen edges. */
    for (int strip = 0; strip < 8; strip++) {
        buf[strip][0].sx = 0;
        buf[strip][6].sx = g_screenWidth;
    }

    float screenW = (float)g_screenWidth;

    R_PushState();
    R_SetTexture(tpage);
    R_SetTexEnv(R_TEXENV_MODULATE);
    R_SetBlendMode(R_BLEND_ALPHA);
    R_FlushState();

    /* Wave is the menu background — must draw BEHIND UI quads.  PVR sorts
     * by 1/w (smaller = farther).  UI quads end up at rhw ≈ 0.001..0.1
     * (depth 10..1000); wave depth is ~2250 from the binary math, giving
     * rhw ≈ 0.00044, comfortably behind everything. Per-vertex depth
     * keeps the perspective right too. */
    for (int strip = 0; strip < 7; strip++) {
        float vTop = (float)strip       * 0.14285714f; // / 7.0f;
        float vBot = (float)(strip + 1) * 0.14285714f; // / 7.0f;
        for (int v = 0; v < 6; v++) {
            const struct WaveVert *tl = &buf[strip    ][v    ];
            const struct WaveVert *tr = &buf[strip    ][v + 1];
            const struct WaveVert *bl = &buf[strip + 1][v    ];
            const struct WaveVert *br = &buf[strip + 1][v + 1];

            #define FOG_COLOR(d) ({                                         \
                int _f = 0xC0 - ((d) - 0x8CA) / 4;                          \
                if (_f < 0)    _f = 0;                                      \
                if (_f > 0xC0) _f = 0xC0;                                   \
                (uint32_t)((0xFFu << 24) | (_f << 16) | (_f << 8) | _f);    \
            })
            uint32_t cTL = FOG_COLOR(tl->depth);
            uint32_t cTR = FOG_COLOR(tr->depth);
            uint32_t cBR = FOG_COLOR(br->depth);
            uint32_t cBL = FOG_COLOR(bl->depth);
            #undef FOG_COLOR

            float rhwTL = 1.0f * reciprocal((float)tl->depth);
            float rhwTR = 1.0f * reciprocal((float)tr->depth);
            float rhwBR = 1.0f * reciprocal((float)br->depth);
            float rhwBL = 1.0f * reciprocal((float)bl->depth);

            float rscreenW = reciprocal(screenW);

            RenderVertex quad[4] = {
                { (float)tl->sx, (float)tl->sy, 0.0f, rhwTL, cTL, 0,
                  (float)tl->sx * rscreenW, vTop },
                { (float)tr->sx, (float)tr->sy, 0.0f, rhwTR, cTR, 0,
                  (float)tr->sx * rscreenW, vTop },
                { (float)br->sx, (float)br->sy, 0.0f, rhwBR, cBR, 0,
                  (float)br->sx * rscreenW, vBot },
                { (float)bl->sx, (float)bl->sy, 0.0f, rhwBL, cBL, 0,
                  (float)bl->sx * rscreenW, vBot },
            };
            R_DrawQuad(quad);
        }
    }

    R_PopState();
}

/* GL_KeepPixels — flag a tpage as "keep system-RAM source alive" so that
 * PVR_UploadTpage doesn't free its g_tpagePixelBuf entry after upload.
 * Required for tpages that get re-mutated after first upload via
 * sub-rect updates (parallax1) or full-buffer reads (sky gradient
 * regenerator). On DC, mutators that don't read prior pixel state are
 * being switched to direct-VRAM mutation, so they don't need to keep. */
int s_pvrKeepPixels[52];
void GL_KeepPixels(int tpage)
{
    if (tpage >= 0 && tpage < 52) {
        s_pvrKeepPixels[tpage] = 1;
    }
}

/* FinalizeMenuTexturesD3D — no-op on DC.
 *
 * The original D3D engine used this to flag every allocated surface dirty
 * after a screen transition; the lazy GL backend honored it by uploading
 * pixels at first draw. Under the DC backend's eager-upload regime, every
 * mutator (LoadTPageRGB / R_SetPendingRGBA / R_SetNoColorKey / sub-rect
 * patches) uploads to VRAM synchronously at the call site — VRAM is
 * always current by the time this would run.
 *
 * The blind "force dirty + re-upload everything" loop this used to do
 * was actively harmful: for tpages flagged GL_KeepPixels (parallax2,
 * UI-alt playfield tiles), g_tpagePixelBuf survives the post-upload
 * release. A subsequent TintBackgroundTPage / direct-VRAM mutator writes
 * fresh content into VRAM but leaves the system-RAM source unchanged.
 * The forced re-upload here would then blast that stale source over
 * the new VRAM content, reverting the tint. Observed post-race when
 * D3D_LoadPlayfieldTilesRGB had set keep on slot 17 = g_uiTexPage. */
void FinalizeMenuTexturesD3D(void) { }
