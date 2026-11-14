/* SPDX-License-Identifier: MIT */
#ifndef HOSTLIB_MGL_CONSTS_H
#define HOSTLIB_MGL_CONSTS_H
/* send_if_changed: compare a context's last shader constants with the new
 * ones and, when they differ, append one SET_CONSTANT_BUFFER. The last
 * values and the command cursor are locked state. Copyright (c) 2026
 * AmiCradle Team. MIT. */
#include "../hl.h"

enum { HL_MC_STATE, HL_MC_CAP, HL_MC_SRC, HL_MC_N, HL_MC_SHADER, HL_MC_CMD, HL_MC_CMD_CAP,
       HL_MC_LONGS };
#define HL_MC_BLOCK (4u * HL_MC_LONGS)
/* Four cursor longs, then cap vec4s of last-sent words (guest byte order). */
#define HL_MC_HDR 16u

#endif
