/* SPDX-License-Identifier: MIT */
#include "cached.h"
#include "../hl_ids.h"

/* minigl's cached() (library/minigl/mgl_vg_state.c in gfopenrtg): the key's
 * hash picks a slot, then up to probes slots from there in turn. An empty
 * slot ends the search (the key is not there; a new object goes in it). A
 * slot whose key matches the n longs, compared as bits, gives its handle.
 * When every probed slot holds another key, the new object replaces the one
 * in the hash's own slot. Arithmetic is 32-bit, as the 68k's. Nothing is
 * written, so there is no state and a decline needs nothing put back. */
HL_TAG(hostlib_mgl_cached, HL_ID_MGL_CACHED)
HL_FN(hostlib_mgl_cached)
{
    hl_addr keys, handles, key;
    uint32_t slots, keyw, n, probes, h, k0, i, j, s, at, hd;
    HL_NEED_R(block, HL_MK_BLOCK);
    keys = HL_ARG(HL_MK_KEYS);
    handles = HL_ARG(HL_MK_HANDLES);
    slots = HL_ARG(HL_MK_SLOTS);
    keyw = HL_ARG(HL_MK_KEYW);
    key = HL_ARG(HL_MK_KEY);
    n = HL_ARG(HL_MK_N);
    probes = HL_ARG(HL_MK_PROBES);
    if (slots == 0 || slots > 1024 || (slots & (slots - 1)) != 0) return 0;
    if (keyw == 0 || keyw > 16 || n == 0 || n > keyw || probes == 0 || probes > slots) return 0;
    if ((uint64_t)keys + (uint64_t)slots * keyw * 4u > 0xffffffffull) return 0;
    if ((uint64_t)handles + (uint64_t)slots * 2u > 0xffffffffull) return 0;
    if ((uint64_t)key + (uint64_t)n * 4u > 0xffffffffull) return 0;
    HL_NEED_R(keys, slots * keyw * 4u);
    HL_NEED_R(handles, slots * 2u);
    HL_NEED_R(key, n * 4u);
    HL_DECLARED();
    k0 = HL_RD32(key);
    h = k0 ^ (k0 >> 11) ^ (n > 1 ? HL_RD32(key + 4u) * 7u : 0) ^ (n > 2 ? HL_RD32(key + 8u) * 13u : 0);
    for (i = 0; i < probes; i++) {
        s = (h + i) & (slots - 1u);
        hd = HL_RD16(handles + s * 2u);
        if (!hd) return HL_MK_FREE | s;
        at = keys + s * keyw * 4u;
        for (j = 0; j < n; j++) {
            uint32_t a = HL_RD32(at + j * 4u), b = HL_RD32(key + j * 4u);
#if !defined(HL_HOST) && defined(__mc68000__)
            /* a in a data register: two memory longs compared make GCC emit
             * CMPM.L (gfopenrtg #246), which older AC090 JITs interpret */
            __asm__("" : "+d"(a));
#endif
            if (a != b) break;
        }
        if (j == n) return HL_MK_FOUND | hd;
    }
    return HL_MK_FULL | (h & (slots - 1u));
}
