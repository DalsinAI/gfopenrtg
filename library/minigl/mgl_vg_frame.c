/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * minigl.library's virgl route: the context on the host and its frames.
 *
 * A context is an ACVirgl context (its id: our struct's address) with a
 * colour and a depth resource at the drawable's size. Its block, its
 * resources, its vertex and index arenas and their buffers are the shared
 * pipe's (library/pipe/, reached through mgl_vg_pipe.c): a frame is built
 * there and sent as one block, with the present at mglSwitchDisplay. When an
 * arena or the stream fills mid-frame, what there is goes as a block of its
 * own and the frame carries on: geometry is never dropped, and the pipe
 * makes the arena that filled larger at the frame's end. */
#include "mgl_vg.h"

/* ---- resources ----------------------------------------------------------------------------- */

struct vg_res *vg_res_create(GLcontext c, ULONG target, ULONG fmt, ULONG bind, ULONG w, ULONG h, ULONG levels)
{
    return vgp_res_create(c, target, fmt, bind, w, h, levels);
}

void vg_res_release(GLcontext c, struct vg_res *r)
{
    if (r) vgp_res_release(c, r);
}

void vg_put(GLcontext c, struct vg_res *r, ULONG level, ULONG x, ULONG y, ULONG w, ULONG h,
            const void *src, ULONG stride, ULONG bytes, ULONG swap)
{
    (void)bytes;
    vgp_put(c, r, level, x, y, w, h, src, stride, swap);
}

/* ---- sending ----------------------------------------------------------------------------------- */

void vg_send(GLcontext c, int present) { vgp_send(c, present); }

/* Room for a draw: va_bytes in the vertex arena, ia_count indices and the
 * stream words its state may need. What there is goes first if not; an
 * arena too small for one draw grows now (the frame so far is sent). */
void vg_need(GLcontext c, ULONG va_bytes, ULONG ia_count) { vgp_need(c, va_bytes, ia_count); }

/* glBegin's arena is full mid-primitive: what there is goes, and the arena is made larger at the frame's end. */
void vg_send_overflow(GLcontext c, ULONG stride) { vgp_overflow(c, stride); }

/* ---- the context ----------------------------------------------------------------------------- */

int mgl_vg_open(GLcontext c) { return vgp_open(c); }

void mgl_vg_close(GLcontext c) { vgp_close(c); }

/* ---- frames ------------------------------------------------------------------------------------ */

void mgl_vg_switch(GLcontext c)
{
    struct mgl_vg *v = c->vg;
    vg_send(c, 1);
    v->frames++;
    {                                   /* the pipe grew its arenas in end_frame; its counts join ours */
        ULONG k[5];
        v->pipe->counts(v->px, k);
        v->blocks += k[0]; v->words += k[1]; v->puts += k[2]; v->renames += k[3]; v->va_bytes += k[4];
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
    if ((c->en & EN_SCISSOR) || ((b & VG_CLEAR_COLOR0) && (c->cmask & 15) != 15)) {
        vg_clear_quad(c, mask);         /* virgl's CLEAR is the whole surface, all channels */
        return;
    }
    vg_need(c, 0, 0);
    vg_prepare(c, 0, 0, 0);             /* the framebuffer, as for a draw */
    rgba[0] = c->clear_color[0]; rgba[1] = c->clear_color[1]; rgba[2] = c->clear_color[2]; rgba[3] = c->clear_color[3];
    vg_clear(c, b, rgba, (float)c->clear_depth);
}

/* w x h pixels at x, y (top-left origin) as ARGB32 words, after everything
 * drawn so far: the frame so far is sent, and the box comes back. */
void mgl_vg_readback(GLcontext c, int x, int y, int w, int h, ULONG *argb, int stride)
{
    ULONG *buf, i;
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
    if (!vgp_readback(c, x0, y0, x1, y1, buf))
        for (j = y0; j < y1; j++)
            for (i = (ULONG)x0; i < (ULONG)x1; i++)
                argb[(ULONG)(j - y) * stride + (i - (ULONG)x)] = GETW(&buf[(ULONG)(j - y0) * (ULONG)(x1 - x0) + (i - (ULONG)x0)]) | 0xFF000000UL;
    mgl_free(buf);
}
