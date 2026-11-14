/* SPDX-License-Identifier: MIT */
#ifndef HOSTLIB_OGFX_LAYOUT_BATCH_H
#define HOSTLIB_OGFX_LAYOUT_BATCH_H
/* Private AC090 transport behind GFOpenLayout's olc_accel table.  One call
 * submits the coarse CPU-shadow operations collected between begin and
 * submit: fill, overlap-safe copy/scroll, ARGB composite and a whole glyph
 * run's 1-bit mask.  It is not a second public GFOpenLayout backend API.
 *
 * The block and records are big-endian 32-bit words.  Pixel memory is in
 * the surface's native byte order.  A colour is therefore already encoded
 * as the target pixel (the most significant of bpp bytes comes first).
 * Clip boxes are non-overlapping and use inclusive corners.  Zero clips
 * means the whole target.  The host accepts at most OLB_MAX_SOURCES distinct
 * source spans; a larger batch runs through the 68k body instead.
 * Copyright (c) 2026 AmiCradle Team. MIT. */
#include "../hl.h"

enum {
    HL_OLB_DST, HL_OLB_DST_BPR, HL_OLB_DST_W, HL_OLB_DST_H, HL_OLB_DST_FMT,
    HL_OLB_CLIPS, HL_OLB_NCLIPS, HL_OLB_CMDS, HL_OLB_NCMDS, HL_OLB_LONGS
};
#define HL_OLB_BLOCK (4u * HL_OLB_LONGS)

enum {
    HL_OLC_OP, HL_OLC_SRC, HL_OLC_SRC_BPR, HL_OLC_SRC_W, HL_OLC_SRC_H, HL_OLC_SRC_FMT,
    HL_OLC_X0, HL_OLC_Y0, HL_OLC_X1, HL_OLC_Y1, HL_OLC_DX, HL_OLC_DY, HL_OLC_ARG,
    HL_OLC_AUX0, HL_OLC_AUX1, HL_OLC_AUX2, HL_OLC_LONGS
};
#define HL_OLC_BYTES (4u * HL_OLC_LONGS)

enum { HL_OLC_FILL = 1, HL_OLC_COPY, HL_OLC_COMPOSITE, HL_OLC_MASK1 };
enum { HL_OLF_CLUT8 = 1, HL_OLF_RGB16, HL_OLF_XRGB32, HL_OLF_ARGB32 };
#define HL_OLB_MAX_CLIPS 64u
#define HL_OLB_MAX_COMMANDS 4096u
#define HL_OLB_MAX_SOURCES 8u

/* FILL: X0..Y1 is the destination box, ARG the encoded pixel.
 * COPY: SRC...FMT and X0..Y1 name the source; DX,DY the destination.
 * COMPOSITE: an ARGB32 source onto XRGB32; ARG is opacity, 0..255.
 * MASK1: SRC is a 1-bit bitmap (bit 7 leftmost), X0..Y1 names its source,
 * DX,DY the destination and ARG the encoded destination pixel. */
#ifndef HL_HOST
uint32_t hostlib_ogfx_layout_batch(hl_addr block);
#else
uint32_t hostlib_ogfx_layout_batch_host(struct hl_ctx *, hl_addr block);
#endif

#endif
