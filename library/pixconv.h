/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * Pixel conversions for screens.c (1 November 2026): a pixel's nearest pen
 * and what keeps its answers. Plain C with no Amiga calls, so the host
 * tests (tests/test_pixconv.c) run them against the per-pixel code they
 * replace: they must give exactly what it gave.
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

#endif
