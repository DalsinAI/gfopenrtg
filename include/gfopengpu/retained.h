/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * Retained GPU surfaces (gfopengpu.library 0.25): a program's picture that
 * stays authoritative in CPU memory, shown through a texture the GPU keeps.
 *
 *  - OGPU_RetainedCreate gives a surface: the program draws into
 *    rs_Pixels (rs_BytesPerRow a row) as it likes, with the CPU.
 *  - OGPU_RetainedPresent shows it. Only the 32x32 tiles that changed since
 *    the last present are uploaded into the texture; then the whole texture
 *    is drawn once into the RastPort's bitmap (scaled to the rectangle
 *    given). Nothing is ever read back from the screen. It finds the
 *    changed tiles itself by hashing each tile, unless the program said
 *    what it changed with OGPU_RetainedMark since the last present: then
 *    the marked tiles are uploaded and nothing is hashed.
 *  - An 8-bit surface (OGPU_FMT_INDEX8) is uploaded as indexes, and drawn
 *    through a 256-entry palette (OGPU_RetainedPalette): a palette change
 *    uploads the palette alone, no tile.
 *  - With no GPU (no driver in LIBS:GFOpenGPU/, or one that can't draw the
 *    formats), the same calls copy the changed tiles into the RastPort with
 *    graphics.library or cybergraphics.library (WriteLUTPixelArray,
 *    WritePixelArray, or WriteChunkyPixels on a CLUT screen), 1:1: no
 *    texture is kept and the rectangle's size only crops.
 *
 * gfopengpu.library is version 0: open it with version 0 and check
 * lib_Revision >= OGPU_RETAINED_REVISION before using these.
 *
 * LVOs (gfopengpu_lib.sfd):
 *   162 OGPU_RetainedCreate(width, height, format, flags)(d0,d1,d2,d3)
 *   168 OGPU_RetainedPalette(rs, first, count, argb)(a0,d0,d1,a1)
 *   174 OGPU_RetainedMark(rs, x, y, width, height)(a0,d0,d1,d2,d3)
 *   180 OGPU_RetainedPresent(rs, rp, x, y, width, height)(a0,a1,d0,d1,d2,d3)
 *   186 OGPU_RetainedStats(rs, stats)(a0,a1)
 *   192 OGPU_RetainedDestroy(rs)(a0)
 *
 * One task at a time uses a surface (the library doesn't lock it). A
 * present waits until its uploads are done, so the program may draw into
 * rs_Pixels again as soon as it returns.
 *
 * In a window, present draws only the layer's visible parts. After a
 * refresh (IDCMP_REFRESHWINDOW, a window moved or uncovered) present
 * again: on the GPU path the whole texture is drawn every time; on the
 * software path mark the whole surface first (OGPU_RetainedMark(rs, 0, 0,
 * -1, -1)), as only the changed tiles are copied there. A smart-refresh
 * window with hidden parts is drawn the software way (layers keep those
 * parts). */
#ifndef OPENGPU_RETAINED_H
#define OPENGPU_RETAINED_H

#include <exec/types.h>
#include "stream.h"

#define OGPU_RETAINED_REVISION  25          /* gfopengpu.library 0.25 (lib_Revision, version 0) */
#define OGPU_RETAINED_TILE      32          /* tiles are 32 x 32 pixels */

/* OGPU_RetainedCreate's format: OGPU_FMT_INDEX8 (or OGPU_FMT_CLUT8, or the
 * depth 8: the same), OGPU_FMT_XRGB32 (or the depth 32) or OGPU_FMT_ARGB32
 * (drawn opaque: its alpha is not used). */
#define OGPU_RETAINED_DEPTH8    8
#define OGPU_RETAINED_DEPTH32   32

/* OGPU_RetainedCreate's flags. */
#define OGPU_RSF_SOFTWARE       (1UL << 0)  /* never the GPU path (for tests, or to compare) */
#define OGPU_RSF_BILINEAR       (1UL << 1)  /* filter when the present scales (GPU path); nearest otherwise */
/* Reserved: the texture authoritative (the program draws into the texture
 * with OGPU stream commands, and rs_Pixels is not kept). Not there yet:
 * OGPU_RetainedCreate answers NULL when it is asked for. */
#define OGPU_RSF_TEXTURE_AUTH   (1UL << 31)

/* What OGPU_RetainedCreate gives: read only, but for the pixels. */
struct OGPURetained {
    APTR  rs_Pixels;                /* row 0 of the surface; draw here */
    ULONG rs_BytesPerRow;           /* a multiple of 16 */
    UWORD rs_Width, rs_Height;
    ULONG rs_Format;                /* OGPU_FMT_INDEX8, OGPU_FMT_XRGB32 or OGPU_FMT_ARGB32 */
    ULONG rs_Flags;                 /* as created */
    ULONG *rs_Palette;              /* INDEX8: the 256 ARGB32 colours (OGPU_RetainedPalette sets them); else NULL */
    /* the library's own fields follow */
};

/* OGPU_RetainedStats fills as much of this as size says (set size first). */
struct OGPURetainedStats {
    ULONG size;                     /* sizeof(struct OGPURetainedStats) */
    ULONG frames;                   /* presents done */
    ULONG tiles_uploaded;           /* tiles uploaded (GPU) or copied (software), in all */
    ULONG readbacks;                /* reads from the screen: always 0 */
    ULONG tiles;                    /* tiles in the surface */
    ULONG last_tiles;               /* the last present's tiles */
    ULONG tiles_hashed;             /* tiles hashed, in all */
    ULONG palette_uploads;          /* presents that uploaded the palette (it changed) */
    ULONG gpu_frames;               /* presents on the GPU path */
    ULONG cpu_frames;               /* presents on the software path */
};

/* OGPU_RetainedPresent's answer: tiles uploaded (0 or more), or one of these. */
#define OGPU_RS_ERR_ARGS        (-1)        /* no surface, no RastPort, or an empty rectangle */
#define OGPU_RS_ERR_UNSUPPORTED (-2)        /* no way to show this format in that RastPort (a 32-bit surface with no cybergraphics.library) */
#define OGPU_RS_ERR_MEMORY      (-3)        /* no memory for the texture or the batch */
#define OGPU_RS_ERR_GPU         (-4)        /* the GPU failed the batch; the next present uploads everything */

#endif
