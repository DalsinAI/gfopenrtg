/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * The shared pipe's arenas (stage M3, pipe_int.h): from minigl.library's mgl_vg_frame.c (arenas, vg_ring, vg_need).
 *
 * A context has a vertex arena and an index arena in memory the host reaches; a block sends their used parts with one
 * ACV_PUT each, into the host's vertex and index buffers. Where they go in those buffers moves on after every block:
 * each block's arenas after the last one's, never over them, since virglrenderer writes a buffer's PUT unsynchronized
 * (GL_MAP_UNSYNCHRONIZED_BIT): a PUT over vertices an earlier block's draws still read would change those draws on the
 * GPU. The buffers are PIPE_RING arenas long; past the end, new buffers are made and the old ones let go of after the
 * next block (the host keeps them for the draws that still read them). An arena too small for one draw grows at once;
 * one that filled mid-frame grows at the frame's end (end_frame). */
#include "pipe_int.h"

static OGPUPipeRes *buffer(OGPUPipeCtx *x, ULONG bind, ULONG bytes)
{
    struct OGPUPipeResDesc d;
    d.target = VG_TARGET_BUFFER; d.format = VG_FMT_R8_UNORM; d.bind = bind;
    d.width = bytes; d.height = 1; d.levels = 1; d.flags = OGPU_RES_STREAM;
    return pipe_res_create(x, &d);
}

int pipe_arenas(OGPUPipeCtx *x, ULONG va, ULONG ia, ULONG cmd)
{
    UBYTE *nva = 0;
    UWORD *nia = 0;
    ULONG *ncmd = 0;
    if (va != x->cva && !(nva = ogpu_pipe_xport_alloc(va))) return 0;
    if (ia != x->cia && !(nia = ogpu_pipe_xport_alloc(ia * 2))) { ogpu_pipe_xport_free(nva); return 0; }
    if (cmd != x->ccmd && !(ncmd = ogpu_pipe_xport_alloc(cmd * 4))) { ogpu_pipe_xport_free(nva); ogpu_pipe_xport_free(nia); return 0; }
    if (nva) {
        OGPUPipeRes *r = buffer(x, VG_BIND_VERTEX_BUFFER, va * PIPE_RING);
        if (!r) { ogpu_pipe_xport_free(nva); ogpu_pipe_xport_free(nia); ogpu_pipe_xport_free(ncmd); return 0; }
        if (x->vbuf) pipe_res_release(x, x->vbuf);
        ogpu_pipe_xport_free(x->va);
        x->va = nva; x->cva = va; x->vbuf = r;
        x->vb_off = 0; x->vb_size = va * PIPE_RING;
        x->changed |= OGPU_PIPE_CH_VBUF;
    }
    if (nia) {
        OGPUPipeRes *r = buffer(x, VG_BIND_INDEX_BUFFER, ia * 2 * PIPE_RING);
        if (!r) { ogpu_pipe_xport_free(nia); ogpu_pipe_xport_free(ncmd); return 0; }
        if (x->ibuf) pipe_res_release(x, x->ibuf);
        ogpu_pipe_xport_free(x->ia);
        x->ia = nia; x->cia = ia; x->ibuf = r;
        x->ib_base = 0; x->ib_size = ia * 2 * PIPE_RING;
        x->changed |= OGPU_PIPE_CH_IBUF;
    }
    if (ncmd) {
        ULONG i;
        for (i = 0; i < x->ncmd && i < cmd; i++) ncmd[i] = x->cmd[i];   /* a stream not yet sent goes with it */
        ogpu_pipe_xport_free(x->cmd); x->cmd = ncmd; x->ccmd = cmd;
    }
    return 1;
}

/* The arenas' PUTs into the block at *at (the stream marks the buffers used). The vertex bytes sent. */
ULONG pipe_arena_puts(OGPUPipeCtx *x, ULONG *at)
{
    ULONG a[14], sent = x->nva;
    if (x->nva && x->vbuf) {
        a[0] = x->vbuf->ver->handle; a[1] = x->ctx_id; a[2] = 0; a[3] = 0; a[4] = 0;
        a[5] = x->vb_off; a[6] = 0; a[7] = 0; a[8] = x->nva; a[9] = 1; a[10] = 1;
        a[11] = ADDR(x->va); a[12] = x->nva; a[13] = VG_SW32;
        vgc_blk_cmd(x->blk + *at, ACV_PUT, 14, a);
        *at += VG_BLK_WORDS(14);
        x->va_bytes += x->nva;
    }
    if (x->nia && x->ibuf) {
        a[0] = x->ibuf->ver->handle; a[1] = x->ctx_id; a[2] = 0; a[3] = 0; a[4] = 0;
        a[5] = x->ib_base; a[6] = 0; a[7] = 0; a[8] = x->nia * 2; a[9] = 1; a[10] = 1;
        a[11] = ADDR(x->ia); a[12] = x->nia * 2; a[13] = VG_SW16;
        vgc_blk_cmd(x->blk + *at, ACV_PUT, 14, a);
        *at += VG_BLK_WORDS(14);
    }
    return sent;
}

/* After a block: the next arenas after this one's in the buffers, or new buffers when no whole arena fits there. */
void pipe_arena_after(OGPUPipeCtx *x, ULONG va_bytes, ULONG ia_bytes)
{
    OGPUPipeRes *r;
    if (va_bytes && x->vbuf) {
        x->vb_off += (va_bytes + 255) & ~255UL;
        x->changed |= OGPU_PIPE_CH_VBUF;
        if (x->vb_off + x->cva > x->vb_size) {
            r = buffer(x, VG_BIND_VERTEX_BUFFER, x->cva * PIPE_RING);
            if (r) { pipe_res_release(x, x->vbuf); x->vbuf = r; x->vb_size = x->cva * PIPE_RING; }
            x->vb_off = 0;                      /* no memory for a new one: over the old (as MiniGL did) */
        }
    }
    if (ia_bytes && x->ibuf) {
        x->ib_base += (ia_bytes + 255) & ~255UL;
        x->changed |= OGPU_PIPE_CH_IBUF;
        if (x->ib_base + x->cia * 2 > x->ib_size) {
            r = buffer(x, VG_BIND_INDEX_BUFFER, x->cia * 2 * PIPE_RING);
            if (r) { pipe_res_release(x, x->ibuf); x->ibuf = r; x->ib_size = x->cia * 2 * PIPE_RING; }
            x->ib_base = 0;
        }
    }
}

/* Room for a draw: va_bytes in the vertex arena, ia_count indices, cmd_words of stream. What there is goes first if
 * not; an arena too small for one draw grows now. 1 when what was there was sent (the front end's arena offsets start
 * again), 0 when there was room, -1 when it can't be made. */
int pipe_need(OGPUPipeCtx *x, ULONG va_bytes, ULONG ia_count, ULONG cmd_words)
{
    if (!cmd_words) cmd_words = PIPE_DRAW_ROOM;
    if (x->nva + va_bytes + 64 <= x->cva && x->nia + ia_count <= x->cia && x->ncmd + cmd_words <= x->ccmd
        && x->npre + 64 <= x->cpre)
        return 0;
    if (x->nva + va_bytes + 64 > x->cva) x->grow_va = x->cva * 2;
    if (x->nia + ia_count > x->cia) x->grow_ia = x->cia * 2;
    if (x->ncmd + cmd_words > x->ccmd && cmd_words > x->ccmd) x->grow_cmd = cmd_words * 2;
    pipe_send(x, 0);
    if (va_bytes + 64 > x->cva || ia_count > x->cia || cmd_words > x->ccmd) {
        ULONG va = x->cva, ia = x->cia, cmd = x->ccmd;
        while (va < va_bytes + 64) va *= 2;
        while (ia < ia_count) ia *= 2;
        while (cmd < cmd_words) cmd *= 2;
        if (!pipe_arenas(x, va, ia, cmd)) return -1;
        pipe_send(x, 0);                        /* the new buffers made before they are used */
        x->grow_va = x->grow_ia = x->grow_cmd = 0;
    }
    return 1;
}

/* ---- the table's members -------------------------------------------------------------------------------------- */

void *pipe_m_vtx_room(OGPUPipeCtx *x, ULONG bytes, ULONG *first)
{
    void *p;
    if (!x || pipe_need(x, bytes, 0, 1) < 0) return 0;
    p = x->va + x->nva;
    if (first) *first = x->nva;                 /* bytes from the arena's start, where VERTEX_BUFFER points */
    x->nva += bytes;
    return p;
}

UWORD *pipe_m_idx_room(OGPUPipeCtx *x, ULONG count, ULONG *first)
{
    UWORD *p;
    if (!x || pipe_need(x, 0, count, 1) < 0) return 0;
    p = x->ia + x->nia;
    if (first) *first = x->nia;
    x->nia += count;
    return p;
}

/* Warp3D's batches: count bytes of vertex memory, copied into the arena in one go. Its first byte's offset. */
ULONG pipe_m_vtx_put_range(OGPUPipeCtx *x, const void *mem, ULONG bytes)
{
    ULONG first = 0, i;
    UBYTE *p = pipe_m_vtx_room(x, bytes, &first);
    if (!p) return 0xFFFFFFFFUL;
    for (i = 0; i < bytes; i++) p[i] = ((const UBYTE *)mem)[i];
    return first;
}

LONG pipe_m_need(OGPUPipeCtx *x, ULONG va_bytes, ULONG ia_count, ULONG cmd_words)
{
    return x ? pipe_need(x, va_bytes, ia_count, cmd_words) : -1;
}

/* Where the arenas are now, for a front end that writes its own bindings and its own vertices (MiniGL, M3 to M7):
 * the arena's memory and its use, and the buffer's handle and the block's offset in it. */
void pipe_m_arena_state(OGPUPipeCtx *x, struct OGPUPipeArenas *a)
{
    if (!x || !a) return;
    a->va = x->va; a->nva = &x->nva; a->cva = x->cva;
    a->ia = x->ia; a->nia = &x->nia; a->cia = x->cia;
    a->vbuf = x->vbuf ? x->vbuf->ver->handle : 0; a->vb_off = x->vb_off;
    a->ibuf = x->ibuf ? x->ibuf->ver->handle : 0; a->ib_base = x->ib_base;
}


/* Version 5 (M6): whether a draw fits now, without sending (Warp3D ends its run of merged draws first when not). */
LONG pipe_m_room(OGPUPipeCtx *x, ULONG va_bytes, ULONG ia_count, ULONG cmd_words, ULONG pre_words)
{
    if (!x) return 0;
    return x->nva + va_bytes + 64 <= x->cva && x->nia + ia_count <= x->cia && x->ncmd + cmd_words <= x->ccmd
        && x->npre + pre_words + 64 <= x->cpre;
}

/* Version 5 (M6): Warp3D's batch memory into its own vertex buffer, PUT from where it is (the 68k copies nothing).
 * The front end keeps the ring (never over what an earlier block's draws read), so this never renames. */
void pipe_m_buf_put(OGPUPipeCtx *x, OGPUPipeRes *r, ULONG offset, const void *src, ULONG bytes, ULONG swap)
{
    ULONG a[14], *p;
    if (!x || !r || !src || !bytes) return;
    if (!(p = pipe_pre(x, VG_BLK_WORDS(14)))) return;
    a[0] = r->ver->handle; a[1] = x->ctx_id; a[2] = 0; a[3] = 0; a[4] = 0;
    a[5] = offset; a[6] = 0; a[7] = 0; a[8] = bytes; a[9] = 1; a[10] = 1;
    a[11] = ADDR(src); a[12] = bytes; a[13] = swap;
    vgc_blk_cmd(p, ACV_PUT, 14, a);
    r->put_seq = x->seq + 1;
    x->puts++;
}
