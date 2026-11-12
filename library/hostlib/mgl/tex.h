/* SPDX-License-Identifier: MIT */
#ifndef HOSTLIB_MGL_TEX_H
#define HOSTLIB_MGL_TEX_H
/* One texture upload: an ACV_PUT in the context's block, the texels only
 * read. The block cursor is locked state. Copyright (c) 2026 AmiCradle
 * Team. MIT. */
#include "../hl.h"

enum { HL_MT_STATE, HL_MT_PRE, HL_MT_CAP, HL_MT_HANDLE, HL_MT_CTX, HL_MT_LEVEL,
       HL_MT_STRIDE, HL_MT_X, HL_MT_Y, HL_MT_W, HL_MT_H, HL_MT_SRC, HL_MT_BYTES,
       HL_MT_SWAP, HL_MT_LONGS };
#define HL_MT_BLOCK (4u * HL_MT_LONGS)
#define HL_MT_STATE_BYTES 16u
#define HL_MT_WORDS 16u

#endif
