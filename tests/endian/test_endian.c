/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * amiga_endian.h against byte-by-byte reference swaps, on the host (little
 * endian) and on the 68k (big endian, inline asm): every array call at
 * lengths 0-80 and with src and dst at each 2-byte offset, the loads and
 * stores at every byte offset, the strided and interleaved calls.
 *
 *   test_endian            the checks; exits 1 on a failure
 *   test_endian bench [KB] also times the array calls over KB (default 1024)
 */
#include <gfopengpu/amiga_endian.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int failures;

#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)

static unsigned rnd_state = 12345;
static unsigned char rnd(void)
{
    rnd_state = rnd_state * 1103515245u + 12345u;
    return (unsigned char)(rnd_state >> 16);
}

/* reference: reverse each group of size bytes */
static void ref_swap(unsigned char *dst, const unsigned char *src, size_t n, size_t size)
{
    size_t i, j;
    for (i = 0; i < n; i++)
        for (j = 0; j < size; j++)
            dst[i * size + j] = src[i * size + size - 1 - j];
}

#define BUF 1024
static union { unsigned char b[BUF]; uint64_t align; } A, B, C, R;

static void fill(unsigned char *p, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++)
        p[i] = rnd();
}

static void test_values(void)
{
    union { float f; uint32_t u; } fv;
    CHECK(ae_swap16(0x1234) == 0x3412, "swap16");
    CHECK(ae_swap32(0x12345678u) == 0x78563412u, "swap32");
    CHECK(ae_swap64(0x0102030405060708ull) == 0x0807060504030201ull, "swap64");
    CHECK(swap16(0xabcd) == 0xcdab && swap32(0xdeadbeefu) == 0xefbeaddeu, "short names");
    CHECK(le_to_be32(0x11223344u) == 0x44332211u && be_to_le16(0x1122) == 0x2211, "le_to_be/be_to_le");
    fv.u = 0x0000803fu;                 /* 1.0f, little-endian bytes read on a BE host */
    fv.f = ae_swapf(fv.f);
    CHECK(fv.u == 0x3f800000u, "swapf");
#if AMIGA_ENDIAN_BIG
    CHECK(ae_le32toh(0x11223344u) == 0x44332211u && ae_be32toh(0x11223344u) == 0x11223344u, "toh (BE)");
#else
    CHECK(ae_le32toh(0x11223344u) == 0x11223344u && ae_be32toh(0x11223344u) == 0x44332211u, "toh (LE)");
#endif
    CHECK(ae_htole64(ae_le64toh(0x0102030405060708ull)) == 0x0102030405060708ull, "64 round trip");
}

static void test_loads(void)
{
    static const unsigned char bytes[16] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
    unsigned off;
    for (off = 0; off < 8; off++) {
        const unsigned char *p = bytes + off;
        unsigned char st[16];
        uint32_t le = (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
        uint32_t be = (uint32_t)p[3] | (uint32_t)p[2] << 8 | (uint32_t)p[1] << 16 | (uint32_t)p[0] << 24;
        uint64_t le64 = le | (uint64_t)((uint32_t)p[4] | (uint32_t)p[5] << 8 | (uint32_t)p[6] << 16 | (uint32_t)p[7] << 24) << 32;
        CHECK(ae_load_le16(p) == (uint16_t)(p[0] | p[1] << 8), "load_le16 +%u", off);
        CHECK(ae_load_be16(p) == (uint16_t)(p[1] | p[0] << 8), "load_be16 +%u", off);
        CHECK(load_le32(p) == le, "load_le32 +%u: %08lx", off, (unsigned long)load_le32(p));
        CHECK(ae_load_be32(p) == be, "load_be32 +%u", off);
        CHECK(ae_load_le64(p) == le64, "load_le64 +%u", off);
        CHECK(ae_load_be64(p) == ae_swap64(le64), "load_be64 +%u", off);
        memset(st, 0, sizeof st);
        store_le32(st + off, le);
        CHECK(memcmp(st + off, p, 4) == 0 && st[off + 4] == 0 && (off == 0 || st[off - 1] == 0), "store_le32 +%u", off);
        memset(st, 0, sizeof st);
        ae_store_be32(st + off, be);
        CHECK(memcmp(st + off, p, 4) == 0, "store_be32 +%u", off);
        memset(st, 0, sizeof st);
        ae_store_le16(st + off, (uint16_t)(p[0] | p[1] << 8));
        CHECK(memcmp(st + off, p, 2) == 0 && st[off + 2] == 0, "store_le16 +%u", off);
        memset(st, 0, sizeof st);
        ae_store_be16(st + off, (uint16_t)(p[1] | p[0] << 8));
        CHECK(memcmp(st + off, p, 2) == 0, "store_be16 +%u", off);
        memset(st, 0, sizeof st);
        ae_store_le64(st + off, le64);
        CHECK(memcmp(st + off, p, 8) == 0, "store_le64 +%u", off);
        memset(st, 0, sizeof st);
        ae_store_be64(st + off, ae_swap64(le64));
        CHECK(memcmp(st + off, p, 8) == 0, "store_be64 +%u", off);
    }
}

typedef void copy_fn(void *, const void *, size_t);
static void call16(void *d, const void *s, size_t n) { ae_swap16_copy(d, s, n); }
static void call32(void *d, const void *s, size_t n) { ae_swap32_copy(d, s, n); }
static void call64(void *d, const void *s, size_t n) { ae_swap64_copy(d, s, n); }

static void test_arrays(void)
{
    static const struct { copy_fn *fn; size_t size; const char *name; } t[] = {
        { call16, 2, "swap16" }, { call32, 4, "swap32" }, { call64, 8, "swap64" },
    };
    unsigned k;
    for (k = 0; k < 3; k++) {
        size_t size = t[k].size, n, so, doff;
        for (n = 0; n <= 80; n++)
            for (so = 0; so < 8; so += 2)
                for (doff = 0; doff < 8; doff += 2) {
                    size_t bytes = n * size;
                    /* copy, with guard bytes around dst */
                    fill(A.b, BUF);
                    fill(B.b, BUF);
                    memcpy(C.b, B.b, BUF);
                    ref_swap(R.b, A.b + so, n, size);
                    t[k].fn(B.b + 16 + doff, A.b + so, n);
                    CHECK(memcmp(B.b + 16 + doff, R.b, bytes) == 0, "%s copy n=%u src+%u dst+%u",
                          t[k].name, (unsigned)n, (unsigned)so, (unsigned)doff);
                    CHECK(memcmp(B.b, C.b, 16 + doff) == 0 && memcmp(B.b + 16 + doff + bytes, C.b + 16 + doff + bytes,
                          BUF - 16 - doff - bytes) == 0, "%s copy n=%u wrote outside", t[k].name, (unsigned)n);
                    /* in place */
                    memcpy(C.b, A.b, BUF);
                    t[k].fn(A.b + so, A.b + so, n);
                    CHECK(memcmp(A.b + so, R.b, bytes) == 0, "%s in place n=%u +%u", t[k].name, (unsigned)n, (unsigned)so);
                    CHECK(memcmp(A.b, C.b, so) == 0 && memcmp(A.b + so + bytes, C.b + so + bytes, BUF - so - bytes) == 0,
                          "%s in place n=%u wrote outside", t[k].name, (unsigned)n);
                }
    }
    /* the macros */
    fill(A.b, 64);
    ref_swap(R.b, A.b, 16, 4);
    swap_float_array((float *)A.b, 16);
    CHECK(memcmp(A.b, R.b, 64) == 0, "swap_float_array");
    fill(A.b, 64);
    memcpy(C.b, A.b, 64);
    ae_le32_array_to_host(A.b, 16);
#if AMIGA_ENDIAN_BIG
    ref_swap(R.b, C.b, 16, 4);
    CHECK(memcmp(A.b, R.b, 64) == 0, "le32_array_to_host swaps on BE");
#else
    CHECK(memcmp(A.b, C.b, 64) == 0, "le32_array_to_host is a no-op on LE");
#endif
}

static void test_strided(void)
{
    /* 10 vertices of 28 bytes: f32x3 position, f32x2 uv, u16x2, then 4 pad bytes */
    enum { COUNT = 10, STRIDE = 28 };
    size_t v;
    fill(A.b, COUNT * STRIDE + 8);
    memcpy(C.b, A.b, BUF);
    memcpy(R.b, A.b, BUF);
    for (v = 0; v < COUNT; v++) {
        ref_swap(R.b + v * STRIDE, C.b + v * STRIDE, 5, 4);
        ref_swap(R.b + v * STRIDE + 20, C.b + v * STRIDE + 20, 2, 2);
    }
    CHECK(ae_swap_interleaved(A.b, COUNT, STRIDE, "4444422") == 0, "interleaved: layout refused");
    CHECK(memcmp(A.b, R.b, BUF) == 0, "interleaved 4444422");

    memcpy(A.b, C.b, BUF);              /* the same with the strided calls */
    ae_swap32_strided(A.b, COUNT, STRIDE, 5);
    ae_swap16_strided(A.b + 20, COUNT, STRIDE, 2);
    CHECK(memcmp(A.b, R.b, BUF) == 0, "strided");

    memcpy(A.b, C.b, BUF);              /* 8-byte and 1-byte fields, an odd stride */
    memcpy(R.b, C.b, BUF);
    for (v = 0; v < 7; v++) {
        ref_swap(R.b + 1 + v * 15, C.b + 1 + v * 15, 1, 8);
        ref_swap(R.b + 9 + v * 15, C.b + 9 + v * 15, 1, 2);
    }
    CHECK(ae_swap_interleaved(A.b, 7, 15, "1821") == 0, "interleaved 1821 refused");
    CHECK(memcmp(A.b, R.b, BUF) == 0, "interleaved 1821, stride 15");

    CHECK(ae_swap_interleaved(A.b, 1, 4, "44") == -1, "fields past the stride");
    CHECK(ae_swap_interleaved(A.b, 1, 16, "43") == -1, "a bad digit");
}

static void bench(size_t kb)
{
    size_t bytes = kb * 1024, i;
    unsigned char *a = (unsigned char *)malloc(bytes + 16), *b = (unsigned char *)malloc(bytes + 16);
    static const struct { const char *name; size_t size; copy_fn *fn; int inplace; } t[] = {
        { "swap16_array", 2, call16, 1 }, { "swap32_array", 4, call32, 1 }, { "swap64_array", 8, call64, 1 },
        { "swap16_copy", 2, call16, 0 }, { "swap32_copy", 4, call32, 0 },
    };
    if (!a || !b) {
        printf("bench: no memory for %u KB\n", (unsigned)kb);
        return;
    }
    memset(a, 0x5a, bytes);
    memset(b, 0, bytes);
    for (i = 0; i < sizeof t / sizeof t[0]; i++) {
        clock_t c0 = clock(), c1;
        unsigned reps = 0;
        do {
            t[i].fn(t[i].inplace ? a : b, a, bytes / t[i].size);
            reps++;
            c1 = clock();
        } while (c1 - c0 < CLOCKS_PER_SEC / 2);
        /* KB/s in integers: no floating point, so it links for any 68k */
        printf("%-14s %5u KB: %9lu KB/s\n", t[i].name, (unsigned)kb,
               (unsigned long)((unsigned long long)bytes * reps * CLOCKS_PER_SEC / 1024 / (unsigned long long)(c1 - c0)));
    }
    free(a);
    free(b);
}

int main(int argc, char **argv)
{
    test_values();
    test_loads();
    test_arrays();
    test_strided();
    printf("amiga_endian: %s host, %s, %d failure(s)\n", AMIGA_ENDIAN_BIG ? "big-endian" : "little-endian",
           AE_M68K ? "68k inline asm" : "C", failures);
    if (argc > 1 && strcmp(argv[1], "bench") == 0)
        bench(argc > 2 ? (size_t)atoi(argv[2]) : 1024);
    return failures ? 1 : 0;
}
