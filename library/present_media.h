/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * ORTG_Present's MEDIA kind (0.30, the shared pipe's plan M8 step 2.6): an
 * ACMedia frame held on the host (ACM_HOLD) written by the host straight
 * into the target's visible pieces (ACM_SHOW), so a film in a window never
 * passes through 68k memory. Plain C, so the host tests build the same code
 * (tests/test_present_media.c).
 *
 * A present gathers the pieces of one bitmap (up to PRM_RECTS rectangles)
 * and sends them as one ACM_SHOW; a piece of another bitmap, or a full
 * list, sends what is gathered first. ACM_SHOW's block is the hostlib's
 * (hostlib/acmedia/acmedia.h): 0 frame, 1 target, 2 pitch, 3 format,
 * 4 rows, 5 at (x << 16 | y), 6 rectangles, 7 count. The rectangles are
 * struct Rectangle's WORDs, in the bitmap.
 *
 * When the host can't write a piece (a bitmap that is not 16 or 32 bits,
 * ACM_E_FORMAT, ACM_E_NOTAVAIL: no broker, or a target it won't write),
 * the present is left to the bounce (prm_bounce_block): the frame shown
 * into a buffer of the caller's as ARGB32, then written by the CPU. */
#ifndef ORTG_PRESENT_MEDIA_H
#define ORTG_PRESENT_MEDIA_H

#define PRM_RECTS 64                /* rectangles one ACM_SHOW carries here (the host takes up to 512) */
#define PRM_SHOW_LONGS 8
#define PRM_PIX_RGB16 2             /* ACM_PIX_RGB16 */
#define PRM_PIX_ARGB32 3            /* ACM_PIX_ARGB32 */
#define PRM_E_NOTAVAIL (-1)         /* ACM_E_NOTAVAIL */
#define PRM_E_FORMAT (-4)           /* ACM_E_FORMAT */
#define PRM_BOUNCE (-100)           /* the host can't: the bounce, then the CPU */

struct prm_ctx {
    unsigned long frame;            /* the held frame (ACM_HOLD's answer) */
    long ox, oy;                    /* the frame's pixel 0, 0 in the bitmap being gathered */
    unsigned long mem, pitch, format, rows;     /* that bitmap: 0 mem none yet */
    unsigned long n;                /* rectangles gathered */
    short rects[4 * PRM_RECTS];
    unsigned long blk[PRM_SHOW_LONGS];
    long (*show)(unsigned long *blk);           /* hostlib_acm_show */
    long pixels;                    /* written so far; < 0 an error, PRM_BOUNCE the host can't */
};

static inline void prm_init(struct prm_ctx *c, unsigned long frame, long (*show)(unsigned long *))
{
    c->frame = frame; c->show = show;
    c->ox = c->oy = 0; c->mem = c->pitch = c->format = c->rows = 0;
    c->n = 0; c->pixels = 0;
}

static inline unsigned long prm_at(long x, long y) { return (unsigned long)(unsigned short)x << 16 | (unsigned short)y; }

/* The gathered rectangles to the host. */
static inline void prm_send(struct prm_ctx *c)
{
    long r;
    if (!c->n || c->pixels < 0) { c->n = 0; return; }
    c->blk[0] = c->frame;
    c->blk[1] = c->mem;
    c->blk[2] = c->pitch;
    c->blk[3] = c->format;
    c->blk[4] = c->rows;
    c->blk[5] = prm_at(c->ox, c->oy);
    c->blk[6] = (unsigned long)c->rects;
    c->blk[7] = c->n;
    r = c->show(c->blk);
    c->n = 0;
    if (r == PRM_E_FORMAT || r == PRM_E_NOTAVAIL) c->pixels = PRM_BOUNCE;
    else if (r < 0) c->pixels = r;
    else c->pixels += r;
}

/* One visible piece: x0..x1, y0..y1 (inclusive) of a bitmap at mem (pitch bytes a row, rows rows,
 * bpp bytes a pixel), the frame's pixel 0, 0 at ox, oy of it. */
static inline void prm_piece(struct prm_ctx *c, unsigned long mem, unsigned long pitch, unsigned long rows, int bpp,
                             long x0, long y0, long x1, long y1, long ox, long oy)
{
    unsigned long fmt = bpp == 4 ? PRM_PIX_ARGB32 : bpp == 2 ? PRM_PIX_RGB16 : 0;
    short *r;
    if (c->pixels < 0) return;
    if (!fmt || !mem || ox < -32768 || ox > 32767 || oy < -32768 || oy > 32767 ||
        x0 < -32768 || x1 > 32767 || y0 < -32768 || y1 > 32767) { c->pixels = PRM_BOUNCE; return; }
    if (c->n && (c->mem != mem || c->ox != ox || c->oy != oy || c->n == PRM_RECTS)) prm_send(c);
    if (c->pixels < 0) return;
    c->mem = mem; c->pitch = pitch; c->format = fmt; c->rows = rows; c->ox = ox; c->oy = oy;
    r = c->rects + 4 * c->n++;
    r[0] = (short)x0; r[1] = (short)y0; r[2] = (short)x1; r[3] = (short)y1;
}

/* The bounce's ACM_SHOW: the frame's part x0..x1, y0..y1 into a buffer at buf of its size, ARGB32,
 * row 0 the part's top. rect (four WORDs) is the caller's. */
static inline void prm_bounce_block(unsigned long *blk, short *rect, unsigned long frame, unsigned long buf,
                                    long x0, long y0, long x1, long y1)
{
    unsigned long w = (unsigned long)(x1 - x0 + 1), h = (unsigned long)(y1 - y0 + 1);
    rect[0] = 0; rect[1] = 0; rect[2] = (short)(w - 1); rect[3] = (short)(h - 1);
    blk[0] = frame;
    blk[1] = buf;
    blk[2] = w * 4;
    blk[3] = PRM_PIX_ARGB32;
    blk[4] = h;
    blk[5] = prm_at(-x0, -y0);
    blk[6] = (unsigned long)rect;
    blk[7] = 1;
}

#endif
