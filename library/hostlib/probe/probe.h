/* SPDX-License-Identifier: MIT */
#ifndef HOSTLIB_PROBE_H
#define HOSTLIB_PROBE_H
/* hostlib_probe (HOSTLIB-ABI.md 8.4: HOSTLIB_PROBE, $00011000, version 1):
 * what this runtime runs as host code, asked by a thin shell's init.
 * Copyright (c) 2026 AmiCradle Team. MIT.
 *
 * The block, big-endian longs (HL_PB_* are their numbers):
 *   0 n         entries asked about, 0 to HL_PROBE_MAX
 *   1 list      n entries of two longs: an id, its contract version
 *   2 answers   n bytes, written: for entry k, HL_PROBE_HOST when this
 *               runtime runs that id at that version as host code now (it
 *               is known and not turned off), plus HL_PROBE_TOTAL when it
 *               runs it as a total function; 0 when not
 *   3 features  0, or HL_PF_BYTES, written: the host's feature level, as
 *               the runtime picked it at start for its host builds:
 *                 long 0 arch    HL_ARCH_*
 *                 long 1 level   HL_LEVEL_* for that arch
 *                 long 2 cores   host cores offered for parallel host work
 *                                (Settings > Cores); 0 at one core
 *                 long 3 0, reserved
 * Returns the hostlib ABI the runtime implements (2), or 0: the 68k body,
 * which writes zeros to answers and features. A thin shell opens as itself
 * only with every answer HL_PROBE_HOST | HL_PROBE_TOTAL; else it opens the
 * full 68k build from disk (8.4).
 *
 * The runtime answers 0 for every id under AC_HOSTLIB=0 and
 * AC_HOSTLIB=verify (a thin shell has no 68k bodies to compare against),
 * and for an id in AC_HOSTLIB_OFF. A runtime that does not know the probe
 * runs its 68k body: 0, all zeros. */
#include "../hl.h"

enum { HL_PB_N, HL_PB_LIST, HL_PB_ANSWERS, HL_PB_FEATURES, HL_PB_LONGS };
#define HL_PB_BLOCK (4u * HL_PB_LONGS)
#define HL_PROBE_MAX 1024u              /* more: the host declines, and the 68k body answers zeros */
#define HL_PROBE_HOST 1u
#define HL_PROBE_TOTAL 2u
#define HL_PF_BYTES 16u
enum { HL_ARCH_NONE, HL_ARCH_X86_64, HL_ARCH_ARM64, HL_ARCH_PPC64 };
/* levels: x86-64 baseline (SSE2) and AVX2 (x86-64-v3); ARMv8.0-A and ARMv8.2-A */
enum { HL_LEVEL_X86_64_BASE = 0, HL_LEVEL_X86_64_AVX2 = 1 };
enum { HL_LEVEL_ARMV8_0 = 0, HL_LEVEL_ARMV8_2 = 1 };

#ifndef HL_HOST
uint32_t hostlib_probe(hl_addr block);
#endif

#endif
