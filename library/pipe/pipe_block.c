/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * The shared pipe's context and its block (stage M3, pipe_int.h): from minigl.library's mgl_vg_enc.c (vg_cmd,
 * vg_pre, vg_run) and mgl_vg_frame.c (vg_send, mgl_vg_open's sizes).
 *
 * A context's work is built in memory the host reaches and sent as one request block, in this order (MiniGL's plan,
 * section 5):
 *   1. resources made and texels sent (ACV_RES_CREATE, ACV_PUT): the pre-commands;
 *   2. the vertex arena's used part (ACV_PUT, 32-bit units turned round by the host) and the index arena's (16-bit);
 *   3. the command stream (ACV_SUBMIT);
 *   4. versions let go of (ACV_RES_UNREF): the post-commands;
 *   5. the present, when the front end's hook gives one (end_frame).
 * When the stream, the pre-commands or the post-commands fill mid-frame, what there is goes as a block of its own
 * and the frame carries on: nothing is dropped. */
#include "pipe_int.h"

LONG pipe_run(OGPUPipeCtx *x, ULONG *blk, ULONG words)
{
    x->blocks++;
    return ogpu_pipe_xport_run(blk, words * 4) ? -1 : 0;
}

ULONG *pipe_pre(OGPUPipeCtx *x, ULONG words)
{
    ULONG *p;
    if (words > x->cpre) return 0;
    if (x->npre + words > x->cpre) pipe_send(x, 0);
    p = x->blk + x->npre;
    x->npre += words;
    return p;
}

ULONG *pipe_cmd(OGPUPipeCtx *x, ULONG words)
{
    ULONG *p;
    if (x->ncmd + words > x->ccmd) {
        pipe_send(x, 0);
        if (words > x->ccmd) { x->grow_cmd = words * 2; return 0; }    /* larger at the next frame's start */
    }
    p = x->cmd + x->ncmd;
    x->ncmd += words;
    return p;
}

ULONG *pipe_post(OGPUPipeCtx *x, ULONG words)
{
    ULONG *p;
    if (words > x->cpost) return 0;
    if (x->npost + words > x->cpost) pipe_send(x, 0);
    p = x->post + x->npost;
    x->npost += words;
    return p;
}

/* What there is to the host, as one block; the hook (OpenRTG's, through the front end) puts the present in it. */
LONG pipe_send(OGPUPipeCtx *x, const struct OGPUPresentHook *hook)
{
    ULONG *blk = x->blk, at = x->npre, a[3], i, submit_at = 0xFFFFFFFFUL, present_at = 0xFFFFFFFFUL;
    ULONG sent_va, sent_ia;
    LONG r = 0;
    if (x->sending) return 0;                   /* a send asked for while one is being built: it is this one */
    if (!at && !x->ncmd && !x->nva && !x->nia && !x->npost && !hook) return 0;
    x->sending = 1;
    sent_va = pipe_arena_puts(x, &at);
    sent_ia = x->nia * 2;
    if (x->ncmd) {
        a[0] = x->ctx_id; a[1] = ADDR(x->cmd); a[2] = x->ncmd;
        submit_at = at;
        vgc_blk_cmd(blk + at, ACV_SUBMIT, 3, a);
        at += VG_BLK_WORDS(3);
        x->words += x->ncmd;
    }
    for (i = 0; i < x->npost; i++) blk[at + i] = x->post[i];
    at += x->npost;
    if (hook && hook->prepare) {
        ULONG n = hook->prepare(hook->user, x->ctx_id, 0, blk + at, PIPE_BLK_SLACK - 64 - x->npost);
        if (n) { present_at = at; at += n; }
    }
    x->errors = 0;
    if (at) {
        if (pipe_run(x, blk, at)) { x->errors++; r = -1; }
        else {
            /* what the host refused: the pre-commands and the stream */
            for (i = 0; i < x->npre; i += ACV_HDR_WORDS(vgc_word(blk, i))) {
                if (!ACV_HDR_WORDS(vgc_word(blk, i))) break;
                if (vgc_result(blk, i)) x->errors++;
            }
            if (submit_at != 0xFFFFFFFFUL && vgc_result(blk, submit_at)) x->errors++;
        }
    }
    pipe_res_free_dead(x);                      /* the block that unrefs them has run */
    x->npre = 0; x->ncmd = 0; x->nva = 0; x->nia = 0; x->npost = 0;
    x->seq++;
    x->changed |= OGPU_PIPE_CH_IBUF | OGPU_PIPE_CH_SENT;   /* the index arena starts again at 0 */
    x->sending = 0;
    if (hook && hook->finish) hook->finish(hook->user, present_at != 0xFFFFFFFFUL ? blk : 0, present_at);
    pipe_arena_after(x, sent_va, sent_ia);      /* after that: a new buffer's create goes where this block was */
    return r;
}

/* ---- the context ---------------------------------------------------------------------------------------------- */

static void free_ctx(OGPUPipeCtx *x)
{
    pipe_res_drop_all(x);
    ogpu_pipe_xport_free(x->blk);
    ogpu_pipe_xport_free(x->post);
    ogpu_pipe_xport_free(x->cmd);
    ogpu_pipe_xport_free(x->va);
    ogpu_pipe_xport_free(x->ia);
    ogpu_pipe_xport_free(x->text);
    ogpu_pipe_xport_free(x);
}

/* M3: the screen's own context (its first, made in the open's block), with its block and arenas. flags: 0. */
OGPUPipeCtx *pipe_ctx_create(OGPUPipeScreen *s, ULONG ctx_id)
{
    OGPUPipeCtx *x;
    if (!s || !(x = ogpu_pipe_xport_alloc(sizeof *x))) return 0;
    x->screen = s;
    x->ctx_id = ctx_id;
    x->cpre = PIPE_PRE_WORDS; x->cpost = PIPE_POST_WORDS;
    x->blk = ogpu_pipe_xport_alloc((PIPE_PRE_WORDS + PIPE_BLK_SLACK) * 4);
    x->post = ogpu_pipe_xport_alloc(PIPE_POST_WORDS * 4);
    if (!x->blk || !x->post || !pipe_arenas(x, PIPE_VA_BYTES, PIPE_IA_COUNT, PIPE_CMD_WORDS)) { free_ctx(x); return 0; }
    return x;
}

void pipe_ctx_destroy(OGPUPipeCtx *x)
{
    ULONG a[1];
    if (!x) return;
    x->ncmd = 0; x->nva = 0; x->nia = 0;       /* nothing more is drawn */
    pipe_res_release(x, x->vbuf);
    pipe_res_release(x, x->ibuf);
    x->vbuf = x->ibuf = 0;
    pipe_send(x, 0);
    a[0] = x->ctx_id;
    vgc_blk_cmd(x->blk, ACV_CTX_DESTROY, 1, a);
    pipe_run(x, x->blk, VG_BLK_WORDS(1));
    free_ctx(x);
}

/* ---- the table's members (version 2's bridge, stage M3) -------------------------------------------------------- */

LONG pipe_m_flush(OGPUPipeCtx *x, ULONG *fence)
{
    if (fence) *fence = 0;                      /* the transport waits for each block: done when this returns */
    return x ? pipe_send(x, 0) : -1;
}

/* M3 implements end_frame early: the frame's send with the hook's present. The arenas grow here when a block of the
 * frame found them too small. */
LONG pipe_m_end_frame(OGPUPipeCtx *x, const struct OGPUPresentHook *hook, ULONG *fence)
{
    LONG r;
    if (!x) return -1;
    if (fence) *fence = 0;
    r = pipe_send(x, hook);
    if (x->grow_va || x->grow_ia || x->grow_cmd) {
        ULONG va = x->grow_va > x->cva ? x->grow_va : x->cva, ia = x->grow_ia > x->cia ? x->grow_ia : x->cia;
        ULONG cmd = x->grow_cmd > x->ccmd ? x->grow_cmd : x->ccmd;
        if (va > 32UL * 1024 * 1024) va = x->cva;   /* grow once, keep; not past 32 MB */
        if (ia > 4UL * 1024 * 1024) ia = x->cia;
        pipe_arenas(x, va, ia, cmd);
        x->grow_va = x->grow_ia = x->grow_cmd = 0;
    }
    return r;
}

ULONG *pipe_m_cmd_room(OGPUPipeCtx *x, ULONG words) { return x ? pipe_cmd(x, words) : 0; }
ULONG pipe_m_sent(OGPUPipeCtx *x) { return x ? x->seq : 0; }
ULONG pipe_m_errors(OGPUPipeCtx *x) { return x ? x->errors : 0; }

ULONG pipe_m_changed(OGPUPipeCtx *x)
{
    ULONG c;
    if (!x) return 0;
    c = x->changed;
    x->changed = 0;
    return c;
}

void pipe_m_counts(OGPUPipeCtx *x, ULONG *v5)
{
    if (!x || !v5) return;
    v5[0] = x->blocks; v5[1] = x->words; v5[2] = x->puts; v5[3] = x->renames; v5[4] = x->va_bytes;
    x->blocks = x->words = x->puts = x->va_bytes = 0;
}

