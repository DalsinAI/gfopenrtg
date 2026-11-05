/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * RFX Progressive inverse DWT for OGPU_OP_DWT_INV and OGPU_OP_RFX_TILE. */
#ifndef OGPU_DWT_H
#define OGPU_DWT_H

/* Exactly 32 and 16 bits on every target the core builds for (the 68k,
 * the Cradle's 64-bit host, the PiStorm's ARM): long is 64 bits on the
 * host, so the RFX kernels never use it for a coefficient. */
typedef int            ogpu_rfx_s32;
typedef unsigned int   ogpu_rfx_u32;
typedef short          ogpu_rfx_s16;

/* buf: 4096 dequantized coefficients in RFX band order, in this CPU's own
 * order; overwritten with the 64x64 spatial component. tmp: at least
 * OGPU_DWT_RFX_WORK_INTS ints. re: non-zero for Reduce-Extrapolate. */
void ogpu_rfx_idwt(ogpu_rfx_s32 *buf, int re, ogpu_rfx_s32 *tmp);

/* Offset (in coefficients), width and height of band 0..9 (HL1..LL3). */
void ogpu_rfx_band(int re, int band, int *off, int *w, int *h);

/* v1.10. Buffers marked BE are in the Amiga's order (big-endian) on every
 * host; the kernels swap on a little-endian one. */
/* this CPU's order <-> big-endian, in place (nothing on the 68k) */
void ogpu_rfx_order32(ogpu_rfx_s32 *p, int n);
/* RLGR1 of len bytes into n BE int16s at out; flags as OGPU_RLGR_* (RE,
 * LL3_DELTA). 0, or -1 when the bits ran out first. */
int ogpu_rfxk_rlgr1(const unsigned char *p, unsigned long len, unsigned char *out, int n, int flags);
/* coef: 3 x 4096 BE int16; quant: 3 x 10 factors (band order); out: 3 x 4096
 * BE int32 (Y, Cb, Cr), 4-byte aligned; work: OGPU_DWT_RFX_WORK_INTS ints.
 * 0, or -1 for a factor over 15 (nothing written). */
int ogpu_rfxk_tile(const unsigned char *coef, const unsigned char *quant, int re,
                  unsigned char *out, ogpu_rfx_s32 *work);
/* w x h at (x, y) of RFX_TILE's out planes to BE ARGB32 words at dst. */
void ogpu_rfxk_ycbcr(const unsigned char *ycc, int x, int y, int w, int h,
                    unsigned char *dst, long bpr);

#endif
