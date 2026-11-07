/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 * For tests/test_gla_virgl_os3.c on the PC: just what gla_virgl_os3.c uses. */
#include <exec/types.h>
#define MEMF_ANY   0UL
#define MEMF_CLEAR (1UL << 16)
APTR AllocVec(ULONG bytes, ULONG flags);
void FreeVec(APTR p);
