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

#include "GL/gl.h"
#include "GL/glext.h"
#include "types.h"
#include "texture.h"
#include "push.h"
#include "array.h"
#include "state.h"

static uint32_t *s_p;
static int s_open;

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
    if (!s_open) {
        pbgl_state_flush();
        s_p = pb_begin();
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
