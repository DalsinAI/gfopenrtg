/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * library/pixconv.h against the per-pixel code it replaces in screens.c
 * (gfopenrtg.library 0.24): the nearest pen and its memo, through palette
 * changes; the planes read and written a row at a time; the runs of a
 * mask and its rectangles; and whole blits, every pair of planar, 8, 16 and 32-bit bitmaps,
 * with and without a mask (cookie), minterms, plane masks, and overlapping
 * copies within one bitmap, compared pixel for pixel and plane for plane
 * with the old loop.
 *   test_pixconv [BLITS] [BENCH]     (default 60000 random blits)
 * BENCH also times 0.24's code and pixconv.h's side by side, on bitmaps in
 * memory: the nearest pens of a 640 x 480 32-bit screen (ReadPixelArray8),
 * 100 masked 40 x 32 icons on the CPU, and a 320 x 256 x 8 planar picture
 * to 32-bit. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../library/pixconv.h"

static unsigned long long seed = 12345;
static ULONG rnd(void) { seed = seed * 6364136223846793005ULL + 1442695040888963407ULL; return (ULONG)(seed >> 33); }
static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; if (fails < 20) { printf(__VA_ARGS__); printf("\n"); } } } while (0)

/* ---- screens.c 0.24's code, as it was ---- */

static UBYTE old_px_pen(const ULONG *pal, UBYTE bpp, ULONG px)
{
    ULONG rgb, best = 0, dist = (ULONG)~0UL;
    if (bpp == 1) return (UBYTE)px;
    rgb = pc_decode(bpp, px);
    for (ULONG i = 0; i < 256; i++) {
        ULONG c = bpp == 2 ? pc_decode(bpp, pc_encode(bpp, pal[i])) : pal[i];
        LONG dr = (LONG)((c >> 16) & 255) - (LONG)((rgb >> 16) & 255), dg = (LONG)((c >> 8) & 255) - (LONG)((rgb >> 8) & 255), db = (LONG)(c & 255) - (LONG)(rgb & 255);
        ULONG d2 = (ULONG)(dr * dr + dg * dg + db * db);
        if (d2 < dist) { dist = d2; best = i; if (!d2) break; }
    }
    return (UBYTE)best;
}

static UBYTE old_planar_pen(const struct pc_surf *bm, LONG x, LONG y)
{
    UBYTE v = 0;
    ULONG off = y * bm->bpr + (x >> 3);
    UBYTE bit = 0x80 >> (x & 7);
    for (int p = 0; p < bm->depth && p < 8; p++) {
        UBYTE *pl = bm->planes[p];
        if (pl == (UBYTE *)-1) v |= 1 << p;
        else if (pl && (pl[off] & bit)) v |= 1 << p;
    }
    return v;
}

static void old_planar_set(const struct pc_surf *bm, LONG x, LONG y, UBYTE v, UBYTE mask)
{
    ULONG off = y * bm->bpr + (x >> 3);
    UBYTE bit = 0x80 >> (x & 7);
    for (int p = 0; p < bm->depth && p < 8; p++) {
        UBYTE *pl = bm->planes[p];
        if (!(mask & (1 << p)) || !pl || pl == (UBYTE *)-1) continue;
        if (v & (1 << p)) pl[off] |= bit; else pl[off] &= ~bit;
    }
}

static ULONG old_get(const struct pc_surf *o, LONG x, LONG y) { return pc_get(o->mem + y * o->stride, o->bpp, x); }
static void old_put(const struct pc_surf *o, LONG x, LONG y, ULONG v) { pc_put(o->mem + y * o->stride, o->bpp, x, v); }

/* blit()'s CPU loop (os: src->bpp, od: dst->bpp; 0 is planar) */
static void old_blit(const struct pc_surf *src, LONG sx, LONG sy, const struct pc_surf *dst, LONG dx, LONG dy, LONG w, LONG h, UBYTE m, UBYTE mask,
                     const UBYTE *cookie, LONG cookie_x, LONG cookie_y, LONG cookie_mod, int same)
{
    int os = src->bpp != 0, od = dst->bpp != 0;
    int down = !(same && sy < dy), right = !(same && sy == dy && sx < dx);
    for (LONG j = 0; j < h; j++) {
        LONG yy = down ? j : h - 1 - j;
        for (LONG i = 0; i < w; i++) {
            LONG xx = right ? i : w - 1 - i;
            ULONG s, d, r;
            if (cookie) {
                LONG bit = cookie_x + xx;
                if (!((cookie[(cookie_y + yy) * cookie_mod + (bit >> 3)] >> (7 - (bit & 7))) & 1)) continue;
            }
            s = os ? old_get(src, sx + xx, sy + yy) : old_planar_pen(src, sx + xx, sy + yy);
            if (od) {
                if (!os || src->bpp == 1) { if (dst->bpp != 1) s = pc_pen_px(dst, s); }
                else if (dst->bpp == 1) s = old_px_pen(src->pal, src->bpp, s);
                else if (src->bpp != dst->bpp) s = pc_encode(dst->bpp, pc_decode(src->bpp, s));
                d = old_get(dst, dx + xx, dy + yy);
                if (cookie) r = s;
                else if (dst->bpp != 1 && ((m >> 7) & 1) == ((m >> 5) & 1) && ((m >> 6) & 1) == ((m >> 4) & 1)) {
                    switch (m & 0xF0) {
                    case 0x00: r = pc_pen_px(dst, 0); break;
                    case 0xF0: r = pc_pen_px(dst, 255); break;
                    case 0x50: r = d ^ (dst->bpp == 2 ? 0xFFFF : 0xFFFFFF); break;
                    default: r = d; break;
                    }
                } else r = pc_minterm(m, s, d);
                if (dst->bpp == 1) r = (d & ~(ULONG)mask) | (r & mask);
                else if (!mask) r = d;
                else if (dst->bpp == 2) r &= 0xFFFF;
                old_put(dst, dx + xx, dy + yy, r);
            } else {
                if (os && src->bpp != 1) s = old_px_pen(src->pal, src->bpp, s);
                d = old_planar_pen(dst, dx + xx, dy + yy);
                r = cookie ? s : pc_minterm(m, s, d);
                old_planar_set(dst, dx + xx, dy + yy, (UBYTE)r, mask);
            }
        }
    }
}

/* ---- the tests ---- */

static ULONG pal[PC_MEMO_TABLES][256];

static void random_pal(ULONG *p)
{
    for (int i = 0; i < 256; i++) {
        switch (rnd() % 4) {
        case 0: p[i] = rnd() & 0xFFFFFF; break;
        case 1: p[i] = i ? p[rnd() % i] : 0; break;                         /* the same colour twice */
        case 2: p[i] = i ? (p[rnd() % i] ^ (rnd() & 0x030303)) & 0xFFFFFF : 0; break;   /* near another */
        default: p[i] = (rnd() & 0xFF000000) | (rnd() & 0x00F8FCF8); break;  /* top byte set, as 16-bit */
        }
    }
}

static UBYTE test_px_pen(const struct pc_surf *s, ULONG px) { return pc_nearest(s->pal, s->bpp, px); }

static void test_nearest_and_memo(void)
{
    static struct pc_memo memo;
    int t;
    for (t = 0; t < PC_MEMO_TABLES; t++) random_pal(pal[t]);
    for (int i = 0; i < 20000; i++) {
        UBYTE bpp = rnd() & 1 ? 2 : 4;
        ULONG px = rnd() % 3 ? rnd() : pal[rnd() % 16][rnd() % 256];
        t = rnd() % PC_MEMO_TABLES;
        if (bpp == 2) px &= 0xFFFF;
        CHECK(pc_nearest(pal[t], bpp, px) == old_px_pen(pal[t], bpp, px), "nearest %08lx bpp %d", (unsigned long)px, bpp);
    }
    /* the memo: random pixels, few enough to repeat, through palette changes */
    for (int i = 0; i < 200000; i++) {
        UBYTE bpp = rnd() & 1 ? 2 : 4;
        ULONG px = (rnd() % 600) * 0x10101 ^ (rnd() % 7) << 9;
        t = rnd() % PC_MEMO_TABLES;
        if (rnd() % 997 == 0) {
            pc_memo_forget(&memo);
            pal[rnd() % PC_MEMO_TABLES][rnd() % 256] = rnd() & 0xFFFFFF;
            pc_memo_forget(&memo);
        }
        if (i == 100000) memo.gen = PC_MEMO_GENS - 1;    /* round the generations */
        if (bpp == 2) px &= 0xFFFF;
        CHECK(pc_memo_pen(&memo, pal[t], bpp, (UBYTE)t, px) == old_px_pen(pal[t], bpp, px), "memo %08lx bpp %d table %d at %d", (unsigned long)px, bpp, t, i);
    }
    /* a table beyond the key's room goes uncached */
    CHECK(pc_memo_pen(&memo, pal[3], 4, 200, 0x123456) == old_px_pen(pal[3], 4, 0x123456), "memo table 200");
}

static void test_runs(void)
{
    UBYTE row[40];
    WORD runs[2 * 5];
    for (int i = 0; i < 20000; i++) {
        LONG x = rnd() % 64, n = 1 + rnd() % 250, at = 0, pos = 0;
        int max = 1 + rnd() % 5;
        int kind = rnd() % 4;
        for (int b = 0; b < 40; b++) row[b] = kind == 0 ? rnd() : kind == 1 ? 0xFF : kind == 2 ? (rnd() & 1 ? 0 : 0xFF) : (rnd() % 5 ? 0 : rnd());
        if (x + n > 320) n = 320 - x;
        while (at < n) {
            int k = pc_runs(row, x, n, &at, runs, max);
            for (int r = 0; r < k; r++) {
                /* every bit from pos to the run's start clear, the run set, its end clear or the row's end */
                for (; pos < runs[2 * r]; pos++) CHECK(!((row[(x + pos) >> 3] >> (7 - ((x + pos) & 7))) & 1), "runs: bit %ld set outside", (long)pos);
                CHECK(runs[2 * r] < runs[2 * r + 1], "runs: empty run");
                for (; pos < runs[2 * r + 1]; pos++) CHECK((row[(x + pos) >> 3] >> (7 - ((x + pos) & 7))) & 1, "runs: bit %ld clear inside", (long)pos);
                CHECK(pos == n || !((row[(x + pos) >> 3] >> (7 - ((x + pos) & 7))) & 1), "runs: run ends early at %ld", (long)pos);
            }
            if (k < max) { CHECK(at == n, "runs: stopped at %ld of %ld", (long)at, (long)n); break; }
        }
        for (; pos < n; pos++) CHECK(!((row[(x + pos) >> 3] >> (7 - ((x + pos) & 7))) & 1), "runs: bit %ld missed", (long)pos);
    }
}

/* pc_mask_rects: every set bit of the box in one rectangle, once; no clear bit in any */
static UBYTE cover[40][200];
static int rect_count, rect_stop;
static int cover_rect(void *ctx, LONG x, LONG y, LONG w, LONG h)
{
    (void)ctx;
    if (rect_stop && ++rect_count > rect_stop) return 0;
    for (LONG j = y; j < y + h; j++) for (LONG i = x; i < x + w; i++) cover[j][i]++;
    return 1;
}

static void test_mask_rects(void)
{
    UBYTE mask[40 * 32];
    for (int t = 0; t < 5000; t++) {
        LONG cmod = 32, cx = rnd() % 40, cy = rnd() % 4, w = 1 + rnd() % 200, h = 1 + rnd() % 36;
        int kind = rnd() % 5;
        if (cx + w > 256) w = 256 - cx;
        for (int y = 0; y < 40; y++)
            for (int b = 0; b < 32; b++) {
                UBYTE v;
                switch (kind) {
                case 0: v = (UBYTE)rnd(); break;                        /* many runs a row */
                case 1: v = 0xFF; break;
                case 2: v = (y / 3) & 1 ? 0xFF : 0x0F; break;            /* bands of rows alike */
                case 3: { int r = (y - 20) * (y - 20); v = 0;            /* a disc, an icon's shape */
                          for (int i = 0; i < 8; i++) { int x = b * 8 + i - 60; if (x * x + r < 300) v |= 0x80 >> i; } break; }
                default: v = rnd() % 4 ? 0 : (UBYTE)rnd(); break;
                }
                mask[y * cmod + b] = v;
            }
        memset(cover, 0, sizeof cover);
        rect_count = 0; rect_stop = 0;
        CHECK(pc_mask_rects(mask, cx, cy, cmod, w, h, cover_rect, NULL) == 1, "mask_rects: stopped");
        for (LONG y = 0; y < h; y++)
            for (LONG x = 0; x < w; x++) {
                LONG bit = cx + x;
                int set = (mask[(cy + y) * cmod + (bit >> 3)] >> (7 - (bit & 7))) & 1;
                CHECK(cover[y][x] == set, "mask_rects: kind %d, %ld,%ld covered %d times, bit %d", kind, (long)x, (long)y, cover[y][x], set);
            }
        rect_stop = 1;
        if (kind == 0) CHECK(pc_mask_rects(mask, cx, cy, cmod, w, h, cover_rect, NULL) == 0 || rect_count <= 1, "mask_rects: no stop");
    }
}

#define BW 100
#define BH 24

struct buf { struct pc_surf s; UBYTE *mem; UBYTE *planes[8]; };

static void make(struct buf *b, int bpp, int table)
{
    memset(b, 0, sizeof *b);
    b->s.bpp = (UBYTE)bpp;
    b->s.pal = pal[table];
    b->s.px_pen = test_px_pen;
    if (bpp) {
        b->s.stride = (BW * bpp + 15) & ~15UL;
        b->mem = malloc(b->s.stride * BH);
        for (ULONG i = 0; i < b->s.stride * BH; i++) b->mem[i] = (UBYTE)rnd();
        if (bpp > 1 && rnd() % 2)       /* pixels of the palette's colours */
            for (int y = 0; y < BH; y++)
                for (int x = 0; x < BW; x++) pc_put(b->mem + y * b->s.stride, (UBYTE)bpp, x, pc_encode((UBYTE)bpp, pal[table][rnd() % 256]));
        b->s.mem = b->mem;
    } else {
        b->s.bpr = 14;
        b->s.depth = 1 + rnd() % 9;
        for (int p = 0; p < 8; p++) {
            int k = rnd() % 12;
            b->planes[p] = malloc(b->s.bpr * BH);
            for (int i = 0; i < b->s.bpr * BH; i++) b->planes[p][i] = (UBYTE)rnd();
            b->s.planes[p] = k == 0 ? NULL : k == 1 ? (UBYTE *)-1 : b->planes[p];
        }
    }
}

static void copy_buf(struct buf *to, const struct buf *from)
{
    *to = *from;
    if (from->s.bpp) {
        to->mem = malloc(from->s.stride * BH);
        memcpy(to->mem, from->mem, from->s.stride * BH);
        to->s.mem = to->mem;
    } else
        for (int p = 0; p < 8; p++) {
            to->planes[p] = malloc(from->s.bpr * BH);
            memcpy(to->planes[p], from->planes[p], from->s.bpr * BH);
            to->s.planes[p] = from->s.planes[p] == from->planes[p] ? to->planes[p] : from->s.planes[p];
        }
}

static int same_buf(const struct buf *a, const struct buf *b)
{
    if (a->s.bpp) return !memcmp(a->mem, b->mem, a->s.stride * BH);
    for (int p = 0; p < 8; p++) if (memcmp(a->planes[p], b->planes[p], a->s.bpr * BH)) return 0;
    return 1;
}

static void free_buf(struct buf *b)
{
    free(b->mem);
    for (int p = 0; p < 8; p++) free(b->planes[p]);
}

static void test_blits(long count)
{
    static const int fmts[4] = { 0, 1, 2, 4 };
    static const UBYTE minterms[] = { 0xC0, 0xE0, 0x60, 0x30, 0x50, 0x00, 0xF0, 0xA0, 0x80, 0x20, 0x10, 0x40, 0x70, 0xD0 };
    UBYTE cookie[8 * BH];
    long n = 0;
    for (long i = 0; i < count; i++) {
        int sf = fmts[rnd() % 4], df = fmts[rnd() % 4], same = sf && sf == df && rnd() % 4 == 0;
        struct buf src, d1, d2;
        LONG w = 1 + rnd() % 90, h = 1 + rnd() % 12, sx = rnd() % (BW - w + 1), sy = rnd() % (BH - h + 1);
        LONG dx = rnd() % (BW - w + 1), dy = rnd() % (BH - h + 1), cx = rnd() % 8, cy = 0, cmod = 8;
        UBYTE m = rnd() % 3 ? minterms[rnd() % sizeof minterms] : (UBYTE)(rnd() & 0xF0);
        UBYTE mask = rnd() % 3 == 0 ? 0xFF : rnd() % 4 == 0 ? 0 : (UBYTE)rnd();
        int kind = rnd() % 4, ck = rnd() % 2;
        if (!sf && !df) continue;               /* graphics' own, never GFOpenRTG's */
        if (!sf && w + sx > 14 * 8) continue;
        if (!df && w + dx > 14 * 8) continue;
        if (ck && w + cx > 8 * 8) continue;
        for (int b = 0; b < (int)sizeof cookie; b++)
            cookie[b] = kind == 0 ? rnd() : kind == 1 ? 0xFF : kind == 2 ? 0 : (rnd() % 3 ? 0xFF : rnd());
        make(&src, sf, rnd() % PC_MEMO_TABLES);
        if (same) { copy_buf(&d1, &src); copy_buf(&d2, &src); }
        else { struct buf d; make(&d, df, rnd() % PC_MEMO_TABLES); copy_buf(&d1, &d); copy_buf(&d2, &d); free_buf(&d); }
        if (same && rnd() % 2) { sy = dy; }     /* overlapping in a row, either way */
        old_blit(same ? &d1.s : &src.s, sx, sy, &d1.s, dx, dy, w, h, m, mask, ck ? cookie : NULL, cx, cy, cmod, same);
        pc_blit(same ? &d2.s : &src.s, sx, sy, &d2.s, dx, dy, w, h, m, mask, ck ? cookie : NULL, cx, cy, cmod, same);
        CHECK(same_buf(&d1, &d2), "blit %ld: %d to %d%s, %ldx%ld from %ld,%ld to %ld,%ld, minterm %02x mask %02x%s",
              i, sf, df, same ? " (one bitmap)" : "", (long)w, (long)h, (long)sx, (long)sy, (long)dx, (long)dy, m, mask, ck ? " cookie" : "");
        n++;
        free_buf(&src); free_buf(&d1); free_buf(&d2);
    }
    printf("test_pixconv: %ld blits the same as the per-pixel loop\n", n);
}

/* ---- BENCH: 0.24's code against pixconv.h's, timed ---- */

static ULONG bpal[256];
static struct pc_memo bmemo;
static UBYTE bench_nearest(const struct pc_surf *s, ULONG px) { (void)s; return pc_nearest(bpal, 4, px); }
static double since(clock_t c) { return (double)(clock() - c) * 1000.0 / CLOCKS_PER_SEC; }

static void bench(void)
{
    static UBYTE planes[8][40 * 256], icon_mask[6 * 32];
    static ULONG icon[40 * 32];
    ULONG *scr = malloc(640 * 480 * 4);
    UBYTE *arr = malloc(640 * 480), *arr2 = malloc(640 * 480);
    struct pc_surf P, S, I;
    clock_t c;
    double t_old, t_new;
    int k;
    if (!scr || !arr || !arr2) { printf("bench: no memory\n"); return; }
    memset(&P, 0, sizeof P); memset(&S, 0, sizeof S); memset(&I, 0, sizeof I);
    for (int i = 0; i < 256; i++) bpal[i] = i % 5 == 0 ? (ULONG)i * 0x010101 : (ULONG)(i * 2654435761UL) & 0xFFFFFF;
    for (int y = 0; y < 480; y++)                   /* gradients, blocks, noise: PixTest's screen */
        for (int x = 0; x < 640; x++)
            scr[y * 640 + x] = y < 160 ? (ULONG)(x * 0x10203 + y * 0x30201) & 0xFFFFFF
                             : y < 320 ? (ULONG)(((x / 16) * 2654435761UL) ^ (y / 8) * 40503UL) & 0xFFFFFF : rnd() & 0xFFFFFF;
    c = clock();
    for (int i = 0; i < 640 * 480; i++) arr[i] = old_px_pen(bpal, 4, scr[i]);
    t_old = since(c);
    c = clock();
    for (int y = 0; y < 480; y++) {                 /* array_read_piece's loop */
        ULONG last = 0; UBYTE pen = 0; int have = 0;
        for (int x = 0; x < 640; x++) {
            ULONG v = scr[y * 640 + x];
            if (!have || v != last) { last = v; pen = pc_memo_pen(&bmemo, bpal, 4, 1, v); have = 1; }
            arr2[y * 640 + x] = pen;
        }
    }
    t_new = since(c);
    printf("bench: nearest pens of 640x480 32-bit: 0.24 %.0f ms, now %.0f ms (%s)\n", t_old, t_new, memcmp(arr, arr2, 640 * 480) ? "DIFFERENT" : "the same");
    if (memcmp(arr, arr2, 640 * 480)) fails++;
    c = clock();
    for (int y = 0; y < 480; y++) {
        ULONG last = 0; UBYTE pen = 0; int have = 0;
        for (int x = 0; x < 640; x++) {
            ULONG v = scr[y * 640 + x];
            if (!have || v != last) { last = v; pen = pc_memo_pen(&bmemo, bpal, 4, 1, v); have = 1; }
            arr2[y * 640 + x] = pen;
        }
    }
    printf("bench: the same again (the memo warm): %.0f ms\n", since(c));

    S.bpp = 4; S.mem = (UBYTE *)scr; S.stride = 640 * 4; S.pal = bpal; S.px_pen = bench_nearest;
    for (int i = 0; i < 40 * 32; i++) icon[i] = rnd() & 0xFFFFFF;
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 40; x++) {
            int dx = x - 20, dy = y - 16;
            if (dx * dx + dy * dy < 225 || (y >= 10 && y < 24 && x >= 4 && x < 36)) icon_mask[y * 6 + x / 8] |= 0x80 >> (x & 7);
        }
    I.bpp = 4; I.mem = (UBYTE *)icon; I.stride = 160; I.pal = bpal; I.px_pen = bench_nearest;
    c = clock();
    for (k = 0; k < 1000; k++) old_blit(&I, 0, 0, &S, (k % 10) * 41, (k / 10 % 10) * 29, 40, 32, 0xE0, 0xFF, icon_mask, 0, 0, 6, 0);
    t_old = since(c) / 10;
    c = clock();
    for (k = 0; k < 1000; k++) pc_blit(&I, 0, 0, &S, (k % 10) * 41, (k / 10 % 10) * 29, 40, 32, 0xE0, 0xFF, icon_mask, 0, 0, 6, 0);
    t_new = since(c) / 10;
    printf("bench: 100 masked 40x32 icons, 32-bit, CPU: 0.24 %.1f ms, now %.1f ms\n", t_old, t_new);

    P.bpp = 0; P.bpr = 40; P.depth = 8;
    for (int p = 0; p < 8; p++) { P.planes[p] = planes[p]; for (int i = 0; i < 40 * 256; i++) planes[p][i] = (UBYTE)rnd(); }
    c = clock();
    for (k = 0; k < 10; k++) old_blit(&P, 0, 0, &S, 7, 5, 320, 256, 0xC0, 0xFF, NULL, 0, 0, 0, 0);
    t_old = since(c) / 10;
    memcpy(arr, scr, 640 * 480);                    /* a sample of what it drew */
    c = clock();
    for (k = 0; k < 10; k++) pc_blit(&P, 0, 0, &S, 7, 5, 320, 256, 0xC0, 0xFF, NULL, 0, 0, 0, 0);
    t_new = since(c) / 10;
    printf("bench: planar 320x256x8 to 32-bit: 0.24 %.1f ms, now %.1f ms (%s)\n", t_old, t_new, memcmp(arr, scr, 640 * 480) ? "DIFFERENT" : "the same");
    if (memcmp(arr, scr, 640 * 480)) fails++;
    c = clock();
    old_blit(&S, 7, 5, &P, 0, 0, 320, 256, 0xC0, 0xFF, NULL, 0, 0, 0, 0);
    t_old = since(c);
    S.px_pen = bench_nearest;
    c = clock();
    pc_blit(&S, 7, 5, &P, 0, 0, 320, 256, 0xC0, 0xFF, NULL, 0, 0, 0, 0);
    t_new = since(c);
    printf("bench: 32-bit to planar 320x256x8: 0.24 %.1f ms, now %.1f ms (no memo in either)\n", t_old, t_new);
    free(scr); free(arr); free(arr2);
}

int main(int argc, char **argv)
{
    test_nearest_and_memo();
    test_runs();
    test_mask_rects();
    test_blits(argc > 1 ? atol(argv[1]) : 60000);
    if (argc > 2 && !strcmp(argv[2], "BENCH")) bench();
    if (fails) { printf("test_pixconv: %d FAILED\n", fails); return 1; }
    printf("test_pixconv: the nearest pens, the memo, the runs, the mask rectangles and the blits are as before\n");
    return 0;
}
