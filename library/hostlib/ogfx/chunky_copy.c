/* SPDX-License-Identifier: MIT */
#include "leaves.h"
#include "../hl_ids.h"

static void hl_occ_move(uint8_t *d, const uint8_t *s, uint32_t n)
{
    if (d < s) while (n--) *d++ = *s++;
    else if (d > s) { d += n; s += n; while (n--) *--d = *--s; }
}

HL_TAG(hostlib_ogfx_chunky_copy, HL_ID_OGFX_CHUNKY_COPY)
HL_FN(hostlib_ogfx_chunky_copy)
{
    hl_addr src, dst;
    uint32_t sbpr, dbpr, bpp, rowbytes, sfirst, dfirst, sn, dn;
    int32_t sx, sy, dx, dy, width, height, y;
    uint64_t sf, sl, df, dl;
    const uint8_t *sp;
    uint8_t *dp;
    HL_NEED_R(block, HL_OCC_BLOCK);
    src = HL_ARG(HL_OCC_SRC); sbpr = HL_ARG(HL_OCC_SBPR);
    dst = HL_ARG(HL_OCC_DST); dbpr = HL_ARG(HL_OCC_DBPR); bpp = HL_ARG(HL_OCC_BPP);
    sx = (int32_t)HL_ARG(HL_OCC_SX); sy = (int32_t)HL_ARG(HL_OCC_SY);
    dx = (int32_t)HL_ARG(HL_OCC_DX); dy = (int32_t)HL_ARG(HL_OCC_DY);
    width = (int32_t)HL_ARG(HL_OCC_WIDTH); height = (int32_t)HL_ARG(HL_OCC_HEIGHT);
    if (bpp < 1 || bpp > 4 || sx < 0 || sy < 0 || dx < 0 || dy < 0 || width <= 0 || height <= 0) return 0;
    if ((uint64_t)(uint32_t)width * bpp > 0xffffffffull) HL_DECLINE();
    rowbytes = (uint32_t)width * bpp;
    sf = (uint64_t)src + (uint64_t)(uint32_t)sy * sbpr + (uint64_t)(uint32_t)sx * bpp;
    sl = sf + (uint64_t)(uint32_t)(height - 1) * sbpr + rowbytes - 1;
    df = (uint64_t)dst + (uint64_t)(uint32_t)dy * dbpr + (uint64_t)(uint32_t)dx * bpp;
    dl = df + (uint64_t)(uint32_t)(height - 1) * dbpr + rowbytes - 1;
    if (sl > 0xffffffffull || dl > 0xffffffffull || sl < sf || dl < df) HL_DECLINE();
    sfirst = (uint32_t)sf; dfirst = (uint32_t)df;
    sn = (uint32_t)(sl - sf + 1); dn = (uint32_t)(dl - df + 1);
    (void)sn; (void)dn; /* range lengths are consumed only by host macros */
    HL_NEED_R(sfirst, sn); HL_NEED_W(dfirst, dn);
    HL_DECLARED();
    HL_PTR_R(sp, sfirst, sn); HL_PTR_W(dp, dfirst, dn);
    if (df > sf) for (y = height - 1; y >= 0; y--)
        hl_occ_move(dp + (uint32_t)y * dbpr, sp + (uint32_t)y * sbpr, rowbytes);
    else for (y = 0; y < height; y++)
        hl_occ_move(dp + (uint32_t)y * dbpr, sp + (uint32_t)y * sbpr, rowbytes);
    return (uint32_t)width * (uint32_t)height;
}
