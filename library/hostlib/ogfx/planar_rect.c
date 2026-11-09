/* SPDX-License-Identifier: MIT */
#include "leaves.h"
#include "../hl_ids.h"

static int hl_opr_overlap(uint32_t a, uint32_t n, uint32_t b, uint32_t m)
{
    return (uint64_t)a + n > b && a < (uint64_t)b + m;
}

HL_TAG(hostlib_ogfx_planar_rect, HL_ID_OGFX_PLANAR_RECT)
HL_FN(hostlib_ogfx_planar_rect)
{
    hl_addr table;
    uint32_t bpr, depth, pen, mask, mode, p, left, right, inner, drawn = 0;
    int32_t x0, y0, x1, y1;
    uint32_t y, height;
    uint8_t lm, rm, *rows[8] = { 0 };
    uint32_t first[8] = { 0 }, bytes[8] = { 0 };
    HL_NEED_R(block, HL_OPR_BLOCK);
    table = HL_ARG(HL_OPR_PLANES); bpr = HL_ARG(HL_OPR_BPR);
    depth = HL_ARG(HL_OPR_DEPTH); x0 = (int32_t)HL_ARG(HL_OPR_X0);
    y0 = (int32_t)HL_ARG(HL_OPR_Y0); x1 = (int32_t)HL_ARG(HL_OPR_X1);
    y1 = (int32_t)HL_ARG(HL_OPR_Y1); pen = HL_ARG(HL_OPR_PEN);
    mask = HL_ARG(HL_OPR_MASK); mode = HL_ARG(HL_OPR_MODE);
    if (depth > 8 || x0 < 0 || y0 < 0 || x1 < x0 || y1 < y0) return 0;
    height = (uint32_t)y1 - (uint32_t)y0 + 1;
    HL_NEED_R(table, 4u * depth);
    left = (uint32_t)x0 >> 3; right = (uint32_t)x1 >> 3;
    for (p = 0; p < depth; p++) {
        uint32_t pl;
        uint64_t f, l;
        if (!(mask >> p & 1u)) continue;
        pl = HL_RD32(table + 4u * p);
        if (!pl || pl == 0xffffffffu) continue;
        f = (uint64_t)pl + (uint64_t)(uint32_t)y0 * bpr + left;
        l = (uint64_t)pl + (uint64_t)(uint32_t)y1 * bpr + right;
        if (l > 0xffffffffull || l < f) HL_DECLINE();
        first[p] = (uint32_t)f; bytes[p] = (uint32_t)(l - f + 1);
        if (hl_opr_overlap(first[p], bytes[p], block, HL_OPR_BLOCK) ||
            hl_opr_overlap(first[p], bytes[p], table, 4u * depth)) HL_DECLINE();
        HL_NEED_W(first[p], bytes[p]);
    }
    HL_DECLARED();
    for (p = 0; p < depth; p++) if (bytes[p]) HL_PTR_W(rows[p], first[p], bytes[p]);
    lm = (uint8_t)(0xffu >> (x0 & 7)); rm = (uint8_t)(0xffu << (7 - (x1 & 7)));
    if (left == right) lm &= rm;
    inner = right > left ? right - left - 1 : 0;
    for (p = 0; p < depth; p++) if (rows[p]) {
        uint8_t *row = rows[p];
        drawn++;
        for (y = 0; y < height; y++, row += bpr) {
            uint32_t i;
            if (mode & 1u) {
                row[0] ^= lm;
                if (right > left) { for (i = 1; i <= inner; i++) row[i] ^= 0xffu; row[right - left] ^= rm; }
            } else if (pen >> p & 1u) {
                row[0] |= lm;
                if (right > left) { for (i = 1; i <= inner; i++) row[i] = 0xffu; row[right - left] |= rm; }
            } else {
                row[0] &= (uint8_t)~lm;
                if (right > left) { for (i = 1; i <= inner; i++) row[i] = 0; row[right - left] &= (uint8_t)~rm; }
            }
        }
    }
    return drawn;
}
