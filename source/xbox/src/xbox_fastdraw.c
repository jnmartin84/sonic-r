/*
 * Fast immediate-mode fan submission for the NV2A.
 *
 * pbgl's glBegin/glVertex4f/glEnd each end with pb_end(), which flushes the
 * write-combine cache and writes the DMA PUT register -- six times per quad.
 * With ~2200 quads per frame that was ~40 ms of pure bus stalls. Here a whole
 * run of consecutive fans is written with a single pb_begin()/pb_end() pair.
 *
 * The batch MUST be closed (xb_batch_end) before anything else touches the push
 * buffer: any other GL call, texture upload, clear or buffer swap.
 */
#include <stdint.h>
#include <pbkit/pbkit.h>

#include <stdio.h>
#include "GL/gl.h"
#include "GL/glext.h"
#include "types.h"
#include "texture.h"
#include "push.h"
#include "array.h"
#include "state.h"

static uint32_t *s_p;
static int s_open;

/* Start of the push buffer (set by the frame swap code after pb_reset).
 * platform_init enlarges the push buffer to 4 MB (pb_size). pbkit has no wrap,
 * so a frame writing more than that would run past the end. Safety net only:
 * reset mid-frame (GPU drains, then jumps back to head) at 3 MB. Normal frames
 * use well under 1.5 MB, so this should never fire (it is logged if it does). */
uint32_t *xb_pb_head;
int xb_pb_resets;
static void xb_pb_note(void)
{
    if (++xb_pb_resets <= 20)
        fprintf(stderr, "pushbuffer: mid-frame reset #%d\n", xb_pb_resets);
}
#define XB_PB_LIMIT_DWORDS (768 * 1024)

void xb_batch_end(void)
{
    if (s_open) {
        pb_end(s_p);
        s_open = 0;
    }
}

/* verts: n entries of {x, y, z, w, r, g, b, a, u, v} (x,y,z already multiplied by w) */
void xb_flip_watch(void);
void xb_draw_fan(const float *verts, int n)
{
    {
        static unsigned s_fwN;
        if ((++s_fwN & 127u) == 0) xb_flip_watch();
    }
    if (s_open && xb_pb_head && (s_p - xb_pb_head) > XB_PB_LIMIT_DWORDS) {
        pb_end(s_p);
        s_open = 0;
        pb_reset();
        xb_pb_note();
    }
    if (!s_open) {
        pbgl_state_flush();
        s_p = pb_begin();
        if (xb_pb_head && (s_p - xb_pb_head) > XB_PB_LIMIT_DWORDS) {
            pb_end(s_p);
            pb_reset();
            xb_pb_note();
            s_p = pb_begin();
        }
        s_open = 1;
    }

    /* Bilinear on texture atlases: a polygon whose UVs run right up to the
     * edge of its tile also samples half a texel of the NEIGHBOURING tile,
     * which showed up as thin coloured lines and dots along polygon edges
     * (fences, grass borders, cliffs). Pull the UVs at the edge of each
     * polygon's UV box in by half a texel (tpages are 256x256). */
    extern int g_xbLinearNow;
    float uMin = 0.f, uMax = 0.f, vMin = 0.f, vMax = 0.f;
    int inset = 0;
    if (g_xbLinearNow && n >= 3) {
        uMin = uMax = verts[8];
        vMin = vMax = verts[9];
        for (int i = 1; i < n; i++) {
            float uu = verts[i * 10 + 8], vv = verts[i * 10 + 9];
            if (uu < uMin) uMin = uu; else if (uu > uMax) uMax = uu;
            if (vv < vMin) vMin = vv; else if (vv > vMax) vMax = vv;
        }
        const float half = 0.5f / 256.f;
        if (uMax - uMin > 2.f * half) { uMin += half; uMax -= half; inset |= 1; }
        if (vMax - vMin > 2.f * half) { vMin += half; vMax -= half; inset |= 2; }
    }

    uint32_t *p = s_p;
    p = push_command_parameter(p, NV097_SET_BEGIN_END, GL_TRIANGLE_FAN + 1);

    for (int i = 0; i < n; i++) {
        const float *v = verts + i * 10;

        p = push_command(p, NV097_SET_VERTEX_DATA4F_M + NV2A_VERTEX_ATTR_DIFFUSE * 4 * 4, 4);
        p = push_float(p, v[4]);
        p = push_float(p, v[5]);
        p = push_float(p, v[6]);
        p = push_float(p, v[7]);

        for (int u = 0; u < TEXUNIT_COUNT; ++u) {
            if (pbgl.tex[u].enabled) {
                p = push_command(p, NV097_SET_VERTEX_DATA4F_M + (NV2A_VERTEX_ATTR_TEXTURE0 + u) * 4 * 4, 4);
                float tu = v[8], tv = v[9];
                if (inset & 1) tu = tu < uMin ? uMin : (tu > uMax ? uMax : tu);
                if (inset & 2) tv = tv < vMin ? vMin : (tv > vMax ? vMax : tv);
                p = push_float(p, tu);
                p = push_float(p, tv);
                p = push_float(p, 0.f);
                p = push_float(p, 1.f);
            }
        }

        p = push_command(p, NV097_SET_VERTEX4F, 4);
        p = push_float(p, v[0]);
        p = push_float(p, v[1]);
        p = push_float(p, v[2]);
        p = push_float(p, v[3]);
    }

    p = push_command_parameter(p, NV097_SET_BEGIN_END, NV097_SET_BEGIN_END_OP_END);
    s_p = p;
}

/*
 * Fast RGBA8 texture upload.
 *
 * pbgl's glTexImage2D swizzles one pixel at a time, rebuilding the swizzled
 * offset bit by bit and calling memcpy per pixel. Here pbgl only allocates the
 * texture (data = NULL) and the pixels are written straight into its swizzled
 * storage with the usual incremental-mask walk. Same memory layout as pbgl's
 * own path (RGBA -> RGBA needs no conversion there either).
 */
#include "swizzle.h"
#include <SDL.h>

void xb_tex_image_rgba(int w, int h, const void *rgba)
{
    xb_batch_end();
    {
        /* Re-uploading a texture the GPU may still be sampling from commands
         * already queued this frame rewrites (or frees) its memory under the
         * GPU: earlier draws then show the wrong texture. Drain the GPU first. */
        texture_t *cur = pbgl.tex[pbgl.active_tex_sv].tex;
        if (cur && cur->allocated) {
            static int s_syncs;
            /* Bounded: a GPU that never goes idle used to freeze the game here
             * for good. Give up after 2 s and say so in the log. */
            unsigned t0 = SDL_GetTicks();
            while (pb_busy()) {
                if (SDL_GetTicks() - t0 > 2000) {
                    fprintf(stderr, "texture re-upload: GPU still busy after 2 s, continuing\n");
                    break;
                }
            }
            if (++s_syncs <= 10)
                fprintf(stderr, "texture re-upload: GPU drained (#%d)\n", s_syncs);
        }
    }
    if (w <= 0 || h <= 0 || (w & (w - 1)) || (h & (h - 1)) || rgba == NULL) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        return;
    }
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    texture_t *tex = pbgl.tex[pbgl.active_tex_sv].tex;
    if (!tex || !tex->allocated || tex->bytespp != 4 || tex->gl.gen_mipmap ||
        tex->mips[0].data == NULL ||
        tex->mips[0].width != (GLuint)w || tex->mips[0].height != (GLuint)h) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        return;
    }
    uint32_t mx, my, mz;
    swizzle_generate_masks((unsigned)w, (unsigned)h, 1, &mx, &my, &mz);
    uint32_t *dst = (uint32_t *)tex->mips[0].data;
    const uint32_t *src = (const uint32_t *)rgba;
    uint32_t yo = 0;
    for (int y = 0; y < h; y++) {
        uint32_t xo = 0;
        for (int x = 0; x < w; x++) {
            dst[xo | yo] = *src++;
            xo = (xo - mx) & mx;
        }
        yo = (yo - my) & my;
    }
}
