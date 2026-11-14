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
/* the long or word at p + 4i or 2i: the 68k's own loads, the host's bytes
 * (its compiler makes one load and a swap of them) */
#ifdef HL_HOST
static inline uint32_t mk_g32(const uint8_t *p, uint32_t i)
{
    p += 4 * i;
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
static inline uint32_t mk_g16(const uint8_t *p, uint32_t i) { p += 2 * i; return (uint32_t)p[0] << 8 | p[1]; }
#else
static inline uint32_t mk_g32(const uint8_t *p, uint32_t i) { return ((const uint32_t *)(const void *)p)[i]; }
static inline uint32_t mk_g16(const uint8_t *p, uint32_t i) { return ((const uint16_t *)(const void *)p)[i]; }
#endif

HL_TAG(hostlib_mgl_cached, HL_ID_MGL_CACHED)
HL_FN(hostlib_mgl_cached)
{
    hl_addr keys, handles, key;
    uint32_t slots, keyw, n, probes, h, k0, i, j, s, at, hd;
    const uint8_t *kt, *ht, *kk;
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
    /* the three spans once, then plain loads: HL_RD32 alternating between
     * the table and the key looks each range up again on every long */
    HL_PTR_R(kt, keys, slots * keyw * 4u);
    HL_PTR_R(ht, handles, slots * 2u);
    HL_PTR_R(kk, key, n * 4u);
    k0 = mk_g32(kk, 0);
    h = k0 ^ (k0 >> 11) ^ (n > 1 ? mk_g32(kk, 1) * 7u : 0) ^ (n > 2 ? mk_g32(kk, 2) * 13u : 0);
    for (i = 0; i < probes; i++) {
        s = (h + i) & (slots - 1u);
        hd = mk_g16(ht, s);
        if (!hd) return HL_MK_FREE | s;
        at = s * keyw;
        for (j = 0; j < n; j++) {
            uint32_t a = mk_g32(kt, at + j), b = mk_g32(kk, j);
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
