/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * ORTG_Present's plan (0.30): which route a present takes, and the one
 * ACV_PRESENT command a prepared present writes into the caller's block.
 * Plain C, so the host tests build the same code (tests/test_present_plan.c).
 *
 * The routes, in this order (docs/design/OpenGPU-Shared-Pipe-Plan.md 2.7):
 *  - PRS_IN_BLOCK: VIRGL, prepared, a target without a layer (a screen or a
 *    bare bitmap) that is a GFOpenRTG bitmap of 32 or 16 bits, no box to
 *    stretch into, and the host has ACV_PRESENT: one command in the
 *    caller's block, as MiniGL's and Warp3D's own presents wrote it;
 *  - PRS_VIRGL: VIRGL otherwise, the host has ACV_PRESENT and the target is
 *    a GFOpenRTG bitmap: ORTG_PresentVirgl's way (the layer's visible
 *    pieces, a block of GFOpenRTG's own); a prepared present takes it in
 *    ORTG_PresentDone, after the caller's block;
 *  - PRS_READBACK: VIRGL anything else (and when PRS_VIRGL fails): the
 *    frame back from the host, then as MEMORY;
 *  - PRS_MEMORY: the CPU writes it (ortg_write_pixels: planar targets too);
 *  - PRS_MEDIA: MEDIA, an ACMedia frame held on the host: the host writes it
 *    into the visible pieces, else into a buffer the CPU then writes
 *    (library/present_media.h). A prepared one is presented in
 *    ORTG_PresentDone, after the caller's block.
 */
#ifndef ORTG_PRESENT_PLAN_H
#define ORTG_PRESENT_PLAN_H

enum { PRS_NONE, PRS_IN_BLOCK, PRS_VIRGL, PRS_READBACK, PRS_MEMORY, PRS_MEDIA };

/* What ORTG_Present knows of a call. */
struct prs_call {
    unsigned long kind;             /* ORTG_SURF_ */
    int prepare;                    /* ORTG_PRESENT_PREPARE */
    int host_present;               /* GFOpenGPU carries virgl, and the host has ACV_PRESENT */
    int rtg;                        /* the target's bitmap is a GFOpenRTG one of 32 or 16 bits */
    int layered;                    /* the target RastPort has a layer (a window) */
    int boxed;                      /* a box to stretch into (dst) */
};

static inline int prs_route(const struct prs_call *c)
{
    if (c->kind == 2) return PRS_MEMORY;                    /* ORTG_SURF_MEMORY */
    if (c->kind == 3) return PRS_MEDIA;                     /* ORTG_SURF_MEDIA */
    if (c->kind != 1) return PRS_NONE;                      /* ORTG_SURF_VIRGL: the rest are later stages */
    if (c->host_present && c->rtg) {
        if (c->prepare && !c->layered && !c->boxed) return PRS_IN_BLOCK;
        return PRS_VIRGL;
    }
    return PRS_READBACK;
}

/* virgl.h's ACV_PRESENT: header, result, ten arguments, one rectangle. */
#define PRS_CMD_WORDS 14
#define PRS_ACV_PRESENT 13
#define PRS_FLIP_Y 1                /* ACV_PRESENT_FLIP_Y */

/* One present command at p (PRS_CMD_WORDS free): the resource at sx, sy of
 * a tw by th target at mem (stride bytes a row, fmt an OGPU_FMT_), the one
 * rectangle rx, ry, rw by rh of the target. Words as the 68k writes them
 * (the Amiga's order: the block is read as Amiga words). Returns the words. */
static inline unsigned long prs_cmd(unsigned long *p, unsigned long ctx, unsigned long handle, long sx, long sy,
                                    unsigned long mem, unsigned long stride, unsigned long fmt,
                                    unsigned long tw, unsigned long th, unsigned long flags,
                                    long rx, long ry, unsigned long rw, unsigned long rh)
{
    p[0] = (unsigned long)PRS_ACV_PRESENT << 16 | PRS_CMD_WORDS;
    p[1] = 0;
    p[2] = ctx;
    p[3] = handle;
    p[4] = 0;                                               /* layer */
    p[5] = (unsigned long)(unsigned short)sx << 16 | (unsigned short)sy;
    p[6] = mem;
    p[7] = stride;
    p[8] = fmt;
    p[9] = (tw & 0xFFFF) << 16 | (th & 0xFFFF);
    p[10] = flags;
    p[11] = 1;
    p[12] = (unsigned long)(unsigned short)rx << 16 | (unsigned short)ry;
    p[13] = (rw & 0xFFFF) << 16 | (rh & 0xFFFF);
    return PRS_CMD_WORDS;
}

/* The source's part (x0, y0, x1, y1 inclusive, NULL: all) clamped to the
 * source; 0 when nothing is left. */
static inline int prs_part(const short *src4, unsigned long w, unsigned long h, long *x0, long *y0, long *x1, long *y1)
{
    *x0 = 0; *y0 = 0; *x1 = (long)w - 1; *y1 = (long)h - 1;
    if (src4) {
        if (src4[0] > *x0) *x0 = src4[0];
        if (src4[1] > *y0) *y0 = src4[1];
        if (src4[2] < *x1) *x1 = src4[2];
        if (src4[3] < *y1) *y1 = src4[3];
    }
    return *x0 <= *x1 && *y0 <= *y1;
}

#endif
