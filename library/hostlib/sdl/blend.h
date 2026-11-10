/* SPDX-License-Identifier: MIT */
#ifndef HOSTLIB_SDL_BLEND_H
#define HOSTLIB_SDL_BLEND_H
/* SDL 2's software blit for ARGB8888 with SDL_BLENDMODE_BLEND.
 * Copyright (c) 2026 AmiCradle Team. MIT.
 *
 * One pixel is four bytes, A, R, G, B, in that order: SDL's ARGB8888 as the
 * 68k stores a 32-bit pixel. The blend is SDL 2.32's ALPHA_BLEND_RGBA. A
 * source alpha of 0 leaves the destination pixel as it was, so the
 * destination is state (HOSTLIB-ABI.md 8.1): a decline puts it back, and
 * the 68k body then blends it once. */
#include "../hl.h"

enum { HL_SBL_SRC, HL_SBL_DST, HL_SBL_SSTRIDE, HL_SBL_DSTRIDE,
       HL_SBL_WIDTH, HL_SBL_HEIGHT, HL_SBL_LONGS };
#define HL_SBL_BLOCK (4u * HL_SBL_LONGS)

#endif
