/* SPDX-License-Identifier: MIT */
#include "leaves.h"
#include "../hl_ids.h"

static uint32_t hl_opb_bit(uint32_t m, uint32_t s, uint32_t d)
{
    return m >> (4u + (s ? 2u : 0u) + (d ? 1u : 0u)) & 1u;
}
static int hl_opb_overlap(uint32_t a, uint32_t n, uint32_t b, uint32_t m)
{
    return (uint64_t)a + n > b && a < (uint64_t)b + m;
}

HL_TAG(hostlib_ogfx_planar_blit, HL_ID_OGFX_PLANAR_BLIT)
HL_FN(hostlib_ogfx_planar_blit)
{
    hl_addr st, dt;
    uint32_t sbpr, dbpr, depth, minterm, mask, same, p, planes = 0;
    int32_t sx, sy, dx, dy, width, height, yy, xx;
    uint32_t sa[8] = { 0 }, da[8] = { 0 }, sn[8] = { 0 }, dn[8] = { 0 };
    const uint8_t *sp[8] = { 0 };
    uint8_t *dp[8] = { 0 };
    HL_NEED_R(block, HL_OPB_BLOCK);
    st = HL_ARG(HL_OPB_SRC_PLANES); sbpr = HL_ARG(HL_OPB_SBPR);
    dt = HL_ARG(HL_OPB_DST_PLANES); dbpr = HL_ARG(HL_OPB_DBPR);
    depth = HL_ARG(HL_OPB_DEPTH); sx = (int32_t)HL_ARG(HL_OPB_SX);
    sy = (int32_t)HL_ARG(HL_OPB_SY); dx = (int32_t)HL_ARG(HL_OPB_DX);
    dy = (int32_t)HL_ARG(HL_OPB_DY); width = (int32_t)HL_ARG(HL_OPB_WIDTH);
    height = (int32_t)HL_ARG(HL_OPB_HEIGHT); minterm = HL_ARG(HL_OPB_MINTERM);
    mask = HL_ARG(HL_OPB_MASK); same = HL_ARG(HL_OPB_SAME);
    if (!depth || depth > 8 || sx < 0 || sy < 0 || dx < 0 || dy < 0 ||
        width <= 0 || height <= 0 || !mask) return 0;
    if ((uint64_t)(uint32_t)sx + (uint32_t)(width - 1) > 0x7fffffffull ||
        (uint64_t)(uint32_t)dx + (uint32_t)(width - 1) > 0x7fffffffull ||
        (uint64_t)(uint32_t)sy + (uint32_t)(height - 1) > 0x7fffffffull ||
        (uint64_t)(uint32_t)dy + (uint32_t)(height - 1) > 0x7fffffffull) HL_DECLINE();
    HL_NEED_R(st, 4u * depth); HL_NEED_R(dt, 4u * depth);
    for (p = 0; p < depth; p++) if (mask >> p & 1u) {
        uint32_t sv = HL_RD32(st + 4u * p), dv = HL_RD32(dt + 4u * p);
        uint64_t f, l;
        if (!dv || dv == 0xffffffffu) continue;
        f = (uint64_t)dv + (uint64_t)(uint32_t)dy * dbpr + ((uint32_t)dx >> 3);
        l = (uint64_t)dv + ((uint64_t)(uint32_t)dy + (uint32_t)(height - 1)) * dbpr +
            (((uint32_t)dx + (uint32_t)(width - 1)) >> 3);
        if (l > 0xffffffffull || l < f) HL_DECLINE();
        da[p] = (uint32_t)f; dn[p] = (uint32_t)(l - f + 1);
        if (hl_opb_overlap(da[p], dn[p], block, HL_OPB_BLOCK) ||
            hl_opb_overlap(da[p], dn[p], st, 4u * depth) ||
            hl_opb_overlap(da[p], dn[p], dt, 4u * depth)) HL_DECLINE();
        HL_NEED_W(da[p], dn[p]);
        if (sv && sv != 0xffffffffu) {
            f = (uint64_t)sv + (uint64_t)(uint32_t)sy * sbpr + ((uint32_t)sx >> 3);
            l = (uint64_t)sv + ((uint64_t)(uint32_t)sy + (uint32_t)(height - 1)) * sbpr +
                (((uint32_t)sx + (uint32_t)(width - 1)) >> 3);
            if (l > 0xffffffffull || l < f) HL_DECLINE();
            sa[p] = (uint32_t)f; sn[p] = (uint32_t)(l - f + 1);
            HL_NEED_R(sa[p], sn[p]);
        } else sa[p] = sv;
    }
    HL_DECLARED();
    for (p = 0; p < depth; p++) if (dn[p]) {
        HL_PTR_W(dp[p], da[p], dn[p]);
        if (sn[p]) HL_PTR_R(sp[p], sa[p], sn[p]);
    }
    for (p = 0; p < depth; p++) if (dp[p]) {
        int32_t y0 = 0, y1 = height, ys = 1, x0 = 0, x1 = width, xs = 1;
        planes++;
        if (same && dy > sy) { y0 = height - 1; y1 = -1; ys = -1; }
        else if (same && dy == sy && dx > sx) { x0 = width - 1; x1 = -1; xs = -1; }
        for (yy = y0; yy != y1; yy += ys) for (xx = x0; xx != x1; xx += xs) {
            uint32_t sxp = (uint32_t)(sx + xx), dxp = (uint32_t)(dx + xx);
            uint32_t sbit = sa[p] == 0xffffffffu ? 1u : !sa[p] ? 0u :
                (sp[p][(uint32_t)yy * sbpr + (sxp >> 3) - ((uint32_t)sx >> 3)] & (0x80u >> (sxp & 7u))) != 0;
            uint8_t *d = dp[p] + (uint32_t)yy * dbpr + (dxp >> 3) - ((uint32_t)dx >> 3);
            uint8_t bit = (uint8_t)(0x80u >> (dxp & 7u));
            if (hl_opb_bit(minterm, sbit, (*d & bit) != 0)) *d |= bit; else *d &= (uint8_t)~bit;
        }
    }
    return planes;
}
