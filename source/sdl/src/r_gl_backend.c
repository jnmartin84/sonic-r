/**
 * r_gl_backend.c — OpenGL 1.x implementation of the immediate-mode render API.
 *
 * Implements r_state.h, r_draw.h, r_texture.h, and frame lifecycle.
 * Uses a lazy state tracker to minimize redundant GL calls.
 */

/* SOFT=1 builds replace this whole file with r_soft_backend.c. */
#ifndef SONICR_SOFT_RENDER

#ifdef __APPLE__
#include <stdio.h>
#include <OpenGL/gl.h>
#elif defined(_WIN32)
#include <GL/glew.h>
#include <GL/gl.h>
#else
#include <GL/gl.h>
#endif

#include "r_types.h"
#include "r_state.h"
#include "r_draw.h"
#include "r_texture.h"
#include "sonicr_types.h"
#include "sonicr_globals.h"
#include <string.h>
#include <stdlib.h>

/* Forward declarations for existing frame functions in render_gl.c */
extern void BeginFrame(void);
extern void EndFrame(void);
extern void FlipD3D(void);
extern void ProcessTpageStates(void);

/* Forward declarations for existing GL_* texture functions in render_gl.c */
extern void GL_UploadTpage(int tpage);
extern void GL_UploadTpageRGBA(int tpage, unsigned char *rgba, int w, int h);
extern void GL_UploadTpageSubRect(int tpage, int destX, int destY, int width, int height);
extern void GL_MarkTpageDirty(int tpage);
extern void GL_ClearTpageDirty(int tpage);
extern void GL_FreezeTpage(int tpage);
extern void GL_SetPendingRGBA(int tpage, unsigned char *rgba, int w, int h);
extern void GL_SetNoColorKey(int tpage);
extern void GL_ClearNoColorKey(int tpage);
extern void GL_SetTpageGreen6(int tpage, int on);
extern void GL_InitTextures(void);
extern void GL_SetTpageRGBA8(int tpage, unsigned char *rgba);

/* =====================================================================
 * Internal state snapshot
 * ===================================================================== */

typedef struct {
    int          textureId;    /* tpage index, or -1 for untextured */
    R_BlendMode  blendMode;
    int          depthTest;
    R_DepthFunc  depthFunc;
    int          depthWrite;
    R_TexEnvMode texEnv;
    R_FilterMode filter;
    R_CullMode   cullMode;
    int          alphaTest;
    float        alphaRef;
    int          scissorEnabled;
    int          scissorX, scissorY, scissorW, scissorH;
} R_StateSnapshot;

static R_StateSnapshot s_desired;
static R_StateSnapshot s_current;

/* State stack for R_PushState / R_PopState */
#define R_STATE_STACK_DEPTH 4
static R_StateSnapshot s_stateStack[R_STATE_STACK_DEPTH];
static int s_stackDepth = 0;

/* Default state values */
static const R_StateSnapshot s_defaults = {
    .textureId      = -1,
    .blendMode      = R_BLEND_ALPHA,
    .depthTest      = 1,
    .depthFunc      = R_DEPTH_LEQUAL,
    .depthWrite     = 1,
    .texEnv         = R_TEXENV_MODULATE,
    .filter         = R_FILTER_NEAREST,
    .cullMode       = R_CULL_NONE,
    .alphaTest      = 1,
    .alphaRef       = 0.01f,
    .scissorEnabled = 0,
    .scissorX       = 0,
    .scissorY       = 0,
    .scissorW       = 640,
    .scissorH       = 480,
};

/* =====================================================================
 * Texture backend state
 * ===================================================================== */

/* GL texture objects and dirty flags — still owned by render_gl.c,
 * accessed here via extern for texture binding during R_FlushState. */
extern GLuint s_glTextures[];       /* render_gl.c */
#ifdef SONICR_XBOX
extern void xb_batch_end(void);
extern void xb_draw_fan(const float *verts, int n);
#define XB_BEND() xb_batch_end()
#else
#define XB_BEND() ((void)0)
#endif

/* Per-tpage filter override, and the filter actually resident on each GL
 * texture object.
 *
 * GL keeps filter state on the TEXTURE OBJECT, not globally — so a single
 * cached "current filter" is wrong the moment a different texture is bound.
 * It is also wrong after any upload: every GL_UploadTpage* path hard-sets
 * GL_NEAREST (render_gl.c:139, :247), so a filter applied earlier is stomped
 * whenever the tpage is re-uploaded. Together those made the old global
 * R_SetFilter path reach only whichever texture happened to be bound at the
 * moment the value changed, and nothing else.
 *
 * The filter is therefore resolved and applied per tpage at BIND time. Both
 * arrays encode 0 = unset and (mode + 1) otherwise, so the zero initialiser
 * means "no override" / "resident filter unknown". s_glTextureFilter is
 * invalidated by R_MarkTextureDirty, which precedes every upload path. */
static unsigned char s_tpageFilter[52];
static unsigned char s_glTextureFilter[52];

/* Apply the effective filter to tpage `tp`, which must be bound already. */
static void GL_ApplyTpageFilter(int tp)
{
    unsigned char want = s_tpageFilter[tp];
    if (want == 0) {
        want = (unsigned char)(s_desired.filter + 1);
    }
    if (s_glTextureFilter[tp] != want) {
        XB_BEND();
        GLenum f = (want == (unsigned char)(R_FILTER_LINEAR + 1)) ? GL_LINEAR
                                                                  : GL_NEAREST;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, f);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, f);
        s_glTextureFilter[tp] = want;
    }
}

/* =====================================================================
 * R_FlushState — apply only the GL calls for state that changed
 * ===================================================================== */

void R_FlushState(void)
{
    /* Texture binding — skip if tpage unchanged.
     * Dirty-texture uploads still happen even on a cache hit (the tpage
     * data changed, not the binding). */
    if (s_desired.textureId != s_current.textureId) {
        XB_BEND();
        if (s_desired.textureId < 0) {
            glDisable(GL_TEXTURE_2D);
        } else {
            int tp = s_desired.textureId;
            if (tp < 52) {
                if (g_tpagePixelBuf[tp] != NULL) {
                    if (s_glTextureDirty[tp]) {
                        GL_UploadTpage(tp);
                    }
                    glEnable(GL_TEXTURE_2D);
                    glBindTexture(GL_TEXTURE_2D, s_glTextures[tp]);
                }
                else {
                    glDisable(GL_TEXTURE_2D);
                }
            }
        }
        s_current.textureId = s_desired.textureId;
    }
    else if (s_desired.textureId >= 0 && s_desired.textureId < 52) {
        /* Same tpage, but check if pixel data was updated */
        if (s_glTextureDirty[s_desired.textureId]) {
            GL_UploadTpage(s_desired.textureId);
        }
    }

    /* Filter, resolved per tpage against the texture actually bound. Placed
     * after the whole binding block so it covers all three ways the resident
     * filter can go stale: a fresh bind, an in-place re-upload of the same
     * tpage, and a global R_SetFilter while the same tpage stays bound. */
    if (s_desired.textureId >= 0 && s_desired.textureId < 52 &&
        g_tpagePixelBuf[s_desired.textureId] != NULL) {
        GL_ApplyTpageFilter(s_desired.textureId);
    }

    /* Depth test */
    if (s_desired.depthTest != s_current.depthTest) {
        XB_BEND();
        if (s_desired.depthTest) {
            glEnable(GL_DEPTH_TEST);
        }
        else {
            glDisable(GL_DEPTH_TEST);
        }
        s_current.depthTest = s_desired.depthTest;
    }

    /* Depth function */
    if (s_desired.depthFunc != s_current.depthFunc) {
        XB_BEND();
        switch (s_desired.depthFunc) {
            case R_DEPTH_LEQUAL:
                glDepthFunc(GL_LEQUAL);
                break;
            case R_DEPTH_LESS:
                glDepthFunc(GL_LESS);
                break;
            case R_DEPTH_ALWAYS:
                glDepthFunc(GL_ALWAYS);
                break;
        }
        s_current.depthFunc = s_desired.depthFunc;
    }

    /* Depth write */
    if (s_desired.depthWrite != s_current.depthWrite) {
        XB_BEND();
        glDepthMask(s_desired.depthWrite ? GL_TRUE : GL_FALSE);
        s_current.depthWrite = s_desired.depthWrite;
    }

    /* Texture environment (overbright) */
    if (s_desired.texEnv != s_current.texEnv) {
        XB_BEND();
#if defined(__EMSCRIPTEN__) || defined(SONICR_XBOX)
        /* WebGL legacy GL emulation (and pbgl on the Xbox) doesn't support GL_COMBINE/GL_ADD_SIGNED.
         * Fall back to plain GL_MODULATE for all modes. */
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
#else
        if (s_desired.texEnv == R_TEXENV_ADD_SIGNED) {
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
            glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_ADD_SIGNED);
            glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_TEXTURE);
            glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_PREVIOUS);
            glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);
            glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR);
        }
        else {
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
            glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE, 1.0f);
        }
#endif
        s_current.texEnv = s_desired.texEnv;
    }

    /* Blend mode */
    if (s_desired.blendMode != s_current.blendMode) {
        XB_BEND();
        switch (s_desired.blendMode) {
            case R_BLEND_NONE:
                glDisable(GL_BLEND);
                break;
            case R_BLEND_ALPHA:
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                break;
            case R_BLEND_ADDITIVE:
                glEnable(GL_BLEND);
                glBlendFunc(GL_ONE, GL_ONE);
                break;
        }
        s_current.blendMode = s_desired.blendMode;
    }

    /* Filter is applied per tpage at bind time above — see GL_ApplyTpageFilter.
     * GL keeps it on the texture object, so it cannot be driven from here. The
     * snapshot field is still synced so the two fields never disagree, which
     * would trip up any whole-struct compare added later (the GLES2 backend
     * already has one). */
    s_current.filter = s_desired.filter;

    {
        static int s_cullOff;
        if (!s_cullOff) {
            XB_BEND();
            glDisable(GL_CULL_FACE);   /* pbgl keeps this; no need to re-send per draw */
            s_cullOff = 1;
        }
    }

    /* Alpha test */
    if (s_desired.alphaTest != s_current.alphaTest) {
        XB_BEND();
        if (s_desired.alphaTest) {
            glEnable(GL_ALPHA_TEST);
        }
        else {
            glDisable(GL_ALPHA_TEST);
        }
        s_current.alphaTest = s_desired.alphaTest;
    }

    /* Alpha ref */
    if (s_desired.alphaRef != s_current.alphaRef) {
        XB_BEND();
        glAlphaFunc(GL_GREATER, s_desired.alphaRef);
        s_current.alphaRef = s_desired.alphaRef;
    }

    /* Scissor enable/disable */
    if (s_desired.scissorEnabled != s_current.scissorEnabled) {
        XB_BEND();
        if (s_desired.scissorEnabled) {
            glEnable(GL_SCISSOR_TEST);
        }
        else {
            glDisable(GL_SCISSOR_TEST);
        }
        s_current.scissorEnabled = s_desired.scissorEnabled;
    }

    /* Scissor rect */
    if (s_desired.scissorEnabled &&
        (s_desired.scissorX != s_current.scissorX ||
         s_desired.scissorY != s_current.scissorY ||
         s_desired.scissorW != s_current.scissorW ||
         s_desired.scissorH != s_current.scissorH))
    {
        XB_BEND();
        glScissor(s_desired.scissorX, s_desired.scissorY,
                  s_desired.scissorW, s_desired.scissorH);
        s_current.scissorX = s_desired.scissorX;
        s_current.scissorY = s_desired.scissorY;
        s_current.scissorW = s_desired.scissorW;
        s_current.scissorH = s_desired.scissorH;
    }
}

/* =====================================================================
 * Render state API implementation
 * ===================================================================== */

void R_SetTexture(int tpageIndex)
{
    s_desired.textureId = tpageIndex;
}

void R_SetBlendMode(R_BlendMode mode)
{
    s_desired.blendMode = mode;
}

void R_SetDepthTest(int enable)
{
    s_desired.depthTest = enable;
}

void R_SetDepthFunc(R_DepthFunc func)
{
    s_desired.depthFunc = func;
}

void R_SetDepthWrite(int enable) 
{
    s_desired.depthWrite = enable;
}

void R_SetTexEnv(R_TexEnvMode mode)
{
    s_desired.texEnv = mode;
}

void R_SetFilter(R_FilterMode mode)
{
    s_desired.filter = mode;
}

/* Per-tpage filter pin. Takes priority over the global R_SetFilter value;
 * applied to the texture object at bind time by GL_ApplyTpageFilter. */
void R_SetTpageFilter(int tpage, R_FilterMode mode)
{
    if (tpage >= 0 && tpage < 52) {
        s_tpageFilter[tpage] = (unsigned char)(mode + 1);
    }
}

void R_ClearTpageFilter(int tpage)
{
    if (tpage >= 0 && tpage < 52) {
        s_tpageFilter[tpage] = 0;
    }
}

/* Chroma gain is a DC-only upload-time transform — GL uploads RGBA directly
 * and the SDL path renders true Add Signed, so there is nothing to correct. */
void R_SetTpageSatBoost(int tpage, int k256)
{
    (void)tpage;
    (void)k256;
}

void R_SetCullMode(R_CullMode mode)
{
    s_desired.cullMode = mode;
}

void R_SetAlphaTest(int enable)
{
    s_desired.alphaTest = enable;
}

void R_SetAlphaRef(float ref)
{
    s_desired.alphaRef = ref;
}

void R_SetScissor(int x, int y, int w, int h)
{
    s_desired.scissorEnabled = 1;
    s_desired.scissorX = x;
    s_desired.scissorY = y;
    s_desired.scissorW = w;
    s_desired.scissorH = h;
}

void R_DisableScissor(void)
{
    s_desired.scissorEnabled = 0;
}

void R_PushState(void)
{
    if (s_stackDepth < R_STATE_STACK_DEPTH) {
        s_stateStack[s_stackDepth++] = s_desired;
    }
}

void R_PopState(void)
{
    if (s_stackDepth > 0) {
        s_desired = s_stateStack[--s_stackDepth];
    }
}

void R_DebugGetState(int *depthTest, int *depthWrite, int *depthFunc,
                     int *blendMode, int *alphaTest, float *alphaRef)
{
    if (depthTest)  *depthTest  = s_desired.depthTest;
    if (depthWrite) *depthWrite = s_desired.depthWrite;
    if (depthFunc)  *depthFunc  = (int)s_desired.depthFunc;
    if (blendMode)  *blendMode  = (int)s_desired.blendMode;
    if (alphaTest)  *alphaTest  = s_desired.alphaTest;
    if (alphaRef)   *alphaRef   = s_desired.alphaRef;
}

#ifdef SONICR_XBOX
void R_InvalidateBoundTexture(void)
{
    s_current.textureId = -99;   /* force the next flush to glEnable+glBindTexture */
}
#endif

void R_ResetState(void)
{
    s_desired = s_defaults;
    /* Force full re-sync on next R_FlushState by invalidating current.
     * Can't use plain memset(0xFF) because 0xFFFFFFFF == -1, which matches
     * s_defaults.textureId — so textureId would appear "unchanged" and
     * R_FlushState would skip the glDisable(GL_TEXTURE_2D) call. */
    memset(&s_current, 0xFF, sizeof(s_current));
    s_current.textureId = -99;   /* won't match any valid id or -1 */
    s_current.alphaRef = -1.0f;  /* ensure float comparison triggers */
    /* Resident texture-object filters are not part of this snapshot; drop
     * what we believe about them so the next bind re-applies. */
    memset(s_glTextureFilter, 0, sizeof(s_glTextureFilter));
    s_stackDepth = 0;
}

/* =====================================================================
 * Geometry submission — immediate draw
 * ===================================================================== */

/* Emit a single vertex via GL immediate mode.
 * Uses glVertex4f with W=1/RHW for perspective-correct interpolation. */
#ifdef SONICR_XBOX
extern int g_dbgMode;
#endif
static inline void R_EmitVertex(const RenderVertex *v)
{
    uint32_t argb = v->color;
    float a = (float)((argb >> 24) & 0xFF) / 255.0f;
    float r = (float)((argb >> 16) & 0xFF) / 255.0f;
    float g = (float)((argb >>  8) & 0xFF) / 255.0f;
    float b = (float)((argb      ) & 0xFF) / 255.0f;
    glColor4f(r, g, b, a);
    glTexCoord2f(v->u, v->v);

    float w = (v->rhw > 0.0f) ? (1.0f / v->rhw) : 1.0f;
    glVertex4f(v->sx * w, v->sy * w, v->sz * w, w);
}

#ifdef SONICR_XBOX
/* Per-tpage draw statistics for on-hardware debugging (see platform_gl_swap). */
int   g_dbgMode;     /* Back button cycles 0..4 for on-hardware experiments */
int   g_dbgDraws[53];
int   g_dbgBigQuads;   /* draws with a vertex far outside the screen */
float g_dbgVert[53][6];
#endif

#ifdef SONICR_XBOX
/*
 * Polygons that pass very close to the camera project to huge or wild screen
 * coordinates (thousands of pixels outside the screen). The NV2A rasteriser
 * does not cope with those: the textures smear into horizontal streaks or the
 * floor vanishes. Such polygons are clipped here against a rectangle slightly
 * larger than the screen (Sutherland-Hodgman, convex polygons). Depth and
 * 1/w interpolate linearly in screen space, u/w and v/w too, so the result is
 * exact for what is visible.
 */
int g_dbgClipped;
typedef struct { float x, y, z, rhw, uw, vw, a, r, g, b; } XbClipV;

static int xb_clip_edge(const XbClipV *in, int n, XbClipV *out, int axis, float lim, int keepGreater)
{
    int m = 0;
    for (int i = 0; i < n; i++) {
        const XbClipV *a = &in[i];
        const XbClipV *b = &in[(i + 1) % n];
        float av = axis ? a->y : a->x;
        float bv = axis ? b->y : b->x;
        int ain = keepGreater ? (av >= lim) : (av <= lim);
        int bin = keepGreater ? (bv >= lim) : (bv <= lim);
        if (ain) out[m++] = *a;
        if (ain != bin) {
            float t = (lim - av) / (bv - av);
            XbClipV *o = &out[m++];
            o->x = a->x + (b->x - a->x) * t;
            o->y = a->y + (b->y - a->y) * t;
            o->z = a->z + (b->z - a->z) * t;
            o->rhw = a->rhw + (b->rhw - a->rhw) * t;
            o->uw = a->uw + (b->uw - a->uw) * t;
            o->vw = a->vw + (b->vw - a->vw) * t;
            o->a = a->a + (b->a - a->a) * t;
            o->r = a->r + (b->r - a->r) * t;
            o->g = a->g + (b->g - a->g) * t;
            o->b = a->b + (b->b - a->b) * t;
        }
    }
    return m;
}

/* Returns the vertex count written to buf (10 floats per vertex, ready for
 * xb_draw_fan), or 0 when the polygon is entirely outside the clip rectangle. */
static int xb_clip_to_screen(const RenderVertex *v, int count, float *buf)
{
    XbClipV A[40], B[40];
    const float k = 1.0f / 255.0f;
    for (int i = 0; i < count; i++) {
        uint32_t argb = v[i].color;
        A[i].x = v[i].sx; A[i].y = v[i].sy; A[i].z = v[i].sz; A[i].rhw = v[i].rhw;
        A[i].uw = v[i].u * v[i].rhw; A[i].vw = v[i].v * v[i].rhw;
        A[i].a = (float)((argb >> 24) & 0xFF) * k;
        A[i].r = (float)((argb >> 16) & 0xFF) * k;
        A[i].g = (float)((argb >>  8) & 0xFF) * k;
        A[i].b = (float)((argb      ) & 0xFF) * k;
    }
    int n = count;
    n = xb_clip_edge(A, n, B, 0, -128.0f, 1);  if (n < 3) return 0;
    n = xb_clip_edge(B, n, A, 0, 768.0f, 0);   if (n < 3) return 0;
    n = xb_clip_edge(A, n, B, 1, -128.0f, 1);  if (n < 3) return 0;
    n = xb_clip_edge(B, n, A, 1, 608.0f, 0);   if (n < 3) return 0;
    for (int i = 0; i < n; i++) {
        float rhw = A[i].rhw;
        float w = (rhw > 0.0f) ? (1.0f / rhw) : 1.0f;
        float *o = &buf[i * 10];
        o[0] = A[i].x * w;  o[1] = A[i].y * w;  o[2] = A[i].z * w;  o[3] = w;
        o[4] = A[i].r;  o[5] = A[i].g;  o[6] = A[i].b;  o[7] = A[i].a;
        o[8] = A[i].uw * w;  o[9] = A[i].vw * w;
    }
    return n;
}
#endif
void R_DrawTriFan(const RenderVertex *v, int count)
{
    if (count < 3) {
        return;
    }
#ifdef SONICR_XBOX
    {
        int ti = (s_desired.textureId >= 0 && s_desired.textureId < 52) ? s_desired.textureId : 52;
        g_dbgDraws[ti]++;
        g_dbgVert[ti][0] = v[0].sx; g_dbgVert[ti][1] = v[0].sy; g_dbgVert[ti][2] = v[0].sz;
        g_dbgVert[ti][3] = v[0].rhw; g_dbgVert[ti][4] = (float)(v[0].color >> 24);
        g_dbgVert[ti][5] = (float)(s_desired.blendMode * 100 + s_desired.depthTest * 10 + s_desired.alphaTest);
    }
#endif

    R_FlushState();
#ifdef SONICR_XBOX
    for (int i = 0; i < count; i++) {
        if (v[i].sx < -2000.0f || v[i].sx > 2640.0f || v[i].sy < -2000.0f || v[i].sy > 2480.0f) {
            g_dbgBigQuads++;
            {
                static int s_bigLog;
                if (s_bigLog < 40) {
                    s_bigLog++;
                    fprintf(stderr, "bigquad tpage %d n=%d:", s_desired.textureId, count);
                    for (int k = 0; k < count && k < 4; k++)
                        fprintf(stderr, " (%.0f,%.0f rhw=%g uv=%.2f,%.2f)", v[k].sx, v[k].sy, v[k].rhw, v[k].u, v[k].v);
                    fprintf(stderr, "\n");
                }
            }
            break;
        }
    }
    if (count <= 16) {
        float buf[16 * 10];
        int needClip = 0;
        for (int i = 0; i < count; i++) {
            if (!(v[i].rhw > 0.0f) ||
                v[i].sx < -256.0f || v[i].sx > 896.0f || v[i].sy < -256.0f || v[i].sy > 736.0f) {
                needClip = 1;
                break;
            }
        }
        if (needClip && g_dbgMode == 4) {   /* A/B test: White+Back cycles to mode 4 */
            int ok = 1;
            for (int i = 0; i < count; i++) if (!(v[i].rhw > 0.0f)) ok = 0;
            if (ok) {
                float cbuf[40 * 10];
                int n = xb_clip_to_screen(v, count, cbuf);
                g_dbgClipped++;
                if (n >= 3) xb_draw_fan(cbuf, n);
                return;
            }
        }
        for (int i = 0; i < count; i++) {
            uint32_t argb = v[i].color;
            float w = (v[i].rhw > 0.0f) ? (1.0f / v[i].rhw) : 1.0f;
            float *o = &buf[i * 10];
            o[0] = v[i].sx * w;  o[1] = v[i].sy * w;  o[2] = v[i].sz * w;  o[3] = w;
            /* multiply, not divide: four FDIVs per vertex added up */
            const float k = 1.0f / 255.0f;
            o[4] = (float)((argb >> 16) & 0xFF) * k;
            o[5] = (float)((argb >>  8) & 0xFF) * k;
            o[6] = (float)((argb      ) & 0xFF) * k;
            o[7] = (float)((argb >> 24) & 0xFF) * k;
            o[8] = v[i].u;  o[9] = v[i].v;
        }
        xb_draw_fan(buf, count);
        return;
    }
    XB_BEND();
#endif

    glBegin(GL_TRIANGLE_FAN);
    for (int i = 0; i < count; i++) {
        R_EmitVertex(&v[i]);
    }
    glEnd();
}

void R_DrawTri(const RenderVertex v[3])
{
    R_DrawTriFan(v, 3);
}

void R_DrawQuad(const RenderVertex v[4])
{
    R_DrawTriFan(v, 4);
}

/* =====================================================================
 * 2D quad helpers
 * ===================================================================== */

void R_DrawQuad2D(float x0, float y0, float x1, float y1,
                  float u0, float v0, float u1, float v1,
                  float z, uint32_t color)
{
    float rhw = (z > 0.0f) ? (1.0f / z) : 1.0f;
    float farSafe = (g_farClipFloat > 0.0f) ? g_farClipFloat : 1.0f;
    float normZ = z / farSafe;

    RenderVertex v[4];
    v[0] = (RenderVertex){ x0, y0, normZ, rhw, color, 0, u0, v0 };
    v[1] = (RenderVertex){ x1, y0, normZ, rhw, color, 0, u1, v0 };
    v[2] = (RenderVertex){ x1, y1, normZ, rhw, color, 0, u1, v1 };
    v[3] = (RenderVertex){ x0, y1, normZ, rhw, color, 0, u0, v1 };
    R_DrawQuad(v);
}

void R_DrawQuad2DSolid(float x0, float y0, float x1, float y1,
                       float z, uint32_t color)
{
    /* Temporarily force untextured rendering */
    int savedTex = s_desired.textureId;
    s_desired.textureId = -1;

    float rhw = (z > 0.0f) ? (1.0f / z) : 1.0f;
    float farSafe = (g_farClipFloat > 0.0f) ? g_farClipFloat : 1.0f;
    float normZ = z / farSafe;

    RenderVertex v[4];
    v[0] = (RenderVertex){ x0, y0, normZ, rhw, color, 0, 0.0f, 0.0f };
    v[1] = (RenderVertex){ x1, y0, normZ, rhw, color, 0, 0.0f, 0.0f };
    v[2] = (RenderVertex){ x1, y1, normZ, rhw, color, 0, 0.0f, 0.0f };
    v[3] = (RenderVertex){ x0, y1, normZ, rhw, color, 0, 0.0f, 0.0f };
    R_DrawQuad(v);

    /* Restore previous texture state */
    s_desired.textureId = savedTex;
}

/* =====================================================================
 * Texture API (forward to render_gl.c)
 * ===================================================================== */

void R_InitTextures(void)
{
    GL_InitTextures();
}

void R_UploadTexture(int tpage)
{
    GL_UploadTpage(tpage);
}

void R_MarkTextureDirty(int tpage)
{
    /* The upload this schedules will hard-set GL_NEAREST on the texture
     * object (render_gl.c:139, :247), so forget the resident filter and let
     * the next bind re-apply it. */
    if (tpage >= 0 && tpage < 52) {
        s_glTextureFilter[tpage] = 0;
    }
    GL_MarkTpageDirty(tpage);
}

void R_ClearTextureDirty(int tpage)
{
    GL_ClearTpageDirty(tpage);
}

void R_FreezeTexture(int tpage)
{
    GL_FreezeTpage(tpage);
}

void R_ThawTexture(int tpage)
{
    (void)tpage; /* GL has no persistent freeze flag */
}

void R_SetNoColorKey(int tpage)
{
    GL_SetNoColorKey(tpage);
}

void R_ClearNoColorKey(int tpage)
{
    GL_ClearNoColorKey(tpage);
}

void R_SetTpageGreen6(int tpage, int on)
{
    GL_SetTpageGreen6(tpage, on);
}

void R_SetTpageRGBA8(int tpage, unsigned char *rgba) {
    GL_SetTpageRGBA8(tpage, rgba);
}

void R_UploadTextureRGBA(int tpage, unsigned char *rgba, int w, int h)
{
    GL_UploadTpageRGBA(tpage, rgba, w, h);
}

void R_UploadTextureSubRect(int tpage, int x, int y, int w, int h)
{
    GL_UploadTpageSubRect(tpage, x, y, w, h);
}

void R_SetPendingRGBA(int tpage, unsigned char *rgba, int w, int h)
{
    GL_SetPendingRGBA(tpage, rgba, w, h);
}

void R_EndFrame(void)
{
    EndFrame();
    /* EndFrame is just glFlush — no GL state changes, tracker stays valid. */
}

void R_Flip(void)
{
    FlipD3D();
}

void R_ClearAndReset(void)
{
    ProcessTpageStates();
}

void R_ClearDepth(void)
{
    R_FlushState();
    XB_BEND();
    glClear(GL_DEPTH_BUFFER_BIT);
}

#endif /* !SONICR_SOFT_RENDER */
