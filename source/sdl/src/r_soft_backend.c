/**
 * r_soft_backend.c — software wireframe renderer (SDL2 port)
 *
 * Compile-time alternate backend, selected with `make SOFT=1`
 * (-DSONICR_SOFT_RENDER). Replaces BOTH r_gl_backend.c and render_gl.c —
 * those two files gate themselves out under the same define, and this file
 * provides every symbol of theirs that game code links against.
 *
 * Every primitive — 3D scene and 2D UI alike — is drawn as its colored
 * outline: the perimeter of the submitted fan/tri/quad, with per-vertex
 * Gouraud color along each edge, rasterized into a CPU framebuffer at the
 * virtual resolution (g_screenWidth × g_screenHeight). No depth, no
 * texture sampling, no blending; a primitive whose vertices are all
 * alpha-zero is skipped, everything else draws at full opacity.
 *
 * The game's own pipeline is software-transform (pre-projected D3DTLVERTEX
 * screen coordinates), so this backend never sees 3D — only 2D lines.
 *
 * Present still goes through the existing GL context (platform_sdl.c
 * creates the window with SDL_WINDOW_OPENGL): the buffer is uploaded once
 * per FlipD3D and drawn as a letterboxed fullscreen quad. That is the only
 * GL this file touches.
 *
 * Scissor coordinate contract: R_SetScissor keeps its GL convention
 * (origin bottom-left) because hud_full.c computes rects against
 * g_glBackingWidth/Height and g_glViewportOffsetX/Y. This backend pins
 * backing == virtual and offsets == 0, so those rects arrive in virtual
 * coordinates and only the Y flip remains.
 *
 * Functions copied from render_gl.c (SetViewportFromConfig,
 * ProcessTpageStates, CleanupD3DTPages, RenderWavingMenuBackground) mirror
 * the GL versions minus GL calls — the GL file stays the authoritative
 * translation of the binary; keep them in sync.
 */

#ifdef SONICR_SOFT_RENDER

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
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Viewport globals normally owned by render_gl.c. hud_full.c scales its
 * scissor rects by backing/virtual — pinning backing to the virtual size in
 * BeginFrame makes that scale 1 and the offsets vanish, so every R_SetScissor
 * call arrives in virtual coordinates. Letterboxing happens at present. */
int g_glBackingWidth = 640;
int g_glBackingHeight = 480;
int g_glViewportOffsetX = 0;
int g_glViewportOffsetY = 0;

/* =====================================================================
 * Framebuffer
 * ===================================================================== */

static uint32_t *s_fb;          /* ARGB, native present resolution */
static int s_fbW, s_fbH;

/* The framebuffer is the drawable's 4:3 content region at native pixels, so
 * the submitted virtual coordinates (0..g_screenWidth, 0..g_screenHeight) are
 * scaled up into it — lines rasterize at native resolution instead of being
 * drawn at 640×480 and upscaled at present. Both are the SAME ratio for a 4:3
 * framebuffer over a 4:3 virtual space; kept as two for clarity. Recomputed in
 * BeginFrame; 1.0 until the first frame sizes the buffer. */
static float s_sclX = 1.0f, s_sclY = 1.0f;

static void soft_clear_fb(void)
{
    if (s_fb) {
        memset(s_fb, 0, (size_t)s_fbW * (size_t)s_fbH * 4);
    }
}

/* =====================================================================
 * Render state
 *
 * Everything is tracked so R_PushState/R_PopState/R_DebugGetState behave,
 * but only the scissor rect changes what the rasterizer does.
 * ===================================================================== */

typedef struct {
    int   textureId;
    int   blendMode;
    int   depthTest, depthWrite, depthFunc;
    int   texEnv, filter, cull;
    int   alphaTest;
    float alphaRef;
    int   scissorOn;
    int   scX, scY, scW, scH;   /* GL convention: origin bottom-left */
} SoftState;

static SoftState s_cur;
static SoftState s_stack[4];
static int s_stackDepth;

/* Effective inclusive clip rect in framebuffer (top-origin) coordinates. */
static int s_clipX0, s_clipY0, s_clipX1, s_clipY1;

static void soft_update_clip(void)
{
    s_clipX0 = 0;
    s_clipY0 = 0;
    s_clipX1 = s_fbW - 1;
    s_clipY1 = s_fbH - 1;
    if (!s_cur.scissorOn) {
        return;
    }
    /* Scale the virtual-space rect into native framebuffer pixels, then flip Y:
     * GL rect covers gl-Y [scY, scY+scH-1] from the bottom edge, so top-origin
     * fbY = H-1-glY and the rect is [H-(scY+scH), H-1-scY], all in fb pixels. */
    int x0 = (int)(s_cur.scX * s_sclX);
    int x1 = (int)((s_cur.scX + s_cur.scW) * s_sclX) - 1;
    int y0 = s_fbH - (int)((s_cur.scY + s_cur.scH) * s_sclY);
    int y1 = s_fbH - 1 - (int)(s_cur.scY * s_sclY);
    if (x0 > s_clipX0) { s_clipX0 = x0; }
    if (y0 > s_clipY0) { s_clipY0 = y0; }
    if (x1 < s_clipX1) { s_clipX1 = x1; }
    if (y1 < s_clipY1) { s_clipY1 = y1; }
}

void R_SetTexture(int tpageIndex)      { s_cur.textureId = tpageIndex; }
void R_SetBlendMode(R_BlendMode mode)  { s_cur.blendMode = (int)mode; }
void R_SetDepthTest(int enable)        { s_cur.depthTest = enable; }
void R_SetDepthFunc(R_DepthFunc func)  { s_cur.depthFunc = (int)func; }
void R_SetDepthWrite(int enable)       { s_cur.depthWrite = enable; }
void R_SetTexEnv(R_TexEnvMode mode)    { s_cur.texEnv = (int)mode; }
void R_SetFilter(R_FilterMode mode)    { s_cur.filter = (int)mode; }
void R_SetCullMode(R_CullMode mode)    { s_cur.cull = (int)mode; }
void R_SetAlphaTest(int enable)        { s_cur.alphaTest = enable; }
void R_SetAlphaRef(float ref)          { s_cur.alphaRef = ref; }

void R_SetTpageFilter(int tpage, R_FilterMode mode) { (void)tpage; (void)mode; }
void R_ClearTpageFilter(int tpage)                  { (void)tpage; }
void R_SetTpageSatBoost(int tpage, int k256)        { (void)tpage; (void)k256; }

void R_SetScissor(int x, int y, int w, int h)
{
    s_cur.scissorOn = 1;
    s_cur.scX = x;
    s_cur.scY = y;
    s_cur.scW = w;
    s_cur.scH = h;
    soft_update_clip();
}

void R_DisableScissor(void)
{
    s_cur.scissorOn = 0;
    soft_update_clip();
}

void R_PushState(void)
{
    if (s_stackDepth < 4) {
        s_stack[s_stackDepth] = s_cur;
    }
    s_stackDepth++;
}

void R_PopState(void)
{
    if (s_stackDepth > 0 && s_stackDepth <= 4) {
        s_cur = s_stack[s_stackDepth - 1];
        soft_update_clip();
    }
    if (s_stackDepth > 0) {
        s_stackDepth--;
    }
}

void R_ResetState(void)
{
    /* Same defaults as the GL tracker: cull none, blend alpha, depth lequal,
     * alpha test on, scissor off. */
    s_cur.textureId  = -1;
    s_cur.blendMode  = R_BLEND_ALPHA;
    s_cur.depthTest  = 1;
    s_cur.depthWrite = 1;
    s_cur.depthFunc  = R_DEPTH_LEQUAL;
    s_cur.texEnv     = R_TEXENV_MODULATE;
    s_cur.filter     = R_FILTER_NEAREST;
    s_cur.cull       = R_CULL_NONE;
    s_cur.alphaTest  = 1;
    s_cur.alphaRef   = 0.0f;
    s_cur.scissorOn  = 0;
    soft_update_clip();
}

void R_FlushState(void)
{
    /* Nothing deferred — state applies immediately. */
}

void R_DebugGetState(int *depthTest, int *depthWrite, int *depthFunc,
                     int *blendMode, int *alphaTest, float *alphaRef)
{
    if (depthTest)  { *depthTest  = s_cur.depthTest; }
    if (depthWrite) { *depthWrite = s_cur.depthWrite; }
    if (depthFunc)  { *depthFunc  = s_cur.depthFunc; }
    if (blendMode)  { *blendMode  = s_cur.blendMode; }
    if (alphaTest)  { *alphaTest  = s_cur.alphaTest; }
    if (alphaRef)   { *alphaRef   = s_cur.alphaRef; }
}

/* =====================================================================
 * Line rasterizer
 * ===================================================================== */

/* Liang-Barsky clip of the segment to the current clip rect, then Bresenham
 * with 16.16 per-channel color stepping. Clipping first is load-bearing:
 * off-screen projected vertices can land millions of units out, and an
 * unclipped Bresenham would walk every pixel of the way there. */
static void soft_line(float fx0, float fy0, uint32_t c0,
                      float fx1, float fy1, uint32_t c1)
{
    if (!s_fb) {
        return;
    }
    if (!isfinite(fx0) || !isfinite(fy0) || !isfinite(fx1) || !isfinite(fy1)) {
        return;
    }

    float dx = fx1 - fx0;
    float dy = fy1 - fy0;
    float t0 = 0.0f, t1 = 1.0f;

    /* Clip in float against the inclusive rect, half a pixel outward so
     * edge-hugging lines still land on their boundary pixels. */
    float minX = (float)s_clipX0 - 0.5f, maxX = (float)s_clipX1 + 0.5f;
    float minY = (float)s_clipY0 - 0.5f, maxY = (float)s_clipY1 + 0.5f;

    const float p[4] = { -dx, dx, -dy, dy };
    const float q[4] = { fx0 - minX, maxX - fx0, fy0 - minY, maxY - fy0 };
    for (int i = 0; i < 4; i++) {
        if (p[i] == 0.0f) {
            if (q[i] < 0.0f) {
                return;             /* parallel and fully outside */
            }
        }
        else {
            float r = q[i] / p[i];
            if (p[i] < 0.0f) {
                if (r > t1) { return; }
                if (r > t0) { t0 = r; }
            }
            else {
                if (r < t0) { return; }
                if (r < t1) { t1 = r; }
            }
        }
    }

    int x0 = (int)lrintf(fx0 + dx * t0);
    int y0 = (int)lrintf(fy0 + dy * t0);
    int x1 = (int)lrintf(fx0 + dx * t1);
    int y1 = (int)lrintf(fy0 + dy * t1);

    /* Colors at the clipped endpoints */
    int r0 = (int)((c0 >> 16) & 0xFF), g0 = (int)((c0 >> 8) & 0xFF), b0 = (int)(c0 & 0xFF);
    int r1 = (int)((c1 >> 16) & 0xFF), g1 = (int)((c1 >> 8) & 0xFF), b1 = (int)(c1 & 0xFF);
    int rA = r0 + (int)((float)(r1 - r0) * t0), rB = r0 + (int)((float)(r1 - r0) * t1);
    int gA = g0 + (int)((float)(g1 - g0) * t0), gB = g0 + (int)((float)(g1 - g0) * t1);
    int bA = b0 + (int)((float)(b1 - b0) * t0), bB = b0 + (int)((float)(b1 - b0) * t1);

    int adx = abs(x1 - x0);
    int ady = abs(y1 - y0);
    int steps = (adx > ady) ? adx : ady;
    if (steps > s_fbW + s_fbH) {
        return;                     /* clip failed a degenerate case — bail */
    }

    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = adx - ady;

    /* 16.16 color accumulators */
    int cr = rA << 16, cg = gA << 16, cb = bA << 16;
    int srr = 0, sgg = 0, sbb = 0;
    if (steps > 0) {
        srr = ((rB - rA) << 16) / steps;
        sgg = ((gB - gA) << 16) / steps;
        sbb = ((bB - bA) << 16) / steps;
    }

    int x = x0, y = y0;
    for (int i = 0; i <= steps; i++) {
        if (x >= s_clipX0 && x <= s_clipX1 && y >= s_clipY0 && y <= s_clipY1) {
            s_fb[y * s_fbW + x] = 0xFF000000u
                                | ((uint32_t)(cr >> 16) << 16)
                                | ((uint32_t)(cg >> 16) << 8)
                                |  (uint32_t)(cb >> 16);
        }
        int e2 = err * 2;
        if (e2 > -ady) { err -= ady; x += sx; }
        if (e2 <  adx) { err += adx; y += sy; }
        cr += srr; cg += sgg; cb += sbb;
    }
}

/* =====================================================================
 * Primitive submission — everything is its outline
 * ===================================================================== */

void R_DrawTriFan(const RenderVertex *v, int count)
{
    if (count < 3 || s_fb == NULL) {
        return;
    }

    /* Fully transparent primitive draws nothing (fade overlay at rest). */
    int visible = 0;
    for (int i = 0; i < count; i++) {
        if ((v[i].color >> 24) != 0) {
            visible = 1;
            break;
        }
    }
    if (!visible) {
        return;
    }

    /* Perimeter of the submitted polygon — a quad outlines as 4 edges with
     * no diagonal, a fan as its silhouette. (The GL emit's homogeneous
     * sx*w trick divides straight back out, so sx/sy ARE the screen
     * position — use them directly.) Scale virtual → native framebuffer here,
     * the one draw choke point through which every primitive funnels. */
    for (int i = 0; i < count; i++) {
        int j = (i + 1 == count) ? 0 : i + 1;
        soft_line(v[i].sx * s_sclX, v[i].sy * s_sclY, v[i].color,
                  v[j].sx * s_sclX, v[j].sy * s_sclY, v[j].color);
    }
}

void R_DrawTri(const RenderVertex v[3])
{
    R_DrawTriFan(v, 3);
}

void R_DrawQuad(const RenderVertex v[4])
{
    R_DrawTriFan(v, 4);
}

void R_DrawQuad2D(float x0, float y0, float x1, float y1,
                  float u0, float v0, float u1, float v1,
                  float z, uint32_t color)
{
    (void)u0; (void)v0; (void)u1; (void)v1;
    float rhw = (z > 0.0f) ? (1.0f / z) : 1.0f;
    RenderVertex v[4];
    v[0] = (RenderVertex){ x0, y0, 0.5f, rhw, color, 0, 0.0f, 0.0f };
    v[1] = (RenderVertex){ x1, y0, 0.5f, rhw, color, 0, 0.0f, 0.0f };
    v[2] = (RenderVertex){ x1, y1, 0.5f, rhw, color, 0, 0.0f, 0.0f };
    v[3] = (RenderVertex){ x0, y1, 0.5f, rhw, color, 0, 0.0f, 0.0f };
    R_DrawQuad(v);
}

void R_DrawQuad2DSolid(float x0, float y0, float x1, float y1,
                       float z, uint32_t color)
{
    R_DrawQuad2D(x0, y0, x1, y1, 0.0f, 0.0f, 0.0f, 0.0f, z, color);
}

/* =====================================================================
 * Texture API — bookkeeping only, nothing is ever sampled.
 *
 * The two ownership-taking entry points still store their pointers with
 * GL-identical lifetime (callers may retain and rewrite the buffers), and
 * ProcessTpageStates state 6 frees the same things the GL version frees.
 * ===================================================================== */

static unsigned char *s_pendingRGBA[52];
static unsigned char *s_tpageRGBA8Buf[52];

void R_InitTextures(void)                 { }
void R_UploadTexture(int tpage)           { (void)tpage; }
void R_MarkTextureDirty(int tpage)        { (void)tpage; }
void R_ClearTextureDirty(int tpage)       { (void)tpage; }
void R_FreezeTexture(int tpage)           { (void)tpage; }
void R_ThawTexture(int tpage)             { (void)tpage; }
void R_SetNoColorKey(int tpage)           { (void)tpage; }
void R_ClearNoColorKey(int tpage)         { (void)tpage; }
void R_SetTpageGreen6(int tpage, int on)  { (void)tpage; (void)on; }

void R_UploadTextureRGBA(int tpage, unsigned char *rgba, int w, int h)
{
    /* GL twin copies into the GL texture; no ownership taken. */
    (void)tpage; (void)rgba; (void)w; (void)h;
}

void R_UploadTextureSubRect(int tpage, int x, int y, int w, int h)
{
    (void)tpage; (void)x; (void)y; (void)w; (void)h;
}

void R_SetTpageRGBA8(int tpage, unsigned char *rgba)
{
    /* Takes ownership (32-bit sky). Mirror GL lifetime exactly. */
    if (tpage < 0 || tpage >= 52) {
        return;
    }
    if (s_tpageRGBA8Buf[tpage] && s_tpageRGBA8Buf[tpage] != rgba) {
        free(s_tpageRGBA8Buf[tpage]);
    }
    s_tpageRGBA8Buf[tpage] = rgba;
}

void R_SetPendingRGBA(int tpage, unsigned char *rgba, int w, int h)
{
    /* Takes ownership; freed on replace or tpage state 6. */
    (void)w; (void)h;
    if (tpage < 0 || tpage >= 52) {
        free(rgba);
        return;
    }
    if (s_pendingRGBA[tpage]) {
        free(s_pendingRGBA[tpage]);
    }
    s_pendingRGBA[tpage] = rgba;
}

void GL_KeepPixels(int tpage)
{
    /* Write-only flag in the GL backend too — nothing consumes it. */
    (void)tpage;
}

/**
 * FinalizeMenuTexturesD3D — 0x00438D10
 * GL marks every loaded tpage dirty for re-upload; nothing to invalidate here.
 */
void FinalizeMenuTexturesD3D(void)
{
}

/* =====================================================================
 * Frame lifecycle
 * ===================================================================== */

/**
 * BeginFrame — 0x00422AA4
 * Keeps the GL version's SrcVertex depth-field clear (the game relies on it),
 * sizes the framebuffer to the drawable's 4:3 content region at native pixels,
 * derives the virtual→native scale, and pins the backing globals so scissor
 * math (hud_full.c) collapses to virtual coordinates.
 */
void BeginFrame(void)
{
    if (g_vertexArrayBase) {
        SrcVertex *v = g_vertexArrayBase;
        for (int i = 0; i < 32768; i++) {
            v->depth = 0;
            v++;
        }
    }

    /* Largest 4:3 rect inside the drawable (native pixels — HighDPI window, so
     * this is the true backing resolution). Fall back to the virtual size if
     * the drawable isn't reported yet. Present blits this 1:1. */
    int drawW = 0, drawH = 0;
    platform_get_drawable_size(&drawW, &drawH);
    int wantW, wantH;
    if (drawW <= 0 || drawH <= 0) {
        wantW = g_screenWidth;
        wantH = g_screenHeight;
    }
    else if (drawW * 3 > drawH * 4) {
        wantH = drawH;                 /* pillarboxed — height-bound */
        wantW = (drawH * 4) / 3;
    }
    else {
        wantW = drawW;                 /* letterboxed — width-bound */
        wantH = (drawW * 3) / 4;
    }

    if (s_fbW != wantW || s_fbH != wantH || s_fb == NULL) {
        free(s_fb);
        s_fbW = wantW;
        s_fbH = wantH;
        s_fb = (uint32_t *)calloc((size_t)s_fbW * (size_t)s_fbH, 4);
    }

    s_sclX = (g_screenWidth  > 0) ? (float)s_fbW / (float)g_screenWidth  : 1.0f;
    s_sclY = (g_screenHeight > 0) ? (float)s_fbH / (float)g_screenHeight : 1.0f;

    /* hud_full.c scales its scissor rects by backing/virtual; pinning backing
     * to the virtual size keeps that ratio 1, so rects arrive in virtual
     * coordinates and soft_update_clip applies the native scale itself. */
    g_glBackingWidth   = g_screenWidth;
    g_glBackingHeight  = g_screenHeight;
    g_glViewportOffsetX = 0;
    g_glViewportOffsetY = 0;

    R_ResetState();
}

void EndFrame(void)
{
    /* GL twin is just glFlush. */
}

void R_EndFrame(void)
{
    EndFrame();
}

/**
 * FlipD3D — 0x004329FC
 * Present: upload the framebuffer to the GL context once and draw it as a
 * letterboxed 4:3 quad, then swap. The only GL in the backend.
 */
void FlipD3D(void)
{
    int winW, winH;
    platform_get_drawable_size(&winW, &winH);

    if (s_fb == NULL || winW <= 0 || winH <= 0) {
        platform_gl_swap();
        return;
    }

    static GLuint s_presentTex;
    static int s_ptW, s_ptH;

    glViewport(0, 0, winW, winH);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glEnable(GL_TEXTURE_2D);
    if (s_presentTex == 0) {
        glGenTextures(1, &s_presentTex);
    }
    glBindTexture(GL_TEXTURE_2D, s_presentTex);
    if (s_ptW != s_fbW || s_ptH != s_fbH) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, s_fbW, s_fbH, 0,
                     GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        s_ptW = s_fbW;
        s_ptH = s_fbH;
    }
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, s_fbW, s_fbH,
                    GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, s_fb);

    /* Largest 4:3 rect that fits the window — same letterbox math as the GL
     * BeginFrame, just applied at present time instead. */
    int vpW, vpH;
    if (winW * 3 > winH * 4) {
        vpH = winH;
        vpW = (winH * 4) / 3;
    }
    else {
        vpW = winW;
        vpH = (winW * 3) / 4;
    }
    int offX = (winW - vpW) / 2;
    int offY = (winH - vpH) / 2;

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, winW, winH, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex2i(offX,       offY);
    glTexCoord2f(1.0f, 0.0f); glVertex2i(offX + vpW, offY);
    glTexCoord2f(1.0f, 1.0f); glVertex2i(offX + vpW, offY + vpH);
    glTexCoord2f(0.0f, 1.0f); glVertex2i(offX,       offY + vpH);
    glEnd();

    glDisable(GL_TEXTURE_2D);
    platform_gl_swap();
}

void R_Flip(void)
{
    FlipD3D();
}

void R_ClearDepth(void)
{
    /* No depth buffer. */
}

/**
 * D3DAppClearBackBuffer — 0x00435868
 * Binary does IDirect3DViewport2::Clear with a black background material.
 */
void D3DAppClearBackBuffer(void)
{
    soft_clear_fb();
}

/**
 * SetViewportFromConfig — FUN_004cc0e8 — copied from render_gl.c (the
 * function is pure global assignment; the D3D viewport half doesn't apply,
 * exactly as in GL — see the comment there).
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
}

/**
 * ProcessTpageStates — 0x004323cc — state machine copied from render_gl.c.
 * The GL version glClears at every call site (its pipeline relies on that);
 * the framebuffer clear here mirrors it. State transitions are identical;
 * state 6 frees the same buffers.
 */
void ProcessTpageStates(void)
{
    int changed;

    soft_clear_fb();

    do {
        changed = 0;
        for (int i = 0; i < 0x34; i++) {
            unsigned char state = (unsigned char)g_tpageStateArray[i];
            if (state > 7) {
                continue;
            }

            switch (state) {
                case 6:
                case 7:
                    if (state == 6) {
                        if (g_tpagePixelBuf[i] != NULL) {
                            free(g_tpagePixelBuf[i]);
                            g_tpagePixelBuf[i] = NULL;
                        }
                        if (s_pendingRGBA[i] != NULL) {
                            free(s_pendingRGBA[i]);
                            s_pendingRGBA[i] = NULL;
                        }
                    }
                    g_tpageStateArray[i] = 0;
                    break;

                case 1:
                case 2:
                    g_tpageStateArray[i] = 3;
                    changed = 1;
                    break;

                case 3:
                    g_tpageStateArray[i] = 4;
                    changed = 1;
                    break;

                case 5:
                    g_tpageStateArray[i] = 4;
                    break;

                case 0:
                case 4:
                default:
                    break;
            }
        }
    } while (changed);
}

/**
 * CleanupD3DTPages — 0x004332AC — copied from render_gl.c.
 */
void CleanupD3DTPages(void)
{
    for (int i = 0; i < 0x34; i++) {
        char state = g_tpageStateArray[i];
        if (state == 0) {
            continue;
        }
        if (state == 5) {
            continue;
        }
        if (i == TPAGE_PLATFORM_ICONS) {
            continue;
        }
        g_tpageStateArray[i] = 6;
    }

    ProcessTpageStates();
}

void R_ClearAndReset(void)
{
    ProcessTpageStates();
}

/**
 * RenderWavingMenuBackground — 0x004C68B8 — copied from render_gl.c minus
 * the GL texture-upload preamble. The wavy wallpaper mesh comes out as a
 * distorted grid of outlines, which suits the backend.
 */
void RenderWavingMenuBackground(void)
{
    int tpage = g_uiTexPage;

    if (tpage < 0 || tpage >= 52) {
        return;
    }
    if (g_tpageStateArray[tpage] != 4) {
        return;
    }

    struct IntVert { int sx, sy, uvU, uvV; };
    struct IntVert buf[8][8];

    int sinAngle = (g_totalFrames & 0x7F) << 5;
    int yWorld   = 0x483;
    int uvV      = 0x8000;

    for (int row = 0; row < 8; row++) {
        int vertAngle = sinAngle;
        int xWorld    = -0x604;
        int uvU       = 0x8000;

        for (int col = 0; col < 8; col++) {
            int depth = 0x8CA - (g_sinTable[vertAngle] >> 7);

            buf[row][col].sx = g_screenCenterX + (g_projScaleXCurrent * xWorld) / depth;
            buf[row][col].sy = g_screenCenterY - (g_projScaleY * yWorld) / depth;
            buf[row][col].uvU = uvU;
            buf[row][col].uvV = uvV;

            vertAngle = (vertAngle - 0x14D) & 0xFFF;
            xWorld += 0x1B8;
            uvU += 0x246DB6;
        }

        sinAngle = (sinAngle - 0xDE) & 0xFFF;
        yWorld -= 0x14A;
        uvV += 0x246DB6;
    }

    R_PushState();
    R_SetTexture(tpage);
    R_SetTexEnv(R_TEXENV_MODULATE);
    R_SetBlendMode(R_BLEND_ALPHA);
    R_FlushState();

    for (int row = 0; row < 7; row++) {
        for (int col = 0; col < 7; col++) {
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

#else /* !SONICR_SOFT_RENDER */

/* Keep the translation unit non-empty when the backend is compiled out. */
typedef int sonicr_soft_backend_disabled_t;

#endif /* SONICR_SOFT_RENDER */
