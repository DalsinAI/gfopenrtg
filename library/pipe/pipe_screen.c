/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * The shared pipe's screen (docs/design/OpenGPU-Shared-Pipe-Plan.md, 2.2 and stage M2): the one place a library's
 * virgl route says HELLO and reads the host's flags and caps, and the table the libraries reach the pipe by.
 *
 * Moved here from minigl.library's mgl_vg_frame.c (hello) and Warp3D.library's w3d_vg.c (w3dvg_open), which did
 * the same thing word for word. The blocks are as they sent them, so the host sees no change:
 *   1. ACV_HELLO, ACV_CTX_CREATE (the caller's first context) and ACV_CAPSET_INFO for caps set 2, in one block;
 *   2. ACV_CAPSET_INFO for set 1, when the host has no set 2;
 *   3. ACV_CAPS, into the caller's room.
 * Stage MQ moves the HELLO and the caps into gfopengpu.library's OGPU_Query, and this file keeps the context ids.
 *
 * A screen is the caller's alone (one per open): it holds no lock and sends no block of its own after the open. */
#include "pipe_int.h"
#include "../../include/gfopengpu/virgl_cmd.h"

#ifndef ADDR
#define ADDR(p) ((ULONG)(p))
#endif

struct ogpu_pipe_screen {
    ULONG emit;
    ULONG ctx_id;                       /* the first context, made in the open's block (M3: ctx_create's) */
    OGPUPipeCtx *ctx;
    ULONG host_flags, caps_bits, prim_mask;
};

static LONG screen_run(OGPUPipeScreen *s, ULONG *blk, ULONG bytes)
{
    (void)s;
    return ogpu_pipe_xport_run(blk, bytes);
}

static OGPUPipeScreen *screen_open(const struct OGPUPipeOpen *o, struct OGPUPipeInfo *info)
{
    struct OGPUPipeInfo dummy;
    struct ogpu_pipe_screen *s = 0;
    ULONG a[4], *b, ri, n, set = 2;
    if (!info) info = &dummy;
    info->emit = OGPU_EMIT_VIRGL;
    info->host_flags = info->caps_bits = 0;
    info->prim_mask = 0x7F;
    info->max_tex_size = info->max_tex_units = 0;
    info->status = OGPU_PIPE_OPEN_OK;
    info->detail = 0;
    info->blocks = 0;
    if (!o || o->size < sizeof *o || !o->blk || !o->caps) { info->status = OGPU_PIPE_OPEN_NO_RUN; return 0; }
    if (o->emit_wanted && o->emit_wanted != OGPU_EMIT_VIRGL) { info->status = OGPU_PIPE_OPEN_NO_TRANSPORT; return 0; }
    if (!ogpu_pipe_xport_ready()) { info->status = OGPU_PIPE_OPEN_NO_TRANSPORT; return 0; }
    if (!(s = ogpu_pipe_xport_alloc(sizeof *s))) { info->status = OGPU_PIPE_OPEN_NO_RUN; return 0; }
    s->emit = OGPU_EMIT_VIRGL;
    s->ctx_id = o->ctx_id;
    s->host_flags = s->caps_bits = 0;
    s->prim_mask = 0x7F;
    b = o->blk;

    /* 1. HELLO, the context, the caps set 2's size */
    a[0] = 0;
    vgc_blk_cmd(b, ACV_HELLO, 1, a);
    a[0] = o->ctx_id; a[1] = ADDR(o->name); a[2] = o->name_len;
    vgc_blk_cmd(b + 3, ACV_CTX_CREATE, 3, a);
    a[0] = 2; a[1] = 0; a[2] = 0;
    vgc_blk_cmd(b + 8, ACV_CAPSET_INFO, 3, a);
    info->blocks++;
    if (ogpu_pipe_xport_run(b, 13 * 4)) { info->status = OGPU_PIPE_OPEN_NO_RUN; goto fail; }
    if (vgc_result(b, 0) != ACV_PROTOCOL) { info->status = OGPU_PIPE_OPEN_PROTOCOL; info->detail = vgc_result(b, 0); goto fail; }
    if (vgc_result(b, 3) != 0) { info->status = OGPU_PIPE_OPEN_NO_CONTEXT; info->detail = vgc_result(b, 3); goto fail; }
    s->host_flags = vgc_word(b, 2);
    ri = 8;

    /* 2. no caps set 2: set 1 */
    if (!vgc_word(b, ri + 3)) {
        set = 1;
        a[0] = 1;
        vgc_blk_cmd(b, ACV_CAPSET_INFO, 3, a);
        info->blocks++;
        if (ogpu_pipe_xport_run(b, 5 * 4)) { info->status = OGPU_PIPE_OPEN_NO_RUN; goto fail; }
        ri = 0;
    }

    /* 3. the caps, in the caller's room; the host gives them in our order, word by word */
    n = vgc_word(b, ri + 4);
    if (n > o->caps_bytes) n = o->caps_bytes;
    a[0] = set; a[1] = vgc_word(b, ri + 3); a[2] = ADDR(o->caps); a[3] = n;
    vgc_blk_cmd(b, ACV_CAPS, 4, a);
    info->blocks++;
    if (ogpu_pipe_xport_run(b, 6 * 4) || vgc_result(b, 0) <= 0) {
        info->status = OGPU_PIPE_OPEN_NO_CAPS;
    } else {
        n = (ULONG)vgc_result(b, 0) / 4;
        if (n > 72) s->prim_mask = VG_GETW(&o->caps[72]);
        if (n > 98) s->caps_bits = VG_GETW(&o->caps[98]);
    }
    info->host_flags = s->host_flags;
    info->caps_bits = s->caps_bits;
    info->prim_mask = s->prim_mask;
    return s;
fail:
    ogpu_pipe_xport_free(s);
    return 0;
}

static void screen_close(OGPUPipeScreen *s)
{
    ogpu_pipe_xport_free(s);
}

/* M3: the screen's first context (the one its open made), with its block and arenas; one per screen until M4 */
static OGPUPipeCtx *ctx_create(OGPUPipeScreen *s, ULONG flags)
{
    (void)flags;
    if (!s || s->ctx) return 0;
    return s->ctx = pipe_ctx_create(s, s->ctx_id);
}

static void ctx_destroy(OGPUPipeCtx *x)
{
    if (!x) return;
    x->screen->ctx = 0;
    pipe_ctx_destroy(x);
}

static const struct OGPUPipeOps ops = {
    .version = OGPU_PIPE_VERSION,
    .screen_open = screen_open,
    .screen_close = screen_close,
    .run = screen_run,
    /* M3: the context's block, resources and arenas */
    .ctx_create = ctx_create,
    .ctx_destroy = ctx_destroy,
    .res_create = pipe_m_res_create,
    .res_update = pipe_m_res_update,
    .res_read = pipe_m_res_read,
    .res_release = pipe_m_res_release,
    .vtx_room = pipe_m_vtx_room,
    .idx_room = pipe_m_idx_room,
    .vtx_put_range = pipe_m_vtx_put_range,
    .end_frame = pipe_m_end_frame,
    .flush = pipe_m_flush,
    /* version 2's bridge */
    .res_write = pipe_m_res_write,
    .res_ref = pipe_m_res_ref,
    .res_handle = pipe_m_res_handle,
    .res_used = pipe_m_res_used,
    .res_pending = pipe_m_res_pending,
    .cmd_room = pipe_m_cmd_room,
    .sent = pipe_m_sent,
    .errors = pipe_m_errors,
    .changed = pipe_m_changed,
    .counts = pipe_m_counts,
    .need = pipe_m_need,
    .arenas = pipe_m_arena_state,
    /* the rest arrive with their stages (pipe.h) */
};

const struct OGPUPipeOps *ogpu_pipe_ops(ULONG version)
{
    return version <= OGPU_PIPE_VERSION ? &ops : 0;
}
