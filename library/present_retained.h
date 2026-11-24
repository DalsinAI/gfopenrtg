/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * ORTG_Present's RETAINED route (gfopenrtg.library 0.31): a retained surface
 * (gfopengpu/retained.h) shown in a RastPort, moved here from
 * gfopengpu.library's OGPU_RetainedPresent (stage M8, step 2.4).
 *  - The GPU path: the tiles that changed since the last present go up into
 *    the surface's texture, and the texture is drawn once into the target,
 *    scaled to the rectangle, one GFOpenGPU batch, waited for. Only into
 *    GFOpenRTG's own 32 and 16-bit bitmaps (screens.c fills the target in:
 *    the address, the format, the layer's visible pieces).
 *  - The CPU path: the changed tiles straight into the RastPort, 1:1 (the
 *    rectangle crops), as ORTG_WritePixels writes (layers, planar and CLUT
 *    targets too); an INDEX8 surface through its palette on a true-colour
 *    target, as pens on an 8-bit one.
 *  - A batch the GPU fails is shown the CPU way at once (gfopengpu's own
 *    present answered OGPU_RS_ERR_GPU and showed nothing).
 * Plain C, no writable data: tests/test_present_retained.c builds it on the
 * PC with its own rsp_ functions. */
#ifndef PRESENT_RETAINED_H
#define PRESENT_RETAINED_H

#include <exec/types.h>
#include <graphics/rastport.h>
#include <gfopenrtg/gfopenrtg.h>
#include "opengpu/ogpu_rs_handle.h"


struct rsp_target {
    struct RastPort *rp;
    int truecolour;                 /* the CPU path: 1 the target shows colours, 0 pens (CLUT or planar) */
    int gpu;                        /* 1: the GPU path may draw into it (the rest below is filled in) */
    unsigned long addr, bpr;        /* the bitmap's pixels, locked by the caller for the call */
    int w, h, dfmt;                 /* its size, and its OGPU_FMT_ */
    int ox, oy;                     /* the layer's corner in the bitmap (0, 0 without one) */
    const short *clips;             /* the layer's visible pieces, as struct ogpu_rs_present has them */
    int nclips;
};

/* The caller's: memory, a batch sent and waited for (OGPU_OK, 0, or not),
 * and the CPU's write (ortg_write_pixels). */
void *rsp_alloc(ULONG bytes);
void rsp_free(void *p);
LONG rsp_submit(const void *buf, ULONG words);
LONG rsp_write(struct RastPort *rp, LONG x, LONG y, const struct OpenRTGPixels *px);

/* The surface at x, y of the target, w x ht (both above 0). Returns the
 * tiles uploaded (0 or more) or an OGPU_RS_ERR_. */
LONG rsp_present(struct rs_handle *h, const struct rsp_target *t, LONG x, LONG y, LONG w, LONG ht);

#endif
