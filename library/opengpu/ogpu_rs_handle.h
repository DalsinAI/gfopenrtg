/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * A retained surface as the libraries keep it (gfopengpu/retained.h is the
 * program's view): gfopengpu.library makes, marks and frees it; its present
 * is gfopenrtg.library's (0.31, ORTG_Present's RETAINED route,
 * library/present_retained.c), and gfopengpu.library's own present stays
 * for one release behind ENV:GFOpenGPU/RetainedPresentOld. Not an SDK
 * header: both libraries are built from this repository. */
#ifndef OGPU_RS_HANDLE_H
#define OGPU_RS_HANDLE_H

#include <exec/types.h>
#include <gfopengpu/retained.h>
#include "ogpu_retained.h"

/* A surface: what the program sees, then the library's own. One block holds
 * it, the palette, the tiles' state and the pixels; the texture is a block
 * of its own (AllocVec), made at the first present on the GPU path and freed
 * by OGPU_RetainedDestroy. */
struct rs_handle {
    struct OGPURetained pub;
    struct ogpu_rs core;                        /* the tiles (ogpu_retained.c) */
    UBYTE *tex;                                 /* the texture, or NULL */
    UBYTE tex_valid;                            /* the texture holds every tile as last uploaded */
    UBYTE pal_changed;                          /* OGPU_RetainedPalette since the last present */
    UBYTE path;                                 /* the last present's: RS_NONE, RS_GPU or RS_CPU */
    struct RastPort *last_rp;                   /* the software path's last target: the tiles are there */
    struct BitMap *last_bm;
    LONG last_x, last_y, last_w, last_h;
    struct OGPURetainedStats st;
    UBYTE present_old;                          /* ENV:GFOpenGPU/RetainedPresentOld 1 when it was made */
};
#define RS_NONE 0
#define RS_GPU 1
#define RS_CPU 2
#define RS_NO_GPU (-100)                        /* the GPU path: not this way, try the software path */
#define RS_MAX 16383                            /* a side, at most (a 32-bit row in WritePixelArray's UWORD) */

/* A present done: the statistics, and which path it took. */
static inline void rs_count(struct rs_handle *h, long n, int path)
{
    h->st.frames++;
    h->st.tiles_uploaded += (ULONG)n;
    h->st.last_tiles = (ULONG)n;
    h->st.tiles_hashed = h->core.hashed;
    if (path == RS_GPU) h->st.gpu_frames++;
    else h->st.cpu_frames++;
    if (h->pal_changed && h->pub.rs_Palette) h->st.palette_uploads++;
    h->pal_changed = 0;
    h->path = (UBYTE)path;
}

#endif
