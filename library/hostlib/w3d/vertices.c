/* SPDX-License-Identifier: MIT */
#include "vertices.h"
#include "../hl_ids.h"

/* The conversions are gfopenrtg's library/warp3d/w3d_conv.h, on the IEEE
 * bits: integers only, so the 68k build (soft float in Warp3D) and the host
 * build give the same bits, and those are the float code's (rounded to the
 * nearest, ties to even, as FMOVE.L). */
static uint32_t wv_rne(uint32_t a, int sh)
{
    uint32_t q = a >> (sh - 1), r = q >> 1;
    uint32_t low = sh > 1 ? a & ((1u << (sh - 1)) - 1) : 0;
    if ((q & 1) && (low || (r & 1))) r++;
    return r;
}

/* f * 2^k as an integer; out of range saturates; NaN gives 0x7fffffff */
static uint32_t wv_fx(uint32_t u, int k)
{
    uint32_t m, r;
    int e = (int)((u >> 23) & 255);
    if (e == 255) return (u & 0x7fffff) || !(u >> 31) ? 0x7fffffffu : 0x80000000u;
    if (!e) return 0;
    m = (u & 0x7fffff) | 0x800000;
    e += k;
    if (e >= 158) r = 0x80000000u;
    else if (e >= 150) r = m << (e - 150);
    else r = 150 - e > 32 ? 0 : wv_rne(m, 150 - e);
    if (u >> 31) return r >= 0x80000000u ? 0x80000000u : 0u - r;
    return r >= 0x80000000u ? 0x7fffffffu : r;
}

/* f * 255 rounded, 0 .. 255 */
static uint32_t wv_unit8(uint32_t u)
{
    uint32_t p;
    int e = (int)((u >> 23) & 255);
    if (e == 255 && (u & 0x7fffff)) return 255;
    if (u >> 31) return 0;
    if (e >= 127) return 255;
    if (e < 118) return 0;
    p = ((u & 0x7fffff) | 0x800000) * 255u;
    p = p >= 0x80000000u ? wv_rne(p, 8) << 8 : wv_rne(p, 7) << 7;
    return wv_rne(p, 150 - e);
}

/* a double's depth (its two big-endian longs) as the stream holds it */
static uint32_t wv_zd(uint32_t hi, uint32_t lo)
{
    uint32_t m;
    int e = (int)((hi >> 20) & 2047);
    if (e == 2047) return ((hi & 0xfffff) | lo) || (hi >> 31) ? 0 : 0xffffffffu;
    if ((hi >> 31) || !e) return 0;
    if (e >= 1023) return 0xffffffffu;
    if (1024 - e > 32) return 0;
    m = 0x80000000u | (hi & 0xfffff) << 11 | lo >> 21;
    return wv_rne(m - (m >> 30), 1024 - e) << 2;
}

static uint32_t wv_uv(uint32_t u, uint32_t k)
{
    return k == HL_WV_UV_NONE ? 0 : wv_fx(u, (int)(int32_t)k);
}

static int wv_k_ok(uint32_t k)
{
    return k == HL_WV_UV_NONE || ((int32_t)k >= -16 && (int32_t)k <= 32);
}

static int wv_apart(uint32_t a, uint32_t an, uint32_t b, uint32_t bn)
{
    return (uint64_t)a + an <= b || (uint64_t)b + bn <= a;
}

static void wv_be(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

#if defined(HL_HOST) && defined(HOSTLIB_TEST_STOP)
extern uint32_t hostlib_w3d_vertices_stop;
#endif

/* COUNT vertices into OUT. Vertex j is index FIRST for j = 0, AT + j
 * otherwise, in the array at SRC (LIST 0) or through the pointer at
 * SRC + 4 * index (LIST 1). Each output vertex, big-endian:
 *   x, y     the window position * 65536, rounded, saturated
 *   argb     the vertex's colour with HL_WV_GOURAUD, else CURRENT
 *   u, v     u * 2^KU, v * 2^KV, or 0 for HL_WV_UV_NONE
 *   z        the double depth as 0 .. 0xffffffff (0 for z <= 0, all ones for z >= 1)
 *   w        w's bits, 1e-6 (0x358637BD) when w is not positive or is NaN
 *   spec     255 << 24 (no fog), plus the specular colour with HL_WV_SPECULAR
 * The output is the one write range; the vertices (and the pointer list)
 * are read. */
HL_TAG(hostlib_w3d_vertices, HL_ID_W3D_VERTICES)
HL_FN(hostlib_w3d_vertices)
{
    hl_addr src, out, lo_at, hi_at;
    uint32_t list, first, at, count, flags, current, ku, kv;
    uint32_t idx_lo, idx_hi, j, span;
    uint8_t *dst;
    const uint8_t *vp, *pp;
    HL_NEED_R(block, HL_WV_BLOCK);
    src = HL_ARG(HL_WV_SRC);
    list = HL_ARG(HL_WV_LIST);
    first = HL_ARG(HL_WV_FIRST);
    at = HL_ARG(HL_WV_AT);
    count = HL_ARG(HL_WV_COUNT);
    out = HL_ARG(HL_WV_OUT);
    flags = HL_ARG(HL_WV_FLAGS);
    current = HL_ARG(HL_WV_CURRENT);
    ku = HL_ARG(HL_WV_KU);
    kv = HL_ARG(HL_WV_KV);
    if (!count || count > HL_WV_MAX || list > 1 || (flags & ~HL_WV_FLAGS_ALL)) return 0;
    if (!wv_k_ok(ku) || !wv_k_ok(kv)) return 0;
    if ((src | out) & 1) return 0;
    if (first > 0xffffu || at > 0xffffu) return 0;
    idx_lo = first; idx_hi = first;
    if (count > 1) {
        if (at + 1 < idx_lo) idx_lo = at + 1;
        if (at + count - 1 > idx_hi) idx_hi = at + count - 1;
    }
    if ((uint64_t)out + count * HL_WV_OVTX > 0xffffffffull) return 0;
    if (!list) {
        lo_at = src + idx_lo * HL_WV_VERTEX;
        span = (idx_hi - idx_lo + 1) * HL_WV_VERTEX;
        if ((uint64_t)src + (uint64_t)(idx_hi + 1) * HL_WV_VERTEX > 0xffffffffull) return 0;
        if (!wv_apart(lo_at, span, out, count * HL_WV_OVTX)) return 0;
        HL_NEED_R(lo_at, span);
    } else {
        hl_addr pl = src + idx_lo * 4u;
        uint32_t pn = (idx_hi - idx_lo + 1) * 4u, i;
        if ((uint64_t)src + (uint64_t)(idx_hi + 1) * 4u > 0xffffffffull) return 0;
        if (!wv_apart(pl, pn, out, count * HL_WV_OVTX)) return 0;
        HL_NEED_R(pl, pn);
        lo_at = 0xffffffffu; hi_at = 0;
        for (i = idx_lo; i <= idx_hi; i++) {
            hl_addr a = HL_RD32(src + 4u * i);
            if (a & 1) return 0;
            if (a < lo_at) lo_at = a;
            if (a > hi_at) hi_at = a;
        }
        if (hi_at - lo_at > HL_WV_SPREAD || (uint64_t)hi_at + HL_WV_VERTEX > 0xffffffffull) return 0;
        span = hi_at - lo_at + HL_WV_VERTEX;
        if (!wv_apart(lo_at, span, out, count * HL_WV_OVTX)) return 0;
        HL_NEED_R(lo_at, span);
    }
    HL_NEED_W(out, count * HL_WV_OVTX);
    HL_DECLARED();
    pp = 0;
    if (list) HL_PTR_R(pp, src + idx_lo * 4u, (idx_hi - idx_lo + 1) * 4u);
    HL_PTR_R(vp, lo_at, span);
    HL_PTR_W(dst, out, count * HL_WV_OVTX);
    for (j = 0; j < count; j++) {
        uint32_t i = j ? at + j : first;
        const uint8_t *v;
        uint8_t *o = dst + j * HL_WV_OVTX;
        uint32_t w, argb, spec;
#define WV32(p, off) ((uint32_t)(p)[off] << 24 | (uint32_t)(p)[(off) + 1] << 16 | (uint32_t)(p)[(off) + 2] << 8 | (p)[(off) + 3])
        if (list) v = vp + (WV32(pp, (i - idx_lo) * 4u) - lo_at);
        else v = vp + (i - idx_lo) * HL_WV_VERTEX;
        argb = !(flags & HL_WV_GOURAUD) ? current
             : wv_unit8(WV32(v, 44)) << 24 | wv_unit8(WV32(v, 32)) << 16 | wv_unit8(WV32(v, 36)) << 8 | wv_unit8(WV32(v, 40));
        w = WV32(v, 16);
        if ((int32_t)w <= 0 || w > 0x7f800000u) w = 0x358637BDu;
        spec = 255u << 24;
        if (flags & HL_WV_SPECULAR)
            spec |= wv_unit8(WV32(v, 48)) << 16 | wv_unit8(WV32(v, 52)) << 8 | wv_unit8(WV32(v, 56));
        wv_be(o, wv_fx(WV32(v, 0), 16));
        wv_be(o + 4, wv_fx(WV32(v, 4), 16));
        wv_be(o + 8, argb);
        wv_be(o + 12, wv_uv(WV32(v, 20), ku));
        wv_be(o + 16, wv_uv(WV32(v, 24), kv));
        wv_be(o + 20, wv_zd(WV32(v, 8), WV32(v, 12)));
        wv_be(o + 24, w);
        wv_be(o + 28, spec);
#undef WV32
#if defined(HL_HOST) && defined(HOSTLIB_TEST_STOP)
        if (hostlib_w3d_vertices_stop && j + 1 == hostlib_w3d_vertices_stop) HL_DECLINE();
#endif
    }
    return count;
}
