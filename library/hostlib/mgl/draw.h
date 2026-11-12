/* SPDX-License-Identifier: MIT */
#ifndef HOSTLIB_MGL_DRAW_H
#define HOSTLIB_MGL_DRAW_H
/* One MiniGL virgl draw, appended to a context's arenas.
 * Copyright (c) 2026 AmiCradle Team. MIT.
 *
 * The context's cursors are state, so two draws on one context stay in
 * order and a decline does not append twice. The vertex array is only
 * read. HOSTLIB-ABI.md has the contract. */
#include "../hl.h"

enum { HL_MD_STATE, HL_MD_ARENA, HL_MD_ARENA_CAP, HL_MD_CMD, HL_MD_CMD_CAP,
       HL_MD_SRC, HL_MD_COUNT, HL_MD_COMPS, HL_MD_PRIM, HL_MD_START,
       HL_MD_LONGS };
#define HL_MD_BLOCK (4u * HL_MD_LONGS)

/* The state, four big-endian longs: the arena's byte cursor, the command
 * stream's word cursor, the draws so far, and a spare. */
#define HL_MD_STATE_BYTES 16u
#define HL_MD_DRAW_WORDS 13u

#endif
