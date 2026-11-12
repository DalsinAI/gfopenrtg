/* SPDX-License-Identifier: MIT */
/* hostlib_probe's 68k build (probe.h has the block and the contract).
 * Copyright (c) 2026 AmiCradle Team. MIT.
 *
 * Only the 68k side is here: the host side is the runtime's own (its
 * answers are about the host), so hostlib.toml marks it runtime = true and
 * gen.py writes no wrapper for it. Its body is real even in a thin shell's
 * build (HL_THIN): on a 68k without AmiCradle it is what tells the shell to
 * open the full build. So it uses ac_magic.h's tag directly, not HL_TAG. */
#include "probe.h"
#include "../hl_ids.h"

#ifndef HL_HOST
AC_MAGIC(hostlib_probe, HL_ID_HOSTLIB_PROBE)

AC_MAGIC_KEEP uint32_t AC_MAGIC_BODY(hostlib_probe)(hl_addr block)
{
    uint32_t n = HL_ARG(HL_PB_N), i;
    hl_addr answers = HL_ARG(HL_PB_ANSWERS), features = HL_ARG(HL_PB_FEATURES);
    for (i = 0; i < n; i++) HL_WR8(answers + i, 0);
    if (features)
        for (i = 0; i < HL_PF_BYTES; i++) HL_WR8(features + i, 0);
    return 0;
}
#endif
