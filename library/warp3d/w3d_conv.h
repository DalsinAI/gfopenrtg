/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 * Warp3D.library's float conversions (w3d_conv.c). */
#ifndef W3D_CONV_H
#define W3D_CONV_H
ULONG w3d_fbits(float f);
LONG w3d_f2l(float f);                          /* rounded to the nearest */
LONG w3d_fx(float f, int k);                    /* f * 2^k, rounded to the nearest */
ULONG w3d_unit8(float f);                       /* f * 255, rounded, 0..255 */
ULONG w3d_zf(float z);                          /* the stream's depth: 0..1 to 32 bits */
ULONG w3d_zd(const double *z);
#endif
