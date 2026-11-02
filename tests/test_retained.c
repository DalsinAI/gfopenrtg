/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * The retained surface's core (library/opengpu/ogpu_retained.c) on the host:
 * the tiles' hashes and dirty map, marked rectangles, and the present's
 * batch carried out by the CPU core (ogpu_core.c), which must leave the
 * texture equal to the surface and the target equal to the texture drawn
 * once, for INDEX8 (with its palette) and ARGB32 surfaces, 1:1 and scaled,
 * with clips. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../library/opengpu/ogpu_core.h"
#include "../library/opengpu/ogpu_retained.h"

#define ARENA (4u << 20)
#define SRC 0x00010000u
#define TEX 0x00110000u
#define PAL 0x00210000u
#define DST 0x00220000u
#define BUF 0x00380000u
static ogpu_u8 arena[ARENA];
static unsigned long seed = 12345;
static int checks, failures;

static ogpu_u8 *map(void *user, ogpu_u32 address, ogpu_u32 length)
{
    (void)user;
    if (address >= ARENA || length > ARENA - address) return 0;
    return arena + address;
}

static unsigned rnd(void)
{
    seed = seed * 1103515245UL + 12345UL;
    return (unsigned)(seed >> 16) & 0x7FFF;
}

#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static unsigned long rd32(const ogpu_u8 *p) { return (unsigned long)p[0] << 24 | (unsigned long)p[1] << 16 | (unsigned long)p[2] << 8 | p[3]; }
static void wr32(ogpu_u8 *p, unsigned long v) { p[0] = (ogpu_u8)(v >> 24); p[1] = (ogpu_u8)(v >> 16); p[2] = (ogpu_u8)(v >> 8); p[3] = (ogpu_u8)v; }

struct surf {
    struct ogpu_rs rs;
    int w, h, bpp, format;
    long bpr;
    void *mem;
};

static void surf_init(struct surf *s, int w, int h, int format)
{
    s->w = w; s->h = h; s->format = format;
    s->bpp = format == OGPU_FMT_INDEX8 ? 1 : 4;
    s->bpr = ((long)w * s->bpp + 15) & ~15L;
    s->mem = malloc((size_t)ogpu_rs_mem(w, h));
    memset(arena + SRC, 0, (size_t)(s->bpr * h));
    memset(arena + TEX, 0xEE, (size_t)(s->bpr * h));   /* the texture starts as garbage */
    ogpu_rs_init(&s->rs, arena + SRC, s->bpr, w, h, s->bpp, s->mem);
}

static ogpu_u8 *src_px(struct surf *s, int x, int y) { return arena + SRC + y * s->bpr + x * s->bpp; }

static void set_px(struct surf *s, int x, int y, unsigned long v)
{
    if (s->bpp == 1) *src_px(s, x, y) = (ogpu_u8)v;
    else wr32(src_px(s, x, y), v);
}

/* A present through the CPU core into an XRGB32 target dw x dh at x, y,
 * scaled to w x h; returns the tiles uploaded, -1 when the core failed. */
static long present(struct surf *s, int dw, int dh, int x, int y, int w, int h, const short *clips, int nclips)
{
    struct ogpu_rs_present p;
    struct ogpu_core c;
    long n = ogpu_rs_collect(&s->rs), words;
    memset(&p, 0, sizeof p);
    p.src = SRC; p.tex = TEX; p.tex_bpr = (unsigned long)s->bpr;
    p.palette = s->format == OGPU_FMT_INDEX8 ? PAL : 0;
    p.format = s->format;
    p.dst = DST; p.dst_bpr = (unsigned long)dw * 4; p.dst_w = dw; p.dst_h = dh; p.dst_format = OGPU_FMT_XRGB32;
    p.x = x; p.y = y; p.w = w; p.h = h;
    p.clips = clips; p.nclips = nclips;
    p.fence = 7;
    words = ogpu_rs_build_present(&s->rs, &p, arena + BUF, (ARENA - BUF) / 4);
    CHECK(words > 0 && words <= ogpu_rs_present_words(&s->rs, nclips), "batch words %ld", words);
    memset(&c, 0, sizeof c);
    ogpu_core_init(&c);
    c.map = map;
    c.fence = 0;
    c.ext = 0;
    c.user = 0;
    ogpu_core_run(&c, arena + BUF, words);
    if (c.last_error != OGPU_OK) { printf("core error %d at word %ld\n", c.last_error, c.error_word); ogpu_rs_fail(&s->rs); return -1; }
    ogpu_rs_done(&s->rs);
    return n;
}

/* The colour the target should have for surface pixel x, y. */
static unsigned long colour(struct surf *s, int x, int y)
{
    if (s->bpp == 1) return rd32(arena + PAL + *src_px(s, x, y) * 4) & 0xFFFFFFUL;
    return rd32(src_px(s, x, y)) & 0xFFFFFFUL;
}

static int tex_same(struct surf *s)
{
    int y;
    for (y = 0; y < s->h; y++)
        if (memcmp(arena + SRC + y * s->bpr, arena + TEX + y * s->bpr, (size_t)(s->w * s->bpp))) return 0;
    return 1;
}

/* The target 1:1 at ox, oy against the surface, inside the clip x0..x1, y0..y1 (inclusive). */
static int dst_same(struct surf *s, int dw, int ox, int oy)
{
    int x, y;
    for (y = 0; y < s->h; y++)
        for (x = 0; x < s->w; x++)
            if ((rd32(arena + DST + ((oy + y) * dw + ox + x) * 4)) != colour(s, x, y)) {
                printf("  target %d,%d is %08lx, not %08lx\n", ox + x, oy + y, rd32(arena + DST + ((oy + y) * dw + ox + x) * 4), colour(s, x, y));
                return 0;
            }
    return 1;
}

static void fill_random(struct surf *s)
{
    int x, y;
    for (y = 0; y < s->h; y++)
        for (x = 0; x < s->w; x++) set_px(s, x, y, s->bpp == 1 ? rnd() & 255 : ((unsigned long)rnd() << 16 ^ rnd()));
}

static void palette_grey(void)
{
    int i;
    for (i = 0; i < 256; i++) wr32(arena + PAL + i * 4, 0xFF000000UL | (unsigned long)i * 0x010101UL);
}

/* Every single-pixel change is found, in the right tile, and nothing else. */
static void test_hash(int format, int w, int h, int rounds)
{
    struct surf s;
    int r;
    surf_init(&s, w, h, format);
    fill_random(&s);
    CHECK(ogpu_rs_collect(&s.rs) == s.rs.ntiles, "first collect takes every tile");
    ogpu_rs_done(&s.rs);
    CHECK(ogpu_rs_collect(&s.rs) == 0, "unchanged: no tile");
    ogpu_rs_done(&s.rs);
    for (r = 0; r < rounds; r++) {
        int x = (int)(rnd() % (unsigned)w), y = (int)(rnd() % (unsigned)h), bit = (int)(rnd() % (unsigned)(s.bpp * 8));
        long t = (long)(y >> 5) * s.rs.tx + (x >> 5), n, at = 0;
        int rx, ry, rw, rh;
        ogpu_u8 *p = src_px(&s, x, y) + bit / 8;
        *p ^= (ogpu_u8)(1 << (bit & 7));
        n = ogpu_rs_collect(&s.rs);
        CHECK(n == 1 && (s.rs.state[t] & OGPU_RS_DIRTY), "fmt %d %dx%d: pixel %d,%d bit %d: %ld tiles", format, w, h, x, y, bit, n);
        CHECK(ogpu_rs_next_run(&s.rs, &at, &rx, &ry, &rw, &rh) == 1 && x >= rx && x < rx + rw && y >= ry && y < ry + rh
              && rw <= 32 && rh <= 32, "run covers the pixel");
        ogpu_rs_done(&s.rs);
        CHECK(ogpu_rs_collect(&s.rs) == 0, "after the upload: no tile");
        ogpu_rs_done(&s.rs);
    }
    free(s.mem);
}

static void test_sequence(int format)
{
    struct surf s;
    const int W = 100, H = 70, DW = 128, DH = 80;
    unsigned long hashed;
    long n;
    surf_init(&s, W, H, format);
    palette_grey();
    memset(arena + DST, 0x55, DW * DH * 4);
    fill_random(&s);
    n = present(&s, DW, DH, 3, 5, W, H, 0, 0);
    CHECK(n == 12, "fmt %d: first present uploads all 12 tiles (%ld)", format, n);
    CHECK(tex_same(&s) && dst_same(&s, DW, 3, 5), "fmt %d: first present drawn", format);
    n = present(&s, DW, DH, 3, 5, W, H, 0, 0);
    CHECK(n == 0, "fmt %d: second present uploads 0 tiles (%ld)", format, n);
    CHECK(dst_same(&s, DW, 3, 5), "fmt %d: second present drawn", format);
    set_px(&s, 50, 40, s.bpp == 1 ? (*src_px(&s, 50, 40) ^ 0x80) : rd32(src_px(&s, 50, 40)) ^ 0x00808080UL);
    n = present(&s, DW, DH, 3, 5, W, H, 0, 0);
    CHECK(n == 1, "fmt %d: one pixel changed: 1 tile (%ld)", format, n);
    CHECK(tex_same(&s) && dst_same(&s, DW, 3, 5), "fmt %d: the changed pixel drawn", format);
    /* across a tile edge, and in the partial last column and row */
    set_px(&s, 31, 0, 1); set_px(&s, 32, 0, 2); set_px(&s, 99, 69, 3);
    n = present(&s, DW, DH, 3, 5, W, H, 0, 0);
    CHECK(n == 3, "fmt %d: three tiles (%ld)", format, n);
    CHECK(tex_same(&s) && dst_same(&s, DW, 3, 5), "fmt %d: edge tiles drawn", format);
    /* marked: only the marked tiles, nothing hashed (a change elsewhere waits) */
    hashed = s.rs.hashed;
    set_px(&s, 0, 0, s.bpp == 1 ? (*src_px(&s, 0, 0) ^ 1) : rd32(src_px(&s, 0, 0)) ^ 1);
    set_px(&s, 45, 45, 9);
    ogpu_rs_mark(&s.rs, 40, 40, 10, 10);
    n = present(&s, DW, DH, 3, 5, W, H, 0, 0);
    CHECK(n == 1 && s.rs.hashed == hashed, "fmt %d: marked: 1 tile and no hashing (%ld, hashed %lu)", format, n, s.rs.hashed - hashed);
    CHECK(!tex_same(&s), "fmt %d: the unmarked change is not uploaded", format);
    /* the next present hashes: the unmarked change, and the marked tile once more */
    n = present(&s, DW, DH, 3, 5, W, H, 0, 0);
    CHECK(n == 2, "fmt %d: hashing again: 2 tiles (%ld)", format, n);
    CHECK(tex_same(&s) && dst_same(&s, DW, 3, 5), "fmt %d: all drawn", format);
    n = present(&s, DW, DH, 3, 5, W, H, 0, 0);
    CHECK(n == 0, "fmt %d: settled: 0 (%ld)", format, n);
    /* a mark outside the surface, or of nothing: no tile, no hashing */
    hashed = s.rs.hashed;
    ogpu_rs_mark(&s.rs, 500, 500, 10, 10);
    ogpu_rs_mark(&s.rs, 10, 10, 0, 0);
    n = present(&s, DW, DH, 3, 5, W, H, 0, 0);
    CHECK(n == 0 && s.rs.hashed == hashed, "fmt %d: empty marks: 0 tiles, no hashing", format);
    /* the whole surface marked */
    ogpu_rs_mark(&s.rs, 0, 0, -1, -1);
    n = present(&s, DW, DH, 3, 5, W, H, 0, 0);
    CHECK(n == 12 && s.rs.hashed == hashed, "fmt %d: marked whole: 12 tiles, no hashing (%ld)", format, n);
    n = present(&s, DW, DH, 3, 5, W, H, 0, 0);
    CHECK(n == 12, "fmt %d: then hashed once: 12 again (%ld)", format, n);
    n = present(&s, DW, DH, 3, 5, W, H, 0, 0);
    CHECK(n == 0, "fmt %d: and settled (%ld)", format, n);
    /* a lost texture */
    ogpu_rs_all(&s.rs);
    memset(arena + TEX, 0xEE, (size_t)(s.bpr * H));
    n = present(&s, DW, DH, 3, 5, W, H, 0, 0);
    CHECK(n == 12 && tex_same(&s), "fmt %d: everything after ogpu_rs_all (%ld)", format, n);
    if (format == OGPU_FMT_INDEX8) {
        /* a palette change: no tile, the picture changes */
        int i;
        for (i = 0; i < 256; i++) wr32(arena + PAL + i * 4, 0xFF000000UL | (unsigned long)(255 - i) << 16 | (unsigned long)i);
        n = present(&s, DW, DH, 3, 5, W, H, 0, 0);
        CHECK(n == 0, "palette change: 0 tiles (%ld)", n);
        CHECK(dst_same(&s, DW, 3, 5), "palette change drawn");
    }
    free(s.mem);
}

/* Scaled 2x (nearest), and clipped to two rectangles. */
static void test_scaled_clipped(int format)
{
    struct surf s;
    const int W = 40, H = 36, DW = 100, DH = 90;
    static const short clips[8] = { 0, 0, 29, 89, 60, 10, 99, 19 };
    int x, y, bad = 0;
    long n;
    surf_init(&s, W, H, format);
    palette_grey();
    fill_random(&s);
    memset(arena + DST, 0x55, DW * DH * 4);
    n = present(&s, DW, DH, 10, 8, W * 2, H * 2, clips, 2);
    CHECK(n == 4, "scaled: 4 tiles (%ld)", n);
    for (y = 0; y < DH; y++)
        for (x = 0; x < DW; x++) {
            int in = (x <= 29) || (x >= 60 && y >= 10 && y <= 19);
            int on = x >= 10 && y >= 8 && x < 10 + W * 2 && y < 8 + H * 2;
            unsigned long got = rd32(arena + DST + (y * DW + x) * 4);
            unsigned long want = in && on ? colour(&s, (x - 10) / 2, (y - 8) / 2) : 0x55555555UL;
            if (got != want && bad++ < 3) printf("  scaled %d,%d: %08lx, not %08lx\n", x, y, got, want);
        }
    CHECK(!bad, "fmt %d: scaled and clipped: %d pixels wrong", format, bad);
    free(s.mem);
}

static void test_small(void)
{
    struct surf s;
    long n;
    surf_init(&s, 1, 1, OGPU_FMT_ARGB32);
    memset(arena + DST, 0, 16);
    set_px(&s, 0, 0, 0x00123456UL);
    n = present(&s, 2, 2, 1, 1, 1, 1, 0, 0);
    CHECK(n == 1 && rd32(arena + DST + 12) == 0x123456UL, "1x1: %ld", n);
    CHECK(present(&s, 2, 2, 1, 1, 1, 1, 0, 0) == 0, "1x1 again: 0");
    /* nothing of the target shows (a window wholly hidden): the tile goes up, nothing is drawn */
    set_px(&s, 0, 0, 0x00ABCDEFUL);
    CHECK(present(&s, 2, 2, 1, 1, 1, 1, 0, -1) == 1 && rd32(arena + DST + 12) == 0x123456UL && tex_same(&s), "no clip shows: uploaded, not drawn");
    CHECK(present(&s, 2, 2, 1, 1, 1, 1, 0, 0) == 0 && rd32(arena + DST + 12) == 0xABCDEFUL, "then drawn with no upload");
    free(s.mem);
    surf_init(&s, 64, 32, OGPU_FMT_INDEX8);
    CHECK(s.rs.ntiles == 2 && ogpu_rs_mem(64, 32) == 2 * (long)sizeof(unsigned long) + 4, "64x32: 2 tiles");
    free(s.mem);
}

int main(void)
{
    test_hash(OGPU_FMT_INDEX8, 100, 70, 3000);
    test_hash(OGPU_FMT_ARGB32, 100, 70, 3000);
    test_hash(OGPU_FMT_INDEX8, 320, 256, 1000);
    test_sequence(OGPU_FMT_INDEX8);
    test_sequence(OGPU_FMT_ARGB32);
    test_scaled_clipped(OGPU_FMT_INDEX8);
    test_scaled_clipped(OGPU_FMT_ARGB32);
    test_small();
    printf("test_retained: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
