/* SPDX-License-Identifier: MIT */
#include "leaves.h"
#include "../hl_ids.h"

HL_TAG(hostlib_ogfx_chunky_rect, HL_ID_OGFX_CHUNKY_RECT)
HL_FN(hostlib_ogfx_chunky_rect)
{
    hl_addr base;
    uint32_t bpr, bpp, color, mode, width, height, x, k, y;
    int32_t x0, y0, x1, y1;
    uint64_t first64, last64;
    uint8_t px[4], *first;
    HL_NEED_R(block, HL_OCR_BLOCK);
    base = HL_ARG(HL_OCR_BASE); bpr = HL_ARG(HL_OCR_BPR); bpp = HL_ARG(HL_OCR_BPP);
    x0 = (int32_t)HL_ARG(HL_OCR_X0); y0 = (int32_t)HL_ARG(HL_OCR_Y0);
    x1 = (int32_t)HL_ARG(HL_OCR_X1); y1 = (int32_t)HL_ARG(HL_OCR_Y1);
    color = HL_ARG(HL_OCR_COLOR); mode = HL_ARG(HL_OCR_MODE);
    if (bpp < 1 || bpp > 4 || x0 < 0 || y0 < 0 || x1 < x0 || y1 < y0) return 0;
    width = (uint32_t)(x1 - x0) + 1; height = (uint32_t)(y1 - y0) + 1;
    if ((uint64_t)width * bpp > 0xffffffffull) HL_DECLINE();
    first64 = (uint64_t)base + (uint64_t)(uint32_t)y0 * bpr + (uint64_t)(uint32_t)x0 * bpp;
    last64 = (uint64_t)base + (uint64_t)(uint32_t)y1 * bpr + (uint64_t)(uint32_t)x1 * bpp + bpp - 1;
    if (last64 > 0xffffffffull || last64 < first64) HL_DECLINE();
    HL_NEED_W((uint32_t)first64, last64 - first64 + 1);
    HL_DECLARED();
    HL_PTR_W(first, (uint32_t)first64, (uint32_t)(last64 - first64 + 1));
    for (k = 0; k < bpp; k++) px[k] = (uint8_t)(color >> (8u * (bpp - 1 - k)));
    for (y = 0; y < height; y++) {
        uint8_t *q = first + y * bpr;
        for (x = 0; x < width; x++) for (k = 0; k < bpp; k++, q++)
            if (mode & 1u) *q ^= px[k]; else *q = px[k];
    }
    return width * height;
}
