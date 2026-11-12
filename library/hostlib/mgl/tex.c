/* SPDX-License-Identifier: MIT */
#include "tex.h"
#include "../hl_ids.h"

/* ACV_PUT, 16 words: the header, the result, and vg_put's 14 arguments. */
#define HL_MT_HDR 0x000B0010u

static void mt_be(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}
static int mt_apart(uint32_t a, uint32_t an, uint32_t b, uint32_t bn)
{
    return an == 0 || bn == 0 || (uint64_t)a + an <= b || (uint64_t)b + bn <= a;
}

#if defined(HL_HOST) && defined(HOSTLIB_TEST_STOP)
extern uint32_t hostlib_mgl_tex_stop;
#endif

/* Writes one ACV_PUT (the Amiga's byte order, as vg_blk_cmd does) and
 * counts it. The texels are read and not changed: the command names them,
 * and the host turns the units round when the block runs. A part does not
 * do this: the block cursor is state. */
HL_TAG_LOCKED(hostlib_mgl_tex, HL_ID_MGL_TEX)
HL_FN(hostlib_mgl_tex)
{
    hl_addr state, pre, src;
    uint32_t cap, handle, ctx, level, stride, x, y, w, h, bytes, swap;
    uint32_t npre, puts;
    uint8_t *st, *p;
    const uint8_t *sp;
    HL_NEED_R(block, HL_MT_BLOCK);
    state = HL_ARG(HL_MT_STATE);
    pre = HL_ARG(HL_MT_PRE);
    cap = HL_ARG(HL_MT_CAP);
    handle = HL_ARG(HL_MT_HANDLE);
    ctx = HL_ARG(HL_MT_CTX);
    level = HL_ARG(HL_MT_LEVEL);
    stride = HL_ARG(HL_MT_STRIDE);
    x = HL_ARG(HL_MT_X);
    y = HL_ARG(HL_MT_Y);
    w = HL_ARG(HL_MT_W);
    h = HL_ARG(HL_MT_H);
    src = HL_ARG(HL_MT_SRC);
    bytes = HL_ARG(HL_MT_BYTES);
    swap = HL_ARG(HL_MT_SWAP);
    if (!w || !h || !bytes) return 0;
    if (swap != 1 && swap != 2 && swap != 4 && swap != 8) return 0;
    HL_NEED_S(state, HL_MT_STATE_BYTES);
    npre = HL_RD32(state);
    puts = HL_RD32(state + 4);
    if ((uint64_t)npre + HL_MT_WORDS > cap) return 0;
    if ((uint64_t)pre + (uint64_t)(npre + HL_MT_WORDS) * 4u > 0xffffffffull) return 0;
    if ((uint64_t)src + bytes > 0xffffffffull) return 0;
    if (!mt_apart(src, bytes, state, HL_MT_STATE_BYTES)) return 0;
    if (!mt_apart(src, bytes, pre + npre * 4u, HL_MT_WORDS * 4u)) return 0;
    if (!mt_apart(pre + npre * 4u, HL_MT_WORDS * 4u, state, HL_MT_STATE_BYTES)) return 0;
    HL_NEED_R(src, bytes);
    HL_NEED_W(pre + npre * 4u, HL_MT_WORDS * 4u);
    HL_DECLARED();
#if defined(HL_HOST) && defined(HOSTLIB_TEST_STOP)
    if (hostlib_mgl_tex_stop == 0xffffffffu) HL_DECLINE();
#endif
    HL_PTR_R(sp, src, bytes);
    HL_PTR_W(p, pre + npre * 4u, HL_MT_WORDS * 4u);
    HL_PTR_W(st, state, HL_MT_STATE_BYTES);
    (void)sp[0];
    (void)sp[bytes - 1];
    mt_be(p + 0, HL_MT_HDR);
    mt_be(p + 4, 0);
    mt_be(p + 8, handle);
    mt_be(p + 12, ctx);
    mt_be(p + 16, level);
    mt_be(p + 20, stride);
    mt_be(p + 24, 0);
    mt_be(p + 28, x);
    mt_be(p + 32, y);
    mt_be(p + 36, 0);
    mt_be(p + 40, w);
    mt_be(p + 44, h);
    mt_be(p + 48, 1);
    mt_be(p + 52, src);
    mt_be(p + 56, bytes);
    mt_be(p + 60, swap);
#if defined(HL_HOST) && defined(HOSTLIB_TEST_STOP)
    if (hostlib_mgl_tex_stop == 1) HL_DECLINE();
#endif
    mt_be(st, npre + HL_MT_WORDS);
    mt_be(st + 4, puts + 1);
    return 1;
}
