/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * The retained surface's core (ogpu_retained.h): the tiles' dirty map and
 * the present's batch. Plain C, no C library. */
#include "ogpu_retained.h"
#include "../../include/gfopengpu/stream.h"

#define M32 0xFFFFFFFFUL
/* Each step is one-to-one in the hash for a given word, and in the word for
 * a given hash (an odd multiplier, mod 2^32), so a change inside one 32-bit
 * word of a tile (one pixel of any format) always changes the tile's hash. */
#define HASH_STEP(h, v) ((((h) ^ (v)) * 0x9E3779B1UL) & M32)
#define HASH_SEED 0x811C9DC5UL

long ogpu_rs_mem(int w, int h)
{
    long n = (long)((w + OGPU_RS_TILE - 1) >> OGPU_RS_TILE_SHIFT) * (long)((h + OGPU_RS_TILE - 1) >> OGPU_RS_TILE_SHIFT);
    return n * (long)sizeof(unsigned long) + ((n + 3) & ~3L);
}

void ogpu_rs_init(struct ogpu_rs *s, const void *pixels, long bpr, int w, int h, int bpp, void *mem)
{
    long i;
    s->pixels = (const unsigned char *)pixels;
    s->bpr = bpr;
    s->w = w;
    s->h = h;
    s->bpp = bpp;
    s->tx = (w + OGPU_RS_TILE - 1) >> OGPU_RS_TILE_SHIFT;
    s->ty = (h + OGPU_RS_TILE - 1) >> OGPU_RS_TILE_SHIFT;
    s->ntiles = (long)s->tx * s->ty;
    s->hash = (unsigned long *)mem;
    s->state = (unsigned char *)mem + s->ntiles * (long)sizeof(unsigned long);
    for (i = 0; i < s->ntiles; i++) {
        s->hash[i] = 0;
        s->state[i] = OGPU_RS_NOHASH;
    }
    s->marked = 0;
    s->all = 1;
    s->dirty = 0;
    s->hashed = 0;
}

void ogpu_rs_all(struct ogpu_rs *s)
{
    s->all = 1;
}

void ogpu_rs_mark(struct ogpu_rs *s, long x, long y, long w, long h)
{
    long x1, y1, tx0, ty0, tx1, ty1, tx, ty;
    if (w < 0 || h < 0) { x = 0; y = 0; w = s->w; h = s->h; }
    x1 = x + w;
    y1 = y + h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > s->w) x1 = s->w;
    if (y1 > s->h) y1 = s->h;
    s->marked = 1;                      /* this present hashes nothing, even when the rectangle is outside */
    if (x >= x1 || y >= y1) return;
    tx0 = x >> OGPU_RS_TILE_SHIFT;
    ty0 = y >> OGPU_RS_TILE_SHIFT;
    tx1 = (x1 - 1) >> OGPU_RS_TILE_SHIFT;
    ty1 = (y1 - 1) >> OGPU_RS_TILE_SHIFT;
    for (ty = ty0; ty <= ty1; ty++)
        for (tx = tx0; tx <= tx1; tx++)
            s->state[ty * s->tx + tx] |= OGPU_RS_DIRTY;
}

unsigned long ogpu_rs_tile_hash(const struct ogpu_rs *s, long t)
{
    int tx = (int)(t % s->tx), ty = (int)(t / s->tx);
    int x = tx << OGPU_RS_TILE_SHIFT, y = ty << OGPU_RS_TILE_SHIFT;
    int w = s->w - x, h = s->h - y, row;
    unsigned long hsh = HASH_SEED, bytes, i;
    const unsigned char *p = s->pixels + (long)y * s->bpr + (long)x * s->bpp;
    if (w > OGPU_RS_TILE) w = OGPU_RS_TILE;
    if (h > OGPU_RS_TILE) h = OGPU_RS_TILE;
    bytes = (unsigned long)w * (unsigned long)s->bpp;
    for (row = 0; row < h; row++, p += s->bpr) {
        i = 0;
        if (!((unsigned long)p & 3)) {
            /* 32-bit words as they lie in memory: a tile is always read the same way */
            const unsigned int *q = (const unsigned int *)(const void *)p;
            unsigned long n = bytes >> 2, k;
            for (k = 0; k < n; k++) hsh = HASH_STEP(hsh, (unsigned long)q[k]);
            i = n << 2;
        }
        for (; i + 4 <= bytes; i += 4)
            hsh = HASH_STEP(hsh, (unsigned long)p[i] << 24 | (unsigned long)p[i + 1] << 16 | (unsigned long)p[i + 2] << 8 | p[i + 3]);
        for (; i < bytes; i++) hsh = HASH_STEP(hsh, (unsigned long)p[i] | 0x100UL);
    }
    return hsh;
}

long ogpu_rs_collect(struct ogpu_rs *s)
{
    long t, n = 0;
    if (s->all) {
        /* everything (the first present, a lost texture): hashed as it goes,
         * so the next present that hashes finds it unchanged */
        for (t = 0; t < s->ntiles; t++) {
            s->hash[t] = ogpu_rs_tile_hash(s, t);
            s->state[t] = OGPU_RS_DIRTY;
        }
        s->hashed += (unsigned long)s->ntiles;
        n = s->ntiles;
    } else if (s->marked) {
        /* the marked tiles only; nothing is hashed */
        for (t = 0; t < s->ntiles; t++)
            if (s->state[t] & OGPU_RS_DIRTY) { s->state[t] |= OGPU_RS_NOHASH; n++; }
    } else {
        for (t = 0; t < s->ntiles; t++) {
            unsigned long hsh = ogpu_rs_tile_hash(s, t);
            s->hashed++;
            if ((s->state[t] & OGPU_RS_NOHASH) || hsh != s->hash[t]) {
                s->hash[t] = hsh;           /* the hash it will have once uploaded */
                s->state[t] = OGPU_RS_DIRTY;
                n++;
            }
        }
    }
    s->dirty = n;
    return n;
}

int ogpu_rs_next_run(const struct ogpu_rs *s, long *at, int *x, int *y, int *w, int *h)
{
    long t = *at, e;
    int tx, x1, y1;
    while (t < s->ntiles && !(s->state[t] & OGPU_RS_DIRTY)) t++;
    if (t >= s->ntiles) { *at = t; return 0; }
    tx = (int)(t % s->tx);
    e = t + 1;
    while (e < s->ntiles && (int)(e % s->tx) != 0 && (s->state[e] & OGPU_RS_DIRTY)) e++;
    *x = tx << OGPU_RS_TILE_SHIFT;
    *y = (int)(t / s->tx) << OGPU_RS_TILE_SHIFT;
    x1 = (tx + (int)(e - t)) << OGPU_RS_TILE_SHIFT;
    y1 = *y + OGPU_RS_TILE;
    if (x1 > s->w) x1 = s->w;
    if (y1 > s->h) y1 = s->h;
    *w = x1 - *x;
    *h = y1 - *y;
    *at = e;
    return (int)(e - t);
}

long ogpu_rs_runs(const struct ogpu_rs *s)
{
    long at = 0, n = 0;
    int x, y, w, h;
    while (ogpu_rs_next_run(s, &at, &x, &y, &w, &h)) n++;
    return n;
}

void ogpu_rs_done(struct ogpu_rs *s)
{
    long t;
    for (t = 0; t < s->ntiles; t++) s->state[t] &= (unsigned char)~OGPU_RS_DIRTY;
    s->marked = 0;
    s->all = 0;
    s->dirty = 0;
}

void ogpu_rs_fail(struct ogpu_rs *s)
{
    ogpu_rs_done(s);
    s->all = 1;
}

/* ---- the batch ---------------------------------------------------------------- */

static long put(unsigned char *buf, long at, unsigned long v)
{
    unsigned char *p = buf + at * 4;
    p[0] = (unsigned char)(v >> 24);
    p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);
    p[3] = (unsigned char)v;
    return at + 1;
}

long ogpu_rs_present_words(const struct ogpu_rs *s, int nclips)
{
    /* 3 SURFACEs, PALETTE, TARGET, COPY a run, TARGET, CLIP and COMPOSITE a clip, FENCE */
    return 3 * 6 + 3 + 2 + 5 * ogpu_rs_runs(s) + 2 + 11L * (nclips > 0 ? nclips : nclips == 0 ? 1 : 0) + 2;
}

static long surface(unsigned char *b, long at, int slot, unsigned long address, unsigned long bpr, int w, int h, int format)
{
    at = put(b, at, OGPU_HDR(OGPU_OP_SURFACE, 6));
    at = put(b, at, (unsigned long)slot);
    at = put(b, at, address);
    at = put(b, at, bpr);
    at = put(b, at, OGPU_XY(w, h));
    return put(b, at, (unsigned long)format);
}

long ogpu_rs_build_present(const struct ogpu_rs *s, const struct ogpu_rs_present *p, unsigned char *buf, long cap)
{
    long at = 0, run = 0;
    int i, n, x, y, w, h;
    if (ogpu_rs_present_words(s, p->nclips) > cap) return -1;
    at = surface(buf, at, 0, p->src, (unsigned long)s->bpr, s->w, s->h, p->format);
    at = surface(buf, at, 1, p->tex, p->tex_bpr, s->w, s->h, p->format);
    if (p->format == OGPU_FMT_INDEX8) {
        at = put(buf, at, OGPU_HDR(OGPU_OP_PALETTE, 3));
        at = put(buf, at, 1);
        at = put(buf, at, p->palette);
    }
    if (s->dirty) {
        at = put(buf, at, OGPU_HDR(OGPU_OP_TARGET, 2));
        at = put(buf, at, 1);
        while (ogpu_rs_next_run(s, &run, &x, &y, &w, &h)) {
            at = put(buf, at, OGPU_HDR(OGPU_OP_COPY, 5));
            at = put(buf, at, 0);
            at = put(buf, at, OGPU_XY(x, y));
            at = put(buf, at, OGPU_XY(x, y));
            at = put(buf, at, OGPU_XY(w, h));
        }
    }
    at = surface(buf, at, 2, p->dst, p->dst_bpr, p->dst_w, p->dst_h, p->dst_format);
    at = put(buf, at, OGPU_HDR(OGPU_OP_TARGET, 2));
    at = put(buf, at, 2);
    n = p->nclips > 0 ? p->nclips : p->nclips == 0 ? 1 : 0;   /* below 0: nothing of the target shows */
    for (i = 0; i < n; i++) {
        at = put(buf, at, OGPU_HDR(OGPU_OP_CLIP, 3));
        if (p->nclips > 0) {
            const short *c = p->clips + i * 4;
            at = put(buf, at, OGPU_XY(c[0], c[1]));
            at = put(buf, at, OGPU_XY(c[2] - c[0] + 1, c[3] - c[1] + 1));
        } else {
            at = put(buf, at, 0);
            at = put(buf, at, 0);           /* the whole target */
        }
        at = put(buf, at, OGPU_HDR(OGPU_OP_COMPOSITE, 8));
        at = put(buf, at, 1);
        at = put(buf, at, 0);
        at = put(buf, at, OGPU_XY(s->w, s->h));
        at = put(buf, at, OGPU_XY(p->x, p->y));
        at = put(buf, at, OGPU_XY(p->w, p->h));
        at = put(buf, at, 255);
        at = put(buf, at, p->comp_flags);
    }
    at = put(buf, at, OGPU_HDR(OGPU_OP_FENCE, 2));
    at = put(buf, at, p->fence);
    return at;
}
