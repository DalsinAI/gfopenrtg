/* SPDX-License-Identifier: MIT */
#include "arrays.h"
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
    if (idx_hi > 0xffffu) return 0;             /* every index used, not only FIRST and AT, is 65535 or under */
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

/* ---- DrawArray / DrawElements (arrays.h) ---- */

/* a float's depth as the stream holds it: the float as a double, exactly */
static uint32_t wv_zf(uint32_t u)
{
    uint32_t e = (u >> 23) & 255, m = u & 0x7fffff, s = u & 0x80000000u;
    if (!e) return 0;
    return wv_zd(s | (e == 255 ? 2047u : e + 896u) << 20 | m >> 3, m << 29);
}

static uint32_t wa_elem(uint32_t prim, uint32_t t, uint32_t k)
{
    if (prim == HL_WA_LIST) return 3u * t + k;
    if (prim == HL_WA_FAN) return k ? t + k : 0;
    if (t & 1) return k == 0 ? t + 1 : k == 1 ? t : t + 2;
    return t + k;
}

static uint32_t wa_rd(const uint8_t *p, uint32_t n)
{
    if (n == 1) return p[0];
    if (n == 2) return (uint32_t)p[0] << 8 | p[1];
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static int wa_cmode(uint32_t m) { return (m & ~(HL_WA_C_UBYTE | 7u)) == 0 && (m & 7u) <= HL_WA_C_BGR; }
static uint32_t wa_csize(uint32_t m) { return ((m & 7u) >= HL_WA_C_RGB ? 3u : 4u) * (m & HL_WA_C_UBYTE ? 1u : 4u); }

/* a colour as ARGB32: colour_bytes() for bytes, w3d_colour(colour_at()) for floats */
static uint32_t wa_colour(const uint8_t *p, uint32_t m)
{
    uint32_t c[4], i, n = (m & 7u) >= HL_WA_C_RGB ? 3u : 4u;
    for (i = 0; i < n; i++) c[i] = m & HL_WA_C_UBYTE ? p[i] : wv_unit8(wa_rd(p + 4 * i, 4));
    switch (m & 7u) {
    case HL_WA_C_ARGB: return c[0] << 24 | c[1] << 16 | c[2] << 8 | c[3];
    case HL_WA_C_BGRA: return c[3] << 24 | c[2] << 16 | c[1] << 8 | c[0];
    case HL_WA_C_RGB:  return 255u << 24 | c[0] << 16 | c[1] << 8 | c[2];
    case HL_WA_C_BGR:  return 255u << 24 | c[2] << 16 | c[1] << 8 | c[0];
    default:           return c[3] << 24 | c[0] << 16 | c[1] << 8 | c[2];
    }
}

/* One array: the bytes vertices VMIN .. VMAX use, from offset LO to HI of
 * each; 0 when they do not fit. */
struct wa_arr { hl_addr at; uint32_t n; const uint8_t *p; uint32_t stride; int32_t lo; };
static int wa_span(struct wa_arr *a, hl_addr ptr, uint32_t stride, int32_t lo, int32_t hi,
                   uint32_t vmin, uint32_t vmax, hl_addr out, uint32_t outn)
{
    int64_t from;
    uint64_t n;
    if (stride > HL_WA_STRIDE || (uint64_t)vmax * stride > 0x7fffffffu) return 0;
    from = (int64_t)ptr + (int64_t)vmin * stride + lo;
    n = (uint64_t)(vmax - vmin) * stride + (uint64_t)(hi - lo);
    if (from < 0 || (uint64_t)from + n > 0xffffffffull || n > HL_WV_SPREAD) return 0;
    if (!wv_apart((hl_addr)from, (uint32_t)n, out, outn)) return 0;
    a->at = (hl_addr)from; a->n = (uint32_t)n; a->stride = stride; a->lo = lo; a->p = 0;
    return 1;
}
/* vertex VI's bytes at offset OFF (VI - VMIN from the span's start) */
#define WA_AT(a, rel, off) ((a).p + (uint32_t)((int64_t)(rel) * (a).stride + (off) - (a).lo))

#if defined(HL_HOST) && defined(HOSTLIB_TEST_STOP)
extern uint32_t hostlib_w3d_arrays_stop;
#endif

HL_TAG(hostlib_w3d_arrays, HL_ID_W3D_ARRAYS)
HL_FN(hostlib_w3d_arrays)
{
    uint32_t b[HL_WA_LONGS], i, t, k, elo, ehi, vmin, vmax, outn, nv;
    int32_t voff = 0, woff = 0, tlo = 0, thi = 4;
    struct wa_arr vp, tp, cp, sp;
    hl_addr ix = 0;
    const uint8_t *ixp = 0;
    uint8_t *dst;
    HL_NEED_R(block, HL_WA_BLOCK);
    for (i = 0; i < HL_WA_LONGS; i++) b[i] = HL_ARG(i);
    if (!b[HL_WA_NTRI] || b[HL_WA_NTRI] > HL_WA_MAXTRI || b[HL_WA_PRIM] > HL_WA_STRIP) return 0;
    if (b[HL_WA_VPMODE] > HL_WA_VP_FFD || (b[HL_WA_FLAGS] & ~HL_WV_FLAGS_ALL)) return 0;
    if (!wv_k_ok(b[HL_WA_KU]) || !wv_k_ok(b[HL_WA_KV]) || (b[HL_WA_OUT] & 1) || !b[HL_WA_VP]) return 0;
    if (b[HL_WA_CP] && !wa_cmode(b[HL_WA_CPMODE])) return 0;
    if (b[HL_WA_SCP] && !wa_cmode(b[HL_WA_SCPMODE])) return 0;
    if (b[HL_WA_IX] ? b[HL_WA_IXSIZE] != 1 && b[HL_WA_IXSIZE] != 2 && b[HL_WA_IXSIZE] != 4 : b[HL_WA_IXSIZE] != 0) return 0;
    if (b[HL_WA_TP]) {
        voff = (int32_t)b[HL_WA_TPVOFF]; woff = (int32_t)b[HL_WA_TPWOFF];
        if (voff < -HL_WA_OFFS || voff > HL_WA_OFFS || woff < -HL_WA_OFFS || woff > HL_WA_OFFS) return 0;
        if (voff < tlo) tlo = voff;
        if (woff < tlo) tlo = woff;
        if (voff + 4 > thi) thi = voff + 4;
        if (woff + 4 > thi) thi = woff + 4;
    }
    if ((uint64_t)b[HL_WA_FIRST] + b[HL_WA_NTRI] + 2 > HL_WA_ELEMENTS / 3) return 0;
    if (b[HL_WA_PRIM] == HL_WA_LIST) { elo = 3 * b[HL_WA_FIRST]; ehi = 3 * (b[HL_WA_FIRST] + b[HL_WA_NTRI]) - 1; }
    else { elo = b[HL_WA_PRIM] == HL_WA_FAN ? 0 : b[HL_WA_FIRST]; ehi = b[HL_WA_FIRST] + b[HL_WA_NTRI] + 1; }
    nv = 3 * b[HL_WA_NTRI];
    outn = nv * HL_WV_OVTX;
    if ((uint64_t)b[HL_WA_OUT] + outn > 0xffffffffull) return 0;
    if (!wv_apart(block, HL_WA_BLOCK, b[HL_WA_OUT], outn)) return 0;
    if (b[HL_WA_IX]) {
        uint32_t sz = b[HL_WA_IXSIZE], n = (ehi - elo + 1) * sz;
        ix = b[HL_WA_IX] + elo * sz;
        if ((uint64_t)b[HL_WA_IX] + (uint64_t)(ehi + 1) * sz > 0xffffffffull || n > HL_WV_SPREAD) return 0;
        if (!wv_apart(ix, n, b[HL_WA_OUT], outn)) return 0;
        HL_NEED_R(ix, n);
        vmin = 0xffffffffu; vmax = 0;
        for (t = 0; t < b[HL_WA_NTRI]; t++)
            for (k = 0; k < 3; k++) {
                uint32_t e = wa_elem(b[HL_WA_PRIM], b[HL_WA_FIRST] + t, k), a = ix + (e - elo) * sz;
                uint32_t v = sz == 1 ? HL_RD8(a) : sz == 2 ? HL_RD16(a) : HL_RD32(a);
                v += b[HL_WA_BASE];
                if (v < vmin) vmin = v;
                if (v > vmax) vmax = v;
            }
    } else {
        if ((uint64_t)b[HL_WA_BASE] + ehi > 0xffffffffull) return 0;
        vmin = b[HL_WA_BASE] + elo; vmax = b[HL_WA_BASE] + ehi;
    }
    if (!wa_span(&vp, b[HL_WA_VP], b[HL_WA_VPSTRIDE], 0, b[HL_WA_VPMODE] ? 16 : 12, vmin, vmax, b[HL_WA_OUT], outn)) return 0;
    HL_NEED_R(vp.at, vp.n);
    if (b[HL_WA_TP]) {
        if (!wa_span(&tp, b[HL_WA_TP], b[HL_WA_TPSTRIDE], tlo, thi, vmin, vmax, b[HL_WA_OUT], outn)) return 0;
        HL_NEED_R(tp.at, tp.n);
    }
    if (b[HL_WA_CP]) {
        if (!wa_span(&cp, b[HL_WA_CP], b[HL_WA_CPSTRIDE], 0, (int32_t)wa_csize(b[HL_WA_CPMODE]), vmin, vmax, b[HL_WA_OUT], outn)) return 0;
        HL_NEED_R(cp.at, cp.n);
    }
    if (b[HL_WA_SCP]) {
        if (!wa_span(&sp, b[HL_WA_SCP], b[HL_WA_SCPSTRIDE], 0, (int32_t)wa_csize(b[HL_WA_SCPMODE]), vmin, vmax, b[HL_WA_OUT], outn)) return 0;
        HL_NEED_R(sp.at, sp.n);
    }
    HL_NEED_W(b[HL_WA_OUT], outn);
    HL_DECLARED();
    if (ix) HL_PTR_R(ixp, ix, (ehi - elo + 1) * b[HL_WA_IXSIZE]);
    HL_PTR_R(vp.p, vp.at, vp.n);
    if (b[HL_WA_TP]) HL_PTR_R(tp.p, tp.at, tp.n);
    if (b[HL_WA_CP]) HL_PTR_R(cp.p, cp.at, cp.n);
    if (b[HL_WA_SCP]) HL_PTR_R(sp.p, sp.at, sp.n);
    HL_PTR_W(dst, b[HL_WA_OUT], outn);
    for (i = 0; i < nv; i++) {
        uint32_t e = wa_elem(b[HL_WA_PRIM], b[HL_WA_FIRST] + i / 3, i % 3), v, rel, w, argb, spec, z;
        const uint8_t *p;
        uint8_t *o = dst + i * HL_WV_OVTX;
        v = (ixp ? wa_rd(ixp + (e - elo) * b[HL_WA_IXSIZE], b[HL_WA_IXSIZE]) : e) + b[HL_WA_BASE];
        rel = v - vmin;
        p = WA_AT(vp, rel, 0);
        z = b[HL_WA_VPMODE] ? wv_zd(wa_rd(p + 8, 4), wa_rd(p + 12, 4)) : wv_zf(wa_rd(p + 8, 4));
        argb = !(b[HL_WA_FLAGS] & HL_WV_GOURAUD) || !b[HL_WA_CP] ? b[HL_WA_CURRENT]
             : wa_colour(WA_AT(cp, rel, 0), b[HL_WA_CPMODE]);
        spec = 255u << 24;
        if ((b[HL_WA_FLAGS] & HL_WV_SPECULAR) && b[HL_WA_SCP])
            spec |= wa_colour(WA_AT(sp, rel, 0), b[HL_WA_SCPMODE]) & 0xffffffu;
        wv_be(o, wv_fx(wa_rd(p, 4), 16));
        wv_be(o + 4, wv_fx(wa_rd(p + 4, 4), 16));
        wv_be(o + 8, argb);
        if (b[HL_WA_TP]) {
            const uint8_t *q = WA_AT(tp, rel, 0);
            wv_be(o + 12, wv_uv(wa_rd(q, 4), b[HL_WA_KU]));
            wv_be(o + 16, wv_uv(wa_rd(q + voff, 4), b[HL_WA_KV]));
            w = wa_rd(q + woff, 4);
        } else {
            wv_be(o + 12, 0);
            wv_be(o + 16, 0);
            w = 0x3f800000u;
        }
        if ((int32_t)w <= 0 || w > 0x7f800000u) w = 0x358637BDu;
        wv_be(o + 20, z);
        wv_be(o + 24, w);
        wv_be(o + 28, spec);
#if defined(HL_HOST) && defined(HOSTLIB_TEST_STOP)
        if (hostlib_w3d_arrays_stop && i + 1 == hostlib_w3d_arrays_stop) HL_DECLINE();
#endif
    }
    return nv;
}
#undef WA_AT
