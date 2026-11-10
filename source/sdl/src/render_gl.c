/**
 * render_gl.c — OpenGL 1.x rendering backend (SDL2 port)
 *
 * Replaces the Direct3D 5 rendering calls with OpenGL fixed-function equivalents.
 *
 * D3D vertex format (D3DTLVERTEX, 32 bytes = 8 ints per vertex):
 *   [0] X (float)     — screen-space X
 *   [1] Y (float)     — screen-space Y
 *   [2] Z (float)     — depth (0..1)
 *   [3] RHW (float)   — 1/Z reciprocal
 *   [4] color (uint)  — ARGB packed: A<<24 | R<<16 | G<<8 | B
 *   [5] unused
 *   [6] U (float)     — texture coord U
 *   [7] V (float)     — texture coord V
 *
 * These are pre-transformed (already projected to screen space).
 * OpenGL renders them with an orthographic projection matching the screen.
 */

/* SOFT=1 builds replace this whole file with r_soft_backend.c. */
#ifndef SONICR_SOFT_RENDER

#ifdef __APPLE__
#include <OpenGL/gl.h>
#elif defined(_WIN32)
#include <GL/glew.h>
#include <GL/gl.h>
#else
#include <GL/gl.h>
#endif
#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "sonicr_functions.h"
#include "r_types.h"
#include "r_state.h"
#include "r_draw.h"
#include "platform.h"
#include "net_transport.h"
#include "endian_util.h"
#include <math.h>

#ifdef SONICR_GLES2
extern void R_GLES2_FlushVerts(void);
#endif

/* GL backing pixel dimensions (for scissor computation in C code).
 * These reflect the 4:3 viewport area, not the full window. */
int g_glBackingWidth = 640;
int g_glBackingHeight = 480;

/* 4:3 letterbox/pillarbox offset within the full backing surface.
 * All glViewport / glScissor calls add these offsets. */
int g_glViewportOffsetX = 0;
int g_glViewportOffsetY = 0;

/* Static RGBA4444 conversion buffer for texture uploads — max 1024×256×2 = 512 KB.
 * Matches PVR's native ARGB4444 format for DC portability. */
static uint16_t __attribute__((aligned(32))) s_rgba4Storage[1024 * 256];

#if SONICR_BIG_ENDIAN
/* RGBA8888 upload buffer for big-endian platforms.
 * Mesa's GL_UNSIGNED_SHORT_5_6_5 / GL_UNSIGNED_SHORT_5_5_5_1 packed formats
 * have byte-order bugs on PPC64 BE. GL_UNSIGNED_BYTE RGBA is unambiguous. */
static uint8_t __attribute__((aligned(32))) s_rgba8Storage[1024 * 512 * 4];
#endif

/* Tpage pixel data (loaded by LoadTPageRGB) */

/* OpenGL texture IDs — one per tpage slot (non-static for r_gl_backend.c access) */
GLuint s_glTextures[52];
static int s_glTexturesInited = 0;

/* Track which tpages have been uploaded to GL (non-static for r_gl_backend.c access) */
int s_glTextureDirty[52];

/* Per-tpage: 1 = keep pixel buffer in RAM after GL upload (needed for read-back) */
static int s_tpageKeepPixels[52];

/* Per-tpage: 1 = disable color keying for this tpage (e.g. model env map) */
static int s_tpageNoColorKey[52];

/* Per-tpage: 1 = the pixel buffer carries true 6-bit green at bits 10:5 rather
 * than the game's R5 G5 pad B5. Set by the loaders that build their own tiles
 * from a higher-quality source — the title logo (logo_render.c) and the folded
 * sky/parallax (track_load.c) — so the no-key upload reads green at full
 * precision instead of keeping only its top five bits. Mirrors s_pvrGreen6 on
 * the Dreamcast side. */
static int s_tpageGreen6[52];

/* Pending full-quality RGBA data for lazy upload (avoids 16bpp banding) */
static unsigned char *s_pendingRGBA[52];
static int s_pendingRGBAWidth[52];
static int s_pendingRGBAHeight[52];

/* Persistent full-quality RGBA8 override for a tpage (e.g. the 32-bit sky).
 * Unlike s_pendingRGBA this is NOT freed after upload, so it survives the
 * dirty/re-upload lifecycle (state resets re-mark every tpage dirty). When
 * set, GL_UploadTpage prefers it over the 16bpp buffer. Dimensions come from
 * g_tpageWidth/Height like the normal path. */
static unsigned char *s_tpageRGBA8Buf[52];

/**
 * GL_InitTextures — create GL texture objects for all tpage slots.
 * Called once at startup.
 */
void GL_InitTextures(void)
{
    if (s_glTexturesInited) {
        return;
    }
    glGenTextures(52, s_glTextures);
    int i;
    for (i = 0; i < 52; i++) {
        s_glTextureDirty[i] = 1;
    }
    s_glTexturesInited = 1;
}

/**
 * FIXME update comment
 * GL_UploadTpage — upload a tpage's pixel data to its GL texture.
 * Called lazily when DrawBatch needs the texture.
 *
 * Tpage pixel data is 16bpp (RGB565).
 * Standard tpages are 256x256. Wallpaper tpages may be 640x480.
 */
void GL_UploadTpage(int tpage)
{
    if (!s_glTexturesInited) {
        GL_InitTextures();
    }

    unsigned short *pixels = (unsigned short *)g_tpagePixelBuf[tpage];
    if (pixels == NULL) {
        return;
    }

    /* Determine dimensions — standard is 256x256.
     * texture.c stores width/height if non-standard. */
    int w = g_tpageWidth[tpage];
    int h = g_tpageHeight[tpage];
    if (w == 0) {
        w = 256;
    }
    if (h == 0) {
        h = 256;
    }

    glBindTexture(GL_TEXTURE_2D, s_glTextures[tpage]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    /* Persistent full-quality RGBA8 override (32-bit sky) — preferred over the
     * 16bpp buffer and re-uploaded on every dirty, so it never reverts. */
    if (s_tpageRGBA8Buf[tpage]) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, s_tpageRGBA8Buf[tpage]);
        s_glTextureDirty[tpage] = 0;
        return;
    }

    /* Check for pending full-quality RGBA data (from TintBackgroundTPage) */
    if (s_pendingRGBA[tpage]) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                     s_pendingRGBAWidth[tpage], s_pendingRGBAHeight[tpage], 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, s_pendingRGBA[tpage]);
        free(s_pendingRGBA[tpage]);
        s_pendingRGBA[tpage] = NULL;
        s_glTextureDirty[tpage] = 0;
        return;
    }

    /* Convert to GL upload format. Source: R5 at bits 15-11, G5 at bits 10-6, B5 at bits 4-0.
     * On BE: use GL_UNSIGNED_BYTE RGBA (Mesa packed-short formats have byte-order bugs on PPC64).
     * On LE: use packed 16-bit formats (GL_UNSIGNED_SHORT_5_6_5 / GL_UNSIGNED_SHORT_5_5_5_1). */
    int count = w * h;

#if SONICR_BIG_ENDIAN
    uint8_t *out8 = s_rgba8Storage;
    if (s_tpageNoColorKey[tpage]) {
        int green6 = s_tpageGreen6[tpage];
        for (int i = 0; i < count; i++) {
            unsigned short p = pixels[i];
            unsigned char r5 = (p >> 11) & 0x1F;
            unsigned char b5 = p & 0x1F;
            unsigned char g8;
            if (green6) {
                unsigned char g6 = (p >> 5) & 0x3F;   /* true 6-bit green */
                g8 = (unsigned char)((g6 << 2) | (g6 >> 4));
            }
            else {
                unsigned char g5 = (p >> 6) & 0x1F;
                g8 = (unsigned char)((g5 << 3) | (g5 >> 2));
            }
            out8[i * 4 + 0] = (r5 << 3) | (r5 >> 2);
            out8[i * 4 + 1] = g8;
            out8[i * 4 + 2] = (b5 << 3) | (b5 >> 2);
            out8[i * 4 + 3] = 0xFF;
        }
    } else {
        for (int i = 0; i < count; i++) {
            unsigned short p = pixels[i];
            unsigned char r5 = (p >> 11) & 0x1F;
            unsigned char g5 = (p >> 6) & 0x1F;
            unsigned char b5 = p & 0x1F;
            unsigned char a = IS_COLOR_KEY_RGB5(r5, g5, b5) ? 0 : 0xFF;
            out8[i * 4 + 0] = (r5 << 3) | (r5 >> 2);
            out8[i * 4 + 1] = (g5 << 3) | (g5 >> 2);
            out8[i * 4 + 2] = (b5 << 3) | (b5 >> 2);
            out8[i * 4 + 3] = a;
        }
    }
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, out8);
#else
    uint16_t *out = s_rgba4Storage;
    if (s_tpageNoColorKey[tpage]) {
        /* RGB565 path — no alpha needed, full color fidelity.
         * Source R5G5B5 → R5G6B5: duplicate G bit 0 into G bit 5, unless the
         * tpage already carries true 6-bit green, in which case take it whole. */
        int green6 = s_tpageGreen6[tpage];
        for (int i = 0; i < count; i++) {
            unsigned short p = pixels[i];
            unsigned char r5 = (p >> 11) & 0x1F;
            unsigned char b5 = p & 0x1F;
            unsigned char g6;
            if (green6) {
                g6 = (p >> 5) & 0x3F;
            }
            else {
                unsigned char g5 = (p >> 6) & 0x1F;
                g6 = (g5 << 1) | (g5 >> 4);
            }
            out[i] = (uint16_t)((r5 << 11) | (g6 << 5) | b5);
        }
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0,
                     GL_RGB, GL_UNSIGNED_SHORT_5_6_5, out);

    } else {
        /* RGBA5551 path — 1-bit alpha for color key, full 5-bit color.
         * GL_UNSIGNED_SHORT_5_5_5_1 layout: R5 G5 B5 A1 (high to low). */
        for (int i = 0; i < count; i++) {
            unsigned short p = pixels[i];
            unsigned char r5 = (p >> 11) & 0x1F;
            unsigned char g5 = (p >> 6) & 0x1F;
            unsigned char b5 = p & 0x1F;
            unsigned char a1 = IS_COLOR_KEY_RGB5(r5, g5, b5) ? 0 : 1;
            out[i] = (uint16_t)((r5 << 11) | (g5 << 6) | (b5 << 1) | a1);
        }
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
                     GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, out);
    }
#endif

    s_glTextureDirty[tpage] = 0;

    /* TODO: free pixel buffer after upload to simulate DC VRAM model.
     * Blocked: the tpage state machine (6→7→4) in ProcessTpageStates recreates
     * GL textures and re-uploads, requiring the pixel data to persist.
     * Need to understand the full tpage lifecycle before enabling. */
}

/**
 * GL_UploadTpageRGBA — upload pre-built RGBA data directly to a tpage's GL texture.
 * Bypasses the 16bpp pixel buffer entirely for full-quality tinted backgrounds.
 */
void GL_UploadTpageRGBA(int tpage, unsigned char *rgba, int w, int h)
{
    if (!s_glTexturesInited) {
        GL_InitTextures();
    }
    glBindTexture(GL_TEXTURE_2D, s_glTextures[tpage]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    s_glTextureDirty[tpage] = 0;
}

/**
 * GL_SetTpageRGBA8 — register a persistent full-quality RGBA8 buffer for a
 * tpage (takes ownership). Used by the 32-bit sky. Pass NULL to clear.
 */
void GL_SetTpageRGBA8(int tpage, unsigned char *rgba)
{
    if (tpage < 0 || tpage >= 52) {
        return;
    }
    if (s_tpageRGBA8Buf[tpage] && s_tpageRGBA8Buf[tpage] != rgba) {
        free(s_tpageRGBA8Buf[tpage]);
    }
    s_tpageRGBA8Buf[tpage] = rgba;
    s_glTextureDirty[tpage] = 1;   /* upload on next bind */
}

/**
 * GL_MarkTpageDirty — call when tpage pixel data changes.
 */
void GL_MarkTpageDirty(int tpage)
{
    if (tpage >= 0 && tpage < 52) {
        s_glTextureDirty[tpage] = 1;
    }
}

/**
 * GL_FreezeTpage — upload current pixel buffer to GL and lock it.
 * Subsequent CPU buffer modifications won't trigger re-upload.
 * Used for g_tpageParallax1: binary uploads ICON01 to D3D, then sub-rects
 * overwrite the CPU buffer without re-uploading. We replicate this by
 * uploading before sub-rects and clearing dirty after.
 */
void GL_FreezeTpage(int tpage)
{
    if (!s_glTexturesInited) {
        GL_InitTextures();
    }
    if (tpage >= 0 && tpage < 52 && g_tpagePixelBuf[tpage] != NULL) {
        GL_UploadTpage(tpage);
    }
}

void GL_ClearTpageDirty(int tpage)
{
    if (tpage >= 0 && tpage < 52) {
        s_glTextureDirty[tpage] = 0;
    }
}

/**
 * GL_UploadTpageSubRect — patch a rectangular region of a frozen GL texture.
 * Converts the sub-rect from the CPU pixel buffer (16bpp) to RGBA and uploads
 * via glTexSubImage2D without disturbing the rest of the texture.
 * Used for minimap MAP textures which are loaded as sub-rects after the
 * ICON01 freeze.
 */
void GL_UploadTpageSubRect(int tpage, int destX, int destY, int width, int height)
{
    if (!s_glTexturesInited) {
        GL_InitTextures();
    }
    if (tpage < 0 || tpage >= 52) {
        return;
    }

    unsigned short *pixels = (unsigned short *)g_tpagePixelBuf[tpage];
    if (pixels == NULL) {
        return;
    }

    int tpageW = g_tpageWidth[tpage];
    if (tpageW <= 0) {
        tpageW = 256;
    }

    /* Convert sub-rect to uploadable format using static buffer.
     * Sub-rect patches are only used for minimap updates which are color-keyed. */
    glBindTexture(GL_TEXTURE_2D, s_glTextures[tpage]);
#if SONICR_BIG_ENDIAN
    uint8_t *out8 = s_rgba8Storage;
    for (int row = 0; row < height; row++) {
        for (int col = 0; col < width; col++) {
            unsigned short p = pixels[(destY + row) * tpageW + (destX + col)];
            unsigned char r5 = (p >> 11) & 0x1F;
            unsigned char g5 = (p >> 6) & 0x1F;
            unsigned char b5 = p & 0x1F;
            unsigned char a = IS_COLOR_KEY_RGB5(r5, g5, b5) ? 0 : 0xFF;
            int idx = (row * width + col) * 4;
            out8[idx + 0] = (r5 << 3) | (r5 >> 2);
            out8[idx + 1] = (g5 << 3) | (g5 >> 2);
            out8[idx + 2] = (b5 << 3) | (b5 >> 2);
            out8[idx + 3] = a;
        }
    }
    glTexSubImage2D(GL_TEXTURE_2D, 0, destX, destY, width, height,
                    GL_RGBA, GL_UNSIGNED_BYTE, out8);
#else
    uint16_t *out = s_rgba4Storage;
    for (int row = 0; row < height; row++) {
        for (int col = 0; col < width; col++) {
            unsigned short p = pixels[(destY + row) * tpageW + (destX + col)];
            unsigned char r5 = (p >> 11) & 0x1F;
            unsigned char g5 = (p >> 6) & 0x1F;
            unsigned char b5 = p & 0x1F;
            unsigned char a1 = IS_COLOR_KEY_RGB5(r5, g5, b5) ? 0 : 1;
            out[row * width + col] = (uint16_t)((r5 << 11) | (g5 << 6) | (b5 << 1) | a1);
        }
    }
    glTexSubImage2D(GL_TEXTURE_2D, 0, destX, destY, width, height,
                    GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, out);
#endif
}

void GL_SetPendingRGBA(int tpage, unsigned char *rgba, int w, int h)
{
    if (tpage < 0 || tpage >= 52) {
        free(rgba);
        return;
    }
    if (s_pendingRGBA[tpage]) {
        free(s_pendingRGBA[tpage]);
    }
    s_pendingRGBA[tpage] = rgba;
    s_pendingRGBAWidth[tpage] = w;
    s_pendingRGBAHeight[tpage] = h;
    s_glTextureDirty[tpage] = 1;  /* trigger upload on next use */
}

/**
 * BeginFrame — 0x00422AA4
 * Sets up the GL state for a rendering pass.
 * Original: IDirect3DDevice::BeginScene().
 */
void BeginFrame(void)
{
    /* Clear vertex buffer depth fields (offset 0x30 per vertex, stride 0x40)
     * to prevent stale geometry from previous frames showing as glitchy
     * triangles. Only zeroes the depth int, not the source positions. */
    if (g_vertexArrayBase) {
        SrcVertex *v = g_vertexArrayBase;
        for (int i = 0; i < 32768; i++) {
            v->depth = 0;
            v++;
        }
    }

    if (!s_glTexturesInited) {
        GL_InitTextures();
    }

#ifdef SONICR_GLES2
    extern void R_GLES2_InitShaders(void);
    R_GLES2_InitShaders();
#endif

    /* Set orthographic projection matching screen coordinates.
     * D3DTLVERTEX is pre-transformed, so X=0..screenWidth, Y=0..screenHeight.
     * Y=0 is top of screen in D3D. OpenGL Y=0 is bottom, so we flip.
     * Z mapping: glOrtho(near=1, far=0) makes D3D Z=0→GL depth 0 (near),
     * Z=1→GL depth 1 (far). With GL_LEQUAL, smaller Z wins — matching D3D.
     * glViewport is set by SetViewportFromConfig (FUN_004cc0e8), not here. */
    /* Update viewport to match current window size (for resizable window).
     * The game renders at 640×480 virtual coordinates; GL scales to fit.
     * Maintain 4:3 aspect ratio with letterboxing/pillarboxing. */
    int fullW, fullH;
    platform_get_drawable_size(&fullW, &fullH);

    /* Compute largest 4:3 rect that fits in fullW x fullH */
    int vpW, vpH;
    if (fullW * 3 > fullH * 4) {
        /* Window is wider than 4:3 — pillarbox (black bars on sides) */
        vpH = fullH;
        vpW = (fullH * 4) / 3;
    }
    else {
        /* Window is taller than 4:3 — letterbox (black bars top/bottom) */
        vpW = fullW;
        vpH = (fullW * 3) / 4;
    }
    int offsetX = (fullW - vpW) / 2;
    int offsetY = (fullH - vpH) / 2;

    g_glViewportOffsetX = offsetX;
    g_glViewportOffsetY = offsetY;
    g_glBackingWidth = vpW;
    g_glBackingHeight = vpH;
    glViewport(offsetX, offsetY, vpW, vpH);

    /* D3D viewport clip volume: X in [-1, +1], Y in [-screenH, +screenH].
     * D3D maps: screenX = dwX + (clipX - dvClipX) / dvClipWidth * dwWidth
     *           screenY = dwY + (dvClipY - clipY) / dvClipHeight * dwHeight
     *
     * The game produces screen coordinates [0,W]×[0,H]. Apply the D3D
     * clip volume as the GL projection so the same mapping occurs. */
    float dvClipX = -1.0f;
    float dvClipWidth = 2.0f;
    float dvClipY = (float)g_screenHeightFull;           /* 480 */
    float dvClipHeight = (float)g_screenHeightFull * 2.0f; /* 960 */

    /* Map game screen coords → D3D clip coords:
     *   clipX = dvClipX + (screenX - dwX) / dwWidth * dvClipWidth
     *   clipY = dvClipY - (screenY - dwY) / dwHeight * dvClipHeight
     *
     * So glOrtho range = clip volume, with modelview doing screen→clip. */
    float left   = dvClipX;
    float right  = dvClipX + dvClipWidth;
    float top    = dvClipY;                    /* Y=0 maps to dvClipY (top) */
    float bottom = dvClipY - dvClipHeight;     /* Y=H maps to dvClipY - dvClipHeight */

#ifndef SONICR_GLES2
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(left, right, bottom, top, 0, -1);

    /* Modelview: transform game screen coords [0,W]×[0,H] → clip volume */
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    /* screenX → clipX: clipX = screenX * dvClipWidth/dwWidth + dvClipX */
    /* screenY → clipY: clipY = dvClipY - screenY * dvClipHeight/dwHeight */
    glTranslatef(dvClipX, dvClipY, 0.0f);
    glScalef(dvClipWidth / (float)g_screenWidth,
             -dvClipHeight / (float)g_screenHeight,
             1.0f);
#endif

    /* Reset state tracker to defaults (cull=none, blend=alpha, depth=lequal,
     * alpha test on, scissor off). First R_FlushState() will emit all GL
     * calls needed to match these defaults. */
    R_ResetState();
}

/**
 * EndFrame — 0x00422BA0 — 114 bytes
 * Flushes all tpage batches and ends the scene.
 * Original: iterates all 52 tpages, calls DrawBatch for each with data,
 * then calls IDirect3DDevice::EndScene().
 */
void EndFrame(void)
{
#ifdef SONICR_GLES2
    R_GLES2_FlushVerts();
#endif
    glFlush();
}

/**
 * FlipD3D — 0x004329FC
 * Presents the rendered frame.
 * Original: IDirectDrawSurface::Flip().
 * OpenGL: swap the double buffer.
 */
void FlipD3D(void)
{
    platform_gl_swap();
}

/* platform_pump_events is implemented in platform_sdl.c */

/**
 * SetViewportFromConfig — FUN_004cc0e8 — 429 bytes
 * Copies viewport config array entry to active rendering globals.
 * Original: in_EAX = pointer to viewport config entry (21 ints, stride 0x54).
 * D3D path sets IDirect3DViewport. OpenGL path sets glViewport.
 *
 * Performs the same init as untranslated D3D init path call to 0x004d4bdc.
 * 
 * Called before every render frame in the original. Sets clip bounds,
 * projection scale, screen center, and depth sort list pointer.
 */
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
    g_screenHeightFull = config[0xa];
    g_vpClipLeft10 = config[0xb];
    g_vpClipRight10 = config[0xc];
    g_vpClipLeft16 = config[0xd];
    g_vpClipRight16 = config[0xe];
    g_vpParam0F = config[0xf];
    g_vpParam10 = config[0x10];

    /* D3D path (lines 54883-54906): IDirect3DViewport::SetViewport2.
     *
     * Binary calls GetViewport2, then overwrites:
     *   dwX = g_clipLeft, dwY = g_clipTop
     *   dwWidth = g_screenWidthFull, dwHeight = g_screenHeightFull
     *   dvClipX = -1.0, dvClipWidth = 2.0
     *   dvClipY = screenHeightFull, dvClipHeight = screenHeightFull * 2.0
     * then calls SetViewport2.
     *
     * For our GL port: set glViewport to the per-viewport rectangle
     * (scaled to backing resolution for Retina), matching the D3D viewport rect.
     */
    /* D3D viewport: the binary calls SetViewport2 here with per-viewport bounds.
     * But D3DTLVERTEX (pre-transformed) vertices bypass the viewport transform —
     * D3D only uses the viewport rect for clipping and Z-range mapping.
     * Our GL equivalent: BeginFrame sets glViewport to the full backing area,
     * and glScissor (set in the per-viewport render loop) clips to each viewport.
     * Do NOT set glViewport to per-viewport sub-rects — that would compress the
     * absolute screen coordinates into a smaller area, causing offset/squash. */
}

/**
 * ProcessTpageStates — 0x004323cc — 574 bytes — 43 callers
 *
 * Tpage state machine processor.  Iterates all 52 tpages, processes state
 * transitions, and repeats if any transitions occurred.
 *
 * States:
 *   0   — idle (no action)
 *   1,2 — loaded, pending surface creation → advance to 3
 *   3   — surface created, pending upload  → advance to 4 (ready)
 *   4   — ready (no action)
 *   5   — pending validation → advance to 4
 *   6   — cleanup: free resources, set state 0
 *   7   — cleanup without pixel free, set state 0
 *   >7  — locked (no action; set by |=8 flag)
 *
 * Original calls D3D sub-functions:
 *   0x432f74  GetSurfaceInfo   (757 bytes, pure COM surface creation)
 *   0x43326c  ValidateState    (pure COM validation)
 *   0x435928  FlushD3DSurfaces (308 bytes, checks DDERR_SURFACELOST)
 *   0x431140  DebugLog         (13 bytes, no-op in binary)
 * For GL: states 1/2/3/5 advance directly to ready (4).  State 6 frees
 * the pixel buffer and invalidates the GL texture.
 */
void ProcessTpageStates(void)                                   /* 0x4323cc */
{
    int changed;                                           /* EDI */

    /* GL port: clear framebuffer + depth.  The original D3D function doesn't
     * clear the screen, but the GL port's rendering pipeline relies on these
     * call sites for depth buffer clearing (no other caller does it).
     * Clear the full window (including letterbox bars) to black first,
     * then enable a scissor rect so all subsequent glClear calls (e.g.
     * RenderBackground sky color) stay inside the 4:3 area. */

    /* Disable scissor so the black clear covers the entire window.
     * Ensure depth mask is ON — glClear respects glDepthMask, and a
     * previous frame may have left it FALSE (e.g. DrawFootShadows). */
    R_DisableScissor();
    R_SetDepthWrite(1);
    R_FlushState();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    /* Enable scissor to confine all future clears to the 4:3 area */
    R_SetScissor(g_glViewportOffsetX, g_glViewportOffsetY,
                 g_glBackingWidth, g_glBackingHeight);
    R_FlushState();

    do {
        changed = 0;                                       /* 0x4323d9 */
        for (int i = 0; i < 0x34; i++) {                       /* 0x4325c2 */
            unsigned char state = (unsigned char)g_tpageStateArray[i];
            if (state > 7) {
                continue;                       /* 0x4325d2: ja */
            }

            switch (state) {
                case 6:                                        /* 0x4323e0 */
                case 7:
                    /* State 6: free pixel buffer + zero arrays.
                    * State 7: skip pixel free, just reset state.
                    * Original checks cmp byte [i+0x6260a4], 6 to distinguish. */
                    if (state == 6) {                          /* 0x4323e3 */
                        /* Original frees g_ptrArray_625cc0[i] (D3D surface desc
                        * at 0x625cc0) and zeros both 0x625cc0[i] and 0x8f6ba8[i].
                        * g_ptrArray_625cc0 is unused in GL (always NULL).
                        * For GL: free g_tpagePixelBuf (malloc'd pixel data). */
                        if (g_tpagePixelBuf[i] != NULL) {      /* 0x4323f9 */
                            free(g_tpagePixelBuf[i]);          /* 0x4323ff: call free */
                            g_tpagePixelBuf[i] = NULL;         /* 0x43241e */
                        }
                        /* Invalidate GL texture so next render re-uploads */
                        s_glTextureDirty[i] = 1;
                        if (s_pendingRGBA[i] != NULL) {
                            free(s_pendingRGBA[i]);
                            s_pendingRGBA[i] = NULL;
                        }
                    }
                    /* Original releases D3D COM objects at 0x626278[i],
                    * 0x6261a8[i], 0x626348[i] via vtable->Release (offset+8)
                    * and zeros 0x626418[i], 0x63f394[i] when g_renderMode ==
                    * RENDER_D3D (0x432425-0x43249d).  Not applicable for GL. */
                    g_tpageStateArray[i] = 0;                  /* 0x4324a2 */
                    break;

                case 1:                                        /* 0x4324ad */
                case 2:
                    /* Original: GetSurfaceInfo (0x432f74) in D3D mode, advance
                    * to 3 or fail to 6.  Non-D3D path: set state 3. */
                    g_tpageStateArray[i] = 3;                  /* 0x43251f */
                    changed = 1;                               /* 0x43251a */
                    break;

                case 3:                                        /* 0x43252a */
                    /* Original D3D: GetSurfaceInfo → FatalError on failure.
                    * Non-D3D path stays at 3 (infinite loop — dead code in
                    * original since game always ran D3D).  For GL: advance
                    * to 4 (ready); texture upload happens on demand. */
                    g_tpageStateArray[i] = 4;
                    changed = 1;
                    break;

                case 5:                                        /* 0x432570 */
                    /* Original D3D: ValidateState (0x43326c), advance to 4 or
                    * fail to 6.  Non-D3D path: set state 4 (0x4325ba). */
                    g_tpageStateArray[i] = 4;                  /* 0x4325ba */
                    break;

                case 0:                                        /* 0x4325c1 */
                case 4:
                default:
                    break;
            }
        }
    } while (changed);                                     /* 0x4325e7 */

    /* Original calls FlushD3DSurfaces (0x435928) when g_renderMode ==
     * RENDER_D3D (0x4325ed-0x4325f6).  Not applicable for GL. */
    /* Original returns 1 in EAX (0x4325fb) — declared void, ignored. */
}

/**
 * CleanupD3DTPages — 0x004332AC — 59 bytes — called 1x
 *
 * Marks all active tpages (except idle/0 and validating/5) for cleanup
 * (state 6), then calls ProcessTpageStates to process the transitions.
 * Only runs in D3D mode.  For GL: g_renderMode is not RENDER_D3D,
 * so the state-setting loop is skipped; ProcessTpageStates still runs
 * for the glClear + any pending state transitions.
 */
void CleanupD3DTPages(void)                                /* 0x4332ac */
{
    for (int i = 0; i < 0x34; i++) {                           /* 0x4332bf */
        char state = g_tpageStateArray[i];                 /* 0x4332c7 */
        if (state == 0) {
            continue;                          /* 0x4332cf */
        }
        if (state == 5) {
            continue;                          /* 0x4332d4 */
        }
        if (i == TPAGE_PLATFORM_ICONS) {
            continue;
        }
        g_tpageStateArray[i] = 6;                          /* 0x4332d6 */
    }

    ProcessTpageStates();                                       /* 0x4332de */
}

/**
 * RenderBackground — 0x00435868 — 189 bytes
 * Binary does IDirect3DViewport2::Clear (solid color fill).
 * For OpenGL: clear to color.
 */
void RenderBackground(void)
{
    /* Confine glClear to the 4:3 area so sky/menu colors don't
     * leak into the letterbox/pillarbox bars. */
    R_SetScissor(g_glViewportOffsetX, g_glViewportOffsetY,
                 g_glBackingWidth, g_glBackingHeight);
    R_FlushState();

    /* During racing, clear to black — the binary clears to black and uses
     * fog to hide the geometry/sky transition (FUN_00435868). */
    if (g_introCountdown <= 0xD2 && g_introCountdown >= 0) {
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        R_DisableScissor();
        R_FlushState();
        return;
    }

    /* Menu/title: just clear. The wallpaper is drawn with wave distortion
     * by RenderWavingMenuBackground (FUN_004c6b50), called later in the render loop.
     * Binary: RenderBackground (0x435868) does IDirect3DViewport2::Clear only. */
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    R_DisableScissor();
    R_FlushState();
}

/* ---------------------------------------------------------------------------
 * The Direct3D twin, 0x004C6B50. Kept for reference: every menu screen picks
 * between this and the native-software twin at 0x004C68B8 on g_renderMode, and
 * we translated the wrong side of that branch for a long time. See the live
 * RenderWavingMenuBackground below.
 * ------------------------------------------------------------------------- */
#if 0
/**
 * RenderWavingMenuBackgroundD3D — 0x004c6b50 — 1335 bytes
 * Draws an animated wave overlay on menu screens using the game's sine table.
 * Original: builds perspective-projected triangle strips with depth fog via D3D.
 * GL: reproduces the same vertex math, draws with GL_QUAD_STRIP + per-vertex fog color.
 *
 * Called AFTER 3D scene rendering, BEFORE flip — it's a foreground water effect.
 *
 * Binary parameters:
 *   8 strips, 7 vertices each
 *   Sine step/vertex: 0x14D (333/4096)   Step/strip: -0xDE (-222/4096)
 *   Phase: (g_totalFrames & 0x7F) << 5
 *   Depth base: 0x8CA (2250), modulated by sin>>7
 *   Fog: 0xC0 - (depth - 0x8CA) / 4, clamped to gray ARGB
 */
void RenderWavingMenuBackgroundD3D(void)
{
    /* Binary uses g_uiTexPage (0x8F6C48) which in D3D points to the
     * wallpaper texture surface. */
    int tpage = g_uiTexPage;
    if (g_tpageStateArray[tpage] != 4) {
        return;
    }
    if (tpage < 0 || tpage >= 52) {
        return;
    }
    if (g_tpagePixelBuf[tpage] == NULL) {
        return;
    }

    if (!s_glTexturesInited) {
        GL_InitTextures();
    }
    if (s_glTextureDirty[tpage]) {
        GL_UploadTpage(tpage);
    }

    /* Binary vertex generation:
     * 8 strips (outer loop), 7 verts per strip (inner loop).
     * Each vertex: screenX, screenY, depth, uvU, uvV (all ints).
     *
     * Phase into sine table from frame counter */
    int phase = (g_totalFrames & 0x7F) << 5;  /* 0x8fb68c & 0x7F, <<5 */

    /* integer vertex buffer
     * Binary: 8 strips × 7 verts. Each vert = {sx, sy, depth, uvU, uvV}.
     * All integer arithmetic matching binary's imul/idiv at 0x4c6c14-0x4c6c9e.
     *
     * Register/var mapping from disasm:
     *   [ebp-0x24] yWorld:    init 0x483,  step -0x14A per strip
     *   [ebp-0x34] sinAngle:  init phase,  step -0xDE  per strip
     *   [ebp-0x20] uvV:       init 0x8000, step +0x246DB6 per strip
     *   [ebp-0x1c] uvU:       init 0x8000 each strip, step +0x246DB6 per vert
     *   esi        xWorld:    init -0x604 each strip,  step +0x1B8 per vert
     *   edi        vertAngle: copy of sinAngle,        step -0x14D per vert
     */
    struct IntVert { int sx, sy, depth, uvU, uvV; };
    struct IntVert buf[8][7];

    int sinAngle = phase;
    int yWorld   = 0x483;
    int uvV      = 0x8000;

    for (int strip = 0; strip < 8; strip++) {
        int vertAngle = sinAngle;
        int xWorld    = -0x604;   /* ESI at 0x4c6c0c */
        int uvU       = 0x8000;   /* [ebp-0x1c] at 0x4c6bee */

        for (int v = 0; v < 7; v++) {
            /* 0x4c6c1d-0x4c6c2c: depth = 0x8CA - sin[angle]>>7 */
            int sinVal = g_sinTable[vertAngle & 0xFFF] >> 7;
            int depth = 0x8CA - sinVal;

            /* 0x4c6c14-0x4c6c44: screenX (integer divide, matching imul+idiv) */
            int sx = g_screenCenterX + (g_projScaleXCurrent * xWorld) / depth;

            /* 0x4c6c46-0x4c6c78: screenY */
            int sy = g_screenCenterY - (g_projScaleY * yWorld) / depth;

            buf[strip][v].sx = sx;
            buf[strip][v].sy = sy;
            buf[strip][v].depth = depth;
            buf[strip][v].uvU = uvU;
            buf[strip][v].uvV = uvV;

            vertAngle = (vertAngle - 0x14D) & 0xFFF; /* 0x4c6c53,0x4c6c5f */
            xWorld += 0x1B8;                         /* 0x4c6c72 */
            uvU += 0x246DB6;                         /* 0x4c6c87 */
        }

        sinAngle = (sinAngle - 0xDE) & 0xFFF;        /* 0x4c6bb9 */
        yWorld -= 0x14A;                             /* 0x4c6bc9 */
        uvV += 0x246DB6;                             /* 0x4c6bbe */
    }

    /* convert & render
     * Binary (0x4c6d67-0x4c7022): converts integer verts to D3DTLVERTEX
     * and draws as triangle strips. 7 strips rendered (0..6), each using
     * verts from strip N and strip N+1.
     *
     * Per-vertex float conversion (0x4c6db5-0x4c6e43):
     *   tu  = (float)(uvU >> 16) * 0.0000749688f   (ROM float at 0x531430)
     *   tv  = (float)(uvV >> 16) * 0.0000749688f
     *   fog = 0xC0 - (depth - 0x8CA) / 4           (edi=4 at 0x4c6d7a)
     *   color = 0xFF000000 | fog<<16 | fog<<8 | fog
     */

    /* render
     * UV: screen-relative (U = sx/screenW, V = strip/7) so the wallpaper
     * texture fills the screen. Wave distortion is in vertex positions.
     * Fog: 0xC0 - (depth - 0x8CA) / 4, matching binary's idiv edi (edi=4). */

    float screenW = (float)g_screenWidth;

    /* Binary mesh doesn't fully span the screen (xWorld -1540..+1100 is
     * asymmetric). D3D's viewport clipping fills the edges; for GL, extend
     * the first and last vertex of each strip to the screen edges. */
    for (int strip = 0; strip < 8; strip++) {
        buf[strip][0].sx = 0;                  /* left edge */
        buf[strip][6].sx = g_screenWidth;      /* right edge */
    }

    R_PushState();
    R_SetTexture(tpage);
    R_SetTexEnv(R_TEXENV_MODULATE);
    R_SetBlendMode(R_BLEND_ALPHA);
    R_FlushState();

    for (int strip = 0; strip < 7; strip++) {
        float uvTop = (float)strip / 7.0f;
        float uvBot = (float)(strip + 1) / 7.0f;

        for (int col = 0; col < 6; col++) {
            struct IntVert *a0 = &buf[strip][col];
            struct IntVert *b0 = &buf[strip + 1][col];
            struct IntVert *a1 = &buf[strip][col + 1];
            struct IntVert *b1 = &buf[strip + 1][col + 1];

            RenderVertex rv[4];
            struct IntVert *src[4] = { a0, a1, b1, b0 };
            float uvRow[4] = { uvTop, uvTop, uvBot, uvBot };
            for (int k = 0; k < 4; k++) {
                int fog = 0xC0 - (src[k]->depth - 0x8CA) / 4;
                if (fog < 0) {
                    fog = 0;
                }
                if (fog > 0xC0) {
                    fog = 0xC0;
                }
                uint32_t grey = (uint32_t)fog;
                rv[k].sx = (float)src[k]->sx;
                rv[k].sy = (float)src[k]->sy;
                rv[k].sz = 0.5f;
                rv[k].rhw = 1.0f;
                rv[k].color = 0xFF000000u | (grey << 16) | (grey << 8) | grey;
                rv[k].specular = 0;
                rv[k].u = (float)src[k]->sx / screenW;
                rv[k].v = uvRow[k];
            }
            R_DrawQuad(rv);
        }
    }

    R_PopState();
}
#endif  /* Direct3D twin, 0x004C6B50 */

/**
 * RenderWavingMenuBackground — 0x004C68B8 — 664 bytes
 *
 * The native-software twin of the menu wallpaper; 0x004C6B50 above is the
 * Direct3D one. Both have twelve callers and each screen picks between them on
 * g_renderMode — character select branches at 0x48D66F (cmp [0x6DD860], 2) and
 * calls this one at 0x48DA2A. We run RENDER_SOFT, so this is the path.
 *
 * Builds an 8x8 wave-distorted grid and emits 49 textured quads into the
 * software display list at 0x8FB354. The emit (0x4C6A5C) writes only x/y/u/v
 * per vertex and sets flags (+0x74) to 1 and tpage (+0x76) to g_uiTexPage; the
 * per-vertex 13-bit colour fields at +0x08/+0x0C/+0x10 are never touched. With
 * neither 0x40 (gouraud) nor 0x20 (additive) in the flags, the dispatcher at
 * 0x4CAC30 falls through to table slot +0x20 — the unlit textured span — so
 * texels reach the framebuffer undimmed. The D3D twin's grey depth fog,
 * 0xC0 - (depth - 0x8CA) / 4, has no counterpart here.
 *
 * Vertex generation — 0x4C68C4-0x4C6A13:
 *   yWorld     0x483, step -0x14A per row       (8 rows,  +0x483 .. -0x483)
 *   xWorld    -0x604, step +0x1B8 per vertex    (8 verts, -0x604 .. +0x604)
 *   sinAngle   phase, step -0xDE  per row
 *   vertAngle  sinAngle, step -0x14D per vertex
 *   uvV       0x8000, step +0x246DB6 per row
 *   uvU       0x8000, step +0x246DB6 per vertex (texel 0 .. 255)
 *   depth = 0x8CA - (g_sinTable[vertAngle] >> 7)
 * Both spans are symmetric, unlike the D3D twin's seven-vertex row, so the
 * mesh covers the screen on its own and needs no edge extension.
 *
 * The 49 records all go into one depth bucket, ([0x6D7638] + [0x6D763C]) / 2
 * at 0x4C6A3E; for GL that is just draw order.
 */
void RenderWavingMenuBackground(void)
{
    int tpage = g_uiTexPage;                                /* 0x8F6C48 */

    /* 0x4C68B8 has no tpage-state gate — the D3D twin needs one (0x4C6B74:
     * cmp dl, 4) because it touches a surface that can be released. We keep it
     * plus the bounds/null checks so the GL backend always has pixels. */
    if (tpage < 0 || tpage >= 52) {
        return;
    }
    if (g_tpageStateArray[tpage] != 4) {
        return;
    }
    if (g_tpagePixelBuf[tpage] == NULL) {
        return;
    }

    if (!s_glTexturesInited) {
        GL_InitTextures();
    }
    if (s_glTextureDirty[tpage]) {
        GL_UploadTpage(tpage);
    }

    struct IntVert { int sx, sy, uvU, uvV; };
    struct IntVert buf[8][8];

    int sinAngle = (g_totalFrames & 0x7F) << 5;             /* 0x4C68C9 */
    int yWorld   = 0x483;                                   /* 0x4C68C4 */
    int uvV      = 0x8000;                                  /* 0x4C68CE */

    for (int row = 0; row < 8; row++) {
        int vertAngle = sinAngle;                           /* 0x4C6965 */
        int xWorld    = -0x604;                             /* 0x4C6981 */
        int uvU       = 0x8000;                             /* 0x4C6960 */

        for (int col = 0; col < 8; col++) {
            /* 0x4C6996: depth = 0x8CA - (sin >> 7) */
            int depth = 0x8CA - (g_sinTable[vertAngle] >> 7);

            /* 0x4C698D: centerX + projScaleX * xWorld / depth */
            buf[row][col].sx = g_screenCenterX + (g_projScaleXCurrent * xWorld) / depth;
            /* 0x4C69B8: centerY - projScaleY * yWorld / depth */
            buf[row][col].sy = g_screenCenterY - (g_projScaleY * yWorld) / depth;
            buf[row][col].uvU = uvU;
            buf[row][col].uvV = uvV;

            vertAngle = (vertAngle - 0x14D) & 0xFFF;        /* 0x4C69D0 */
            xWorld += 0x1B8;                                /* 0x4C69F1 */
            uvU += 0x246DB6;                                /* 0x4C69E9 */
        }

        sinAngle = (sinAngle - 0xDE) & 0xFFF;               /* 0x4C6935 */
        yWorld -= 0x14A;                                    /* 0x4C6928 */
        uvV += 0x246DB6;                                    /* 0x4C690E */
    }

    R_PushState();
    R_SetTexture(tpage);
    R_SetTexEnv(R_TEXENV_MODULATE);
    R_SetBlendMode(R_BLEND_ALPHA);
    R_FlushState();

    for (int row = 0; row < 7; row++) {
        for (int col = 0; col < 7; col++) {
            /* 0x4C6A5C: quad corners are [row][col], [row][col+1],
             * [row+1][col+1], [row+1][col] — source offsets 0x00, 0x10,
             * 0x90, 0x80 off the 0x10-stride / 0x80-row vertex buffer. */
            const struct IntVert *src[4] = {
                &buf[row][col],
                &buf[row][col + 1],
                &buf[row + 1][col + 1],
                &buf[row + 1][col]
            };

            RenderVertex rv[4];
            for (int k = 0; k < 4; k++) {
                rv[k].sx = (float)src[k]->sx;
                rv[k].sy = (float)src[k]->sy;
                rv[k].sz = 0.5f;
                rv[k].rhw = 1.0f;
                /* Unlit span — no vertex colour in the record at all. */
                rv[k].color = VERTEX_WHITE;
                rv[k].specular = 0;
                rv[k].u = g_uvLUT256[src[k]->uvU >> 16];
                rv[k].v = g_uvLUT256[src[k]->uvV >> 16];
            }
            R_DrawQuad(rv);
        }
    }

    R_PopState();
}

/**
 * GL_SetColorKey — mark a tpage as needing color-key transparency.
 * Called by LoadTitleTextureD3D for background tiles that have green = transparent.
 */
void GL_SetNoColorKey(int tpage)
{
    if (tpage >= 0 && tpage < 52) {
        s_tpageNoColorKey[tpage] = 1;
        s_glTextureDirty[tpage] = 1;  /* re-upload without color keying */
    }
}

void GL_ClearNoColorKey(int tpage)
{
    if (tpage >= 0 && tpage < 52) {
        s_tpageNoColorKey[tpage] = 0;
        s_glTextureDirty[tpage] = 1;  /* re-upload with color keying restored */
    }
}

/**
 * GL_SetTpageGreen6 — mark a tpage's pixel buffer as carrying true 6-bit green
 * at bits 10:5 instead of the game's R5 G5 pad B5. Set before the tpage is
 * uploaded; the no-key path then reads green whole rather than keeping only its
 * top five bits. No re-upload here — callers upload afterwards, and ClaimTpage
 * clears the flag for every new occupant. Mirrors PVR_SetTpageGreen6.
 */
void GL_SetTpageGreen6(int tpage, int on)
{
    if (tpage >= 0 && tpage < 52) {
        s_tpageGreen6[tpage] = on ? 1 : 0;
    }
}

void GL_KeepPixels(int tpage)
{
    if (tpage >= 0 && tpage < 52) {
        s_tpageKeepPixels[tpage] = 1;
    }
}

/**
 * FinalizeMenuTexturesD3D — 0x00438D10 — 787 bytes
 * Original: releases and recreates D3D texture surfaces for all loaded tpages.
 * OpenGL: mark all tpages dirty so they get re-uploaded on next use.
 */
void FinalizeMenuTexturesD3D(void)
{
    for (int i = 0; i < 52; i++) {
        if (g_tpagePixelBuf[i] != NULL) {
            s_glTextureDirty[i] = 1;
        }
    }
}

#endif /* !SONICR_SOFT_RENDER */
