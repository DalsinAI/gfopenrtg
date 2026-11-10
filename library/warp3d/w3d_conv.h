/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * Warp3D.library: its float to integer conversions. They are in this header,
 * inlined where they are used, because the per-vertex path calls them several
 * times a vertex: BlitzQuake 1.50 spent more on the calls into w3d_fx,
 * w3d_unit8, w3d_fbits and w3d_colour than in convert itself (3.0 + 1.3 +
 * 1.1% of the emulation thread against convert's 3.9%, 10 November). The PC
 * test (tests/test_w3d_conv.c) checks them against the float code. */
#ifndef W3D_CONV_H
#define W3D_CONV_H
#define W3D_CONV_FN static __inline__ __attribute__((always_inline, unused))

/* Float conversions in integers, from the IEEE bits: Warp3D is soft float,
 * and every float compare, multiply or conversion is a mathieee library call
 * (BlitzQuake 1.50 on the virgl route made ten million IEEESPCmp calls a
 * second through the old float code, 10 November 2026). The results are the
 * float code's to the bit: rounded to the nearest, ties to even, as the FPU's
 * FMOVE.L gives in its own mode, so pictures match the hard-float build. */

W3D_CONV_FN ULONG w3d_fbits(float f)
{
    union { float f; ULONG u; } x;
    x.f = f;
    return x.u;
}

/* a / 2^sh rounded to the nearest, ties to even; sh 1..32 */
W3D_CONV_FN ULONG rne(ULONG a, int sh)
{
    ULONG q = a >> (sh - 1), r = q >> 1;
    ULONG low = sh > 1 ? a & ((1UL << (sh - 1)) - 1) : 0;
    if ((q & 1) && (low || (r & 1))) r++;
    return r;
}

/* f * 2^k as an integer. Out of range saturates, as FMOVE.L does; NaN gives
 * 0x7fffffff. */
W3D_CONV_FN LONG w3d_fx(float f, int k)
{
    ULONG u = w3d_fbits(f), m, r;
    int e = (int)((u >> 23) & 255);
    if (e == 255) return (u & 0x7fffff) || !(u >> 31) ? 0x7fffffff : (LONG)0x80000000;
    if (!e) return 0;                       /* zero, and below 2^-126 */
    m = (u & 0x7fffff) | 0x800000;          /* f = m * 2^(e - 150) */
    e += k;
    if (e >= 158) r = 0x80000000UL;
    else if (e >= 150) r = m << (e - 150);
    else r = 150 - e > 32 ? 0 : rne(m, 150 - e);
    if (u >> 31) return r >= 0x80000000UL ? (LONG)0x80000000 : -(LONG)r;
    return r >= 0x80000000UL ? 0x7fffffff : (LONG)r;
}

W3D_CONV_FN LONG w3d_f2l(float f) { return w3d_fx(f, 0); }

/* f * 255.0f rounded, 0..255: the product rounded to a float's 24 bits
 * first, as the float multiply did. */
W3D_CONV_FN ULONG w3d_unit8(float f)
{
    ULONG u = w3d_fbits(f), p;
    int e = (int)((u >> 23) & 255);
    if (e == 255 && (u & 0x7fffff)) return 255;     /* NaN, as FMOVE.L's 0x7fffffff clamped */
    if (u >> 31) return 0;
    if (e >= 127) return 255;                       /* 1 or more */
    if (e < 118) return 0;                          /* under 2^-9: under a half */
    p = ((u & 0x7fffff) | 0x800000) * 255UL;        /* f * 255 = p * 2^(e - 150), 31 or 32 bits */
    p = p >= 0x80000000UL ? rne(p, 8) << 8 : rne(p, 7) << 7;
    return rne(p, 150 - e);
}

/* Depth as the stream holds it: 0 for z <= 0, all ones for z >= 1, else
 * z * (2^30 - 1) rounded, shifted up 2. From a 32-bit mantissa M (top bit
 * set) and the double's exponent e: z * 2^30 = M * 2^(e - 1024), and the
 * - z term is M >> 30 (within one in 2^30 of the float code's, 10 November). */
W3D_CONV_FN ULONG zfix(ULONG neg, int e, ULONG m)
{
    if (neg || !e) return 0;
    if (e >= 1023) return 0xFFFFFFFFUL;
    if (1024 - e > 32) return 0;
    return rne(m - (m >> 30), 1024 - e) << 2;
}

W3D_CONV_FN ULONG w3d_zf(float z)
{
    ULONG u = w3d_fbits(z);
    int e = (int)((u >> 23) & 255);
    if (e == 255) return (u & 0x7fffff) || (u >> 31) ? 0 : 0xFFFFFFFFUL;
    return zfix(u >> 31, e ? e + 896 : 0, ((u & 0x7fffff) | 0x800000) << 8);
}

W3D_CONV_FN ULONG w3d_zd(const double *z)
{
    union { double d; ULONG w[2]; } x;
    ULONG hi, lo;
    int e;
    x.d = *z;
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    hi = x.w[1]; lo = x.w[0];
#else
    hi = x.w[0]; lo = x.w[1];
#endif
    e = (int)((hi >> 20) & 2047);
    if (e == 2047) return ((hi & 0xfffff) | lo) || (hi >> 31) ? 0 : 0xFFFFFFFFUL;
    return zfix(hi >> 31, e, 0x80000000UL | (hi & 0xfffff) << 11 | lo >> 21);
}
#endif
