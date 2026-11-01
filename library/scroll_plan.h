/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * ScrollRaster's rules and its move across a layer's ClipRects (screens.c,
 * 1 Nov 2026), plain C so the host tests build the same code
 * (tests/test_scroll_plan.c).
 *
 * sp_scroll has graphics.library's rules: what is cut, what is filled, what
 * is damaged, when nothing happens (AROS's ScrollRaster, and OS 3.2 where
 * the two part: see screens.c). sp_move moves the box's contents as one
 * across the pieces that hold its pixels: each piece's part goes to
 * whichever pieces its destination lies in. The pieces are disjoint, and a
 * copy into a piece overwrites what that piece still has to give, so a piece
 * is copied only once every other piece its destination covers has been
 * copied (read), and to itself last. Disjoint rectangles moved in one
 * direction always have such an order (Guibas and Yao, "On translating a set
 * of rectangles", 1980), so the search in sp_order always finds one; AROS's
 * MoveRaster gets the same from layers' SortLayerCR, which OS 3.x's
 * layers.library does not offer.
 */
#ifndef ORTG_SCROLL_PLAN_H
#define ORTG_SCROLL_PLAN_H

/* A box, inclusive. */
struct sp_box { long x0, y0, x1, y1; };

/* A piece of the box that holds pixels: its part of the box (screen
 * coordinates), the bitmap they are in, and the bitmap position of screen
 * point (0, 0) there. */
struct sp_piece { struct sp_box box; void *bm; long tx, ty; };

static int sp_meets(const struct sp_box *a, const struct sp_box *b)
{
    return a->x0 <= b->x1 && b->x0 <= a->x1 && a->y0 <= b->y1 && b->y0 <= a->y1;
}

/* a and b's common part into o; 0 when they have none. */
static int sp_and(const struct sp_box *a, const struct sp_box *b, struct sp_box *o)
{
    o->x0 = a->x0 > b->x0 ? a->x0 : b->x0;
    o->y0 = a->y0 > b->y0 ? a->y0 : b->y0;
    o->x1 = a->x1 < b->x1 ? a->x1 : b->x1;
    o->y1 = a->y1 < b->y1 ? a->y1 : b->y1;
    return o->x0 <= o->x1 && o->y0 <= o->y1;
}

/* The key the pieces are first sorted by, so that the search below mostly
 * takes the first piece it looks at: the pieces nearest where the contents
 * go come first (the top ones when they go up, dy > 0). */
static long sp_key(const struct sp_box *b, long dx, long dy)
{
    long k = 0;
    if (dy > 0) k = b->y0; else if (dy < 0) k = -b->y1;
    k *= 65536;
    if (dx > 0) k += b->x0; else if (dx < 0) k -= b->x1;
    return k;
}

/* The order to copy n pieces in when the contents move by -dx, -dy: order[]
 * gets their indices. idx (n entries) is scratch. */
static void sp_order(const struct sp_piece *p, int n, long dx, long dy, unsigned short *order, unsigned short *idx)
{
    int i, j, left;
    for (i = 0; i < n; i++) {                           /* insertion sort by sp_key */
        long k = sp_key(&p[i].box, dx, dy);
        for (j = i; j > 0 && sp_key(&p[idx[j - 1]].box, dx, dy) > k; j--) idx[j] = idx[j - 1];
        idx[j] = (unsigned short)i;
    }
    /* idx[0..left-1]: not copied yet. Take the first whose destination
     * meets none of the others still to be copied. */
    for (left = n; left > 0; ) {
        int pick = 0;
        for (i = 0; i < left; i++) {
            const struct sp_box *s = &p[idx[i]].box;
            struct sp_box d;
            d.x0 = s->x0 - dx; d.y0 = s->y0 - dy; d.x1 = s->x1 - dx; d.y1 = s->y1 - dy;
            for (j = 0; j < left; j++)
                if (j != i && sp_meets(&d, &p[idx[j]].box)) break;
            if (j == left) { pick = i; break; }
        }
        *order++ = idx[pick];                           /* pick 0 when none is free: can't happen */
        for (i = pick; i < left - 1; i++) idx[i] = idx[i + 1];
        left--;
    }
}

/* A copy of w by h pixels from (sx, sy) in sbm to (dx, dy) in dbm, right
 * when the two overlap in one bitmap. */
typedef void (*sp_blit_fn)(void *ctx, void *sbm, long sx, long sy, void *dbm, long dx, long dy, long w, long h);

/* The contents of box b (screen coordinates) move by -dx, -dy across the n
 * pieces p that hold its pixels. order and idx: n entries each. */
static void sp_move(const struct sp_piece *p, int n, const struct sp_box *b, long dx, long dy,
                    unsigned short *order, unsigned short *idx, sp_blit_fn blit, void *ctx)
{
    int i, j;
    sp_order(p, n, dx, dy, order, idx);
    for (i = 0; i < n; i++) {
        const struct sp_piece *s = &p[order[i]];
        struct sp_box d, t;
        d.x0 = s->box.x0 - dx; d.y0 = s->box.y0 - dy; d.x1 = s->box.x1 - dx; d.y1 = s->box.y1 - dy;
        if (!sp_and(&d, b, &d)) continue;
        for (j = 0; j < n; j++)                         /* to the others, then to itself */
            if (j != order[i] && sp_and(&d, &p[j].box, &t))
                blit(ctx, s->bm, t.x0 + dx + s->tx, t.y0 + dy + s->ty, p[j].bm, t.x0 + p[j].tx, t.y0 + p[j].ty,
                     t.x1 - t.x0 + 1, t.y1 - t.y0 + 1);
        if (sp_and(&d, &s->box, &t))
            blit(ctx, s->bm, t.x0 + dx + s->tx, t.y0 + dy + s->ty, s->bm, t.x0 + s->tx, t.y0 + s->ty,
                 t.x1 - t.x0 + 1, t.y1 - t.y0 + 1);
    }
}

/* What ScrollRaster does, in the RastPort's coordinates, through ops. */
struct sp_ops {
    void (*damage)(void *ctx, const struct sp_box *r);  /* into a simple refresh layer's DamageList, LAYERREFRESH set */
    void (*fill)(void *ctx, const struct sp_box *r);    /* the B pen, through the layer's ClipRects */
    void (*move)(void *ctx, const struct sp_box *r, long dx, long dy);  /* the box's contents by -dx, -dy */
};

/* ScrollRaster(dx, dy, x0, y0, x1, y1): w and h are the layer's width and
 * height less one (bounds.MaxX - bounds.MinX), or the bitmap's with no
 * layer (layer 0); simple: a simple refresh layer. */
static void sp_scroll(long dx, long dy, long x0, long y0, long x1, long y1, int layer, long w, long h, int simple,
                      const struct sp_ops *ops, void *ctx)
{
    long mx = x1 > w ? w : x1, my = y1 > h ? h : y1;
    long adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
    struct sp_box r;
    if (!layer) {                                       /* no layer: the bitmap's pixels only */
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
    }
    if (x0 > mx || y0 > my || (!dx && !dy)) return;
    if (layer && simple && (x0 < 0 || y0 < 0 || x1 > w || y1 > h)) {
        /* OS 3.2: a box reaching outside a simple refresh layer damages all of it that is in the layer */
        r.x0 = x0 < 0 ? 0 : x0; r.y0 = y0 < 0 ? 0 : y0; r.x1 = mx; r.y1 = my;
        ops->damage(ctx, &r);
    }
    r.x0 = x0; r.y0 = y0; r.x1 = mx; r.y1 = my;
    if (adx > mx - x0 || ady > my - y0) {               /* as wide or high as the box: all of it */
        ops->fill(ctx, &r);
        return;
    }
    ops->move(ctx, &r, dx, dy);
    /* what the move uncovered: the columns, then the rows */
    if (dx) {
        r.x0 = dx > 0 ? mx - dx + 1 : x0; r.x1 = dx > 0 ? mx : x0 - dx - 1; r.y0 = y0; r.y1 = my;
        ops->fill(ctx, &r);
    }
    if (dy) {
        r.x0 = x0; r.x1 = mx; r.y0 = dy > 0 ? my - dy + 1 : y0; r.y1 = dy > 0 ? my : y0 - dy - 1;
        ops->fill(ctx, &r);
    }
}

#endif
