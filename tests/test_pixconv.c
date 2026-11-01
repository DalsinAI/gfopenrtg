/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * library/pixconv.h against the per-pixel code it replaces in screens.c
 * (openrtg.library 0.24): the nearest pen and its memo, through palette
 * changes; the planes read and written a row at a time; and whole blits,
 * every pair of planar, 8, 16 and 32-bit bitmaps, with and without a mask
 * (cookie), minterms, plane masks, and overlapping copies within one
 * bitmap, compared pixel for pixel and plane for plane with the old loop.
 *   test_pixconv [BLITS]     (default 60000 random blits) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
        CHECK(pc_nearest(pal[t], bpp, px) == old_px_pen(pal[t], bpp, px), "nearest %08x bpp %d", px, bpp);
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
        CHECK(pc_memo_pen(&memo, pal[t], bpp, (UBYTE)t, px) == old_px_pen(pal[t], bpp, px), "memo %08x bpp %d table %d at %d", px, bpp, t, i);
    }
    /* a table beyond the key's room goes uncached */
    CHECK(pc_memo_pen(&memo, pal[3], 4, 200, 0x123456) == old_px_pen(pal[3], 4, 0x123456), "memo table 200");
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
        if (!sf && !df) continue;               /* graphics' own, never OpenRTG's */
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

int main(int argc, char **argv)
{
    test_nearest_and_memo();
    test_blits(argc > 1 ? atol(argv[1]) : 60000);
    if (fails) { printf("test_pixconv: %d FAILED\n", fails); return 1; }
    printf("test_pixconv: the nearest pens, the memo and the blits are as before\n");
    return 0;
}
