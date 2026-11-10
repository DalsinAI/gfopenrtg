/* SPDX-License-Identifier: MIT */
#include "blend.h"
#include "../hl_ids.h"

#if defined(HL_HOST) && defined(HOSTLIB_TEST_STOP)
/* The host test sets this to decline after that many pixels, or to all-ones
 * to decline before the first. 0 leaves the blit alone. */
extern uint32_t hostlib_sdl2_stop;
#endif

/* SDL 2.32 ALPHA_BLEND_RGBA, integer division toward zero. */
static uint8_t sbl_ch(int32_t s, int32_t d, int32_t a)
{
    return (uint8_t)((((s - d) * a) / 255) + d);
}
static uint8_t sbl_a(int32_t s, int32_t d)
{
    return (uint8_t)(s + d - (s * d) / 255);
}

/* The destination is state, and a part may not write state (8.3). The rows
 * are independent, but spreading them would write the bytes a decline has
 * to put back, so this stays one loop. A fill can be parallel: it replaces
 * a write range, and a half-run is rewritten by the 68k body. */
HL_TAG_LOCKED(hostlib_sdl2_blend, HL_ID_SDL2_BLEND)
HL_FN(hostlib_sdl2_blend)
{
    hl_addr src, dst;
    uint32_t sstride, dstride, width, height, row, x, pixels;
    uint32_t sspan, dspan;
    const uint8_t *sp;
    uint8_t *dp;
    HL_NEED_R(block, HL_SBL_BLOCK);
    src = HL_ARG(HL_SBL_SRC);
    dst = HL_ARG(HL_SBL_DST);
    sstride = HL_ARG(HL_SBL_SSTRIDE);
    dstride = HL_ARG(HL_SBL_DSTRIDE);
    width = HL_ARG(HL_SBL_WIDTH);
    height = HL_ARG(HL_SBL_HEIGHT);
    if (!width || !height) return 0;
    if (width > 0x3fffffffu || sstride < width * 4u || dstride < width * 4u) return 0;
    if ((uint64_t)(height - 1) * sstride + (uint64_t)width * 4u > 0xffffffffull) return 0;
    if ((uint64_t)(height - 1) * dstride + (uint64_t)width * 4u > 0xffffffffull) return 0;
    sspan = (height - 1) * sstride + width * 4u;
    dspan = (height - 1) * dstride + width * 4u;
    if ((uint64_t)src + sspan > 0xffffffffull || (uint64_t)dst + dspan > 0xffffffffull) return 0;
    /* Overlap is the 68k body's: both builds leave the picture and return 0,
     * so a decline is not a second blend. A span over the state limit is
     * only the host's to refuse; the 68k body still blends it. */
    if (!(src + sspan <= dst || dst + dspan <= src)) return 0;
    if (!(src + sspan <= block || block + HL_SBL_BLOCK <= src)) return 0;
    if (!(dst + dspan <= block || block + HL_SBL_BLOCK <= dst)) return 0;
    HL_NEED_R(src, sspan);
    HL_NEED_S(dst, dspan);
    HL_DECLARED();
#if defined(HL_HOST) && defined(HOSTLIB_TEST_STOP)
    if (hostlib_sdl2_stop == 0xffffffffu) HL_DECLINE();
#endif
    HL_PTR_R(sp, src, sspan);
    HL_PTR_W(dp, dst, dspan);
    pixels = 0;
    for (row = 0; row < height; row++) {
        const uint8_t *s = sp + row * sstride;
        uint8_t *d = dp + row * dstride;
        for (x = 0; x < width; x++, s += 4, d += 4) {
            int32_t a = s[0];
            if (a) {
                int32_t da = d[0];
                d[1] = sbl_ch(s[1], d[1], a);
                d[2] = sbl_ch(s[2], d[2], a);
                d[3] = sbl_ch(s[3], d[3], a);
                d[0] = sbl_a(a, da);
            }
            pixels++;
#if defined(HL_HOST) && defined(HOSTLIB_TEST_STOP)
            if (hostlib_sdl2_stop && pixels == hostlib_sdl2_stop) HL_DECLINE();
#endif
        }
    }
    return width * height;
}
