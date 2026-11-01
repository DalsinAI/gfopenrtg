/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 * For tests/test_gla_present.c on the PC: just what gla_present_os3.c uses. */
#include <exec/types.h>
LONG OGPU_Submit(APTR stream, ULONG words, ULONG *fence);
LONG OGPU_Wait(ULONG fence);
ULONG OGPU_Query(ULONG op, ULONG format);
