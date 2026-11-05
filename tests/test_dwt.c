/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * Host test for OGPU_OP_DWT_INV: forward 5/3 DWT (lifting from
 * [MS-RDPRFX] 3.1.8.1.4) then inverse through the core; bit-exact match
 * of the reconstructed tile within the same tolerance as GFAmigaRDP's
 * progressive host test (max abs error <= 8 from integer lifting). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../library/opengpu/ogpu_core.h"
#include "../include/gfopengpu/build.h"

#define ARENA (1L << 20)
static ogpu_u8 arena[ARENA];

static ogpu_u8 *map(void *user, ogpu_u32 address, ogpu_u32 length) {
    (void)user;
    if (address >= ARENA || length > (ogpu_u32)ARENA - address) return 0;
    return arena + address;
}

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

enum { B_HL1, B_LH1, B_HH1, B_HL2, B_LH2, B_HH2, B_HL3, B_LH3, B_HH3, B_LL3, NBANDS };

static void level_sizes(int re, int level, int *nl, int *nh) {
    static const unsigned char lo_re[3] = { 33, 17, 9 }, hi_re[3] = { 31, 16, 8 };
    static const unsigned char lo[3] = { 32, 16, 8 }, hi[3] = { 32, 16, 8 };
    *nl = re ? lo_re[level - 1] : lo[level - 1];
    *nh = re ? hi_re[level - 1] : hi[level - 1];
}

static void band_off(int re, int off[NBANDS]) {
    int band, o = 0;
    for (band = 0; band < NBANDS; band++) {
        int level = band == B_LL3 ? 3 : band / 3 + 1, nl, nh, bw, bh;
        level_sizes(re, level, &nl, &nh);
        switch (band == B_LL3 ? 3 : band % 3) {
        case 0: bw = nh; bh = nl; break;
        case 1: bw = nl; bh = nh; break;
        case 2: bw = nh; bh = nh; break;
        default: bw = nl; bh = nl; break;
        }
        off[band] = o;
        o += bw * bh;
    }
}

static void fwd1(const long *xin, int xs, int n, long *L, int ls, long *H, int hs, int nl, int nh) {
    int i, m = n;
    long x[70], h[70];
    for (i = 0; i < n; i++) x[i] = xin[i * xs];
    if (nl + nh < 2 * nl && n == 64 && nl == 33) {
        x[64] = 2 * x[63] - x[62];
        m = 65;
    }
    nh = m / 2;
    for (i = 0; i < nh; i++) {
        long a = x[2 * i], b = 2 * i + 2 < m ? x[2 * i + 2] : x[2 * i];
        h[i] = (x[2 * i + 1] - ((a + b) >> 1)) >> 1;
    }
    for (i = 0; i < nl; i++) {
        long hp = h[i > 0 ? i - 1 : 0], hn = i < nh ? h[i] : h[nh - 1];
        L[i * ls] = x[2 * i] + ((hp + hn + 1) >> 1);
    }
    if (m == 65) nh = 31;
    for (i = 0; i < nh; i++) H[i * hs] = h[i];
}

static void fwd2(const long *x, int n, int nl, int nh, long *LL, long *HL, long *LH, long *HH) {
    static long lo[66 * 66], hi[66 * 66];
    int i;
    for (i = 0; i < n; i++) fwd1(x + i, n, n, lo + i, n, hi + i, n, nl, nh);
    for (i = 0; i < nl; i++) fwd1(lo + i * n, 1, n, LL + i * nl, 1, HL + i * nh, 1, nl, nh);
    for (i = 0; i < nh; i++) fwd1(hi + i * n, 1, n, LH + i * nl, 1, HH + i * nh, 1, nl, nh);
}

#define IN_A   0x1000UL
#define WORK_A 0x20000UL
#define OUT_A  0x40000UL

static int test_roundtrip(int re) {
    static long img[64 * 64], buf[4096], ll1[33 * 33], ll2[17 * 17];
    int off[NBANDS], i, maxerr = 0;
    int n1 = re ? 33 : 32, h1 = re ? 31 : 32, n2 = re ? 17 : 16, h2 = 16, n3 = re ? 9 : 8, h3 = 8;
    unsigned char sbuf[256];
    struct OGPUBatch B;
    struct ogpu_core C;
    long *mapped;

    band_off(re, off);
    for (i = 0; i < 64 * 64; i++)
        img[i] = ((i % 64) * 3 + (i / 64) * 2) - 128 + ((i * 7919) % 5);
    memset(buf, 0, sizeof buf);
    fwd2(img, 64, n1, h1, ll1, buf + off[B_HL1], buf + off[B_LH1], buf + off[B_HH1]);
    fwd2(ll1, n1, n2, h2, ll2, buf + off[B_HL2], buf + off[B_LH2], buf + off[B_HH2]);
    fwd2(ll2, n2, n3, h3, buf + off[B_LL3], buf + off[B_HL3], buf + off[B_LH3], buf + off[B_HH3]);

    memcpy(arena + IN_A, buf, sizeof buf);
    memset(arena + WORK_A, 0, OGPU_DWT_RFX_WORK_BYTES);

    ogpu_batch_init(&B, sbuf, sizeof sbuf / 4);
    ogpu_dwt_inv(&B, IN_A, 0, re ? OGPU_DWT_RFX_RE : 0, WORK_A, OGPU_DWT_RFX_WORK_BYTES);
    ogpu_core_init(&C);
    C.map = map;
    CHECK(ogpu_core_run(&C, sbuf, B.words) == 1, "run commands");
    CHECK(C.last_error == OGPU_OK, "last_error %d", C.last_error);

    mapped = (long *)(void *)(arena + IN_A);
    for (i = 0; i < 64 * 64; i++) {
        long e = mapped[i] - img[i];
        if (e < 0) e = -e;
        if (e > maxerr) maxerr = (int)e;
    }
    printf("dwt re=%d max error %d\n", re, maxerr);
    CHECK(maxerr <= 8, "maxerr %d", maxerr);
    return maxerr;
}

static void test_supports(void) {
    CHECK(ogpu_core_supports(OGPU_OP_DWT_INV, OGPU_FMT_ARGB32) == OGPU_FULL, "supports ARGB32");
    CHECK(ogpu_core_supports(OGPU_OP_DWT_INV, OGPU_FMT_CLUT8) == OGPU_FULL, "supports CLUT8 (compute)");
}

static void test_short_work(void) {
    unsigned char sbuf[256];
    struct OGPUBatch B;
    struct ogpu_core C;
    memset(arena + IN_A, 0, 4096 * 4);
    ogpu_batch_init(&B, sbuf, sizeof sbuf / 4);
    ogpu_dwt_inv(&B, IN_A, 0, OGPU_DWT_RFX_RE, WORK_A, 16);
    ogpu_core_init(&C);
    C.map = map;
    ogpu_core_run(&C, sbuf, B.words);
    CHECK(C.last_error == OGPU_ERR_UNSUPPORTED, "short work -> UNSUPPORTED got %d", C.last_error);
}

int main(void) {
    test_supports();
    test_roundtrip(1);
    test_roundtrip(0);
    test_short_work();
    printf(failures ? "FAILED %d\n" : "all ok\n", failures);
    return failures != 0;
}
