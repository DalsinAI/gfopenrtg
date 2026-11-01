/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * ScrollRaster on the host: library/scroll_plan.h's sp_scroll and sp_move,
 * the code screens.c's ScrollRaster runs, on a model of a layer. A screen
 * bitmap holds a layer cut into ClipRects, some showing (their pixels in the
 * screen), some hidden: in a smart refresh layer each hidden one has a
 * backing store bitmap of its own (x origin aligned to 16 pixels, as layers
 * makes them), in a simple refresh layer they keep nothing. The test's own
 * fill, damage, blit and move do what screens.c's do (a fill and a copy
 * through the ClipRects, each copy right for overlap). Random layers, cuts,
 * boxes (in it, past its edges, from negative coordinates) and distances
 * (every direction, up to past the box), and the same with no layer.
 *
 * Each result is compared with what ScrollRaster must give, worked out a
 * pixel at a time: the box's contents moved as one (AROS's MoveRaster), with
 * OS 3.2's rules as tools/ortg_scroll_exact.c measured them on
 * graphics.library 47.100: the box cut to the layer at the right and bottom;
 * a distance as wide or high as the box filling all of it with the B pen; a
 * simple refresh layer leaving what would come from a hidden part as it was,
 * with no damage, and a box reaching outside it damaging all of the box in
 * it; and nothing outside the layer touched. The same cases through the old
 * code (each ClipRect scrolled on its own, main before 1 Nov 2026) must
 * fail, to show the test sees the stripes. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../library/scroll_plan.h"

#define SW 112                  /* the screen */
#define SH 88
#define MAXP 72                 /* cut() makes at most 2^6 pieces */
#define PEN 0xB9B9u             /* the B pen */

struct cr { struct sp_box b; int hidden; unsigned *store; long sw, tx, ty; };

static unsigned screen[SH][SW], before[SH][SW];
static struct cr crs[MAXP];
static int ncr, simple, layered;
static long LX, LY, LW, LH;     /* the layer's place and size on the screen */
static struct sp_box damage[8];
static int ndamage;
static unsigned long seed = 20261101;

static unsigned rnd(unsigned n) { seed = seed * 1103515245UL + 12345UL; return (unsigned)((seed >> 16) % n); }

static void cut(struct sp_box b, int d)
{
    if (d == 0 || rnd(5) == 0 || (b.x1 - b.x0 < 2 && b.y1 - b.y0 < 2)) {
        crs[ncr].b = b;
        crs[ncr].hidden = rnd(3) == 0;
        ncr++;
        return;
    }
    if ((rnd(2) && b.x1 - b.x0 >= 2) || b.y1 - b.y0 < 2) {
        long m = b.x0 + 1 + (long)rnd((unsigned)(b.x1 - b.x0));
        struct sp_box l = b, r = b;
        l.x1 = m - 1; r.x0 = m;
        cut(l, d - 1); cut(r, d - 1);
    } else {
        long m = b.y0 + 1 + (long)rnd((unsigned)(b.y1 - b.y0));
        struct sp_box t = b, u = b;
        t.y1 = m - 1; u.y0 = m;
        cut(t, d - 1); cut(u, d - 1);
    }
}

/* Where screen point (x, y) of ClipRect c is kept, or NULL. */
static unsigned *at(const struct cr *c, long x, long y)
{
    if (!c->hidden) return &screen[y][x];
    if (!c->store) return NULL;
    return &c->store[(y + c->ty) * c->sw + (x + c->tx)];
}

static int cr_of(long x, long y)
{
    for (int i = 0; i < ncr; i++)
        if (x >= crs[i].b.x0 && x <= crs[i].b.x1 && y >= crs[i].b.y0 && y <= crs[i].b.y1) return i;
    return -1;
}

/* ---- the test's own blit, fill, damage and move, as screens.c's ---- */

/* bm: a hidden ClipRect (its backing store), or NULL for the screen. */
static void t_blit(void *ctx, void *sbm, long sx, long sy, void *dbm, long dx, long dy, long w, long h)
{
    static unsigned tmp[SH * SW];
    const struct cr *s = sbm, *d = dbm;
    (void)ctx;
    for (long j = 0; j < h; j++)
        for (long i = 0; i < w; i++)
            tmp[j * w + i] = s ? s->store[(sy + j) * s->sw + sx + i] : screen[sy + j][sx + i];
    for (long j = 0; j < h; j++)
        for (long i = 0; i < w; i++) {
            if (d) d->store[(dy + j) * d->sw + dx + i] = tmp[j * w + i];
            else screen[dy + j][dx + i] = tmp[j * w + i];
        }
}

static void t_fill(void *ctx, const struct sp_box *r)
{
    (void)ctx;
    for (long y = r->y0; y <= r->y1; y++)
        for (long x = r->x0; x <= r->x1; x++) {
            long sx = x + LX, sy = y + LY;
            int c;
            unsigned *p;
            if (!layered) { if (sx >= 0 && sy >= 0 && sx < SW && sy < SH) screen[sy][sx] = PEN; continue; }
            c = cr_of(sx, sy);
            p = c < 0 ? NULL : at(&crs[c], sx, sy);
            if (p) *p = PEN;
        }
}

static void t_damage(void *ctx, const struct sp_box *r)
{
    (void)ctx;
    if (ndamage < 8) damage[ndamage++] = *r;
}

/* screens.c's scroll_where: a ClipRect's piece of b that has pixels */
static int t_where(int i, const struct sp_box *b, struct sp_piece *p)
{
    struct cr *c = &crs[i];
    if (c->hidden && !c->store) return 0;
    p->bm = c->hidden ? (void *)c : NULL;
    p->tx = c->hidden ? c->tx : 0; p->ty = c->hidden ? c->ty : 0;
    return sp_and(&c->b, b, &p->box);
}

static void t_move(void *ctx, const struct sp_box *r, long dx, long dy)
{
    struct sp_piece p[MAXP];
    unsigned short ord[2 * MAXP];
    struct sp_box b;
    int n = 0;
    (void)ctx;
    if (!layered) {
        long adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
        t_blit(NULL, NULL, r->x0 + (dx > 0 ? dx : 0), r->y0 + (dy > 0 ? dy : 0), NULL, r->x0 + (dx < 0 ? -dx : 0),
               r->y0 + (dy < 0 ? -dy : 0), r->x1 - r->x0 + 1 - adx, r->y1 - r->y0 + 1 - ady);
        return;
    }
    b.x0 = r->x0 + LX; b.y0 = r->y0 + LY; b.x1 = r->x1 + LX; b.y1 = r->y1 + LY;
    for (int i = 0; i < ncr; i++)
        if (t_where(i, &b, &p[n])) n++;
    sp_move(p, n, &b, dx, dy, ord, ord + n, t_blit, NULL);
}

/* ---- main's ScrollRaster before 1 Nov 2026: each ClipRect on its own ---- */

static void old_piece(struct cr *c, struct sp_box q, long dx, long dy)
{
    long w = q.x1 - q.x0 + 1, h = q.y1 - q.y0 + 1;
    long mw = w - (dx < 0 ? -dx : dx), mh = h - (dy < 0 ? -dy : dy);
    void *bm = c && c->hidden ? (void *)c : NULL;
    long tx = c && c->hidden ? c->tx : 0, ty = c && c->hidden ? c->ty : 0;
    if (mw > 0 && mh > 0)
        t_blit(NULL, bm, q.x0 + (dx > 0 ? dx : 0) + tx, q.y0 + (dy > 0 ? dy : 0) + ty, bm,
               q.x0 + (dx < 0 ? -dx : 0) + tx, q.y0 + (dy < 0 ? -dy : 0) + ty, mw, mh);
    for (long y = q.y0; y <= q.y1; y++)
        for (long x = q.x0; x <= q.x1; x++)
            if (mw <= 0 || mh <= 0 || (dy > 0 && y > q.y1 - dy) || (dy < 0 && y < q.y0 - dy) ||
                (dx > 0 && x > q.x1 - dx) || (dx < 0 && x < q.x0 - dx)) {
                unsigned *p = c ? at(c, x, y) : &screen[y][x];
                *p = PEN;
            }
}

static void old_scroll(long dx, long dy, long x0, long y0, long x1, long y1)
{
    struct sp_box b = { x0, y0, x1, y1 }, s = { 0, 0, SW - 1, SH - 1 }, q;
    if (!layered) { if (sp_and(&b, &s, &q)) old_piece(NULL, q, dx, dy); return; }
    b.x0 += LX; b.x1 += LX; b.y0 += LY; b.y1 += LY;
    for (int i = 0; i < ncr; i++)
        if ((!crs[i].hidden || crs[i].store) && sp_and(&crs[i].b, &b, &q)) old_piece(&crs[i], q, dx, dy);
}

/* ---- one case ---- */

static unsigned logical0[SH][SW], want[SH][SW];  /* each layer (or bitmap) point's pixel, before and as it must be */
static int known0[SH][SW];                       /* 0: in a simple refresh layer's hidden part (nothing kept) */

static int one_case(int n, int old, int verbose)
{
    long w, h, dx, dy, x0, y0, x1, y1, bad = 0;
    struct sp_ops ops = { t_damage, t_fill, t_move };
    int wdamage = 0;
    struct sp_box wd = { 0, 0, -1, -1 };

    /* the layer, its ClipRects and their backing stores */
    layered = rnd(8) != 0;
    simple = layered && rnd(3) == 0;
    ncr = 0;
    LW = layered ? 8 + (long)rnd(SW - 8) : SW; LH = layered ? 8 + (long)rnd(SH - 8) : SH;
    LX = layered ? (long)rnd((unsigned)(SW - LW + 1)) : 0; LY = layered ? (long)rnd((unsigned)(SH - LH + 1)) : 0;
    if (layered) {
        struct sp_box lb = { LX, LY, LX + LW - 1, LY + LH - 1 };
        cut(lb, 6);
    }
    for (int i = 0; i < ncr; i++) {
        struct cr *c = &crs[i];
        c->store = NULL;
        if (c->hidden && !simple) {
            c->sw = (c->b.x1 - c->b.x0 + 1) + (c->b.x0 & 15);
            c->store = calloc((size_t)(c->sw * (c->b.y1 - c->b.y0 + 1)), sizeof *c->store);
            c->tx = (c->b.x0 & 15) - c->b.x0; c->ty = -c->b.y0;
        }
    }
    for (long y = 0; y < SH; y++)
        for (long x = 0; x < SW; x++) screen[y][x] = 0x10000u + (unsigned)(y * SW + x);    /* outside the layer */
    for (long y = 0; y < LH; y++)
        for (long x = 0; x < LW; x++) {
            int c = layered ? cr_of(LX + x, LY + y) : -1;
            unsigned *p = c < 0 ? &screen[y][x] : at(&crs[c], LX + x, LY + y);
            logical0[y][x] = 1 + (unsigned)(y * SW + x);
            known0[y][x] = p != NULL;
            if (p) *p = logical0[y][x];
        }
    memcpy(before, screen, sizeof screen);
    ndamage = 0;

    /* the call */
    do {
        int kind = (int)rnd(4);
        long rx = LW + 8, ry = LH + 8;
        dx = kind == 1 ? 0 : (long)rnd((unsigned)(2 * rx + 1)) - rx;
        dy = kind == 0 ? 0 : (long)rnd((unsigned)(2 * ry + 1)) - ry;
        if (rnd(3)) { dx /= 4; dy /= 4; }                       /* mostly short */
    } while (!dx && !dy);
    x0 = (long)rnd((unsigned)LW) - (rnd(6) ? 0 : 6); y0 = (long)rnd((unsigned)LH) - (rnd(6) ? 0 : 6);
    x1 = x0 + (long)rnd((unsigned)(LW - x0 + (rnd(5) ? 0 : 10))); y1 = y0 + (long)rnd((unsigned)(LH - y0 + (rnd(5) ? 0 : 10)));
    w = LW - 1; h = LH - 1;
    if (old) old_scroll(dx, dy, x0, y0, x1, y1);
    else sp_scroll(dx, dy, x0, y0, x1, y1, layered, w, h, simple, &ops, NULL);

    /* what it must give, a pixel at a time */
    {
        long mx = x1 > w ? w : x1, my = y1 > h ? h : y1, bx0 = x0, by0 = y0;
        long adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
        int none, all;
        if (!layered) { if (bx0 < 0) bx0 = 0; if (by0 < 0) by0 = 0; }
        none = bx0 > mx || by0 > my;
        all = !none && (adx > mx - bx0 || ady > my - by0);
        if (!none && simple && (x0 < 0 || y0 < 0 || x1 > w || y1 > h)) {
            wdamage = 1;
            wd.x0 = x0 < 0 ? 0 : x0; wd.y0 = y0 < 0 ? 0 : y0; wd.x1 = mx; wd.y1 = my;
        }
        for (long y = 0; y < LH; y++)
            for (long x = 0; x < LW; x++) {
                long sx = x + dx, sy = y + dy;
                unsigned v = logical0[y][x];
                if (!none && x >= bx0 && x <= mx && y >= by0 && y <= my) {
                    if (all || sx < bx0 || sx > mx || sy < by0 || sy > my) v = PEN;    /* uncovered */
                    else if (sx >= 0 && sy >= 0 && known0[sy][sx]) v = logical0[sy][sx];
                    /* else from a simple refresh layer's hidden part, or from outside the layer: as it was */
                }
                want[y][x] = v;
            }
    }

    /* what it gave */
    for (long y = 0; y < SH; y++)
        for (long x = 0; x < SW; x++) {
            long lx = x - LX, ly = y - LY;
            unsigned got, w8;
            if (lx >= 0 && ly >= 0 && lx < LW && ly < LH) {
                int c = layered ? cr_of(x, y) : -1;
                unsigned *p = c < 0 ? &screen[y][x] : at(&crs[c], x, y);
                if (!p) continue;                                   /* nothing kept there */
                got = *p; w8 = want[ly][lx];
            } else { got = screen[y][x]; w8 = before[y][x]; }        /* outside the layer: untouched */
            if (got != w8) {
                if (!bad && verbose)
                    printf("case %d (%s): box %ld,%ld-%ld,%ld by %ld,%ld in %ldx%ld, %d ClipRects: screen %ld,%ld is %x, not %x\n",
                           n, !layered ? "no layer" : simple ? "simple" : "smart", x0, y0, x1, y1, dx, dy, LW, LH, ncr, x, y, got, w8);
                bad++;
            }
        }
    if (!old && ((ndamage > 0) != wdamage || (ndamage && (ndamage != 1 || memcmp(&damage[0], &wd, sizeof wd))))) {
        if (!bad && verbose)
            printf("case %d (%s): box %ld,%ld-%ld,%ld by %ld,%ld in %ldx%ld: %d damage rectangles, not %d (%ld,%ld-%ld,%ld)\n",
                   n, !layered ? "no layer" : simple ? "simple" : "smart", x0, y0, x1, y1, dx, dy, LW, LH,
                   ndamage, wdamage, wd.x0, wd.y0, wd.x1, wd.y1);
        bad++;
    }
    for (int i = 0; i < ncr; i++) free(crs[i].store);
    return bad != 0;
}

int main(void)
{
    int cases = 5000, fails = 0, oldfails = 0;
    unsigned long s0 = seed;
    for (int n = 0; n < cases; n++) fails += one_case(n, 0, fails < 10);
    seed = s0;                                                      /* the same cases through the old code */
    for (int n = 0; n < cases; n++) oldfails += one_case(n, 1, 0);
    printf("test_scroll_plan: %d of %d cases as ScrollRaster must give them (smart and simple refresh layers, no layer); "
           "the old code (each ClipRect on its own): %d\n", cases - fails, cases, cases - oldfails);
    if (!oldfails) { printf("test_scroll_plan: the old code gave every case too: the test sees nothing\n"); return 1; }
    return fails ? 1 : 0;
}
