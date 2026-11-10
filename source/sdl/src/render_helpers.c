/**
 * render_helpers.c - Rendering helper functions
 *
 * DrawTexturedQuad, Draw3DModelD3D, ColorizeTpageHiColor, etc.
 * These are the workhorses called dozens of times per frame.
 */

#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "sonicr_functions.h"
#include "r_types.h"
#include "r_state.h"
#include "r_draw.h"

#ifdef SONICR_DC
#include <kos.h>
#include <math.h>
#endif

extern void RenderIrisQuad(float x0, float y0, float x1, float y1,
                               float x2, float y2, float x3, float y3,
                               float depth);
extern void GL_KeepPixels(int);

extern int g_lightingRamp13bit[];

/* Per-tpage texture handles and surfaces */
extern int g_textureHandles[];      /* array of D3D texture handles */
extern int g_textureMaterialHandles[];

/* Helper: convert int bits to float */
static float IntBitsToFloat(int bits) {
    union {
        int i;
        float f;
    } u;
    u.i = bits;
    return u.f;
}

#ifdef SONICR_DC
extern __attribute__((aligned(32))) pvr_vertex_t s_ptScratch[16];
extern void R_DrawPvrTri(pvr_vertex_t *v);
extern void R_DrawPvrQuad(pvr_vertex_t *v);
extern void PVR_ColorizeTpageVRAM(int tpage, int r, int g, int b);
#endif

/**
 * DrawTexturedQuad - 0x00450C38 - 777 bytes
 * Submits a textured quad (2 triangles, 4 vertices) to the tpage batch.
 *
 * Original Watcom fastcall: EAX=xPos, EDX=yPos, then stack params.
 * The xPos/yPos were hidden register params lost in initial decompilation.
 *
 * Position formula (from binary):
 *   screenX = g_clipLeft + xPos * g_projScaleXCurrent * (1/512)
 *   screenY = g_clipTop  + yPos * g_projScaleY * (1/512)
 *   screenW = width * g_projScaleXCurrent * (1/512)
 *   screenH = height * g_projScaleY * (1/512)
 * UV formula: u = uvCoord * (1/256)
 *
 * DAT_0052c26c = 1/512 = 0.001953125 (position scale, double)
 * DAT_0052c274 = 1/256 = 0.00390625 (UV scale, float)
 */
void DrawTexturedQuad(int xPos, int yPos, int depth, int width, int height,
                      int tpage, int uvX, int uvY, int uvW, int uvH,
                      unsigned int color)
{
    if (g_tpageStateArray[tpage] != 0x04) {
        return;
    }
    // 1 / 512
    #define POS_SCALE  0.00195312f
    // 1 / 256 
    #define UV_SCALE_Q 0.00390625f

    float sx = (float)g_clipLeft + (float)xPos * (float)g_projScaleXCurrent * POS_SCALE;
    float sy = (float)g_clipTop  + (float)yPos * (float)g_projScaleY * POS_SCALE;
    float sx2 = sx + (float)width  * (float)g_projScaleXCurrent * POS_SCALE;
    float sy2 = sy + (float)height * (float)g_projScaleY * POS_SCALE;

    /* Clip test */
    if (sx2 < (float)g_clipLeft || sx > (float)g_clipRight ||
        sy2 < (float)g_clipTop  || sy > (float)g_clipBottom)
    {
        return;
    }

    float z_raw = IntBitsToFloat(depth);
    float farClip = (g_farClipFloat > 0.0f) ? g_farClipFloat : 88064.0f;
    float z = z_raw * reciprocal(farClip);
    float rhw = reciprocal(z_raw);

    /* Half-texel inset to avoid sampling beyond the intended region.
     * The original D3D rasterizer didn't sample past the last texel center;
     * GL's CLAMP_TO_EDGE still reaches the texel edge, pulling in neighbors. */
    float halfTexel = 0.5f * UV_SCALE_Q;
    float u0 = (float)uvX * UV_SCALE_Q + halfTexel;
    float v0 = (float)uvY * UV_SCALE_Q + halfTexel;
    float u1 = (float)(uvX + uvW) * UV_SCALE_Q - halfTexel;
    float v1 = (float)(uvY + uvH) * UV_SCALE_Q - halfTexel;

    /* Immediate submission - set state and draw */
    R_SetTexture(tpage);
    R_SetTexEnv(R_TEXENV_MODULATE);  /* HUD: no overbright */

    RenderVertex v[4] = {
        { sx,  sy,  z, rhw, color, 0, u0, v0 },
        { sx2, sy,  z, rhw, color, 0, u1, v0 },
        { sx2, sy2, z, rhw, color, 0, u1, v1 },
        { sx,  sy2, z, rhw, color, 0, u0, v1 },
    };
    R_DrawQuad(v);
    #undef POS_SCALE
    #undef UV_SCALE_Q
}

/* Signed division by 4096 */
#define SDIV4096(val) ((val) / 4096)

/* UV scale: poly stores byte << 16 (16.16 fixed), >> 16 gives byte (0-255).
 * Divide by 256 to get 0..1 UV range for 256-pixel tpage.
 * Half-texel offset centers on texel instead of sitting on boundary,
 * preventing bilinear filter bleed across atlas tile edges. */
static const float UV_SCALE = 1.0f / 256.0f;
static const float UV_HALF_TEXEL = 0.5f / 256.0f;

/* Build a RenderVertex from a SrcVertex with tint + gamma color processing. */
static RenderVertex BuildModelVertex(const SrcVertex *sv, int uvU16, int uvV16,
                                     float farClipSafe, int alpha)
{
    RenderVertex rv;
    int pz = sv->depth;
    SrcVertex_ProjectFloat(sv, &rv.sx, &rv.sy);
    rv.sz = (float)pz * reciprocal( farClipSafe);
    rv.rhw = reciprocal((float)pz);

    uint32_t r = (uint32_t)sv->colorR;
    uint32_t g = (uint32_t)sv->colorG;
    uint32_t b = (uint32_t)sv->colorB;
    if (g_colorTintEnable) {
        r += g_colorTintR;
        g += g_colorTintG;
        b += g_colorTintB;
    }
    r >>= 13;
    g >>= 13;
    b >>= 13;
#if 0
    #define CONTRAST_K 96

    r = (r * (256 - CONTRAST_K) + ((r * r) >> 8) * CONTRAST_K) >> 8;
    g = (g * (256 - CONTRAST_K) + ((g * g) >> 8) * CONTRAST_K) >> 8;
    b = (b * (256 - CONTRAST_K) + ((b * b) >> 8) * CONTRAST_K) >> 8;

    if (r > 255) {
        r = 255;
    }
    if (g > 255) {
        g = 255;
    }
    if (b > 255) {
        b = 255;
    }
#endif
    rv.color = (uint32_t)alpha << 24 | (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b;
    rv.specular = 0;
    rv.u = (float)(uvU16 >> 16) * UV_SCALE + UV_HALF_TEXEL;
    rv.v = (float)(uvV16 >> 16) * UV_SCALE + UV_HALF_TEXEL;
    return rv;
}

#ifdef SONICR_DC

static inline void modify_model_color(uint32_t c, uint32_t *bc, uint32_t *oc) {
    uint32_t cr = (c >> 16) & 0xff;
    uint32_t cg = (c >> 8) & 0xff;
    uint32_t cb = (c) & 0xff;

    uint32_t br, bg, bb, ocr, ocg, ocb;

    // ADD_SIGNED approximation with PowerVR specular/offset color
    // for Saturn-style vertex colors, where:
    // = 0x7f - full bright
    // > 0x7f - extended bright
    // < 0x7f - extended dark
    //
    // for each color component, split color into base and offset portions
    // base is the portion of the color component <= 0x7f
    // offset is the extended portion of the component > 0x7f
    //
    // base is used as the final diffuse color for vertex
    // offset is used as the specular/offset color for vertex
    //
    // intermediate offset is `max(component - 0x7F, 0)`
    // final offset is `intermediate offset - (intermediate offset / 8)`
    // intermediate base is `(component - intermediate offset)`
    // final base is `0xE0 - (0x7f - (component - intermediate offset))`
    // where `0xE0` is the color component assigned to all geometry by default
    // and `component` is the value of the tinted color component being applied to the geometry
    // which simplifies to `0x60 + (component - offset)`

    // originally:
    // bc = cc > 0x7F ? 0x7F : cc;
    // occ = cc > 0x7F ? (cc - 0x7F) : 0;
    // bc = 0xe0 - (0x7f - bc)
    // occ = occ - (occ > 3)
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
#if 0
    #define CONTRAST_K 240

    br = (br * (256 - CONTRAST_K) + ((br * br) >> 8) * CONTRAST_K) >> 8;
    bg = (bg * (256 - CONTRAST_K) + ((bg * bg) >> 8) * CONTRAST_K) >> 8;
    bb = (bb * (256 - CONTRAST_K) + ((bb * bb) >> 8) * CONTRAST_K) >> 8;

    if (br > 255) {
        br = 255;
    }
    if (bg > 255) {
        bg = 255;
    }
    if (bb > 255) {
        bb = 255;
    }
#endif
    ocr -= (ocr >> 3);
    ocg -= (ocg >> 3);
    ocb -= (ocb >> 3);

    *bc  = (c & 0xff000000) | (br << 16) | (bg << 8) | (bb << 0);
    *oc = (ocr << 16) | (ocg << 8) | ocb;
}

/* Build a pvr_vertex_t directly into a scratch slot. Same color/UV logic
 * as BuildModelVertex but skips the unused sz field (PVR uses pvr_vertex_t.z
 * as rhw - no separate depth field) and uses fsrra for the rhw reciprocal.
 * Caller sets pv->flags after - different slots get EOL. */
static inline void BuildModelVertexPvr(pvr_vertex_t *pv, const SrcVertex *sv,
                                       int uvU16, int uvV16, int alpha)
{
    int pz = sv->depth;
    if (pz < 1) {
        pz = 1;                  /* guard fsrra against 0 */
    }

    uint32_t r = (uint32_t)sv->colorR;
    uint32_t g = (uint32_t)sv->colorG;
    uint32_t b = (uint32_t)sv->colorB;
    if (g_colorTintEnable) {
        r += g_colorTintR;
        g += g_colorTintG;
        b += g_colorTintB;
    }

    r >>= 13;
    g >>= 13;
    b >>= 13;

    if (r > 255) {
        r = 255;
    }
    if (g > 255) {
        g = 255;
    }
    if (b > 255) {
        b = 255;
    }

    SrcVertex_ProjectFloat(sv, &pv->x, &pv->y);
    pv->z = reciprocal(pz);        /* rhw - fsrra */
    pv->u = (float)(uvU16 >> 16) * UV_SCALE + UV_HALF_TEXEL;
    pv->v = (float)(uvV16 >> 16) * UV_SCALE + UV_HALF_TEXEL;

    uint32_t baseColor = (alpha << 24) | (r << 16) | (g << 8) | b;
    modify_model_color(baseColor, &pv->argb, &pv->oargb);
}
#endif

/**
 * SubmitTriD3D - FUN_0044f0f4 - 1165 bytes (simplified)
 * Writes one triangle (3 D3DTLVERTEX) to the per-tpage vertex/index buffers.
 * poly = polygon struct pointer (stride 0x30), contains tpage at byte 0x28,
 *        UV data at int[0]..int[3] (16.16 fixed point).
 * v0, v1, v2 = projected vertex pointers (stride 0x40 = 16 ints):
 *   [0]=screenX, [1]=screenY, [2..4]=colors, [0xC]=projZ.
 * alpha = vertex alpha (0x00-0xFF).
 */
static void SubmitTriD3D(int *poly, SrcVertex *v0, SrcVertex *v1, SrcVertex *v2, int alpha)
{
    unsigned char tpage = *(unsigned char *)((char *)poly + 0x28);
    if (g_tpageStateArray[tpage] != 0x04) {
        return;
    }

    SrcVertex *verts[3] = { v0, v1, v2 };

    R_SetTexture(tpage);
    R_SetTexEnv(R_TEXENV_ADD_SIGNED);

#ifdef SONICR_DC
//    R_SetFilter(R_FILTER_LINEAR);
    BuildModelVertexPvr(&s_ptScratch[0], verts[0], poly[0], poly[1], alpha);
    BuildModelVertexPvr(&s_ptScratch[1], verts[1], poly[2], poly[3], alpha);
    BuildModelVertexPvr(&s_ptScratch[2], verts[2], poly[4], poly[5], alpha);
    s_ptScratch[0].flags = PVR_CMD_VERTEX;
    s_ptScratch[1].flags = PVR_CMD_VERTEX;
    s_ptScratch[2].flags = PVR_CMD_VERTEX_EOL;
    R_DrawPvrTri(s_ptScratch);
//    R_SetFilter(R_FILTER_NEAREST);
#else
    float farClipSafe = (g_farClipFloat > 0.0f) ? g_farClipFloat : 88064.0f;
    RenderVertex rv[3];
    for (int i = 0; i < 3; i++) {
        rv[i] = BuildModelVertex(verts[i], poly[i * 2], poly[i * 2 + 1], farClipSafe, alpha);
    }
    R_DrawTri(rv);
#endif
}

static void SubmitQuadD3D(int *poly, SrcVertex *v0, SrcVertex *v1, SrcVertex *v2, SrcVertex *v3, int alpha)
{
    unsigned char tpage = *(unsigned char *)((char *)poly + 0x28);
    if (g_tpageStateArray[tpage] != 0x04) {
        return;
    }

    SrcVertex *verts[4] = { v0, v1, v2, v3 };

    R_SetTexture(tpage);
    R_SetTexEnv(R_TEXENV_ADD_SIGNED);

#ifdef SONICR_DC
//    R_SetFilter(R_FILTER_LINEAR);
    /* PVR strip order TL,TR,BR,BL → 0,1,3,2: src verts 0,1,2,3 land in
     * scratch slots 0,1,3,2. Last physical slot (index 3) is EOL. */
    BuildModelVertexPvr(&s_ptScratch[0], verts[0], poly[0], poly[1], alpha);
    BuildModelVertexPvr(&s_ptScratch[1], verts[1], poly[2], poly[3], alpha);
    BuildModelVertexPvr(&s_ptScratch[3], verts[2], poly[4], poly[5], alpha);
    BuildModelVertexPvr(&s_ptScratch[2], verts[3], poly[6], poly[7], alpha);
    s_ptScratch[0].flags = PVR_CMD_VERTEX;
    s_ptScratch[1].flags = PVR_CMD_VERTEX;
    s_ptScratch[2].flags = PVR_CMD_VERTEX;
    s_ptScratch[3].flags = PVR_CMD_VERTEX_EOL;
    R_DrawPvrQuad(s_ptScratch);
//    R_SetFilter(R_FILTER_NEAREST);
#else
    float farClipSafe = (g_farClipFloat > 0.0f) ? g_farClipFloat : 88064.0f;
    RenderVertex rv[4];
    for (int i = 0; i < 4; i++) {
        rv[i] = BuildModelVertex(verts[i], poly[i * 2], poly[i * 2 + 1], farClipSafe, alpha);
    }
    R_DrawQuad(rv);
#endif
}

/**
 * Draw3DModelD3D - FUN_00454c70 - 2376 bytes
 * Transforms and renders a 3D character/object model.
 *
 * Builds a rotation matrix from up to 3 Euler angles, transforms all
 * vertices from model space to screen space, then iterates polygons
 * doing backface culling, clip testing, and D3D vertex buffer submission.
 *
 * Watcom fastcall: EAX, EDX, EBX, ECX then stack.
 *   EAX = xOffset  - X projection offset
 *   EDX = yOffset  - Y projection offset
 *   EBX = zBase    - Z camera distance (projection base)
 *   ECX = angleA   - 3rd Euler angle (12-bit, indexes sin/cos tables)
 *   [ebp+08] = rotation - 1st Euler angle (main yaw rotation)
 *   [ebp+0C] = angleC   - 2nd Euler angle
 *   [ebp+10] = objPtr   - object struct (0x44 bytes per entry)
 *   [ebp+14],[ebp+18] unused (always 0)
 *   [ebp+1C] = scale    - vertex scale divisor (0 = no scale)
 */
void Draw3DModelD3D(int xOffset, int yOffset, int zBase, int angleA,
                    int rotation, int angleC, intptr_t objPtr, int scale)
{
    if (objPtr == 0) {
        return;
    }
    int *obj = (int *)objPtr;
    float rfScale = reciprocal((float)scale);

    unsigned int vertCount = (unsigned int)*(unsigned short *)((char *)obj + 0x3A);
    unsigned int polyCount = (unsigned int)*(unsigned short *)((char *)obj + 0x32);

    if (vertCount == 0 || polyCount == 0) {
        return;
    }

    /* The far clip threshold = g_farClipTimes8.
     * Vertices with projZ > this are clipped. */
    int farClipThresh = g_farClipDepth << 3;
    if (farClipThresh <= 0) {
        farClipThresh = 0x15800; /* 88064 = 0x2B00 << 3 */
    }

    /* ---------------------------------------------------------------
     * 1. Build rotation matrix from 3 Euler angles
     *    Binary param mapping: A=rotation([ebp+8]), B=angleC([ebp+C]),
     *    C=angleA(ECX). All >> 4 to scale from 16384-range to 1024-range.
     * --------------------------------------------------------------- */
    int sinA = g_sinTable[rotation & 0xFFF] >> 4;   /* sin(rotation) / 16 */
    int cosA = g_cosTable[rotation & 0xFFF] >> 4;   /* cos(rotation) / 16 */
    int sinB = g_sinTable[angleC & 0xFFF] >> 4;     /* sin(angleC) / 16 */
    int cosB = g_cosTable[angleC & 0xFFF] >> 4;     /* cos(angleC) / 16 */
    int sinC = g_sinTable[angleA & 0xFFF] >> 4;     /* sin(angleA) / 16 */
    int cosC = g_cosTable[angleA & 0xFFF] >> 4;     /* cos(angleA) / 16 */

    /* Matrix elements
     * With only A(rotation) non-zero, this gives Y-axis turntable rotation. */
    int m00 = cosA * cosB >> 8;                      /* iVar14 */
    int m01 = (sinB * cosA * cosC >> 18) + (sinA * sinC >> 8);  /* iVar15 */
    int m02 = (sinB * cosA * sinC >> 18) + (-sinA * cosC >> 8); /* iVar16 */
    int m10 = -sinB;                                 /* iVar17 */
    int m11 = cosB * cosC >> 8;                      /* iVar18 */
    int m12 = cosB * sinC >> 8;                      /* iVar19 */
    int m20 = cosB * sinA >> 8;                      /* iVar6 reused */
    int m21 = (-sinC * cosA >> 8) + (sinA * sinB * cosC >> 18); /* iVar20 */
    int m22 = (sinA * sinB * sinC >> 18) + (cosC * cosA >> 8);  /* iVar1 reused */

    /* ---------------------------------------------------------------
     * 2. Get polygon and vertex base addresses
     * --------------------------------------------------------------- */
    intptr_t polyBase = (intptr_t)((unsigned int)*(unsigned short *)((char *)obj + 0x30) * 0x30 +
                        (intptr_t)g_polygonArrayBase);
    SrcVertex *vtxBase = &g_vertexArrayBase[(unsigned int)*(unsigned short *)((char *)obj + 0x38)];

    /* ---------------------------------------------------------------
     * 3. Transform all vertices to screen space
     *    Three modes based on short at obj+0x2E:
     *      mode 2: direct (no position offset in vertex data)
     *      mode 1: subtract obj pivot (obj[8..10])
     *      mode 0: subtract obj position (obj[0..2])
     * --------------------------------------------------------------- */
    int mode = *(short *)((char *)obj + 0x2E);
    SrcVertex *vtx = vtxBase;

    for (unsigned int vi = 0; vi < vertCount; vi++) {
        int vx, vy, vz;

        if (mode == 2) {
            /* Mode 2: use vertex coords directly */
            vx = vtx->posX; vy = vtx->posY; vz = vtx->posZ;
        }
        else if (mode == 1) {
            /* Mode 1: subtract obj pivot (offsets 0x20, 0x24, 0x28 = ints 8, 9, 10) */
            vx = vtx->posX - obj[8]; vy = vtx->posY - obj[9]; vz = vtx->posZ - obj[10];
        }
        else {
            /* Mode 0: subtract obj position (offsets 0, 4, 8 = ints 0, 1, 2) */
            vx = vtx->posX - obj[0]; vy = vtx->posY - obj[1]; vz = vtx->posZ - obj[2];
        }

        /* Scale if requested */
        if (scale != 0) {
            vx = vx * rfScale; // / scale;
            vy = vy * rfScale; // / scale;
            vz = vz * rfScale; // / scale;
        }

        /* Rotate: compute camera-space Z (decompile row: vz*m22 + vy*m12 + vx*m02) */
        int rz_raw = vz * m22 + vy * m12 + vx * m02;
        int projZ = zBase + SDIV4096(rz_raw);
        vtx->depth = projZ;

        if (projZ > 0) {
            float rfProjZ = reciprocal((float)projZ);
            /* Rotate X and Y */
            int rx_raw = vz * m20 + vy * m10 + vx * m00;
            int ry_raw = vy * m11 + vx * m01 + vz * m21;

            int cx = xOffset + SDIV4096(rx_raw);
            int cy = yOffset + SDIV4096(ry_raw);
            vtx->camX = cx;
            vtx->camY = cy;

            /* Project to screen - D3D left-handed: negate X for GL right-handed */
            vtx->screenX = g_screenCenterX +
                      (cx * g_projScaleXCurrent) * rfProjZ; // / projZ;
            vtx->screenY = g_screenCenterY -
                      (cy * g_projScaleY) * rfProjZ; // / projZ;
        }

        vtx++; /* stride 0x40 bytes = one SrcVertex */
    }

    /* ---------------------------------------------------------------
     * 4. Emerald track special vertex animation (track 5 only)
     * --------------------------------------------------------------- */
    if (g_trackId == TRACK_RADIANT_EMERALD) {
        SrcVertex *ev = &g_vertexArrayBase[(unsigned int)*(unsigned short *)((char *)obj + 0x38)];
        for (unsigned int evi = 0; evi < vertCount; evi++) {
            if (ev->depth > 0) {
                ev->colorR = g_sinTable[(ev->posX + g_emeraldSineOffX) & 0xFFF] * 0x40 + 0x100020;
                ev->colorG = g_sinTable[(ev->posY + g_emeraldSineOffY) & 0xFFF] * 0x40 + 0x100020;
                ev->colorB = g_sinTable[(ev->posZ + g_emeraldSineOffZ) & 0xFFF] * 0x40 + 0x100020;
            }
            ev++;
        }
    }

    /* ---------------------------------------------------------------
     * 5. Iterate polygons: backface cull, clip test, submit
     *    Polygon stride = 0x30 (48 bytes). Key fields:
     *      +0x20 (ushort): vertex index 0
     *      +0x22 (ushort): vertex index 1
     *      +0x24 (ushort): vertex index 2
     *      +0x26 (ushort): vertex index 3 (quad only)
     *      +0x28 (byte):   tpage index
     *      +0x2E (byte):   flags (bit0=quad, bit2=double-sided, bit1=alt winding)
     * --------------------------------------------------------------- */
    int *polyPtr = (int *)polyBase;

    for (unsigned int pi = 0; pi < polyCount; pi++) {
        char *pp = (char *)polyPtr;

        /* Check tpage is ready */
        if (g_tpageStateArray[*(unsigned char *)(pp + 0x28)] != 0x04) {
            goto next_poly;
        }

        /* Get vertex pointers */
        SrcVertex *pv0 = &g_vertexArrayBase[(unsigned int)*(unsigned short *)(pp + 0x20)];
        if (pv0->depth <= 0 || pv0->depth > farClipThresh) {
            goto next_poly;
        }

        SrcVertex *pv1 = &g_vertexArrayBase[(unsigned int)*(unsigned short *)(pp + 0x22)];
        if (pv1->depth <= 0 || pv1->depth > farClipThresh) {
            goto next_poly;
        }

        SrcVertex *pv2 = &g_vertexArrayBase[(unsigned int)*(unsigned short *)(pp + 0x24)];
        if (pv2->depth <= 0 || pv2->depth > farClipThresh) {
            goto next_poly;
        }

        unsigned char flags = *(unsigned char *)(pp + 0x2E);

        /* Triangle */
        if ((flags & 1) == 0) {
            /* Backface cull (cross product Z component) */
            if ((flags & 4) == 0) {
                int cross = (pv0->screenY - pv1->screenY) * (pv2->screenX - pv1->screenX) -
                            (pv0->screenX - pv1->screenX) * (pv2->screenY - pv1->screenY);
                if (cross < 0) {
                    goto next_poly;
                }
            }

            /* Clip test: at least one vertex must be in bounds */
            if ((pv0->screenX < g_clipLeft && pv1->screenX < g_clipLeft && pv2->screenX < g_clipLeft) ||
                (pv0->screenY < g_clipTop && pv1->screenY < g_clipTop && pv2->screenY < g_clipTop) ||
                (pv0->screenX > g_clipRight && pv1->screenX > g_clipRight && pv2->screenX > g_clipRight) ||
                (pv0->screenY > g_clipBottom && pv1->screenY > g_clipBottom && pv2->screenY > g_clipBottom))
            {
                goto next_poly;
            }

            SubmitTriD3D(polyPtr, pv0, pv1, pv2, 0xFF);
        }
        /* Quad - need 4th vertex */
        else {
            SrcVertex *pv3 = &g_vertexArrayBase[(unsigned int)*(unsigned short *)(pp + 0x26)];
            if (pv3->depth <= 0 || pv3->depth > farClipThresh) {
                goto next_poly;
            }

            /* Backface cull for quads */
            if ((flags & 4) == 0) {
                int cross;
                if ((flags & 2) == 0) {
                    cross = (pv0->screenY - pv1->screenY) * (pv2->screenX - pv1->screenX) -
                            (pv0->screenX - pv1->screenX) * (pv2->screenY - pv1->screenY);
                    if (cross < 0) {
                        goto next_poly;
                    }
                }
                else {
                    /* Alt winding: non-planar quad - check both sub-triangles.
                     * Only cull if BOTH (0-1-2) and (0-2-3) are backfacing. */
                    cross = (pv0->screenY - pv1->screenY) * (pv2->screenX - pv1->screenX) -
                            (pv0->screenX - pv1->screenX) * (pv2->screenY - pv1->screenY);
                    if (cross < 0) {
                        cross = (pv0->screenY - pv2->screenY) * (pv3->screenX - pv2->screenX) -
                                (pv0->screenX - pv2->screenX) * (pv3->screenY - pv2->screenY);
                        if (cross < 0) {
                            goto next_poly;
                        }
                    }
                }
            }

            /* Clip test for quad */
            if ((pv0->screenX < g_clipLeft && pv1->screenX < g_clipLeft &&
                 pv2->screenX < g_clipLeft && pv3->screenX < g_clipLeft) ||
                (pv0->screenY < g_clipTop && pv1->screenY < g_clipTop &&
                 pv2->screenY < g_clipTop && pv3->screenY < g_clipTop) ||
                (pv0->screenX > g_clipRight && pv1->screenX > g_clipRight &&
                 pv2->screenX > g_clipRight && pv3->screenX > g_clipRight) ||
                (pv0->screenY > g_clipBottom && pv1->screenY > g_clipBottom &&
                 pv2->screenY > g_clipBottom && pv3->screenY > g_clipBottom))
            {
                goto next_poly;
            }

            SubmitQuadD3D(polyPtr, pv0, pv1, pv2, pv3, 0xFF);
        }

    next_poly:
        polyPtr = (int *)((char *)polyPtr + 0x30);
    }
}

/* =====================================================================
 * RenderBalloonModelForResultsScreen - FUN_0045f334 - 1989 bytes
 * Transforms and renders the 52-vertex / 34-quad balloon mesh at an arbitrary
 * screen position with 3-angle rotation, optional vertex scaling, and
 * per-vertex coloring. Sole caller is DrawResultsBalloonsForPlayer (Balloon results
 * screen); the in-race balloons go through RenderBalloonModelForRace instead.
 *
 * The binary branches here on render mode: this is the software-path twin
 * (0x45F334). The D3D twin is 0x4488DC, untranslated - both take identical
 * arguments and are pure-integer, so they produce the same geometry.
 *
 * Params (Watcom fastcall, ret 0x1c = 7 stack params):
 *   EAX=xOff, EDX=yOff, EBX=zBase, ECX=angleA,
 *   stack: angleB, angleC, scaleDivisor, colorR, colorG, colorB
 * ===================================================================== */
void RenderBalloonModelForResultsScreen(int xOff, int yOff, int zBase,
                                 int angleA, int angleB, int angleC,
                                 int scaleDivisor,
                                 int colorR, int colorG, int colorB)
{
    /* Trig lookups - 0x0045f345-0x0045f3b3 */
    /* DELIBERATE LOCAL DIVERGENCE - restores the true sin/cos peak here only.
     *
     * g_sinTable's peaks are trunc()'d to +-16383 (see sin_table_data.h), so
     * (cos>>4) is 1023 not 1024 and the rotation matrix scales by
     * (1023*1023)>>8 = 4088 instead of 4096. This renderer divides the model
     * down by scaleDivisor FIRST, which puts the balloon's knot verts at
     * vx = +-5/3 = +-1; +-1*4088 then truncates to 0 in the rx/4096 divide
     * below, collapsing all four knot quads to zero-width lines. The knot
     * vanishes. Verified against the real EXE, which draws it (2026-07-31).
     *
     * Correcting the table globally makes this right but desyncs the attract
     * demos - our physics has a separate divergence the flat peak cancels.
     * Rather than re-open that, the peak is fixed here, where it affects only
     * the Balloon results models. RenderBalloonModelForRace (in-race balloons) needs
     * no such fix: it draws unscaled, so vx = +-5 and +-20440/4096 = +-4
     * survives the truncation either way.
     *
     * DELETE THIS once the physics divergence is found and the table peaks can
     * be corrected at the source. */
#define BALLOON_TRIG_PEAK(v) ((v) == 16383 ? 16384 : ((v) == -16383 ? -16384 : (v)))
    int sA = BALLOON_TRIG_PEAK(g_sinTable[angleA & 0xFFF]) >> 4;  /* EAX=0x0045f34e */
    int cA = BALLOON_TRIG_PEAK(g_cosTable[angleA & 0xFFF]) >> 4;  /* EAX=0x0045f355 */
    int sB = BALLOON_TRIG_PEAK(g_sinTable[angleB & 0xFFF]) >> 4;  /* EAX=0x0045f358 */
    int cB = BALLOON_TRIG_PEAK(g_cosTable[angleB & 0xFFF]) >> 4;  /* EAX=0x0045f365 */
    int sC = BALLOON_TRIG_PEAK(g_sinTable[angleC & 0xFFF]) >> 4;  /* EAX=0x0045f371 */
    int cC = BALLOON_TRIG_PEAK(g_cosTable[angleC & 0xFFF]) >> 4;  /* EAX=0x0045f36e */

    /* Build 3x3 rotation matrix - 0x0045f37b-0x0045f44c */
    int mtx0 = (cB * cC) >> 8;                                           /* [ebp-0x4c] */
    int mtx1 = -sC;                                                       /* [ebp-0x40] */
    int mtx2 = (cC * sB) >> 8;                                           /* [ebp-0x34] */
    int sC_cB = sC * cB;                                                  /* intermediate */
    int sB_sC = sB * sC;                                                  /* intermediate */
    int mtx3 = ((sC_cB * cA) >> 18) + ((sB * sA) >> 8);                  /* [ebp-0x48] */
    int mtx4 = (cC * cA) >> 8;                                            /* [ebp-0x3c] */
    int mtx5 = ((-sA * cB) >> 8) + ((sB_sC * cA) >> 18);                /* [ebp-0x30] */
    int mtx6 = ((-sB * cA) >> 8) + ((sC_cB * sA) >> 18);                /* [ebp-0x44] */
    int mtx7 = (cC * sA) >> 8;                                            /* [ebp-0x38] */
    int mtx8 = ((sA * sB_sC) >> 18) + ((cA * cB) >> 8);                  /* [ebp-0x2c] */

    /* Vertex transform loop - 52 vertices, 0x0045f451-0x0045f5f5 */
    SrcVertex *vtx = (SrcVertex *)s_balloonModelVtx;   /* 0x508940 */
    const int *nrm = s_balloonModelNormal;              /* 0x4ff788 */
    for (int vi = 0; vi < 52; vi++) {
        SrcVertex *v = &vtx[vi];
        int vx, vy, vz;

        if (scaleDivisor != 0) {
            /* Scaled path - 0x0045f457-0x0045f47f */
            vx = v->posX / scaleDivisor;
            vy = v->posY / scaleDivisor;
            vz = v->posZ / scaleDivisor;
        }
        else {
            /* Unscaled path - 0x0045f5f7-0x0045f606 */
            vx = v->posX;
            vy = v->posY;
            vz = v->posZ;
        }

        /* Z rotation - 0x0045f482-0x0045f4b4 */
        int rz = mtx6 * vx + mtx7 * vy + mtx8 * vz;
        int projZ = zBase + rz / 4096;
        v->depth = projZ;

        if (projZ > 0) {
            /* X rotation - 0x0045f4ca-0x0045f4fa */
            int rx = mtx0 * vx + mtx1 * vy + mtx2 * vz;
            int rxd = xOff + rx / 4096;

            /* Y rotation - 0x0045f4ff-0x0045f528 */
            int ry = mtx3 * vx + mtx4 * vy + mtx5 * vz;
            int ryd = yOff + ry / 4096;

            /* Camera-space coords. The binary has no equivalent - it rasterizes
             * from the integer screenX/screenY below. SubmitQuadD3D re-projects
             * through SrcVertex_ProjectFloat, which reads camX/camY, so they
             * must be stored or every vertex collapses onto the screen centre. */
            v->camX = rxd;
            v->camY = ryd;

            /* Perspective projection - 0x0045f52a-0x0045f561 */
            v->screenX = g_screenCenterX + (rxd * g_projScaleXCurrent) / projZ;
            v->screenY = g_screenCenterY - (ryd * g_projScaleY) / projZ;
        }

        /* Per-vertex color - 0x0045f564-0x0045f5e5 */
        int ns = nrm[vi];  /* normal scale factor */
        int cr = (colorR * ns) / 256 + 0x80000;
        if (cr > 0x1FFFFF) {
            cr = 0x1FFFFF;
        }
        v->colorR = cr;

        int cg = (colorG * ns) / 256 + 0x80000;
        if (cg > 0x1FFFFF) {
            cg = 0x1FFFFF;
        }
        v->colorG = cg;

        int cb = (colorB * ns) / 256 + 0x80000;
        if (cb > 0x1FFFFF) {
            cb = 0x1FFFFF;
        }
        v->colorB = cb;
    }

    /* Face loop - 34 quads, 0x0045f786-0x0045faea */
    unsigned char *faceBase = (unsigned char *)s_balloonModelPoly;  /* 0x509640 */
    for (int fi = 0; fi < 34; fi++) {
        unsigned char *face = faceBase + fi * 0x30;
        unsigned short *idx = (unsigned short *)(face + 0x20);

        /* Look up 4 vertex pointers - 0x0045f793-0x0045f819 */
        SrcVertex *v0 = &vtx[idx[0]];
        SrcVertex *v1 = &vtx[idx[1]];
        SrcVertex *v2 = &vtx[idx[2]];
        SrcVertex *v3 = &vtx[idx[3]];

        /* Z visibility - all 4 must be in [1, g_farClipDepth] - 0x0045f7a3-0x0045f82b */
        if (v0->depth < 1 || v0->depth > g_farClipDepth) {
            continue;
        }
        if (v1->depth < 1 || v1->depth > g_farClipDepth) {
            continue;
        }
        if (v2->depth < 1 || v2->depth > g_farClipDepth) {
            continue;
        }
        if (v3->depth < 1 || v3->depth > g_farClipDepth) {
            continue;
        }

        /* Backface cull - 0x0045f831-0x0045fa21.
         *
         * g_mirrorMode selects the cross-product form; bit 1 of the face flags
         * only decides whether a failed test gets a second chance on triangle
         * (v0, v2, v3). Both flag branches share the same first-test pair -
         * 0x45F852 and 0x45F98D are the same expression, as are 0x45F8EB and
         * 0x45F9D5 - so the mirror test hoists out of the flag test. */
        unsigned char flags = face[0x2E];
        if (!(flags & 0x04)) {
            int cross;
            if (g_mirrorMode != 0) {
                /* 0x0045f852 / 0x0045f98d */
                cross = (v2->screenY - v1->screenY) * (v0->screenX - v1->screenX) -
                        (v2->screenX - v1->screenX) * (v0->screenY - v1->screenY);
            }
            else {
                /* 0x0045f8eb / 0x0045f9d5 */
                cross = (v0->screenY - v1->screenY) * (v2->screenX - v1->screenX) -
                        (v0->screenX - v1->screenX) * (v2->screenY - v1->screenY);
            }
            if (cross < 0) {
                if (!(flags & 0x02)) {
                    /* Normal winding culls outright - 0x0045f9d0 / 0x0045fa1d jl */
                    continue;
                }
                if (g_mirrorMode != 0) {
                    /* Second triangle (v0, v2, v3) - 0x0045f8a0 */
                    cross = (v3->screenY - v2->screenY) * (v0->screenX - v2->screenX) -
                            (v3->screenX - v2->screenX) * (v0->screenY - v2->screenY);
                }
                else {
                    /* 0x0045f939 */
                    cross = (v0->screenY - v2->screenY) * (v3->screenX - v2->screenX) -
                            (v0->screenX - v2->screenX) * (v3->screenY - v2->screenY);
                }
                if (cross < 0) {
                    continue;
                }
            }
        }

        /* Clip test - 4-vertex quad, 0x0045fa23-0x0045fad1 */
        if (v0->screenX < g_clipLeft && v1->screenX < g_clipLeft &&
            v2->screenX < g_clipLeft && v3->screenX < g_clipLeft)
        {
            continue;
        }
        if (v0->screenY < g_clipTop && v1->screenY < g_clipTop &&
            v2->screenY < g_clipTop && v3->screenY < g_clipTop)
        {
            continue;
        }
        if (v0->screenX > g_clipRight && v1->screenX > g_clipRight &&
            v2->screenX > g_clipRight && v3->screenX > g_clipRight)
        {
            continue;
        }
        if (v0->screenY > g_clipBottom && v1->screenY > g_clipBottom &&
            v2->screenY > g_clipBottom && v3->screenY > g_clipBottom)
        {
            continue;
        }

        /* Submit quad - 0x0045fad3-0x0045fadb */
        SubmitQuadD3D((int *)face, v0, v1, v2, v3, 0xFF);
    }
}

/**
 * DrawSplitBorder - 0x004619FC - 360 bytes
 * Draws lines between split-screen viewports.
 */
void DrawSplitBorder(void)
{
    /* Horizontal border for 2P horizontal split or 3+P */
    if ((g_numHumans == 2 && g_viewportIndex == 0) || g_numHumans > 2) {  /* 0x461A1E */
        int y0, y1;
        if (g_screenWidth < 321) {
            y0 = g_dispCenterY - 1;
            y1 = g_dispCenterY;
        }
        else {
            y0 = g_dispCenterY - 2;
            y1 = g_dispCenterY + 1;
        }
        RenderIrisQuad(0, (float)y0, (float)g_screenWidth, (float)y0,
                           (float)g_screenWidth, (float)y1, 0, (float)y1, 5.1f);
    }

    /* Vertical border for 2P vertical split or 3+P */
    if ((g_numHumans == 2 && g_viewportIndex == 1) || g_numHumans > 2) {  /* 0x461AB8 */
        int x0, x1;
        if (g_screenHeight < 241) {
            x0 = g_dispCenterX - 1;
            x1 = g_dispCenterX;
        }
        else {
            x0 = g_dispCenterX - 2;
            x1 = g_dispCenterX + 1;
        }
        RenderIrisQuad((float)x0, 0, (float)x1, 0,
                           (float)x1, (float)g_screenHeight, (float)x0, (float)g_screenHeight, 5.1f);
    }
}

/* =====================================================================
 * UpdateVertexLightingPhase - FUN_00430ed4 - 282 bytes
 *
 * Computes per-vertex lighting colors for a character model based on
 * the player's pitch angle and a lighting phase table.
 * EAX = player pointer (Watcom fastcall).
 * ===================================================================== */
void UpdateVertexLightingPhase(Player *player)
{
    int phase = g_lightingPhaseGlobal;
    int raw = (phase << 12) - phase;
    int idx = raw / 0xD00;

    int pitchField = player->angleYaw;
    int phaseIdx = (pitchField - idx) & 0xFFF;
    int tableRow = (phaseIdx >> 7);
    tableRow = 0x1F - tableRow + 0x10;
    tableRow &= 0x1F;

    short charId = player->charId;
    int vertexStart = g_modelMeta[charId].vertexStart;
    int vertCount   = g_modelMeta[charId].vertexCount;

    int *lightSrc = (int *)((char *)g_charLightingTable + vertexStart * 0x180
                   + tableRow * vertCount * 12);

    SrcVertex *vtx = &g_vertexArrayBase[vertexStart];

    if (g_trackId == TRACK_RADIANT_EMERALD || g_colorTintEnable == 0) {
        for (int i = 0; i < vertCount; i++) {
            vtx->colorR = lightSrc[0];
            vtx->colorG = lightSrc[1];
            vtx->colorB = lightSrc[2];
            vtx++;
            lightSrc += 3;
        }
    } else {
        for (int i = 0; i < vertCount; i++) {
            int r = lightSrc[0] + g_colorTintR;
            int g = lightSrc[1] + g_colorTintG;
            int b = lightSrc[2] + g_colorTintB;
            if (r > 0x1FFFFF) {
                r = 0x1FFFFF;
            }
            if (g > 0x1FFFFF) {
                g = 0x1FFFFF;
            }
            if (b > 0x1FFFFF) {
                b = 0x1FFFFF;
            }
            vtx->colorR = r;
            vtx->colorG = g;
            vtx->colorB = b;
            vtx++;
            lightSrc += 3;
        }
    }
}

/* =====================================================================
 * FillVertexColorsByDepth - FUN_004657a0 - 435 bytes
 *
 * Fills 188-entry RGB color arrays for vertex coloring based on depth.
 * ===================================================================== */
void FillVertexColorsByDepth(int r1, int g1, int b1, int r2,
                              int g2, int b2)
{
    int far0 = (r1 << 13) + 0x1000;
    int far1 = (g1 << 13) + 0x1000;
    int far2 = (b1 << 13) + 0x1000;
    int near0 = (r2 << 13) + 0x1000;
    int near1 = (g2 << 13) + 0x1000;
    int near2 = (b2 << 13) + 0x1000;

    int *out = g_lightingRamp13bit;
    for (int off = 0; off < 0x2F00; off += 0x40) {
        int depth = g_lightingDepthTable[off / 4];
        int absD = (depth < 0) ? -depth : depth;
        if (absD > 0xFA0) {
            *out++ = far0;
            *out++ = far1;
            *out++ = far2;
        }
        else {
            *out++ = near0;
            *out++ = near1;
            *out++ = near2;
        }
    }

    far0 = (int)((unsigned)r1 * 128u) / 256 + 0x80;
    far1 = (int)((unsigned)g1 * 128u) / 256 + 0x80;
    far2 = (int)((unsigned)b1 * 128u) / 256 + 0x80;
    near0 = (int)((unsigned)r2 * 128u) / 256 + 0x80;
    near1 = (int)((unsigned)g2 * 128u) / 256 + 0x80;
    near2 = (int)((unsigned)b2 * 128u) / 256 + 0x80;

    out = g_lightingRamp8bit;
    for (int off = 0; off < 0x2F00; off += 0x40) {
        int depth = g_lightingDepthTable[off / 4];
        int absD = (depth < 0) ? -depth : depth;
        if (absD > 0xFA0) {
            *out++ = far0;
            *out++ = far1;
            *out++ = far2;
        } else {
            *out++ = near0;
            *out++ = near1;
            *out++ = near2;
        }
    }
}

/* =====================================================================
 * ColorizeTpageHiColor - 0x00488310 - 417 bytes
 *
 * Builds a 32-entry gradient from black to target RGB, applies it to
 * a 65536-pixel tpage surface.
 * ===================================================================== */
void ColorizeTpageHiColor(int targetR, int targetG, int targetB)
{
#ifdef SONICR_DC
    /* DC path: recolor in twiddled VRAM in place. The g_tpagePixelBuf
     * shadow has been freed by release_system_pixels_if_unused after
     * the preceding LoadTPageRGB upload, so the SDL/system-RAM body
     * below would early-return on a NULL surf. The VRAM still holds
     * the freshly uploaded source pixels for us to recolor. */
    PVR_ColorizeTpageVRAM(g_uiTexPage, targetR, targetG, targetB);
    return;
#endif
    int stepR = (targetR << 16) / 31;
    int stepG = (targetG << 16) / 31;
    int stepB = (targetB << 16) / 31;

    int bpp = g_bitsPerPixel;
    int accR = 0x8000;
    int accG = 0x8000;
    int accB = 0x8000;

    unsigned short gradient16[32];
    unsigned char gradient8[32];

    if (bpp == 16) {
        for (int i = 0; i < 32; i++) {
            int r5 = (accR >> 19) << 11;
            int g5 = (accG >> 19) << 6;
            int b5 = accB >> 19;
            gradient16[i] = (unsigned short)(r5 | g5 | b5);
            accR += stepR; accG += stepG; accB += stepB;
        }
    }
    else if (bpp == 15) {
        for (int i = 0; i < 32; i++) {
            int r5 = (accR >> 19) << 10;
            int g5 = (accG >> 19) << 5;
            int b5 = accB >> 19;
            gradient16[i] = (unsigned short)(r5 | g5 | b5);
            accR += stepR; accG += stepG; accB += stepB;
        }
    }
    else {
        for (int i = 0; i < 32; i++) {
            int r5 = (accR >> 19) << 10;
            int g5 = (accG >> 19) << 5;
            int b5 = accB >> 19;
            gradient8[i] = g_rgb555Remap[r5 + g5 + b5];
            accR += stepR; accG += stepG; accB += stepB;
        }
    }

    int surfIdx = g_uiTexPage;
    GL_KeepPixels(surfIdx);
    unsigned char *surf = (unsigned char *)g_tpagePixelBuf[surfIdx];
    if (surf == NULL) {
        return;
    }

    unsigned short *surf16 = (unsigned short *)surf;
    for (int i = 0; i < 65536; i++) {
        int idx = surf16[i] & 0x1F;
        surf16[i] = gradient16[idx];
    }
}