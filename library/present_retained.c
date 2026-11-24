/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * ORTG_Present's RETAINED route (present_retained.h). */
#include "present_retained.h"

static LONG rsp_gpu(struct rs_handle *h, const struct rsp_target *t, LONG x, LONG y, LONG w, LONG ht)
{
    struct ogpu_rs_present p;
    unsigned char *buf;
    long n, words, built;
    LONG r;
    if (!h->tex) {
        if (!(h->tex = rsp_alloc(h->pub.rs_BytesPerRow * h->pub.rs_Height))) return OGPU_RS_ERR_MEMORY;
        h->tex_valid = 0;
    }
    if (!h->tex_valid || h->path != RS_GPU) ogpu_rs_all(&h->core);
    n = ogpu_rs_collect(&h->core);
    words = ogpu_rs_present_words(&h->core, t->nclips);
    if (!(buf = rsp_alloc((ULONG)words * 4))) {
        ogpu_rs_fail(&h->core);
        return OGPU_RS_ERR_MEMORY;
    }
    p.src = (unsigned long)h->pub.rs_Pixels;
    p.tex = (unsigned long)h->tex;
    p.tex_bpr = h->pub.rs_BytesPerRow;
    p.palette = (unsigned long)h->pub.rs_Palette;
    p.format = (int)h->pub.rs_Format;
    p.dst = t->addr;
    p.dst_bpr = t->bpr;
    p.dst_w = t->w;
    p.dst_h = t->h;
    p.dst_format = t->dfmt;
    p.x = (int)(x + t->ox);
    p.y = (int)(y + t->oy);
    p.w = (int)w;
    p.h = (int)ht;
    p.clips = t->clips;
    p.nclips = t->nclips;
    p.comp_flags = h->pub.rs_Flags & OGPU_RSF_BILINEAR ? OGPU_COMP_BILINEAR : 0;
    p.fence = 0x52530000UL;                 /* 'RS' */
    built = ogpu_rs_build_present(&h->core, &p, buf, words);
    r = built > 0 ? rsp_submit(buf, (ULONG)built) : -1;
    rsp_free(buf);
    if (r != OGPU_OK) {
        ogpu_rs_fail(&h->core);
        h->tex_valid = 0;
        return OGPU_RS_ERR_GPU;
    }
    ogpu_rs_done(&h->core);
    h->tex_valid = 1;
    rs_count(h, n, RS_GPU);
    return n;
}

static LONG rsp_cpu(struct rs_handle *h, const struct rsp_target *t, LONG x, LONG y, LONG w, LONG ht)
{
    struct RastPort *rp = t->rp;
    struct BitMap *bm = rp->BitMap;
    int index8 = h->pub.rs_Format == OGPU_FMT_INDEX8, colours = index8 && t->truecolour;
    struct OpenRTGPixels px;
    long at = 0, n;
    int rx, ry, rw, rh;
    if (h->path != RS_CPU || h->last_rp != rp || h->last_bm != bm || h->last_x != x || h->last_y != y || h->last_w != w
        || h->last_h != ht || (colours && h->pal_changed))
        ogpu_rs_all(&h->core);
    n = ogpu_rs_collect(&h->core);
    px.data = h->pub.rs_Pixels;
    px.modulo = (long)h->pub.rs_BytesPerRow;
    px.format = !index8 ? ORTG_PIX_ARGB : colours ? ORTG_PIX_INDEX : ORTG_PIX_PEN;
    px.ctable = colours ? (const unsigned long *)h->pub.rs_Palette : NULL;
    px.dest_width = px.dest_height = 0;
    while (ogpu_rs_next_run(&h->core, &at, &rx, &ry, &rw, &rh)) {
        if (rx + rw > w) rw = (int)w - rx;
        if (ry + rh > ht) rh = (int)ht - ry;
        if (rw <= 0 || rh <= 0) continue;
        px.x = rx; px.y = ry; px.width = rw; px.height = rh;
        rsp_write(rp, x + rx, y + ry, &px);
    }
    ogpu_rs_done(&h->core);
    h->tex_valid = 0;                       /* the texture, if there is one, missed these */
    h->last_rp = rp;
    h->last_bm = bm;
    h->last_x = x;
    h->last_y = y;
    h->last_w = w;
    h->last_h = ht;
    rs_count(h, n, RS_CPU);
    return n;
}

LONG rsp_present(struct rs_handle *h, const struct rsp_target *t, LONG x, LONG y, LONG w, LONG ht)
{
    LONG r;
    if (!h || !t || !t->rp || w <= 0 || ht <= 0 || w > RS_MAX || ht > RS_MAX) return OGPU_RS_ERR_ARGS;
    if (t->gpu && !(h->pub.rs_Flags & OGPU_RSF_SOFTWARE)) {
        r = rsp_gpu(h, t, x, y, w, ht);
        if (r != OGPU_RS_ERR_GPU) return r;
    }
    return rsp_cpu(h, t, x, y, w, ht);
}
