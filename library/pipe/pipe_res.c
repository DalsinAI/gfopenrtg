/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * The shared pipe's resources (stage M3, pipe_int.h): from minigl.library's vg_res_* (mgl_vg_frame.c) and its
 * textures' renaming (mgl_vg_tex.c).
 *
 * An OGPUPipeRes is what a front end holds; the host sees its versions. A write to a resource a stream not yet sent
 * still reads makes a new version (a rename): the later draws use the new one, and the old one is let go of after
 * that block, so the host never writes into something a pending draw reads, and no frame is split for it. A front
 * end learns of a rename from res_write's answer (or res_handle's change) and sends everything the resource holds.
 *
 * Letting go (res_release): reference counted. At the last release the current version's ACV_RES_UNREF goes after the
 * stream of the block that last used it: this context's, or, when another context's stream not yet sent used it
 * last, that context's. Its memory is freed after that block has run. So nothing is freed while a block that reads it
 * is still to run (deferred destroy, fence-safe: blocks run in order and each send waits for its block). */
#include "pipe_int.h"

static struct pipe_ver *new_ver(OGPUPipeCtx *x, const struct OGPUPipeResDesc *d)
{
    struct pipe_ver *v = ogpu_pipe_xport_alloc(sizeof *v);
    ULONG a[12], *p;
    if (!v) return 0;
    if (!(p = pipe_pre(x, VG_BLK_WORDS(12)))) { ogpu_pipe_xport_free(v); return 0; }
    v->handle = ADDR(v);
    a[0] = v->handle; a[1] = d->target; a[2] = d->format; a[3] = d->bind;
    a[4] = d->width; a[5] = d->height; a[6] = 1; a[7] = 1; a[8] = d->levels ? d->levels - 1 : 0; a[9] = 0; a[10] = 0;
    a[11] = x->ctx_id;
    vgc_blk_cmd(p, ACV_RES_CREATE, 12, a);
    return v;
}

/* A version's ACV_RES_UNREF after the stream of x's block; its memory freed after that block ran. */
static void unref_ver(OGPUPipeCtx *x, struct pipe_ver *v)
{
    ULONG a[1], *p;
    if (!v) return;
    if (!(p = pipe_post(x, VG_BLK_WORDS(1)))) return;      /* sent first when full; never fails */
    a[0] = v->handle;
    vgc_blk_cmd(p, ACV_RES_UNREF, 1, a);
    v->next_dead = x->dead;
    x->dead = v;
}

OGPUPipeRes *pipe_res_create(OGPUPipeCtx *x, const struct OGPUPipeResDesc *d)
{
    OGPUPipeRes *r;
    if (!x || !d) return 0;
    if (!(r = ogpu_pipe_xport_alloc(sizeof *r))) return 0;
    r->desc = *d;
    if (!(r->ver = new_ver(x, d))) { ogpu_pipe_xport_free(r); return 0; }
    r->refs = 1;
    r->versions = 1;
    return r;
}

static int pending_use(const OGPUPipeRes *r)
{
    return r->used_ctx && r->used_ctx->seq == r->used_seq;
}

void pipe_res_release(OGPUPipeCtx *x, OGPUPipeRes *r)
{
    OGPUPipeCtx *at;
    if (!r || !r->refs) return;
    if (--r->refs) return;
    /* the last release: after the stream that last used it */
    at = pending_use(r) && r->used_ctx != x ? r->used_ctx : x;
    unref_ver(at, r->ver);
    r->ver = 0;
    ogpu_pipe_xport_free(r);
}

void pipe_res_free_dead(OGPUPipeCtx *x)
{
    struct pipe_ver *v;
    while ((v = x->dead) != 0) { x->dead = v->next_dead; ogpu_pipe_xport_free(v); }
}

void pipe_res_drop_all(OGPUPipeCtx *x)
{
    pipe_res_free_dead(x);
}

/* ---- the table's members -------------------------------------------------------------------------------------- */

/* Before writing r: 1 when a stream not yet sent reads it, so a new version was made (everything it holds must go up
 * again; the old version goes after that stream); 0 when it is written in place. */
LONG pipe_m_res_write(OGPUPipeCtx *x, OGPUPipeRes *r)
{
    struct pipe_ver *v;
    if (!x || !r || !pending_use(r)) return 0;
    if (!(v = new_ver(x, &r->desc))) return 0;      /* no memory: in place (as MiniGL did before renaming) */
    unref_ver(r->used_ctx, r->ver);                 /* after the stream that reads it */
    r->ver = v;
    r->versions++;
    r->used_ctx = 0;
    x->renames++;
    return 1;
}

void pipe_m_res_update(OGPUPipeCtx *x, OGPUPipeRes *r, ULONG level, const struct OGPUBox *b, const void *src,
                       ULONG stride, ULONG swap)
{
    ULONG a[14], *p, bytes;
    if (!x || !r || !b || !src || !b->w || !b->h) return;
    (void)pipe_m_res_write(x, r);
    if (!(p = pipe_pre(x, VG_BLK_WORDS(14)))) return;
    bytes = (b->h - 1) * stride + b->w * (swap == VG_SW16 ? 2 : 4);
    a[0] = r->ver->handle; a[1] = x->ctx_id; a[2] = level; a[3] = stride; a[4] = 0;
    a[5] = (ULONG)b->x; a[6] = (ULONG)b->y; a[7] = 0; a[8] = b->w; a[9] = b->h; a[10] = 1;
    a[11] = ADDR(src); a[12] = bytes; a[13] = swap;
    vgc_blk_cmd(p, ACV_PUT, 14, a);
    r->put_seq = x->seq + 1;
    x->puts++;
}

/* The box back into dst (the Amiga's order): what was drawn so far is sent first. 0 when it came. */
LONG pipe_m_res_read(OGPUPipeCtx *x, OGPUPipeRes *r, ULONG level, const struct OGPUBox *b, void *dst, ULONG stride)
{
    ULONG a[14];
    if (!x || !r || !b || !dst || !b->w || !b->h) return -1;
    if (pipe_send(x, 0)) return -1;
    a[0] = r->ver->handle; a[1] = x->ctx_id; a[2] = level; a[3] = stride; a[4] = 0;
    a[5] = (ULONG)b->x; a[6] = (ULONG)b->y; a[7] = 0; a[8] = b->w; a[9] = b->h; a[10] = 1;
    a[11] = ADDR(dst); a[12] = (b->h - 1) * stride + b->w * 4; a[13] = VG_SW32;
    vgc_blk_cmd(x->blk, ACV_GET, 14, a);
    if (pipe_run(x, x->blk, VG_BLK_WORDS(14))) return -1;
    return vgc_result(x->blk, 0) ? -1 : 0;
}

OGPUPipeRes *pipe_m_res_create(OGPUPipeCtx *x, const struct OGPUPipeResDesc *d) { return pipe_res_create(x, d); }
void pipe_m_res_release(OGPUPipeCtx *x, OGPUPipeRes *r) { pipe_res_release(x, r); }
void pipe_m_res_ref(OGPUPipeRes *r) { if (r) r->refs++; }
ULONG pipe_m_res_handle(const OGPUPipeRes *r) { return r && r->ver ? r->ver->handle : 0; }

/* r is read by the stream being built (a draw samples it, a binding names it) */
void pipe_m_res_used(OGPUPipeCtx *x, OGPUPipeRes *r)
{
    if (!x || !r) return;
    r->used_ctx = x;
    r->used_seq = x->seq;
}

ULONG pipe_m_res_pending(OGPUPipeCtx *x, const OGPUPipeRes *r)
{
    ULONG f = 0;
    if (!x || !r) return 0;
    if (pending_use(r)) f |= OGPU_PIPE_RES_DRAWN;
    if (r->put_seq == x->seq + 1) f |= OGPU_PIPE_RES_PUT;
    return f;
}

/* res_dirty: texels changed in Amiga memory: M3 has no copy of the front end's texels to send from, so a front end
 * PUTs what changed with res_update; this member stays 0 until a stage keeps the texels' address (M6, Warp3D). */

