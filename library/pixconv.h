/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * Pixel conversions for screens.c (1 November 2026): a pixel's nearest pen
 * and what keeps its answers, and the CPU's blits between planar and chunky
 * bitmaps a row at a time. Plain C with no Amiga calls, so the host tests
 * (tests/test_pixconv.c) run them against the per-pixel code they replace:
 * they must draw exactly what it drew.
 */
#ifndef ORTG_PIXCONV_H
#define ORTG_PIXCONV_H

#include <exec/types.h>

/* ---- a colour as a 16 or 32-bit pixel, as screens.h's ortg_encode and ortg_decode ---- */

static inline ULONG pc_encode(UBYTE bpp, ULONG rgb)
{
    if (bpp == 2) return ((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x07E0) | ((rgb >> 3) & 0x001F);
    return rgb & 0xFFFFFF;
}
static inline ULONG pc_decode(UBYTE bpp, ULONG px)
{
    if (bpp == 2) {
        ULONG r = (px >> 11) & 31, g = (px >> 5) & 63, b = px & 31;
        return (r << 19 | (r >> 2) << 16) | (g << 10 | (g >> 4) << 8) | (b << 3 | b >> 2);
    }
    return px & 0xFFFFFF;
}

/* The pen of pal (256 colours, 0x00RRGGBB) nearest a 16 or 32-bit pixel's
 * colour: the first of the nearest, each pen's colour as the pixel's format
 * holds it (on 16-bit, cut to RGB565 and widened again). Up to 256 colours
 * a pixel: what the memo below saves. A colour stops being measured once
 * its red, or red and green, are already as far as the nearest so far: it
 * can't then be nearer, so the answer is the full search's. */
static inline UBYTE pc_nearest(const ULONG *pal, UBYTE bpp, ULONG px)
{
    ULONG rgb = pc_decode(bpp, px), best = 0, dist = (ULONG)~0UL;
    LONG r = (LONG)((rgb >> 16) & 255), g = (LONG)((rgb >> 8) & 255), b = (LONG)(rgb & 255);
    for (ULONG i = 0; i < 256; i++) {
        ULONG c = pal[i], d2;
        LONG dr, dg, db;
        if (bpp == 2) {                             /* pc_decode(pc_encode(c)), a channel at a time */
            ULONG r5 = (c >> 19) & 31, g6 = (c >> 10) & 63, b5 = (c >> 3) & 31;
            dr = (LONG)(r5 << 3 | r5 >> 2) - r; dg = (LONG)(g6 << 2 | g6 >> 4) - g; db = (LONG)(b5 << 3 | b5 >> 2) - b;
        } else {
            dr = (LONG)((c >> 16) & 255) - r; dg = (LONG)((c >> 8) & 255) - g; db = (LONG)(c & 255) - b;
        }
        d2 = (ULONG)(dr * dr);
        if (d2 >= dist) continue;
        d2 += (ULONG)(dg * dg);
        if (d2 >= dist) continue;
        d2 += (ULONG)(db * db);
        if (d2 < dist) { dist = d2; best = i; if (!d2) break; }
    }
    return (UBYTE)best;
}

/* ---- the nearest pens found, kept --------------------------------------------------------
 *
 * PC_MEMO answers, by a hash of the pixel and its pen table. An entry is
 * two longwords: the key (the pixel's colour bits, the table in bits 24-27,
 * bit 28 for 16-bit), and the pen with the generation it was found in
 * (bits 8-30) and bit 31 set. Changing any pen table moves the generation
 * on (pc_memo_forget), so every answer found before is not used again. Two
 * tasks may look up and fill at once: the key is written last and read
 * before and after the pen, so a half-written entry is never taken. */
#define PC_MEMO 256
#define PC_MEMO_TABLES 16           /* the tables a key has room for (ORTG_PEN_TABLES) */
struct pc_memo_e { ULONG key, val; };
struct pc_memo { struct pc_memo_e e[PC_MEMO]; ULONG gen; };

#define PC_MEMO_VALID 0x80000000UL
#define PC_MEMO_GENS  0x7FFFFFUL

static inline ULONG pc_memo_key(UBYTE bpp, UBYTE table, ULONG px)
{
    return (bpp == 2 ? (px & 0xFFFF) | 0x10000000UL : px & 0xFFFFFF) | (ULONG)table << 24;
}

static inline ULONG pc_memo_hash(ULONG key)
{
    return (key ^ (key >> 8) ^ (key >> 16) ^ (key >> 21)) & (PC_MEMO - 1);
}

/* pc_nearest(pal, bpp, px), pal being pen table `table`. */
static inline UBYTE pc_memo_pen(volatile struct pc_memo *m, const ULONG *pal, UBYTE bpp, UBYTE table, ULONG px)
{
    volatile struct pc_memo_e *e;
    ULONG key, gen, v;
    UBYTE pen;
    if (table >= PC_MEMO_TABLES) return pc_nearest(pal, bpp, px);
    key = pc_memo_key(bpp, table, px);
    e = &m->e[pc_memo_hash(key)];
    gen = m->gen;                                   /* before the table is read */
    if (e->key == key) {
        v = e->val;
        if (e->key == key && (v & ~255UL) == (PC_MEMO_VALID | gen << 8)) return (UBYTE)v;
    }
    pen = pc_nearest(pal, bpp, px);
    e->key = (ULONG)~0UL;
    e->val = PC_MEMO_VALID | gen << 8 | pen;
    e->key = key;
    return pen;
}

/* A pen table changed (call before and after writing it): what was found is forgotten. */
static inline void pc_memo_forget(volatile struct pc_memo *m)
{
    ULONG gen = (m->gen + 1) & PC_MEMO_GENS;
    if (!gen)                                       /* round again: no entry may carry an old 0 */
        for (int i = 0; i < PC_MEMO; i++) m->e[i].key = (ULONG)~0UL;
    m->gen = gen;
}

/* ---- planes, a row at a time ----------------------------------------------------------- */

/* 8 bits of a row of bits from bit x, the first in bit 7, of which n (1-8)
 * are wanted: the rest are 0. Only the bytes the wanted bits are in are read. */
static inline UBYTE pc_bits8(const UBYTE *row, LONG x, LONG n)
{
    const UBYTE *q = row + (x >> 3);
    int sh = (int)(x & 7);
    UBYTE v = (UBYTE)(q[0] << sh);
    if (sh && n > 8 - sh) v |= (UBYTE)(q[1] >> (8 - sh));
    if (n < 8) v &= (UBYTE)(0xFF << (8 - n));
    return v;
}

/* A nibble's 4 bits as 4 bytes of 0 or 1, the nibble's top bit in the first
 * byte in memory: 8 pixels of a plane become 2 longword ORs. */
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define PC_NIB(n) (((n) >> 3 & 1UL) | ((n) >> 2 & 1UL) << 8 | ((n) >> 1 & 1UL) << 16 | ((n) & 1UL) << 24)
#else
#define PC_NIB(n) (((n) >> 3 & 1UL) << 24 | ((n) >> 2 & 1UL) << 16 | ((n) >> 1 & 1UL) << 8 | ((n) & 1UL))
#endif
static const ULONG pc_nib[16] = {
    PC_NIB(0), PC_NIB(1), PC_NIB(2), PC_NIB(3), PC_NIB(4), PC_NIB(5), PC_NIB(6), PC_NIB(7),
    PC_NIB(8), PC_NIB(9), PC_NIB(10), PC_NIB(11), PC_NIB(12), PC_NIB(13), PC_NIB(14), PC_NIB(15)
};

/* The pixels of a blit are done PC_CH at a time (a multiple of 8). */
#define PC_CH 32

/* n (1 to PC_CH) pens of a planar row from pixel x, into pens (as bytes),
 * as graphics reads them: plane p for p below depth and 8; a NULL plane is
 * all 0, a plane of -1 all 1. off: the row's offset in each plane. */
static inline void pc_planar_pens(ULONG *pens, UBYTE *const *planes, int depth, LONG off, LONG x, LONG n)
{
    LONG groups = (n + 7) >> 3;
    for (LONG g = 0; g < 2 * groups; g++) pens[g] = 0;
    for (int p = 0; p < depth && p < 8; p++) {
        const UBYTE *pl = planes[p];
        if (!pl) continue;
        if (pl == (const UBYTE *)-1) {
            for (LONG g = 0; g < 2 * groups; g++) pens[g] |= pc_nib[15] << p;
            continue;
        }
        pl += off;
        for (LONG g = 0; g < groups; g++) {
            UBYTE b = pc_bits8(pl, x + 8 * g, n - 8 * g < 8 ? n - 8 * g : 8);
            pens[2 * g] |= pc_nib[b >> 4] << p;
            pens[2 * g + 1] |= pc_nib[b & 15] << p;
        }
    }
}

/* Bit `bit` of n values into a plane's row from pixel x, where on (when
 * not NULL) is not 0: each byte read and written once. */
static inline void pc_plane_put(UBYTE *row, LONG x, LONG n, const UBYTE *vals, const UBYTE *on, UBYTE bit)
{
    LONG k = 0;
    while (k < n) {
        UBYTE *q = row + (x >> 3);
        UBYTE b = (UBYTE)(0x80 >> (x & 7)), wm = 0, v = 0;
        do {
            if (!on || on[k]) { wm |= b; if (vals[k] & bit) v |= b; }
            k++; x++; b >>= 1;
        } while (k < n && b);
        if (wm) *q = (UBYTE)((*q & ~wm) | (v & wm));
    }
}

/* The runs of set bits among n bits of a row from bit x, from offset *at:
 * up to max runs as [start, end) offsets, two WORDs each; *at moves past the
 * last run given (n when the row is done). Whole zero bytes go by a byte at
 * a time. */
static inline int pc_runs(const UBYTE *row, LONG x, LONG n, LONG *at, WORD *runs, int max)
{
    int k = 0;
    LONG i = *at;
    while (i < n) {
        LONG s;
        UBYTE b = pc_bits8(row, x + i, n - i < 8 ? n - i : 8);
        if (!b) { i += n - i < 8 ? n - i : 8; continue; }
        while (!(b & 0x80)) { b <<= 1; i++; }
        if (k == max) break;
        s = i;
        for (;;) {                                  /* to the end of the run */
            b = pc_bits8(row, x + i, n - i < 8 ? n - i : 8);
            if (b == 0xFF && n - i >= 8) { i += 8; if (i >= n) break; continue; }
            while (b & 0x80) { b <<= 1; i++; }
            break;
        }
        runs[2 * k] = (WORD)s; runs[2 * k + 1] = (WORD)i;
        k++;
    }
    *at = i < n ? i : n;
    return k;
}

/* The set bits of a w x h box of a mask (cmod bytes a row, the box's first
 * at bit cx of row cy) as rectangles, offsets in the box: one for each run
 * of set bits, and runs alike in rows one after another as one taller
 * rectangle (an icon's body is one). emit returns 0 to stop: then this does. */
#define PC_RUNS 8
static inline int pc_mask_rects(const UBYTE *cookie, LONG cx, LONG cy, LONG cmod, LONG w, LONG h,
                                int (*emit)(void *ctx, LONG x, LONG y, LONG w, LONG h), void *ctx)
{
    WORD prev[2 * PC_RUNS], cur[2 * PC_RUNS];
    int np = 0, nc;
    LONG py = 0, ph = 0;
    if (w > 0x7FFF) return 0;
    for (LONG j = 0; j <= h; j++) {
        const UBYTE *row = cookie;
        LONG at = 0;
        int whole = 0;
        nc = 0;
        if (j < h) {
            row = cookie + (cy + j) * cmod;
            nc = pc_runs(row, cx, w, &at, cur, PC_RUNS);
            whole = at >= w;
            if (whole && ph && nc == np) {          /* as the rows before */
                int k = 0;
                while (k < 2 * nc && cur[k] == prev[k]) k++;
                if (k == 2 * nc) { ph++; continue; }
            }
        }
        for (int k = 0; k < np; k++)
            if (!emit(ctx, prev[2 * k], py, prev[2 * k + 1] - prev[2 * k], ph)) return 0;
        np = 0; ph = 0;
        if (j == h) break;
        if (whole) {
            for (int k = 0; k < 2 * nc; k++) prev[k] = cur[k];
            np = nc; py = j; ph = 1;
            continue;
        }
        for (;;) {                                  /* more runs than kept: this row's, as they come */
            for (int k = 0; k < nc; k++)
                if (!emit(ctx, cur[2 * k], j, cur[2 * k + 1] - cur[2 * k], 1)) return 0;
            if (at >= w) break;
            nc = pc_runs(row, cx, w, &at, cur, PC_RUNS);
        }
    }
    return 1;
}

/* ---- blits on the CPU: a row at a time ---------------------------------------------------- */

/* A bitmap as pc_blit sees it: chunky (bpp 1, 2 or 4) or planar (bpp 0). */
struct pc_surf {
    UBYTE bpp;
    UBYTE *mem;                 /* chunky: the pixels, stride bytes a row */
    ULONG stride;
    const ULONG *pal;           /* chunky 16 and 32-bit: its pens' colours (as a target) */
    UBYTE (*px_pen)(const struct pc_surf *s, ULONG px);   /* chunky 16 and 32-bit: a pixel's nearest pen (as a source) */
    const void *owner;          /* for px_pen */
    UBYTE *planes[8];           /* planar */
    LONG bpr;
    int depth;
};

static inline ULONG pc_pen_px(const struct pc_surf *d, ULONG pen)
{
    return d->bpp == 1 ? (pen & 255) : pc_encode(d->bpp, d->pal[pen & 255]);
}

/* BltBitMap's minterm, bit by bit: B the source, C the destination. */
static inline ULONG pc_minterm(UBYTE m, ULONG s, ULONG d)
{
    ULONG r = 0;
    if (m & 0x80) r |= s & d;
    if (m & 0x40) r |= s & ~d;
    if (m & 0x20) r |= ~s & d;
    if (m & 0x10) r |= ~s & ~d;
    return r;
}

static inline ULONG pc_get(const UBYTE *row, UBYTE bpp, LONG x)
{
    if (bpp == 1) return row[x];
    if (bpp == 2) return ((const UWORD *)row)[x];
    return ((const ULONG *)row)[x];
}
static inline void pc_put(UBYTE *row, UBYTE bpp, LONG x, ULONG v)
{
    if (bpp == 1) row[x] = (UBYTE)v;
    else if (bpp == 2) ((UWORD *)row)[x] = (UWORD)v;
    else ((ULONG *)row)[x] = v;
}

/* The last pixel a blit found the pen of. */
struct pc_last { ULONG px; UBYTE pen, have; };

static inline UBYTE pc_pen_of(const struct pc_surf *s, ULONG px, struct pc_last *l)
{
    if (!l->have || l->px != px) { l->px = px; l->pen = s->px_pen(s, px); l->have = 1; }
    return l->pen;
}

/* n source pixels from x, y as the destination's: a pen becomes its colour
 * on 16 and 32-bit, a colour its nearest pen on 8-bit and planar, a colour
 * of the other depth is encoded again. */
static inline void pc_fetch(const struct pc_surf *S, const struct pc_surf *D, LONG x, LONG y, LONG n, ULONG *sv, struct pc_last *l)
{
    const UBYTE *row;
    LONG k;
    if (!S->bpp) {
        ULONG pl[PC_CH / 4];
        const UBYTE *pn = (const UBYTE *)pl;
        pc_planar_pens(pl, S->planes, S->depth, y * S->bpr, x, n);
        if (D->bpp > 1) for (k = 0; k < n; k++) sv[k] = pc_pen_px(D, pn[k]);
        else for (k = 0; k < n; k++) sv[k] = pn[k];
        return;
    }
    row = S->mem + y * S->stride;
    if (S->bpp == 1) {
        const UBYTE *p = row + x;
        if (D->bpp > 1) for (k = 0; k < n; k++) sv[k] = pc_pen_px(D, p[k]);
        else for (k = 0; k < n; k++) sv[k] = p[k];
        return;
    }
    for (k = 0; k < n; k++) {
        ULONG v = pc_get(row, S->bpp, x + k);
        if (D->bpp <= 1) v = pc_pen_of(S, v, l);
        else if (D->bpp != S->bpp) v = pc_encode(D->bpp, pc_decode(S->bpp, v));
        sv[k] = v;
    }
}

/* A rectangle from S to D (one of them chunky), as GFOpenRTG's per-pixel
 * loop drew it: through cookie (a plane of bits, cmod bytes a row, the
 * rectangle's first at bit cx of row cy) the source is copied where a bit
 * is set, whatever the minterm; else the minterm; on 8-bit and planar
 * targets the mask picks the bits (planes) written, on 16 and 32-bit a mask
 * of 0 writes nothing. A minterm that ignores the source works on pens on
 * 16 and 32-bit: a clear is pen 0, a set pen 255. same: S and D are one
 * bitmap, so the rows and pixels go in the order that reads each source
 * pixel before it is written. Each run of PC_CH pixels is read whole before
 * any of it is written, which that order makes the same. */
static inline void pc_blit(const struct pc_surf *S, LONG sx, LONG sy, const struct pc_surf *D, LONG dx, LONG dy, LONG w, LONG h,
                    UBYTE m, UBYTE mask, const UBYTE *cookie, LONG cx, LONG cy, LONG cmod, int same)
{
    ULONG sv[PC_CH], dpl[PC_CH / 4], onl[PC_CH / 4];
    UBYTE rv[PC_CH];
    const UBYTE *on = (const UBYTE *)onl, *dp = (const UBYTE *)dpl;
    struct pc_last last = { 0, 0, 0 };
    int down = !(same && sy < dy), right = !(same && sy == dy && sx < dx);
    int ign = ((m >> 7) & 1) == ((m >> 5) & 1) && ((m >> 6) & 1) == ((m >> 4) & 1);
    int cpy = !cookie && (m & 0xF0) == 0xC0 && D->bpp && (D->bpp == 1 ? mask == 0xFF : mask != 0);
    int nosrc = !cookie && D->bpp > 1 && ign;       /* the source isn't read */
    ULONG ones = D->bpp == 2 ? 0xFFFF : 0xFFFFFF, clear = 0, set = 0;
    LONG chunks = (w + PC_CH - 1) / PC_CH;
    if (D->bpp > 1) { clear = pc_pen_px(D, 0); set = pc_pen_px(D, 255); }
    for (LONG j = 0; j < h; j++) {
        LONG yy = down ? j : h - 1 - j;
        for (LONG c = 0; c < chunks; c++) {
            LONG i0 = (right ? c : chunks - 1 - c) * PC_CH, n = w - i0 < PC_CH ? w - i0 : PC_CH, k;
            if (cookie) {
                const UBYTE *cr = cookie + (cy + yy) * cmod;
                UBYTE any = 0;
                for (LONG g = 0; g < n; g += 8) {
                    UBYTE b = pc_bits8(cr, cx + i0 + g, n - g < 8 ? n - g : 8);
                    any |= b;
                    onl[g / 4] = pc_nib[b >> 4];
                    onl[g / 4 + 1] = pc_nib[b & 15];
                }
                if (!any) continue;
            }
            if (!nosrc) pc_fetch(S, D, sx + i0, sy + yy, n, sv, &last);
            if (D->bpp) {
                UBYTE *row = D->mem + (dy + yy) * D->stride;
                LONG x = dx + i0;
                if (cpy) {
                    if (D->bpp == 1) { UBYTE *q = row + x; for (k = 0; k < n; k++) q[k] = (UBYTE)sv[k]; }
                    else if (D->bpp == 2) { UWORD *q = (UWORD *)row + x; for (k = 0; k < n; k++) q[k] = (UWORD)sv[k]; }
                    else { ULONG *q = (ULONG *)row + x; for (k = 0; k < n; k++) q[k] = sv[k]; }
                    continue;
                }
                for (k = 0; k < n; k++) {
                    ULONG d, r;
                    if (cookie && !on[k]) continue;
                    d = pc_get(row, D->bpp, x + k);
                    if (cookie) r = sv[k];
                    else if (D->bpp != 1 && ign) {
                        switch (m & 0xF0) {
                        case 0x00: r = clear; break;
                        case 0xF0: r = set; break;
                        case 0x50: r = d ^ ones; break;
                        default: r = d; break;
                        }
                    } else r = pc_minterm(m, sv[k], d);
                    if (D->bpp == 1) r = (d & ~(ULONG)mask) | (r & mask);
                    else if (!mask) r = d;
                    else if (D->bpp == 2) r &= 0xFFFF;
                    pc_put(row, D->bpp, x + k, r);
                }
            } else {
                LONG off = (dy + yy) * D->bpr;
                if (!cookie) pc_planar_pens(dpl, D->planes, D->depth, off, dx + i0, n);
                for (k = 0; k < n; k++) rv[k] = (UBYTE)(cookie ? sv[k] : pc_minterm(m, sv[k], dp[k]));
                for (int p = 0; p < D->depth && p < 8; p++) {
                    UBYTE *pl = D->planes[p];
                    if (!(mask & (1 << p)) || !pl || pl == (UBYTE *)-1) continue;
                    pc_plane_put(pl + off, dx + i0, n, rv, cookie ? on : (const UBYTE *)0, (UBYTE)(1 << p));
                }
            }
        }
    }
}

#endif
