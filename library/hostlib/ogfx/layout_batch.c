/* SPDX-License-Identifier: MIT */
/* A coarse GFOpenLayout CPU-shadow batch for AC090.  The public boundary is
 * GFOpenLayout's olc_accel; layout_batch.h is its private AC090 transport.
 * One tagged call executes every record, after all input and spans have been
 * validated, so a declined call has not changed a pixel. */
#include "layout_batch.h"
#include "../hl_ids.h"

#ifdef HL_HOST
#define OLB_CTX struct hl_ctx *hl_,
#define OLB_PASS hl_,
#else
#define OLB_CTX
#define OLB_PASS
#endif

static uint32_t olb_bpp(uint32_t fmt)
{
    if (fmt == HL_OLF_CLUT8) return 1;
    if (fmt == HL_OLF_RGB16) return 2;
    if (fmt == HL_OLF_XRGB32 || fmt == HL_OLF_ARGB32) return 4;
    return 0;
}

static int olb_box_ok(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t w, uint32_t h)
{
    return x0 >= 0 && y0 >= 0 && x1 >= x0 && y1 >= y0 &&
           (uint32_t)x1 < w && (uint32_t)y1 < h;
}

static int olb_boxes_cross(int32_t ax0, int32_t ay0, int32_t ax1, int32_t ay1,
                           int32_t bx0, int32_t by0, int32_t bx1, int32_t by1)
{
    return ax0 <= bx1 && bx0 <= ax1 && ay0 <= by1 && by0 <= ay1;
}

static int olb_ranges_cross(hl_addr a, uint32_t an, hl_addr b, uint32_t bn)
{
    return an && bn && (uint64_t)a < (uint64_t)b + bn && (uint64_t)b < (uint64_t)a + an;
}

static int olb_clip(OLB_CTX hl_addr clips, uint32_t nclips, uint32_t i,
                    int32_t *x0, int32_t *y0, int32_t *x1, int32_t *y1)
{
    hl_addr p;
    if (!nclips) return 1;
    p = clips + i * 16u;
    {
        int32_t cx0 = (int32_t)HL_RD32(p), cy0 = (int32_t)HL_RD32(p + 4);
        int32_t cx1 = (int32_t)HL_RD32(p + 8), cy1 = (int32_t)HL_RD32(p + 12);
        if (*x0 < cx0) *x0 = cx0;
        if (*y0 < cy0) *y0 = cy0;
        if (*x1 > cx1) *x1 = cx1;
        if (*y1 > cy1) *y1 = cy1;
    }
    return *x0 <= *x1 && *y0 <= *y1;
}

static void olb_move(uint8_t *d, const uint8_t *s, uint32_t n)
{
    if (d == s || !n) return;
    if (d < s) while (n--) *d++ = *s++;
    else { d += n; s += n; while (n--) *--d = *--s; }
}

static void olb_pixel(uint8_t *d, uint32_t bpp, uint32_t pixel)
{
    uint32_t k;
    for (k = 0; k < bpp; ++k) d[k] = (uint8_t)(pixel >> (8u * (bpp - 1u - k)));
}

static uint32_t olb_cmd(OLB_CTX hl_addr cmds, uint32_t i, uint32_t n)
{
    (void)n;
    return HL_RD32(cmds + i * HL_OLC_BYTES);
}

static uint32_t olb_word(OLB_CTX hl_addr cmds, uint32_t i, uint32_t word)
{
    return HL_RD32(cmds + i * HL_OLC_BYTES + word * 4u);
}

/* One clipped rectangle of a non-copy command. */
static uint32_t olb_draw_rect(OLB_CTX hl_addr block, hl_addr cmds, uint32_t ci,
                              int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    uint32_t op = olb_word(OLB_PASS cmds, ci, HL_OLC_OP), dbpr = HL_ARG(HL_OLB_DST_BPR);
    uint32_t dfmt = HL_ARG(HL_OLB_DST_FMT), bpp = olb_bpp(dfmt), arg = olb_word(OLB_PASS cmds, ci, HL_OLC_ARG);
    hl_addr dst = HL_ARG(HL_OLB_DST), src = olb_word(OLB_PASS cmds, ci, HL_OLC_SRC);
    uint32_t sbpr = olb_word(OLB_PASS cmds, ci, HL_OLC_SRC_BPR);
    int32_t ox0 = (int32_t)olb_word(OLB_PASS cmds, ci, HL_OLC_X0), oy0 = (int32_t)olb_word(OLB_PASS cmds, ci, HL_OLC_Y0);
    int32_t dx = (int32_t)olb_word(OLB_PASS cmds, ci, HL_OLC_DX), dy = (int32_t)olb_word(OLB_PASS cmds, ci, HL_OLC_DY);
    int32_t y, x;
    if (op == HL_OLC_FILL) {
        for (y = y0; y <= y1; ++y) {
            uint8_t *d;
            HL_PTR_W(d, dst + (uint32_t)y * dbpr + (uint32_t)x0 * bpp, (uint32_t)(x1 - x0 + 1) * bpp);
            for (x = x0; x <= x1; ++x, d += bpp) olb_pixel(d, bpp, arg);
        }
    } else if (op == HL_OLC_COMPOSITE) {
        uint32_t opacity = arg;
        for (y = y0; y <= y1; ++y) for (x = x0; x <= x1; ++x) {
            int32_t sx = ox0 + x - dx, sy = oy0 + y - dy;
            const uint8_t *s;
            uint8_t *d;
            uint32_t a, k;
            HL_PTR_R(s, src + (uint32_t)sy * sbpr + (uint32_t)sx * 4u, 4);
            HL_PTR_W(d, dst + (uint32_t)y * dbpr + (uint32_t)x * 4u, 4);
            a = ((uint32_t)s[0] * opacity + 127u) / 255u;
            d[0] = 0;              /* XRGB32's unused byte is kept deterministic */
            for (k = 1; k < 4; ++k) d[k] = (uint8_t)(((uint32_t)s[k] * a + (uint32_t)d[k] * (255u - a) + 127u) / 255u);
        }
    } else if (op == HL_OLC_MASK1) {
        for (y = y0; y <= y1; ++y) for (x = x0; x <= x1; ++x) {
            int32_t sx = ox0 + x - dx, sy = oy0 + y - dy;
            const uint8_t *s;
            HL_PTR_R(s, src + (uint32_t)sy * sbpr + ((uint32_t)sx >> 3), 1);
            if (*s & (uint8_t)(0x80u >> (sx & 7))) {
                uint8_t *d;
                HL_PTR_W(d, dst + (uint32_t)y * dbpr + (uint32_t)x * bpp, bpp);
                olb_pixel(d, bpp, arg);
            }
        }
    }
    return 1;
}

/* A copy's clipped horizontal spans in global memmove order.  Clip boxes are
 * disjoint, but a scroll's source for one box may be another box's
 * destination, so processing each rectangle independently is not safe. */
static uint32_t olb_copy(OLB_CTX hl_addr block, hl_addr cmds, uint32_t ci, hl_addr clips, uint32_t nclips)
{
    hl_addr dst = HL_ARG(HL_OLB_DST), src = olb_word(OLB_PASS cmds, ci, HL_OLC_SRC);
    uint32_t dbpr = HL_ARG(HL_OLB_DST_BPR), sbpr = olb_word(OLB_PASS cmds, ci, HL_OLC_SRC_BPR);
    uint32_t bpp = olb_bpp(HL_ARG(HL_OLB_DST_FMT));
    int32_t sx0 = (int32_t)olb_word(OLB_PASS cmds, ci, HL_OLC_X0), sy0 = (int32_t)olb_word(OLB_PASS cmds, ci, HL_OLC_Y0);
    int32_t sx1 = (int32_t)olb_word(OLB_PASS cmds, ci, HL_OLC_X1), sy1 = (int32_t)olb_word(OLB_PASS cmds, ci, HL_OLC_Y1);
    int32_t dx = (int32_t)olb_word(OLB_PASS cmds, ci, HL_OLC_DX), dy = (int32_t)olb_word(OLB_PASS cmds, ci, HL_OLC_DY);
    int32_t w = sx1 - sx0 + 1, h = sy1 - sy0 + 1, y, yend, ystep;
    uint64_t sa = (uint64_t)src + (uint64_t)(uint32_t)sy0 * sbpr + (uint64_t)(uint32_t)sx0 * bpp;
    uint64_t da = (uint64_t)dst + (uint64_t)(uint32_t)dy * dbpr + (uint64_t)(uint32_t)dx * bpp;
    int backwards = da > sa;
    if (backwards) { y = dy + h - 1; yend = dy - 1; ystep = -1; }
    else { y = dy; yend = dy + h; ystep = 1; }
    for (; y != yend; y += ystep) {
        uint32_t pass, parts = nclips ? nclips : 1;
        int32_t last = backwards ? dx + w : dx - 1;
        for (pass = 0; pass < parts; ++pass) {
            uint32_t k, best = parts;
            int32_t bx0 = 0, bx1 = -1;
            for (k = 0; k < parts; ++k) {
                int32_t x0 = dx, y0 = y, x1 = dx + w - 1, y1 = y;
                if (!olb_clip(OLB_PASS clips, nclips, k, &x0, &y0, &x1, &y1)) continue;
                if ((!backwards && x0 > last && (best == parts || x0 < bx0)) ||
                    (backwards && x1 < last && (best == parts || x1 > bx1))) {
                    best = k; bx0 = x0; bx1 = x1;
                }
            }
            if (best == parts) break;
            {
                int32_t sy = sy0 + y - dy, sx = sx0 + bx0 - dx;
                uint32_t bytes = (uint32_t)(bx1 - bx0 + 1) * bpp;
                const uint8_t *s;
                uint8_t *d;
                HL_PTR_R(s, src + (uint32_t)sy * sbpr + (uint32_t)sx * bpp, bytes);
                HL_PTR_W(d, dst + (uint32_t)y * dbpr + (uint32_t)bx0 * bpp, bytes);
                olb_move(d, s, bytes);
            }
            last = backwards ? bx0 : bx1;
        }
    }
    return 1;
}

HL_TAG(hostlib_ogfx_layout_batch, HL_ID_OGFX_LAYOUT_BATCH)
HL_FN(hostlib_ogfx_layout_batch)
{
    hl_addr dst, clips, cmds;
    uint32_t dbpr, dw, dh, dfmt, dbpp, dstn, nclips, ncmds, i, j, nsrc = 0;
    hl_addr source[HL_OLB_MAX_SOURCES];
    uint32_t source_n[HL_OLB_MAX_SOURCES];
    uint64_t bytes64;

    HL_NEED_R(block, HL_OLB_BLOCK);
    dst = HL_ARG(HL_OLB_DST); dbpr = HL_ARG(HL_OLB_DST_BPR);
    dw = HL_ARG(HL_OLB_DST_W); dh = HL_ARG(HL_OLB_DST_H); dfmt = HL_ARG(HL_OLB_DST_FMT);
    clips = HL_ARG(HL_OLB_CLIPS); nclips = HL_ARG(HL_OLB_NCLIPS);
    cmds = HL_ARG(HL_OLB_CMDS); ncmds = HL_ARG(HL_OLB_NCMDS);
    dbpp = olb_bpp(dfmt);
    if (!dbpp || !dw || !dh || dw > 0x7fffffffu || dh > 0x7fffffffu ||
        (uint64_t)dbpr < (uint64_t)dw * dbpp ||
        nclips > HL_OLB_MAX_CLIPS || ncmds > HL_OLB_MAX_COMMANDS)
        HL_DECLINE();
    bytes64 = (uint64_t)dbpr * dh;
    if (bytes64 > 0xffffffffull) HL_DECLINE();
    dstn = (uint32_t)bytes64;
    if (nclips) {
        if (!clips || (uint64_t)nclips * 16u > 0xffffffffull) HL_DECLINE();
        HL_NEED_R(clips, nclips * 16u);
    }
    if (ncmds) {
        if (!cmds || (uint64_t)ncmds * HL_OLC_BYTES > 0xffffffffull) HL_DECLINE();
        HL_NEED_R(cmds, ncmds * HL_OLC_BYTES);
    }
    if (olb_ranges_cross(dst, (uint32_t)bytes64, block, HL_OLB_BLOCK) ||
        (nclips && olb_ranges_cross(dst, (uint32_t)bytes64, clips, nclips * 16u)) ||
        (ncmds && olb_ranges_cross(dst, (uint32_t)bytes64, cmds, ncmds * HL_OLC_BYTES))) HL_DECLINE();
    /* Validate every clip, including the non-overlap promise, before writes. */
    for (i = 0; i < nclips; ++i) {
        int32_t x0 = (int32_t)HL_RD32(clips + i * 16u), y0 = (int32_t)HL_RD32(clips + i * 16u + 4);
        int32_t x1 = (int32_t)HL_RD32(clips + i * 16u + 8), y1 = (int32_t)HL_RD32(clips + i * 16u + 12);
        if (!olb_box_ok(x0, y0, x1, y1, dw, dh)) HL_DECLINE();
        for (j = 0; j < i; ++j) {
            int32_t qx0 = (int32_t)HL_RD32(clips + j * 16u), qy0 = (int32_t)HL_RD32(clips + j * 16u + 4);
            int32_t qx1 = (int32_t)HL_RD32(clips + j * 16u + 8), qy1 = (int32_t)HL_RD32(clips + j * 16u + 12);
            if (olb_boxes_cross(x0, y0, x1, y1, qx0, qy0, qx1, qy1)) HL_DECLINE();
        }
    }
    /* Validate records and collect their distinct full source spans. */
    for (i = 0; i < ncmds; ++i) {
        uint32_t op = olb_cmd(OLB_PASS cmds, i, ncmds);
        int32_t x0 = (int32_t)olb_word(OLB_PASS cmds, i, HL_OLC_X0), y0 = (int32_t)olb_word(OLB_PASS cmds, i, HL_OLC_Y0);
        int32_t x1 = (int32_t)olb_word(OLB_PASS cmds, i, HL_OLC_X1), y1 = (int32_t)olb_word(OLB_PASS cmds, i, HL_OLC_Y1);
        if (op == HL_OLC_FILL) {
            if (!olb_box_ok(x0, y0, x1, y1, dw, dh)) HL_DECLINE();
        } else if (op == HL_OLC_COPY || op == HL_OLC_COMPOSITE || op == HL_OLC_MASK1) {
            hl_addr s = olb_word(OLB_PASS cmds, i, HL_OLC_SRC);
            uint32_t sbpr = olb_word(OLB_PASS cmds, i, HL_OLC_SRC_BPR), sw = olb_word(OLB_PASS cmds, i, HL_OLC_SRC_W);
            uint32_t sh = olb_word(OLB_PASS cmds, i, HL_OLC_SRC_H), sfmt = olb_word(OLB_PASS cmds, i, HL_OLC_SRC_FMT);
            int32_t dx = (int32_t)olb_word(OLB_PASS cmds, i, HL_OLC_DX), dy = (int32_t)olb_word(OLB_PASS cmds, i, HL_OLC_DY);
            uint32_t sbpp = op == HL_OLC_MASK1 ? 0 : olb_bpp(sfmt), needbpr;
            uint32_t ww, hh, sn;
            if (!sw || !sh || sw > 0x7fffffffu || sh > 0x7fffffffu ||
                !olb_box_ok(x0, y0, x1, y1, sw, sh)) HL_DECLINE();
            ww = (uint32_t)(x1 - x0 + 1); hh = (uint32_t)(y1 - y0 + 1);
            if (dx < 0 || dy < 0 || (uint64_t)(uint32_t)dx + ww > dw ||
                (uint64_t)(uint32_t)dy + hh > dh) HL_DECLINE();
            if (op == HL_OLC_MASK1) needbpr = (uint32_t)(((uint64_t)sw + 7u) >> 3);
            else {
                if ((uint64_t)sw * sbpp > 0xffffffffull) HL_DECLINE();
                needbpr = sw * sbpp;
            }
            if ((uint64_t)sbpr < needbpr || (op == HL_OLC_COPY && sfmt != dfmt) ||
                (op == HL_OLC_COMPOSITE && (sfmt != HL_OLF_ARGB32 || dfmt != HL_OLF_XRGB32 ||
                                             olb_word(OLB_PASS cmds, i, HL_OLC_ARG) > 255u))) HL_DECLINE();
            bytes64 = (uint64_t)sbpr * sh;
            if (!s || bytes64 > 0xffffffffull) HL_DECLINE();
            sn = (uint32_t)bytes64;
            if (olb_ranges_cross(s, sn, dst, dstn) &&
                (op != HL_OLC_COPY || s != dst || sbpr != dbpr)) HL_DECLINE();
            for (j = 0; j < nsrc; ++j) if (source[j] == s && source_n[j] == sn) break;
            if (j == nsrc) {
                if (nsrc == HL_OLB_MAX_SOURCES) HL_DECLINE();
                source[nsrc] = s; source_n[nsrc] = sn; ++nsrc;
                HL_NEED_R(s, sn);
            }
        } else HL_DECLINE();
    }
    HL_NEED_W(dst, (uint32_t)((uint64_t)dbpr * dh));
    HL_DECLARED();

    for (i = 0; i < ncmds; ++i) {
        uint32_t op = olb_word(OLB_PASS cmds, i, HL_OLC_OP);
        int32_t x0, y0, x1, y1;
        if (op == HL_OLC_COPY) { if (!olb_copy(OLB_PASS block, cmds, i, clips, nclips)) return 0; continue; }
        if (op == HL_OLC_FILL) {
            x0 = (int32_t)olb_word(OLB_PASS cmds, i, HL_OLC_X0); y0 = (int32_t)olb_word(OLB_PASS cmds, i, HL_OLC_Y0);
            x1 = (int32_t)olb_word(OLB_PASS cmds, i, HL_OLC_X1); y1 = (int32_t)olb_word(OLB_PASS cmds, i, HL_OLC_Y1);
        } else {
            int32_t sx0 = (int32_t)olb_word(OLB_PASS cmds, i, HL_OLC_X0), sy0 = (int32_t)olb_word(OLB_PASS cmds, i, HL_OLC_Y0);
            x0 = (int32_t)olb_word(OLB_PASS cmds, i, HL_OLC_DX); y0 = (int32_t)olb_word(OLB_PASS cmds, i, HL_OLC_DY);
            x1 = x0 + (int32_t)olb_word(OLB_PASS cmds, i, HL_OLC_X1) - sx0;
            y1 = y0 + (int32_t)olb_word(OLB_PASS cmds, i, HL_OLC_Y1) - sy0;
        }
        if (!nclips) { if (!olb_draw_rect(OLB_PASS block, cmds, i, x0, y0, x1, y1)) return 0; }
        else for (j = 0; j < nclips; ++j) {
            int32_t a = x0, b = y0, c = x1, d = y1;
            if (olb_clip(OLB_PASS clips, nclips, j, &a, &b, &c, &d) &&
                !olb_draw_rect(OLB_PASS block, cmds, i, a, b, c, d)) return 0;
        }
    }
    return ncmds;
}
