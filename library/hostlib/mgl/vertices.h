/* SPDX-License-Identifier: MIT */
#ifndef HOSTLIB_MGL_VERTICES_H
#define HOSTLIB_MGL_VERTICES_H
/* Vertices into a MiniGL context's arena: an array draw's arrays, or a
 * batch of immediate vertices. The arena cursor is locked state.
 * Copyright (c) 2026 AmiCradle Team. MIT. */
#include "../hl.h"

enum { HL_MV_STATE, HL_MV_ARENA, HL_MV_CAP, HL_MV_COUNT, HL_MV_STRIDE,
       HL_MV_POS, HL_MV_POS_STRIDE, HL_MV_COMPS,
       HL_MV_COLOR, HL_MV_COLOR_STRIDE, HL_MV_COLOR_OFF,
       HL_MV_TC, HL_MV_TC_STRIDE, HL_MV_TC_COMPS, HL_MV_TC_OFF,
       HL_MV_LONGS };
#define HL_MV_BLOCK (4u * HL_MV_LONGS)
#define HL_MV_STATE_BYTES 16u

#endif
