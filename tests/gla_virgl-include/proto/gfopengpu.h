/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 * For tests/test_gla_virgl_os3.c on the PC: just what gla_virgl_os3.c uses,
 * with the library's own header as the real proto file brings it. */
#include <exec/types.h>
#include <gfopengpu/gfopengpu.h>
LONG OGPU_Submit(APTR stream, ULONG words, ULONG *fence);
LONG OGPU_Wait(ULONG fence);
ULONG OGPU_Query(ULONG op, ULONG format);
