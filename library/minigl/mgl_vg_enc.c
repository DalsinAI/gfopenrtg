/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * minigl.library's virgl route: the encoder. The command stream virglrenderer
 * reads (little-endian words: each written turned round on the 68k, three
 * instructions) and the ACVirgl request block that carries it (the Amiga's
 * own order). The commands themselves are written by the shared writers in
 * include/gfopengpu/virgl_cmd.h, which Warp3D.library's virgl route builds
 * from too; each layout follows virglrenderer's protocol (Mesa's MIT
 * virgl_protocol.h); the encoder is ours. */
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

ULONG vg_fbits(float f) { return vgc_fbits(f); }

ULONG vg_new_obj(GLcontext c)
{
    c->vg->objects++;
    return ++c->vg->next_obj;
}

/* The commands' layouts are the shared writers' (virgl_cmd.h); these find
 * the room in the frame's stream and keep the context's own records. */

void vg_bind(GLcontext c, ULONG obj, ULONG handle)
{
    ULONG *p = vg_cmd(c, VGC_BIND_WORDS);
    if (p) vgc_bind(p, obj, handle);
}

void vg_set_viewport(GLcontext c, const float *v6)
{
    ULONG *p = vg_cmd(c, VGC_VIEWPORT_WORDS);
    if (p) vgc_viewport(p, v6);
}

void vg_set_scissor(GLcontext c, int x0, int y0, int x1, int y1)
{
    ULONG *p = vg_cmd(c, VGC_SCISSOR_WORDS);
    if (p) vgc_scissor(p, x0, y0, x1, y1);
}

void vg_set_framebuffer(GLcontext c)
{
    ULONG *p = vg_cmd(c, VGC_FRAMEBUFFER_WORDS);
    if (p) vgc_framebuffer(p, c->vg->surf_z, c->vg->surf_c);
}

void vg_clear(GLcontext c, ULONG buffers, const float *rgba, float depth)
{
    ULONG *p = vg_cmd(c, VGC_CLEAR_WORDS);
    if (p) vgc_clear(p, buffers, rgba, depth, 0);
}

void vg_set_vertex_buffer(GLcontext c, ULONG stride)
{
    ULONG *p = vg_cmd(c, VGC_VERTEX_BUFFER_WORDS);
    if (!p) return;
    /* this block's arena in the buffer (vg_ring) */
    vgc_vertex_buffer(p, stride, c->vg->vb_off, c->vg->vbuf->handle);
    c->vg->stride = stride;
}

/* virglrenderer draws indexed from the index buffer's offset (its DRAW_VBO
 * start is not read for indexed draws, as Mesa's virgl folds it in here):
 * set again whenever a draw starts elsewhere in the index arena. */
void vg_set_index_buffer(GLcontext c, ULONG offset)
{
    ULONG *p = vg_cmd(c, VGC_INDEX_BUFFER_WORDS);
    if (!p) return;
    vgc_index_buffer(p, c->vg->ibuf->handle, 2, offset);
    c->vg->ib_off = offset;
}

void vg_draw(GLcontext c, ULONG prim, ULONG start, ULONG count, int indexed, LONG bias)
{
    ULONG *p;
    if (indexed) {
        ULONG off = c->vg->ib_base + start * 2;     /* this block's arena in the buffer (vg_ring) */
        if (off != c->vg->ib_off || (c->vg->dirty & VG_D_IBUF)) { vg_set_index_buffer(c, off); c->vg->dirty &= ~VG_D_IBUF; }
        start = 0;
    }
    p = vg_cmd(c, VGC_DRAW_WORDS);
    if (!p) return;
    vgc_draw(p, prim, start, count, indexed, bias);
    c->vg->draws++;
    if (mgl_trace > 2) mgl_log("draw prim %ld start %ld count %ld idx %ld bias %ld (nva %ld nia %ld ncmd %ld)", (long)prim, (long)start, (long)count, (long)indexed, (long)bias, (long)c->vg->nva, (long)c->vg->nia, (long)c->vg->ncmd);
}

void vg_constants(GLcontext c, int shader, const float *v, int vec4s)
{
    ULONG *p = vg_cmd(c, VGC_CONSTANTS_WORDS(vec4s));
    if (!p) return;
    if (mgl_trace > 2) mgl_log("consts %ld: %ld vec4, mvp row0 %ld %ld %ld %ld", (long)shader, (long)vec4s, (long)(v[0]*1000), (long)(v[1]*1000), (long)(v[2]*1000), (long)(v[3]*1000));
    vgc_constants(p, shader, (const ULONG *)v, vec4s);
}

/* TGSI text, as bytes, after the header; one command (our shaders are far
 * below a command's 64K words). */
void vg_shader(GLcontext c, ULONG handle, int type, const char *text, ULONG len)
{
    ULONG *p = vg_cmd(c, VGC_SHADER_WORDS(len));
    if (!p) return;
    vgc_shader(p, handle, type, text, len);
    c->vg->objects++;
}

void vg_bind_shader(GLcontext c, ULONG handle, int type)
{
    ULONG *p = vg_cmd(c, VGC_BIND_SHADER_WORDS);
    if (p) vgc_bind_shader(p, handle, type);
}

void vg_sampler_views(GLcontext c, int n, const ULONG *h)
{
    ULONG *p = vg_cmd(c, VGC_SAMPLERS_WORDS(n));
    if (p) vgc_sampler_views(p, n, h);
}

void vg_sampler_states(GLcontext c, int n, const ULONG *h)
{
    ULONG *p = vg_cmd(c, VGC_SAMPLERS_WORDS(n));
    if (p) vgc_sampler_states(p, n, h);
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

void vg_blk_cmd(ULONG *p, ULONG cmd, ULONG n, const ULONG *args) { vgc_blk_cmd(p, cmd, n, args); }
LONG vg_result(const ULONG *blk, ULONG at) { return vgc_result(blk, at); }
ULONG vg_word(const ULONG *blk, ULONG at) { return vgc_word(blk, at); }

int vg_run(GLcontext c, ULONG *blk, ULONG words)
{
    c->vg->blocks++;
    return vg_os_run(c, blk, words * 4);
}
