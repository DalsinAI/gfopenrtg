/* SPDX-License-Identifier: MIT */
#include "vertices.h"
#include "../hl_ids.h"

static void mv_be(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}
static int mv_apart(uint32_t a, uint32_t an, uint32_t b, uint32_t bn)
{
    return an == 0 || bn == 0 || (uint64_t)a + an <= b || (uint64_t)b + bn <= a;
}
static int mv_fits(uint32_t off, uint32_t n, uint32_t stride)
{
    return n == 0 || (uint64_t)off + n <= stride;
}

#if defined(HL_HOST) && defined(HOSTLIB_TEST_STOP)
extern uint32_t hostlib_mgl_vertices_stop;
#endif

/* Copies count vertices into the arena and advances the cursor. Positions
 * are comps big-endian floats (2 to 4). A colour, when its address is not
 * 0, is four raw bytes. Texture coordinates, when theirs is not 0, are
 * tc_comps floats. A stride of 0 on a source means the values are packed.
 * Every byte of each vertex is written (the rest zero), so the new arena
 * bytes are a write range. A part does not do this: the cursor is state. */
HL_TAG_LOCKED(hostlib_mgl_vertices, HL_ID_MGL_VERTICES)
HL_FN(hostlib_mgl_vertices)
{
    hl_addr state, arena, pos, color, tc;
    uint32_t cap, count, stride, pos_st, comps, col_st, col_off, tc_st, tc_comps, tc_off;
    uint32_t nva, done, bytes, i, k;
    uint32_t pos_n, col_n, tc_n;
    uint8_t *st, *dst;
    const uint8_t *pp, *cp, *tp;
    HL_NEED_R(block, HL_MV_BLOCK);
    state = HL_ARG(HL_MV_STATE);
    arena = HL_ARG(HL_MV_ARENA);
    cap = HL_ARG(HL_MV_CAP);
    count = HL_ARG(HL_MV_COUNT);
    stride = HL_ARG(HL_MV_STRIDE);
    pos = HL_ARG(HL_MV_POS);
    pos_st = HL_ARG(HL_MV_POS_STRIDE);
    comps = HL_ARG(HL_MV_COMPS);
    color = HL_ARG(HL_MV_COLOR);
    col_st = HL_ARG(HL_MV_COLOR_STRIDE);
    col_off = HL_ARG(HL_MV_COLOR_OFF);
    tc = HL_ARG(HL_MV_TC);
    tc_st = HL_ARG(HL_MV_TC_STRIDE);
    tc_comps = HL_ARG(HL_MV_TC_COMPS);
    tc_off = HL_ARG(HL_MV_TC_OFF);
    if (!count || comps < 2 || comps > 4 || !stride || stride > 64) return 0;
    if (color && (col_off > 60 || col_off + 4 > stride)) return 0;
    if (tc && (tc_comps < 1 || tc_comps > 4 || tc_off > 60 || tc_off + tc_comps * 4 > stride)) return 0;
    if (!mv_fits(0, comps * 4, stride)) return 0;
    if (color && col_off < comps * 4) return 0;
    if (tc && !mv_fits(tc_off, tc_comps * 4, stride)) return 0;
    if (tc && color && !mv_apart(col_off, 4, tc_off, tc_comps * 4)) return 0;
    if ((uint64_t)count * stride > cap) return 0;
    bytes = count * stride;
    if (!pos_st) pos_st = comps * 4;
    if (color && !col_st) col_st = 4;
    if (tc && !tc_st) tc_st = tc_comps * 4;
    if (pos_st < comps * 4) return 0;
    if (color && col_st < 4) return 0;
    if (tc && tc_st < tc_comps * 4) return 0;
    pos_n = (count - 1) * pos_st + comps * 4;
    col_n = color ? (count - 1) * col_st + 4 : 0;
    tc_n = tc ? (count - 1) * tc_st + tc_comps * 4 : 0;
    HL_NEED_S(state, HL_MV_STATE_BYTES);
    nva = HL_RD32(state);
    done = HL_RD32(state + 4);
    if ((uint64_t)nva + bytes > cap) return 0;
    if ((uint64_t)arena + nva + bytes > 0xffffffffull) return 0;
    if ((uint64_t)pos + pos_n > 0xffffffffull) return 0;
    if (color && (uint64_t)color + col_n > 0xffffffffull) return 0;
    if (tc && (uint64_t)tc + tc_n > 0xffffffffull) return 0;
    if (!mv_apart(pos, pos_n, arena + nva, bytes) || !mv_apart(pos, pos_n, state, HL_MV_STATE_BYTES)) return 0;
    if (color && (!mv_apart(color, col_n, arena + nva, bytes) || !mv_apart(color, col_n, state, HL_MV_STATE_BYTES))) return 0;
    if (tc && (!mv_apart(tc, tc_n, arena + nva, bytes) || !mv_apart(tc, tc_n, state, HL_MV_STATE_BYTES))) return 0;
    if (!mv_apart(arena + nva, bytes, state, HL_MV_STATE_BYTES)) return 0;
    HL_NEED_R(pos, pos_n);
    if (color) HL_NEED_R(color, col_n);
    if (tc) HL_NEED_R(tc, tc_n);
    HL_NEED_W(arena + nva, bytes);
    HL_DECLARED();
#if defined(HL_HOST) && defined(HOSTLIB_TEST_STOP)
    if (hostlib_mgl_vertices_stop == 0xffffffffu) HL_DECLINE();
#endif
    HL_PTR_R(pp, pos, pos_n);
    cp = 0; tp = 0;
    if (color) HL_PTR_R(cp, color, col_n);
    if (tc) HL_PTR_R(tp, tc, tc_n);
    HL_PTR_W(dst, arena + nva, bytes);
    HL_PTR_W(st, state, HL_MV_STATE_BYTES);
    for (i = 0; i < count; i++) {
        uint8_t *d = dst + i * stride;
        const uint8_t *s = pp + i * pos_st;
        for (k = 0; k < stride; k++) d[k] = 0;
        for (k = 0; k < comps * 4; k++) d[k] = s[k];
        if (color) for (k = 0; k < 4; k++) d[col_off + k] = cp[i * col_st + k];
        if (tc) for (k = 0; k < tc_comps * 4; k++) d[tc_off + k] = tp[i * tc_st + k];
#if defined(HL_HOST) && defined(HOSTLIB_TEST_STOP)
        if (hostlib_mgl_vertices_stop && i + 1 == hostlib_mgl_vertices_stop) HL_DECLINE();
#endif
    }
    mv_be(st, nva + bytes);
    mv_be(st + 4, done + count);
    return count;
}
