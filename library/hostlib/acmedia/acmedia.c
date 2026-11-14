/* SPDX-License-Identifier: MIT */
/* ACMedia v1's 68k builds (acmedia.h has the blocks and the contract).
 * Copyright (c) 2026 AmiCradle Team. MIT.
 *
 * Only the 68k side is here. These are host services (HOSTLIB-ABI.md 8.7):
 * the host side is the runtime's broker, so hostlib.toml marks them
 * service = true and gen.py writes no wrapper. A decoder has no 68k twin,
 * so each body writes nothing and returns ACM_E_NOTAVAIL: the answer when
 * the runtime has no broker or its gates leave the call to the 68k, and the
 * cue for akikoplus.device's next rung (design 7.5). The bodies are real in
 * a thin shell's build too (HL_THIN), as the probe's is, so they use
 * ac_magic.h's tag directly. */
#include "acmedia.h"
#include "../hl_ids.h"

#ifndef HL_HOST
#define ACM_NOTAVAIL_BODY(name, id)                                            \
    AC_MAGIC(name, id)                                                         \
    AC_MAGIC_KEEP int32_t AC_MAGIC_BODY(name)(hl_addr block)                   \
    {                                                                          \
        (void)block;                                                           \
        return ACM_E_NOTAVAIL;                                                 \
    }

ACM_NOTAVAIL_BODY(hostlib_acm_caps, HL_ID_ACM_CAPS)
ACM_NOTAVAIL_BODY(hostlib_acm_probe, HL_ID_ACM_PROBE)
ACM_NOTAVAIL_BODY(hostlib_acm_open, HL_ID_ACM_OPEN)
ACM_NOTAVAIL_BODY(hostlib_acm_info, HL_ID_ACM_INFO)
ACM_NOTAVAIL_BODY(hostlib_acm_control, HL_ID_ACM_CONTROL)
ACM_NOTAVAIL_BODY(hostlib_acm_frame, HL_ID_ACM_FRAME)
ACM_NOTAVAIL_BODY(hostlib_acm_audio, HL_ID_ACM_AUDIO)
ACM_NOTAVAIL_BODY(hostlib_acm_feed, HL_ID_ACM_FEED)
ACM_NOTAVAIL_BODY(hostlib_acm_events, HL_ID_ACM_EVENTS)
ACM_NOTAVAIL_BODY(hostlib_acm_close, HL_ID_ACM_CLOSE)
ACM_NOTAVAIL_BODY(hostlib_acm_result, HL_ID_ACM_RESULT)
ACM_NOTAVAIL_BODY(hostlib_acm_fault, HL_ID_ACM_FAULT)
#endif
