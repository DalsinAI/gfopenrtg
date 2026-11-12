/* SPDX-License-Identifier: MIT */
#include "leaves.h"
#include "../hl_ids.h"

/* One disjoint scanline per part: the 68k build runs them in order; ABI 2's
 * host runtime may spread them across its fixed worker pool. */
HL_PART(hl_ocr_row)
{
    uint32_t bpr = HL_ARG(HL_OCR_BPR), bpp = HL_ARG(HL_OCR_BPP);
    uint32_t x0 = HL_ARG(HL_OCR_X0), y0 = HL_ARG(HL_OCR_Y0);
    uint32_t x1 = HL_ARG(HL_OCR_X1), color = HL_ARG(HL_OCR_COLOR);
    uint32_t mode = HL_ARG(HL_OCR_MODE), width = x1 - x0 + 1, x, k;
    hl_addr row = HL_ARG(HL_OCR_BASE) + (y0 + part) * bpr + x0 * bpp;
    uint8_t px[4], *q;
    (void)parts;
#ifdef HL_HOST
    if (!hl_span(hl_, row, width * bpp, 1)) return;
    q = hl_mem_at(hl_, row);
#else
    q = (uint8_t *)(uintptr_t)row;
#endif
    for (k = 0; k < bpp; k++) px[k] = (uint8_t)(color >> (8u * (bpp - 1 - k)));
    for (x = 0; x < width; x++) for (k = 0; k < bpp; k++, q++)
        if (mode & 1u) *q ^= px[k]; else *q = px[k];
}

HL_TAG(hostlib_ogfx_chunky_rect, HL_ID_OGFX_CHUNKY_RECT)
HL_FN(hostlib_ogfx_chunky_rect)
{
    hl_addr base;
    uint32_t bpr, bpp, width, height;
    int32_t x0, y0, x1, y1;
    uint64_t first64, last64;
    HL_NEED_R(block, HL_OCR_BLOCK);
    base = HL_ARG(HL_OCR_BASE); bpr = HL_ARG(HL_OCR_BPR); bpp = HL_ARG(HL_OCR_BPP);
    x0 = (int32_t)HL_ARG(HL_OCR_X0); y0 = (int32_t)HL_ARG(HL_OCR_Y0);
    x1 = (int32_t)HL_ARG(HL_OCR_X1); y1 = (int32_t)HL_ARG(HL_OCR_Y1);
    if (bpp < 1 || bpp > 4 || x0 < 0 || y0 < 0 || x1 < x0 || y1 < y0) return 0;
    width = (uint32_t)(x1 - x0) + 1; height = (uint32_t)(y1 - y0) + 1;
    if ((uint64_t)width * bpp > 0xffffffffull) HL_DECLINE();
    first64 = (uint64_t)base + (uint64_t)(uint32_t)y0 * bpr + (uint64_t)(uint32_t)x0 * bpp;
    last64 = (uint64_t)base + (uint64_t)(uint32_t)y1 * bpr + (uint64_t)(uint32_t)x1 * bpp + bpp - 1;
    if (last64 > 0xffffffffull || last64 < first64) HL_DECLINE();
    /* rows that share bytes (bytes per row under the row's width): the
     * 68k code writes them in order, the parts would race, so it runs */
    if (height > 1 && (uint64_t)bpr < (uint64_t)width * bpp) HL_DECLINE();
    HL_NEED_W((uint32_t)first64, last64 - first64 + 1);
    HL_DECLARED();
    HL_PARALLEL(height, hl_ocr_row);
#if defined(HL_HOST) && defined(HOSTLIB_TEST_BROKEN)
    /* CI's deliberate bad host build (HOSTLIB-ABI.md D4).  Production never
     * defines this: the runtime test uses it to prove verify catches a wrong
     * generated ABI-2 user, keeps the 68k result and turns the id off. */
    extern int hostlib_test_broken;
    if (hostlib_test_broken) hl_mem_at(hl_, (uint32_t)first64)[0] ^= 1;
#endif
    return width * height;
}
