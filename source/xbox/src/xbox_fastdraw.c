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
 * pbkit has no wrap: a frame writing more than the 512 KB buffer runs past its
 * end and corrupts whatever memory follows (textures, geometry). Reset mid-frame
 * (GPU drains, then jumps back to head) when the buffer gets 3/4 full. */
uint32_t *xb_pb_head;
int xb_pb_resets;
static void xb_pb_note(void)
{
    if (++xb_pb_resets <= 20)
        fprintf(stderr, "pushbuffer: mid-frame reset #%d\n", xb_pb_resets);
}
#define XB_PB_LIMIT_DWORDS (96 * 1024)

void xb_batch_end(void)
{
    if (s_open) {
        pb_end(s_p);
        s_open = 0;
    }
}

/* verts: n entries of {x, y, z, w, r, g, b, a, u, v} (x,y,z already multiplied by w) */
void xb_draw_fan(const float *verts, int n)
{
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
                p = push_float(p, v[8]);
                p = push_float(p, v[9]);
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

void xb_tex_image_rgba(int w, int h, const void *rgba)
{
    xb_batch_end();
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
