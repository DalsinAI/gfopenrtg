/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * minigl.library's virgl route: the encoder. The command stream virglrenderer
 * reads (little-endian words: each written turned round on the 68k, three
 * instructions) and the ACVirgl request block that carries it (the Amiga's
 * own order). Each command's layout follows virglrenderer's protocol
 * (Mesa's MIT virgl_protocol.h); the encoder is ours. */
#include "mgl_vg.h"

/* ---- the stream ------------------------------------------------------------------------------ */

ULONG *vg_cmd(GLcontext c, ULONG n)
{
    struct mgl_vg *v = c->vg;
    ULONG *p;
    if (v->ncmd + n > v->ccmd) {
        /* draws reserve their room first (vg_need), so this is between draws */
        vg_send(c, 0);
        if (n > v->ccmd) { v->grow_cmd = n * 2; return 0; }
    }
    p = v->cmd + v->ncmd;
    v->ncmd += n;
    return p;
}

ULONG vg_fbits(float f)
{
    union { float f; ULONG u; } x;
    x.f = f;
    return x.u;
}

ULONG vg_new_obj(GLcontext c)
{
    c->vg->objects++;
    return ++c->vg->next_obj;
}

void vg_bind(GLcontext c, ULONG obj, ULONG handle)
{
    ULONG *p = vg_cmd(c, 2);
    if (!p) return;
    VG_W(p, VG_CMD0(VG_CCMD_BIND_OBJECT, obj, 1));
    VG_W(p, handle);
}

void vg_set_viewport(GLcontext c, const float *v6)
{
    ULONG *p = vg_cmd(c, 8);
    int i;
    if (!p) return;
    VG_W(p, VG_CMD0(VG_CCMD_SET_VIEWPORT_STATE, 0, 7));
    VG_W(p, 0);
    for (i = 0; i < 6; i++) VG_W(p, vg_fbits(v6[i]));
}

void vg_set_scissor(GLcontext c, int x0, int y0, int x1, int y1)
{
    ULONG *p = vg_cmd(c, 4);
    if (!p) return;
    VG_W(p, VG_CMD0(VG_CCMD_SET_SCISSOR_STATE, 0, 3));
    VG_W(p, 0);
    VG_W(p, (ULONG)x0 | ((ULONG)y0 << 16));
    VG_W(p, (ULONG)x1 | ((ULONG)y1 << 16));
}

void vg_set_framebuffer(GLcontext c)
{
    ULONG *p = vg_cmd(c, 4);
    if (!p) return;
    VG_W(p, VG_CMD0(VG_CCMD_SET_FRAMEBUFFER_STATE, 0, 3));
    VG_W(p, 1);
    VG_W(p, c->vg->surf_z);
    VG_W(p, c->vg->surf_c);
}

void vg_clear(GLcontext c, ULONG buffers, const float *rgba, float depth)
{
    ULONG *p = vg_cmd(c, 9);
    union { double d; ULONG w[2]; } z;
    int i;
    if (!p) return;
    z.d = depth;
    VG_W(p, VG_CMD0(VG_CCMD_CLEAR, 0, 8));
    VG_W(p, buffers);
    for (i = 0; i < 4; i++) VG_W(p, vg_fbits(rgba[i]));
    /* the depth is a double, its low word first (little-endian qword) */
#ifdef MGL_HOST
    VG_W(p, z.w[0]); VG_W(p, z.w[1]);
#else
    VG_W(p, z.w[1]); VG_W(p, z.w[0]);
#endif
    VG_W(p, 0);
}

void vg_set_vertex_buffer(GLcontext c, ULONG stride)
{
    ULONG *p = vg_cmd(c, 4);
    if (!p) return;
    VG_W(p, VG_CMD0(VG_CCMD_SET_VERTEX_BUFFERS, 0, 3));
    VG_W(p, stride);
    VG_W(p, 0);
    VG_W(p, c->vg->vbuf->handle);
    c->vg->stride = stride;
}

/* virglrenderer draws indexed from the index buffer's offset (its DRAW_VBO
 * start is not read for indexed draws, as Mesa's virgl folds it in here):
 * set again whenever a draw starts elsewhere in the index arena. */
void vg_set_index_buffer(GLcontext c, ULONG offset)
{
    ULONG *p = vg_cmd(c, 4);
    if (!p) return;
    VG_W(p, VG_CMD0(VG_CCMD_SET_INDEX_BUFFER, 0, 3));
    VG_W(p, c->vg->ibuf->handle);
    VG_W(p, 2);
    VG_W(p, offset);
    c->vg->ib_off = offset;
}

void vg_draw(GLcontext c, ULONG prim, ULONG start, ULONG count, int indexed, LONG bias)
{
    ULONG *p;
    if (indexed) {
        if (start * 2 != c->vg->ib_off || (c->vg->dirty & VG_D_IBUF)) { vg_set_index_buffer(c, start * 2); c->vg->dirty &= ~VG_D_IBUF; }
        start = 0;
    }
    p = vg_cmd(c, 13);
    if (!p) return;
    VG_W(p, VG_CMD0(VG_CCMD_DRAW_VBO, 0, 12));
    VG_W(p, start);
    VG_W(p, count);
    VG_W(p, prim);
    VG_W(p, indexed ? 1 : 0);
    VG_W(p, 1);                         /* instances */
    VG_W(p, indexed ? (ULONG)bias : 0);
    VG_W(p, 0);                         /* start instance */
    VG_W(p, 0);                         /* primitive restart */
    VG_W(p, 0);
    VG_W(p, 0);                         /* min index */
    VG_W(p, 0xFFFFFFFFUL);              /* max index */
    VG_W(p, 0);
    c->vg->draws++;
    if (mgl_trace > 2) mgl_log("draw prim %ld start %ld count %ld idx %ld bias %ld (nva %ld nia %ld ncmd %ld)", (long)prim, (long)start, (long)count, (long)indexed, (long)bias, (long)c->vg->nva, (long)c->vg->nia, (long)c->vg->ncmd);
}

void vg_constants(GLcontext c, int shader, const float *v, int vec4s)
{
    ULONG n = (ULONG)vec4s * 4, *p = vg_cmd(c, n + 3), i;
    const ULONG *src = (const ULONG *)v;
    if (!p) return;
    if (mgl_trace > 2) mgl_log("consts %ld: %ld vec4, mvp row0 %ld %ld %ld %ld", (long)shader, (long)vec4s, (long)(v[0]*1000), (long)(v[1]*1000), (long)(v[2]*1000), (long)(v[3]*1000));
    VG_W(p, VG_CMD0(VG_CCMD_SET_CONSTANT_BUFFER, 0, n + 2));
    VG_W(p, (ULONG)shader);
    VG_W(p, 0);
    for (i = 0; i < n; i++) VG_W(p, src[i]);
}

/* TGSI text, as bytes, after the header; one command (our shaders are far
 * below a command's 64K words). */
void vg_shader(GLcontext c, ULONG handle, int type, const char *text, ULONG len)
{
    ULONG words = (len + 3) / 4, *p = vg_cmd(c, words + 6);
    UBYTE *b;
    ULONG i;
    if (!p) return;
    VG_W(p, VG_CMD0(VG_CCMD_CREATE_OBJECT, VG_OBJ_SHADER, words + 5));
    VG_W(p, handle);
    VG_W(p, (ULONG)type);
    VG_W(p, len);                       /* the whole length, in this one command */
    VG_W(p, len + 300);                 /* tokens to make room for: fewer than its bytes */
    VG_W(p, 0);                         /* no stream output */
    b = (UBYTE *)p;
    for (i = 0; i < len; i++) b[i] = (UBYTE)text[i];
    for (; i < words * 4; i++) b[i] = 0;
    c->vg->objects++;
}

void vg_bind_shader(GLcontext c, ULONG handle, int type)
{
    ULONG *p = vg_cmd(c, 3);
    if (!p) return;
    VG_W(p, VG_CMD0(VG_CCMD_BIND_SHADER, 0, 2));
    VG_W(p, handle);
    VG_W(p, (ULONG)type);
}

void vg_sampler_views(GLcontext c, int n, const ULONG *h)
{
    ULONG *p = vg_cmd(c, (ULONG)n + 3);
    int i;
    if (!p) return;
    VG_W(p, VG_CMD0(VG_CCMD_SET_SAMPLER_VIEWS, 0, n + 2));
    VG_W(p, VG_SHADER_FRAGMENT);
    VG_W(p, 0);
    for (i = 0; i < n; i++) VG_W(p, h[i]);
}

void vg_sampler_states(GLcontext c, int n, const ULONG *h)
{
    ULONG *p = vg_cmd(c, (ULONG)n + 3);
    int i;
    if (!p) return;
    VG_W(p, VG_CMD0(VG_CCMD_BIND_SAMPLER_STATES, 0, n + 2));
    VG_W(p, VG_SHADER_FRAGMENT);
    VG_W(p, 0);
    for (i = 0; i < n; i++) VG_W(p, h[i]);
}

/* ---- the block --------------------------------------------------------------------------------- */

ULONG *vg_pre(GLcontext c, ULONG n)
{
    struct mgl_vg *v = c->vg;
    ULONG *p;
    if (v->npre + n > v->cpre) vg_send(c, 0);
    p = v->blk + v->npre;
    v->npre += n;
    return p;
}

void vg_blk_cmd(ULONG *p, ULONG cmd, ULONG n, const ULONG *args)
{
    ULONG i;
    PUTW(p, ACV_HDR(cmd, n + 2));
    PUTW(p + 1, 0);
    for (i = 0; i < n; i++) PUTW(p + 2 + i, args[i]);
}

LONG vg_result(const ULONG *blk, ULONG at) { return (LONG)GETW(blk + at + 1); }
ULONG vg_word(const ULONG *blk, ULONG at) { return GETW(blk + at); }

int vg_run(GLcontext c, ULONG *blk, ULONG words)
{
    c->vg->blocks++;
    return vg_os_run(c, blk, words * 4);
}
