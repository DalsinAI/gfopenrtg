/* SPDX-License-Identifier: MIT */
#include "draw.h"
#include "../hl_ids.h"

/* virgl's DRAW_VBO header: command 8, object 0, length 12. */
#define HL_MD_DRAW_HDR 0x000C0008u

static void mgl_put_be(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}
static void mgl_put_le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

#if defined(HL_HOST) && defined(HOSTLIB_TEST_STOP)
extern uint32_t hostlib_mgl_stop;
#endif

/* Appends one non-indexed DRAW_VBO and the vertices it reads. The command
 * words are little-endian, as virgl's stream is; the vertices are copied
 * as they stand, which for a 68k float array is the big-endian words the
 * arena keeps (VG_A32). A part does not do this: the cursors are state. */
HL_TAG_LOCKED(hostlib_mgl_draw, HL_ID_MGL_DRAW)
HL_FN(hostlib_mgl_draw)
{
    hl_addr state, arena, cmd, src;
    uint32_t cap, cmdcap, count, comps, prim, start;
    uint32_t nva, ncmd, draws, bytes, i, k;
    uint8_t *st, *dst, *cw;
    const uint8_t *sp;
    HL_NEED_R(block, HL_MD_BLOCK);
    state = HL_ARG(HL_MD_STATE);
    arena = HL_ARG(HL_MD_ARENA);
    cap = HL_ARG(HL_MD_ARENA_CAP);
    cmd = HL_ARG(HL_MD_CMD);
    cmdcap = HL_ARG(HL_MD_CMD_CAP);
    src = HL_ARG(HL_MD_SRC);
    count = HL_ARG(HL_MD_COUNT);
    comps = HL_ARG(HL_MD_COMPS);
    prim = HL_ARG(HL_MD_PRIM);
    start = HL_ARG(HL_MD_START);
    if (!count || comps < 1 || comps > 4) return 0;
    if ((uint64_t)count * comps > 0xffffffffull / 4u) return 0;
    bytes = count * comps * 4u;
    /* The cursors are state, not a read range: a state range may not
     * overlap a read (8.1). They are read through the state range. */
    HL_NEED_S(state, HL_MD_STATE_BYTES);
    nva = HL_RD32(state);
    ncmd = HL_RD32(state + 4);
    draws = HL_RD32(state + 8);
    if ((uint64_t)nva + bytes > cap || (uint64_t)ncmd + HL_MD_DRAW_WORDS > cmdcap) return 0;
    if ((uint64_t)arena + nva + bytes > 0xffffffffull) return 0;
    if ((uint64_t)cmd + (uint64_t)(ncmd + HL_MD_DRAW_WORDS) * 4u > 0xffffffffull) return 0;
    if ((uint64_t)src + bytes > 0xffffffffull) return 0;
    /* The new bytes must not be the source or the cursors: both builds
     * then write nothing, so a decline is not a second append. */
    if (!(src + bytes <= arena + nva || arena + nva + bytes <= src)) return 0;
    if (!(src + bytes <= state || state + HL_MD_STATE_BYTES <= src)) return 0;
    if (!(arena + nva + bytes <= state || state + HL_MD_STATE_BYTES <= arena + nva)) return 0;
    if (!(cmd + ncmd * 4u + HL_MD_DRAW_WORDS * 4u <= state ||
          state + HL_MD_STATE_BYTES <= cmd + ncmd * 4u)) return 0;
    HL_NEED_R(src, bytes);
    HL_NEED_W(arena + nva, bytes);
    HL_NEED_W(cmd + ncmd * 4u, HL_MD_DRAW_WORDS * 4u);
    HL_DECLARED();
#if defined(HL_HOST) && defined(HOSTLIB_TEST_STOP)
    if (hostlib_mgl_stop == 0xffffffffu) HL_DECLINE();
#endif
    HL_PTR_R(sp, src, bytes);
    HL_PTR_W(dst, arena + nva, bytes);
    HL_PTR_W(cw, cmd + ncmd * 4u, HL_MD_DRAW_WORDS * 4u);
    HL_PTR_W(st, state, HL_MD_STATE_BYTES);
    for (i = 0; i < count; i++) {
        for (k = 0; k < comps * 4u; k++) dst[i * comps * 4u + k] = sp[i * comps * 4u + k];
#if defined(HL_HOST) && defined(HOSTLIB_TEST_STOP)
        if (hostlib_mgl_stop && i + 1 == hostlib_mgl_stop) HL_DECLINE();
#endif
    }
    mgl_put_le(cw + 0, HL_MD_DRAW_HDR);
    mgl_put_le(cw + 4, start);
    mgl_put_le(cw + 8, count);
    mgl_put_le(cw + 12, prim);
    mgl_put_le(cw + 16, 0);
    mgl_put_le(cw + 20, 1);
    mgl_put_le(cw + 24, 0);
    mgl_put_le(cw + 28, 0);
    mgl_put_le(cw + 32, 0);
    mgl_put_le(cw + 36, 0);
    mgl_put_le(cw + 40, 0);
    mgl_put_le(cw + 44, 0xffffffffu);
    mgl_put_le(cw + 48, 0);
    mgl_put_be(st, nva + bytes);
    mgl_put_be(st + 4, ncmd + HL_MD_DRAW_WORDS);
    mgl_put_be(st + 8, draws + 1);
    return count;
}
