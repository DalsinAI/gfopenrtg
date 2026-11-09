/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * minigl.library's virgl route: the context on the host and its frames.
 *
 * A context is an ACVirgl context (its id: our struct's address) with a
 * colour and a depth resource at the drawable's size, a vertex and an index
 * buffer the size of the frame's arenas, and the objects drawing needs.
 * A frame is built in memory the host reaches (Fast RAM) and sent as one
 * block, in this order (plan section 5):
 *   1. resources made this frame, and texture uploads (ACV_RES_CREATE, ACV_PUT);
 *   2. the vertex arena's used part (ACV_PUT, 32-bit units turned round by
 *      the host) and the index arena's (16-bit units);
 *   3. the command stream (ACV_SUBMIT);
 *   4. resources let go of this frame (ACV_RES_UNREF);
 *   5. ACV_PRESENT, at mglSwitchDisplay.
 * When an arena or the stream fills mid-frame, what there is goes as a
 * block of its own and the frame carries on: geometry is never dropped,
 * and the arena that filled is made larger at the next frame's start. */
#include "mgl_vg.h"

#define CMD_WORDS   (64 * 1024)         /* 256 KB of commands */
#define PRE_WORDS   (8 * 1024)          /* creates and puts before the stream */
#define POST_WORDS  1024                /* unrefs after it */
#define BLK_SLACK   (64 + 2 * ACV_PRESENT_MAX + 16 + POST_WORDS)
#define VA_BYTES    (2 * 1024 * 1024)   /* vertices */
#define IA_COUNT    (256 * 1024)        /* 16-bit indices */
#define DRAW_ROOM   4096                /* stream words a draw may need (state, a shader) */

/* ---- resources ----------------------------------------------------------------------------- */

struct vg_res *vg_res_create(GLcontext c, ULONG target, ULONG fmt, ULONG bind, ULONG w, ULONG h, ULONG levels)
{
    struct vg_res *r = mgl_alloc(sizeof *r);
    ULONG a[12], *p;
    if (!r) return 0;
    r->handle = ADDR(r);
    r->fmt = fmt; r->w = w; r->h = h; r->levels = levels;
    a[0] = r->handle; a[1] = target; a[2] = fmt; a[3] = bind;
    a[4] = w; a[5] = h; a[6] = 1; a[7] = 1; a[8] = levels ? levels - 1 : 0; a[9] = 0; a[10] = 0;
    a[11] = c->vg->ctx_id;
    p = vg_pre(c, VG_BLK_WORDS(12));
    vg_blk_cmd(p, ACV_RES_CREATE, 12, a);
    return r;
}

void vg_res_release(GLcontext c, struct vg_res *r)
{
    struct mgl_vg *v = c->vg;
    ULONG a[1];
    if (!r) return;
    if (v->npost + VG_BLK_WORDS(1) > v->cpost) vg_send(c, 0);
    a[0] = r->handle;
    vg_blk_cmd(v->post + v->npost, ACV_RES_UNREF, 1, a);
    v->npost += VG_BLK_WORDS(1);
    r->next_dead = v->dead;
    v->dead = r;
}

void vg_put(GLcontext c, struct vg_res *r, ULONG level, ULONG x, ULONG y, ULONG w, ULONG h,
            const void *src, ULONG stride, ULONG bytes, ULONG swap)
{
    ULONG a[14], *p = vg_pre(c, VG_BLK_WORDS(14));
    a[0] = r->handle; a[1] = c->vg->ctx_id; a[2] = level; a[3] = stride; a[4] = 0;
    a[5] = x; a[6] = y; a[7] = 0; a[8] = w; a[9] = h; a[10] = 1;
    a[11] = ADDR(src); a[12] = bytes; a[13] = swap;
    vg_blk_cmd(p, ACV_PUT, 14, a);
    c->vg->puts++;
}

/* ---- arenas ------------------------------------------------------------------------------- */

static int arenas(GLcontext c, ULONG va, ULONG ia, ULONG cmd)
{
    struct mgl_vg *v = c->vg;
    UBYTE *nva = 0;
    UWORD *nia = 0;
    ULONG *ncmd = 0;
    if (va != v->cva && !(nva = mgl_alloc(va))) return 0;
    if (ia != v->cia && !(nia = mgl_alloc(ia * 2))) { mgl_free(nva); return 0; }
    if (cmd != v->ccmd && !(ncmd = mgl_alloc(cmd * 4))) { mgl_free(nva); mgl_free(nia); return 0; }
    if (nva) {
        struct vg_res *r = vg_res_create(c, VG_TARGET_BUFFER, VG_FMT_R8_UNORM, VG_BIND_VERTEX_BUFFER, va, 1, 1);
        if (!r) { mgl_free(nva); mgl_free(nia); mgl_free(ncmd); return 0; }
        if (v->vbuf) vg_res_release(c, v->vbuf);
        mgl_free(v->va);
        v->va = nva; v->cva = va; v->vbuf = r;
        v->dirty |= VG_D_VBUF;
    }
    if (nia) {
        struct vg_res *r = vg_res_create(c, VG_TARGET_BUFFER, VG_FMT_R8_UNORM, VG_BIND_INDEX_BUFFER, ia * 2, 1, 1);
        if (!r) { mgl_free(nia); mgl_free(ncmd); return 0; }
        if (v->ibuf) vg_res_release(c, v->ibuf);
        mgl_free(v->ia);
        v->ia = nia; v->cia = ia; v->ibuf = r;
        v->dirty |= VG_D_IBUF;
    }
    if (ncmd) { mgl_free(v->cmd); v->cmd = ncmd; v->ccmd = cmd; }
    return 1;
}

/* ---- sending ----------------------------------------------------------------------------------- */

void vg_send(GLcontext c, int present)
{
    struct mgl_vg *v = c->vg;
    ULONG *blk = v->blk, at = v->npre, a[14], submit_at = 0xFFFFFFFFUL, present_at = 0xFFFFFFFFUL, i;
    struct vg_res *r;
    ULONG t0 = mgl_trace > 0 ? mgl_millis() : 0;
    if (!at && !v->ncmd && !v->nva && !v->npost && !present) return;
    if (v->nva) {
        a[0] = v->vbuf->handle; a[1] = v->ctx_id; a[2] = 0; a[3] = 0; a[4] = 0;
        a[5] = 0; a[6] = 0; a[7] = 0; a[8] = v->nva; a[9] = 1; a[10] = 1;
        a[11] = ADDR(v->va); a[12] = v->nva; a[13] = VG_SW32;
        vg_blk_cmd(blk + at, ACV_PUT, 14, a);
        at += VG_BLK_WORDS(14);
        v->va_bytes += v->nva;
    }
    if (v->nia) {
        a[0] = v->ibuf->handle; a[8] = v->nia * 2; a[11] = ADDR(v->ia); a[12] = v->nia * 2; a[13] = VG_SW16;
        vg_blk_cmd(blk + at, ACV_PUT, 14, a);
        at += VG_BLK_WORDS(14);
    }
    if (v->ncmd) {
        a[0] = v->ctx_id; a[1] = ADDR(v->cmd); a[2] = v->ncmd;
        submit_at = at;
        vg_blk_cmd(blk + at, ACV_SUBMIT, 3, a);
        at += VG_BLK_WORDS(3);
        v->words += v->ncmd;
    }
    for (i = 0; i < v->npost; i++) blk[at + i] = v->post[i];
    at += v->npost;
    if (present) {
        present_at = at;
        at += vg_os_present(c, blk + at, BLK_SLACK - 64 - v->npost);
        if (at == present_at) present_at = 0xFFFFFFFFUL;
    }
    v->pre_errors = 0;
    if (at) {
        if (vg_run(c, blk, at)) { mgl_log("virgl: the block didn't run (%ld words)", (long)at); v->pre_errors++; }
        else {
            /* what the host refused: creates and puts, the stream */
            for (i = 0; i < v->npre; i += ACV_HDR_WORDS(GETW(blk + i)))
                if (!ACV_HDR_WORDS(GETW(blk + i))) break;
                else if (vg_result(blk, i)) {
                    v->pre_errors++;
                    mgl_log("virgl: command %ld refused (%ld)", (long)ACV_HDR_CMD(GETW(blk + i)), (long)vg_result(blk, i));
                }
            if (submit_at != 0xFFFFFFFFUL && vg_result(blk, submit_at)) {
                v->pre_errors++;
                mgl_log("virgl: the host refused the stream (%ld)", (long)vg_result(blk, submit_at));
            }
        }
    }
    if (mgl_trace > 0)
        mgl_log("virgl block: %ld words, stream %ld, vertices %ld bytes, %ld indices, %ld draws: %ld ms",
                (long)at, (long)v->ncmd, (long)v->nva, (long)v->nia, (long)v->draws, (long)(mgl_millis() - t0));
    while ((r = v->dead) != 0) { v->dead = r->next_dead; mgl_free(r); }
    v->npre = 0; v->ncmd = 0; v->nva = 0; v->nia = 0; v->npost = 0;
    v->seq++;
    v->lk_valid = 0;
    /* after the reset: a present that falls back reads the frame back */
    if (present) vg_os_presented(c, present_at != 0xFFFFFFFFUL ? blk : 0, present_at);
}

/* Room for a draw: va_bytes in the vertex arena, ia_count indices and the
 * stream words its state may need. What there is goes first if not; an
 * arena too small for one draw grows now (the frame so far is sent). */
void vg_need(GLcontext c, ULONG va_bytes, ULONG ia_count)
{
    struct mgl_vg *v = c->vg;
    if (v->nva + va_bytes + 64 <= v->cva && v->nia + ia_count <= v->cia && v->ncmd + DRAW_ROOM <= v->ccmd
        && v->npre + 64 <= v->cpre)
        return;
    if (v->nva + va_bytes + 64 > v->cva) v->grow_va = v->cva * 2;
    if (v->nia + ia_count > v->cia) v->grow_ia = v->cia * 2;
    vg_send(c, 0);
    if (va_bytes + 64 > v->cva || ia_count > v->cia) {
        ULONG va = v->cva, ia = v->cia;
        while (va < va_bytes + 64) va *= 2;
        while (ia < ia_count) ia *= 2;
        if (arenas(c, va, ia, v->ccmd)) vg_send(c, 0);   /* the new buffers made before they are used */
        v->grow_va = v->grow_ia = 0;
    }
}

/* ---- the context ----------------------------------------------------------------------------- */

static void free_vg(GLcontext c)
{
    struct mgl_vg *v = c->vg;
    struct vg_res *r;
    while ((r = v->dead) != 0) { v->dead = r->next_dead; mgl_free(r); }
    mgl_free(v->colour); mgl_free(v->depth); mgl_free(v->vbuf); mgl_free(v->ibuf);
    mgl_free(v->blk); mgl_free(v->post); mgl_free(v->cmd); mgl_free(v->va); mgl_free(v->ia);
    mgl_free(v);
    c->vg = 0;
}

/* HELLO, the context, the caps: 0 when the host has no virgl for us. */
static int hello(GLcontext c)
{
    struct mgl_vg *v = c->vg;
    static const char name[] = "minigl.library";
    ULONG a[4], *b = v->blk, rh, rc, ri, rcap, n, set = 2, i;
    ULONG *caps;
    char *nm = (char *)(v->post);       /* the name, in memory the host reaches */
    for (i = 0; i < sizeof name; i++) nm[i] = name[i];
    a[0] = 0;
    vg_blk_cmd(b, ACV_HELLO, 1, a); rh = 0;
    a[0] = v->ctx_id; a[1] = ADDR(nm); a[2] = sizeof name - 1;
    vg_blk_cmd(b + 3, ACV_CTX_CREATE, 3, a); rc = 3;
    a[0] = 2; a[1] = 0; a[2] = 0;
    vg_blk_cmd(b + 8, ACV_CAPSET_INFO, 3, a); ri = 8;
    if (vg_run(c, b, 13)) { mgl_log("virgl: the first block didn't run"); return 0; }
    if (vg_result(b, rh) != ACV_PROTOCOL) { mgl_log("virgl: the host speaks another ACVirgl (%ld)", (long)vg_result(b, rh)); return 0; }
    if (vg_result(b, rc) != 0) { mgl_log("virgl: the host refused a context (%ld)", (long)vg_result(b, rc)); return 0; }
    v->host_flags = vg_word(b, rh + 2);
    if (!vg_word(b, ri + 3)) {          /* no caps set 2: set 1 */
        set = 1;
        a[0] = 1;
        vg_blk_cmd(b, ACV_CAPSET_INFO, 3, a);
        if (vg_run(c, b, 5)) return 0;
        ri = 0;
    }
    n = vg_word(b, ri + 4);
    if (n > VG_CAPS_WORDS * 4) n = VG_CAPS_WORDS * 4;
    caps = (ULONG *)v->va;              /* the arena isn't in use yet */
    a[0] = set; a[1] = vg_word(b, ri + 3); a[2] = ADDR(caps); a[3] = n;
    vg_blk_cmd(b, ACV_CAPS, 4, a); rcap = 0;
    if (vg_run(c, b, 6) || vg_result(b, rcap) <= 0) { mgl_log("virgl: no caps"); return 0; }
    n = (ULONG)vg_result(b, rcap) / 4;  /* the host gives them in our order, word by word */
    v->prim_mask = n > 72 ? GETW(&caps[72]) : 0x7F;
    v->caps_bits = n > 98 ? GETW(&caps[98]) : 0;
    v->gles = (v->host_flags & ACV_HOST_GLES) != 0;
    return 1;
}

int mgl_vg_open(GLcontext c)
{
    struct mgl_vg *v;
    ULONG zfmt;
    if (!vg_os_open(c)) return 0;
    if (!(v = c->vg = mgl_alloc(sizeof *v))) return 0;
    v->ctx_id = ADDR(v);
    v->cpre = PRE_WORDS; v->cpost = POST_WORDS;
    v->blk = mgl_alloc((PRE_WORDS + BLK_SLACK) * 4);
    v->post = mgl_alloc(POST_WORDS * 4);
    if (!v->blk || !v->post) { free_vg(c); return 0; }
    /* the arenas' memory first (hello reads the caps into it); their buffers after */
    v->va = mgl_alloc(VA_BYTES);
    if (!v->va) { free_vg(c); return 0; }
    if (!hello(c)) { vg_os_close(c); free_vg(c); return 0; }
    mgl_free(v->va); v->va = 0;
    if (!arenas(c, VA_BYTES, IA_COUNT, CMD_WORDS)) goto fail;
    /* colour and depth, at the drawable's size; their surfaces; the framebuffer */
    zfmt = mgl_prefs.zbits == 16 ? VG_FMT_Z16_UNORM : VG_FMT_Z24X8_UNORM;
    v->colour = vg_res_create(c, VG_TARGET_2D, VG_FMT_B8G8R8A8_UNORM, VG_BIND_RENDER_TARGET | VG_BIND_SAMPLER_VIEW,
                              (ULONG)c->width, (ULONG)c->height, 1);
    v->depth = vg_res_create(c, VG_TARGET_2D, zfmt, VG_BIND_DEPTH_STENCIL, (ULONG)c->width, (ULONG)c->height, 1);
    if (!v->colour || !v->depth) goto fail;
    {
        ULONG *p;
        v->surf_c = vg_new_obj(c);
        v->surf_z = vg_new_obj(c);
        p = vg_cmd(c, 12);
        VG_W(p, VG_CMD0(VG_CCMD_CREATE_OBJECT, VG_OBJ_SURFACE, 5));
        VG_W(p, v->surf_c); VG_W(p, v->colour->handle); VG_W(p, VG_FMT_B8G8R8A8_UNORM); VG_W(p, 0); VG_W(p, 0);
        VG_W(p, VG_CMD0(VG_CCMD_CREATE_OBJECT, VG_OBJ_SURFACE, 5));
        VG_W(p, v->surf_z); VG_W(p, v->depth->handle); VG_W(p, zfmt); VG_W(p, 0); VG_W(p, 0);
    }
    vg_state_reset(c);
    vg_send(c, 0);                      /* made now, to see the host take them */
    if (v->pre_errors) { mgl_log("virgl: the host refused the context's resources"); goto fail; }
    mgl_log("virgl: context %lx, host flags %lx, prims %lx, caps %lx%s", (unsigned long)v->ctx_id,
            (unsigned long)v->host_flags, (unsigned long)v->prim_mask, (unsigned long)v->caps_bits, v->gles ? ", GLES" : "");
    return 1;
fail:
    mgl_vg_close(c);
    return 0;
}

void mgl_vg_close(GLcontext c)
{
    struct mgl_vg *v = c->vg;
    ULONG a[1];
    if (!v) return;
    v->ncmd = 0; v->nva = 0; v->nia = 0;    /* nothing more is drawn */
    if (v->colour) vg_res_release(c, v->colour);
    if (v->depth) vg_res_release(c, v->depth);
    if (v->vbuf) vg_res_release(c, v->vbuf);
    if (v->ibuf) vg_res_release(c, v->ibuf);
    v->colour = v->depth = v->vbuf = v->ibuf = 0;
    vg_send(c, 0);
    a[0] = v->ctx_id;
    vg_blk_cmd(v->blk, ACV_CTX_DESTROY, 1, a);
    vg_run(c, v->blk, VG_BLK_WORDS(1));
    vg_os_close(c);
    free_vg(c);
}

/* ---- frames ------------------------------------------------------------------------------------ */

void mgl_vg_switch(GLcontext c)
{
    struct mgl_vg *v = c->vg;
    vg_send(c, 1);
    v->frames++;
    if (v->grow_va || v->grow_ia || v->grow_cmd) {
        ULONG va = v->grow_va > v->cva ? v->grow_va : v->cva, ia = v->grow_ia > v->cia ? v->grow_ia : v->cia;
        ULONG cmd = v->grow_cmd > v->ccmd ? v->grow_cmd : v->ccmd;
        if (va > 32UL * 1024 * 1024) va = v->cva;  /* grow once, keep; not past 32 MB */
        if (ia > 4UL * 1024 * 1024) ia = v->cia;
        if (arenas(c, va, ia, cmd)) mgl_log("virgl: arenas now %ld bytes, %ld indices, %ld words", (long)va, (long)ia, (long)cmd);
        v->grow_va = v->grow_ia = v->grow_cmd = 0;
    }
    if (mgl_trace > 0)
        mgl_log("virgl frame %ld: %ld draws, %ld blocks, %ld objects, %ld words, %ld vertex bytes, %ld puts",
                (long)v->frames, (long)v->draws, (long)v->blocks, (long)v->objects, (long)v->words, (long)v->va_bytes, (long)v->puts);
    v->draws = v->blocks = v->words = v->va_bytes = v->puts = 0;
}

void mgl_vg_finish(GLcontext c)
{
    vg_send(c, 0);
}

void mgl_vg_clear(GLcontext c, GLbitfield mask)
{
    ULONG b = 0;
    float rgba[4];
    if ((mask & GL_COLOR_BUFFER_BIT) && (c->cmask & 15)) b |= VG_CLEAR_COLOR0;
    if ((mask & GL_DEPTH_BUFFER_BIT) && c->depth_mask) b |= VG_CLEAR_DEPTH;
    if (!b) return;
    vg_need(c, 0, 0);
    vg_prepare(c, 0, 0);                /* the framebuffer, as for a draw */
    rgba[0] = c->clear_color[0]; rgba[1] = c->clear_color[1]; rgba[2] = c->clear_color[2]; rgba[3] = c->clear_color[3];
    vg_clear(c, b, rgba, (float)c->clear_depth);
}

/* w x h pixels at x, y (top-left origin) as ARGB32 words, after everything
 * drawn so far: the frame so far is sent, and the box comes back. */
void mgl_vg_readback(GLcontext c, int x, int y, int w, int h, ULONG *argb, int stride)
{
    struct mgl_vg *v = c->vg;
    ULONG a[14], *buf, i;
    int j, x0 = x, y0 = y, x1 = x + w, y1 = y + h;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > c->width) x1 = c->width;
    if (y1 > c->height) y1 = c->height;
    for (j = 0; j < h; j++) for (i = 0; i < (ULONG)w; i++) argb[(ULONG)j * stride + i] = 0;
    if (x1 <= x0 || y1 <= y0) return;
    vg_send(c, 0);
    buf = mgl_alloc((ULONG)(x1 - x0) * (ULONG)(y1 - y0) * 4);
    if (!buf) return;
    a[0] = v->colour->handle; a[1] = v->ctx_id; a[2] = 0; a[3] = (ULONG)(x1 - x0) * 4; a[4] = 0;
    a[5] = (ULONG)x0; a[6] = (ULONG)y0; a[7] = 0; a[8] = (ULONG)(x1 - x0); a[9] = (ULONG)(y1 - y0); a[10] = 1;
    a[11] = ADDR(buf); a[12] = (ULONG)(x1 - x0) * (ULONG)(y1 - y0) * 4; a[13] = VG_SW32;
    vg_blk_cmd(v->blk, ACV_GET, 14, a);
    if (!vg_run(c, v->blk, VG_BLK_WORDS(14)) && !vg_result(v->blk, 0))
        for (j = y0; j < y1; j++)
            for (i = (ULONG)x0; i < (ULONG)x1; i++)
                argb[(ULONG)(j - y) * stride + (i - (ULONG)x)] = buf[(ULONG)(j - y0) * (ULONG)(x1 - x0) + (i - (ULONG)x0)] | 0xFF000000UL;
    mgl_free(buf);
}
