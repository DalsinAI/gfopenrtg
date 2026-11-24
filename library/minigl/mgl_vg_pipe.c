/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * minigl.library's virgl route on the OpenGPU shared pipe (docs/design/OpenGPU-Shared-Pipe-Plan.md): the context's
 * block, its resources, its arenas and every command of the frame are the pipe's (library/pipe/), reached only
 * through its table (version 7). MiniGL fills the arenas itself: their memory and use come from arenas(). */
#include "mgl_vg.h"

/* The arenas, the buffers' handles and what a block changed: the pipe's, mirrored where the drawing code reads
 * them. After anything that may have sent. */
void vg_sync(GLcontext c)
{
    struct mgl_vg *v = c->vg;
    struct OGPUPipeArenas a;
    ULONG s;
    v->pipe->arenas(v->px, &a);
    v->va = a.va; v->pnva = a.nva; v->cva = a.cva;
    v->ia = a.ia; v->pnia = a.nia; v->cia = a.cia;
    v->vbuf_h = a.vbuf; v->vb_off = a.vb_off;
    v->ibuf_h = a.ibuf; v->ib_base = a.ib_base;
    (void)v->pipe->changed(v->px);      /* the buffers are bound by the pipe, which sees their changes itself */
    s = v->pipe->sent(v->px);
    if (s != v->seq) {
        v->seq = s;
        v->lk_valid = 0;                /* the locked range goes into the next block's arena again */
    }
}

/* ---- resources ----------------------------------------------------------------------------------------------- */

struct vg_res *vgp_res_create(GLcontext c, ULONG target, ULONG fmt, ULONG bind, ULONG w, ULONG h, ULONG levels)
{
    struct mgl_vg *v = c->vg;
    struct OGPUPipeResDesc d;
    struct vg_res *r = mgl_alloc(sizeof *r);
    ULONG s = v->seq;
    if (!r) return 0;
    d.target = target; d.format = fmt; d.bind = bind;
    d.width = w; d.height = h; d.levels = levels; d.flags = 0;
    if (!(r->pr = v->pipe->res_create(v->px, &d))) { mgl_free(r); return 0; }
    r->handle = v->pipe->res_handle(r->pr);     /* MiniGL never writes a resource a pending stream reads: no rename */
    r->fmt = fmt; r->w = w; r->h = h; r->levels = levels;
    if (v->pipe->sent(v->px) != s) vg_sync(c);
    return r;
}

void vgp_res_release(GLcontext c, struct vg_res *r)
{
    struct mgl_vg *v = c->vg;
    ULONG s = v->seq;
    v->pipe->res_release(v->px, r->pr);        /* the host lets go of it after the block that last used it */
    mgl_free(r);
    if (v->pipe->sent(v->px) != s) vg_sync(c);
}

void vgp_put(GLcontext c, struct vg_res *r, ULONG level, ULONG x, ULONG y, ULONG w, ULONG h, const void *src,
             ULONG stride, ULONG swap)
{
    struct mgl_vg *v = c->vg;
    struct OGPUBox b;
    ULONG s = v->seq;
    b.x = (LONG)x; b.y = (LONG)y; b.z = 0; b.w = w; b.h = h; b.d = 1;
    v->pipe->res_update(v->px, r->pr, level, &b, src, stride, swap);
    if (v->pipe->sent(v->px) != s) vg_sync(c);
}

/* ---- the frame's commands ------------------------------------------------------------------------------------ */

ULONG vg_fbits(float f) { return vgc_fbits(f); }

void vg_clear(GLcontext c, ULONG buffers, const float *rgba, float depth)
{
    struct mgl_vg *v = c->vg;
    VGP(c, v->pipe->clear(v->px, buffers, rgba, depth, 0));
}

void vg_draw(GLcontext c, ULONG prim, ULONG start, ULONG count, int indexed, LONG bias)
{
    struct mgl_vg *v = c->vg;
    struct OGPUDrawBuf d;
    d.size = sizeof d;
    d.prim = prim; d.first = start; d.count = count; d.indexed = indexed ? 16 : 0; d.bias = bias;
    d.stride = v->stride;
    d.vbuf = d.ibuf = 0;                /* the arenas: the pipe adds this block's places in its buffers */
    d.vb_offset = d.ib_offset = 0;
    VGP(c, v->pipe->draw_buf(v->px, &d));
    v->draws++;
    if (mgl_trace > 2) mgl_log("draw prim %ld start %ld count %ld idx %ld bias %ld (nva %ld nia %ld)", (long)prim, (long)start, (long)count, (long)indexed, (long)bias, (long)VG_NVA(v), (long)VG_NIA(v));
}

/* ---- the block ----------------------------------------------------------------------------------------------- */

void vg_blk_cmd(ULONG *p, ULONG cmd, ULONG n, const ULONG *args) { vgc_blk_cmd(p, cmd, n, args); }
LONG vg_result(const ULONG *blk, ULONG at) { return vgc_result(blk, at); }
ULONG vg_word(const ULONG *blk, ULONG at) { return vgc_word(blk, at); }

/* ---- the arenas ---------------------------------------------------------------------------------------------- */

void vgp_need(GLcontext c, ULONG va_bytes, ULONG ia_count)
{
    struct mgl_vg *v = c->vg;
    v->pipe->need(v->px, va_bytes, ia_count, 0);
    vg_sync(c);                         /* an arena too small for the draw is a new one */
}

/* glBegin's arena is full: what there is goes, and the arena is larger from the frame's end (as before: need()
 * marks it for growing when the vertex doesn't fit, and sends). */
void vgp_overflow(GLcontext c, ULONG stride)
{
    struct mgl_vg *v = c->vg;
    if (v->pipe->need(v->px, stride, 0, 1) == 0) v->pipe->flush(v->px, 0);
    vg_sync(c);
}

/* ---- sending ------------------------------------------------------------------------------------------------- */

static ULONG hook_prepare(void *user, ULONG ctx, ULONG handle, ULONG *p, ULONG room)
{
    (void)ctx; (void)handle;
    return vg_os_present((GLcontext)user, p, room);
}

static void hook_finish(void *user, const ULONG *blk, ULONG at)
{
    vg_os_presented((GLcontext)user, (ULONG *)blk, at);
}

void vgp_send(GLcontext c, int present)
{
    struct mgl_vg *v = c->vg;
    if (present) {
        struct OGPUPresentHook h;
        h.prepare = hook_prepare; h.finish = hook_finish; h.user = c;
        v->pipe->end_frame(v->px, &h, 0);       /* grows the arenas that filled this frame */
    } else
        v->pipe->flush(v->px, 0);
    v->pre_errors = v->pipe->errors(v->px);
    if (v->pre_errors) mgl_log("virgl: the host refused %ld commands of the block", (long)v->pre_errors);
    vg_sync(c);
}

int vgp_readback(GLcontext c, int x0, int y0, int x1, int y1, ULONG *buf)
{
    struct mgl_vg *v = c->vg;
    struct OGPUBox b;
    LONG r;
    b.x = x0; b.y = y0; b.z = 0; b.w = (ULONG)(x1 - x0); b.h = (ULONG)(y1 - y0); b.d = 1;
    r = v->pipe->res_read(v->px, v->colour->pr, 0, &b, buf, (ULONG)(x1 - x0) * 4);
    vg_sync(c);
    return r ? -1 : 0;
}

/* ---- the context --------------------------------------------------------------------------------------------- */

int vgp_open(GLcontext c)
{
    static const char name[] = "minigl.library";
    struct mgl_vg *v;
    struct OGPUPipeOpen o;
    struct OGPUPipeInfo info;
    ULONG zfmt, i, *scratch;
    char *nm;
    const struct OGPUPipeOps *pipe = ogpu_pipe_ops(OGPU_PIPE_VERSION);
    if (!pipe || !ogpu_pipe_xport_ready()) { mgl_log("virgl: no gfopengpu.library, or no driver of it carries virgl"); return 0; }
    if (!(v = c->vg = mgl_alloc(sizeof *v))) return 0;
    v->pipe = pipe;
    v->ctx_id = ADDR(v);
    /* the open's blocks, the name and the caps, in memory the host reaches, while opening */
    if (!(scratch = mgl_alloc(16 * 4 + 32 + VG_CAPS_WORDS * 4))) { mgl_free(v); c->vg = 0; return 0; }
    nm = (char *)(scratch + 16);
    for (i = 0; i < sizeof name; i++) nm[i] = name[i];
    o.size = sizeof o; o.emit_wanted = OGPU_EMIT_VIRGL;
    o.ctx_id = v->ctx_id; o.name = nm; o.name_len = sizeof name - 1;
    o.blk = scratch;
    o.caps = scratch + 16 + 8;
    o.caps_bytes = VG_CAPS_WORDS * 4;
    v->screen = pipe->screen_open(&o, &info);
    mgl_free(scratch);
    v->blocks += info.blocks;
    switch (info.status) {
    case OGPU_PIPE_OPEN_OK: break;
    case OGPU_PIPE_OPEN_NO_TRANSPORT: mgl_log("virgl: gfopengpu.library has no driver that carries virgl"); goto fail;
    case OGPU_PIPE_OPEN_NO_RUN: mgl_log("virgl: the first block didn't run"); goto fail;
    case OGPU_PIPE_OPEN_PROTOCOL: mgl_log("virgl: the host speaks another ACVirgl (%ld)", (long)info.detail); goto fail;
    case OGPU_PIPE_OPEN_NO_CONTEXT: mgl_log("virgl: the host refused a context (%ld)", (long)info.detail); goto fail;
    default: mgl_log("virgl: no caps"); goto fail;
    }
    v->host_flags = info.host_flags;
    v->prim_mask = info.prim_mask;
    v->caps_bits = info.caps_bits;
    v->gles = (v->host_flags & ACV_HOST_GLES) != 0;
    /* the pipe's context: the one the open made, with its block, its arenas and their buffers */
    if (!(v->px = pipe->ctx_create(v->screen, 0))) goto fail;
    vg_sync(c);
    /* colour and depth, at the drawable's size (their surfaces are the pipe's, made at the first draw's bind) */
    zfmt = mgl_prefs.zbits == 16 ? VG_FMT_Z16_UNORM : VG_FMT_Z24X8_UNORM;
    v->colour = vg_res_create(c, VG_TARGET_2D, VG_FMT_B8G8R8A8_UNORM, VG_BIND_RENDER_TARGET | VG_BIND_SAMPLER_VIEW,
                              (ULONG)c->width, (ULONG)c->height, 1);
    v->depth = vg_res_create(c, VG_TARGET_2D, zfmt, VG_BIND_DEPTH_STENCIL, (ULONG)c->width, (ULONG)c->height, 1);
    if (!v->colour || !v->depth) goto fail;
    v->objects += 2;
    vg_state_reset(c);
    vg_send(c, 0);                      /* made now, to see the host take them */
    if (v->pre_errors) { mgl_log("virgl: the host refused the context's resources"); goto fail; }
    mgl_log("virgl: context %lx on the shared pipe, host flags %lx, prims %lx, caps %lx%s", (unsigned long)v->ctx_id,
            (unsigned long)v->host_flags, (unsigned long)v->prim_mask, (unsigned long)v->caps_bits, v->gles ? ", GLES" : "");
    return 1;
fail:
    vgp_close(c);
    return 0;
}

void vgp_close(GLcontext c)
{
    struct mgl_vg *v = c->vg;
    if (!v) return;
    if (v->px) {
        if (v->colour) vgp_res_release(c, v->colour);
        if (v->depth) vgp_res_release(c, v->depth);
        v->colour = v->depth = 0;
        v->pipe->ctx_destroy(v->px);            /* the buffers let go of, what there is sent, the host's context gone */
        v->px = 0;
    }
    if (v->screen) v->pipe->screen_close(v->screen);
    mgl_free(v);
    c->vg = 0;
}
