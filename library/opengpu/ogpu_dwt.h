/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * RFX Progressive inverse DWT for OGPU_OP_DWT_INV. */
#ifndef OGPU_DWT_H
#define OGPU_DWT_H

/* buf: 4096 dequantized coefficients in RFX band order; overwritten with the
 * 64x64 spatial component. tmp: at least OGPU_DWT_RFX_WORK_INTS longs.
 * re: non-zero for Reduce-Extrapolate. */
void ogpu_rfx_idwt(long *buf, int re, long *tmp);

#endif
