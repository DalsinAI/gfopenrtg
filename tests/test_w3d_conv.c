/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * Warp3D.library's integer float conversions (library/warp3d/w3d_conv.c)
 * against the float code they replace, on the PC with SSE floats (IEEE
 * single, as the mathieee libraries do it).
 *   tests/run_w3d_conv.sh [count] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <exec/types.h>

#include "../library/warp3d/w3d_conv.h"

static LONG ref_f2l(float f)
{
    LONG i;
    float r;
    if (f >= 2147483648.0f) return 0x7fffffff;
    if (f <= -2147483648.0f) return (LONG)0x80000000;
    if (f != f) return 0x7fffffff;
    i = (LONG)f;
    r = f - (float)i;
    if (r > 0.5f || (r == 0.5f && (i & 1))) i++;
    else if (r < -0.5f || (r == -0.5f && (i & 1))) i--;
    return i;
}

static ULONG ref_unit8(float v)
{
    LONG c = ref_f2l(v * 255.0f);
    return c < 0 ? 0 : c > 255 ? 255 : (ULONG)c;
}

static ULONG ref_z(double z)
{
    return z <= 0.0 ? 0 : z >= 1.0 ? 0xFFFFFFFFUL : (ULONG)ref_f2l((float)z * 1073741823.0f) << 2;
}

/* For a double z the integer code keeps more than a float's 24 bits: the
 * float code rounded z to a float first (and z within 2^-25 of 1 became 1.0f,
 * whose product overflowed to depth 0). So against z * (2^30 - 1) in doubles. */
static ULONG ref_zd(double z)
{
    double p;
    if (z <= 0.0) return 0;
    if (z >= 1.0) return 0xFFFFFFFFUL;
    p = z * 1073741823.0 + 0.5;
    return (ULONG)p << 2;
}

static unsigned rnd_state = 12345;
static ULONG rnd(void) { rnd_state = rnd_state * 1103515245u + 12345u; return (rnd_state >> 8) ^ (rnd_state << 20); }
static float fromb(ULONG u) { float f; memcpy(&f, &u, 4); return f; }

int main(int argc, char **argv)
{
    long n = argc > 1 ? atol(argv[1]) : 2000000, i, bad_f = 0, bad_x = 0, bad_c = 0, bad_zf = 0, bad_zd = 0;
    long zworst = 0;
    static const float edge[] = { 0.0f, -0.0f, 0.5f, -0.5f, 1.5f, 2.5f, -2.5f, 0.49999997f, 255.5f, 1.0f, 0.99999994f,
                                  2147483520.0f, 2147483648.0f, -2147483648.0f, 1e-6f, 1e30f, -1e30f, 1.0f / 510.0f, 3.0f / 510.0f };
    for (i = 0; i < n + (long)(sizeof edge / sizeof edge[0]); i++) {
        float f;
        int k;
        long d;
        if (i < (long)(sizeof edge / sizeof edge[0])) f = edge[i];
        else switch (i & 3) {
        case 0: f = fromb(rnd()); break;                                  /* any bits */
        case 1: f = ((LONG)(rnd() & 0xFFFFF) - 0x80000) / 256.0f; break;  /* screen positions, halves */
        case 2: f = (rnd() & 0xFFFFFF) / 16777216.0f; break;              /* 0..1 */
        default: f = ((LONG)(rnd() & 0x3FFF) - 0x2000) / 64.0f + (rnd() & 0xFF) / 1e4f;
        }
        if (w3d_f2l(f) != ref_f2l(f)) { if (bad_f++ < 5) printf("f2l %.9g (%08lx): %ld, float code %ld\n", f, (unsigned long)w3d_fbits(f), (long)w3d_f2l(f), (long)ref_f2l(f)); }
        for (k = 8; k <= 16; k += 4)
            if (w3d_fx(f, k) != ref_f2l(f * (float)(1L << k))) { if (bad_x++ < 5) printf("fx %.9g k %d: %ld, float code %ld\n", f, k, (long)w3d_fx(f, k), (long)ref_f2l(f * (float)(1L << k))); }
        if (w3d_unit8(f) != ref_unit8(f)) { if (bad_c++ < 5) printf("unit8 %.9g: %lu, float code %lu\n", f, (unsigned long)w3d_unit8(f), (unsigned long)ref_unit8(f)); }
        if (f == f) {
            double z = f;
            d = (long)((w3d_zf(f) >> 2) - (ref_z(z) >> 2));
            if (d < 0) d = -d;
            if (d > 1) { if (bad_zf++ < 5) printf("zf %.9g: %08lx, float code %08lx\n", f, (unsigned long)w3d_zf(f), (unsigned long)ref_z(z)); }
            z = (double)f + (rnd() & 0xFFFF) * 1e-12;
            d = (long)((w3d_zd(&z) >> 2) - (ref_zd(z) >> 2));
            if (d < 0) d = -d;
            if (d > zworst && z > 0 && z < 1) zworst = d;
            if (d > 1) { if (bad_zd++ < 5) printf("zd %.17g: %08lx, float code %08lx\n", z, (unsigned long)w3d_zd(&z), (unsigned long)ref_zd(z)); }
        }
    }
    printf("%ld values: f2l %ld differ, fx %ld, unit8 %ld, z (float) %ld, z (double) %ld beyond 1 (worst %ld in 2^30)\n",
           n, bad_f, bad_x, bad_c, bad_zf, bad_zd, zworst);
    return bad_f || bad_x || bad_c || bad_zf || bad_zd;
}
