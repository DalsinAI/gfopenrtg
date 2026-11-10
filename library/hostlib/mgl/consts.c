/* SPDX-License-Identifier: MIT */
#include "consts.h"
#include "../hl_ids.h"

/* virgl SET_CONSTANT_BUFFER: command 12, object 0, length n*4+2. */
#define HL_MC_VCMD 12u

static void mc_be(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}
static void mc_le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static int mc_apart(uint32_t a, uint32_t an, uint32_t b, uint32_t bn)
{
    return an == 0 || bn == 0 || (uint64_t)a + an <= b || (uint64_t)b + bn <= a;
}

#if defined(HL_HOST) && defined(HOSTLIB_TEST_STOP)
extern uint32_t hostlib_mgl_consts_stop;
#endif

/* Returns 1 when a command was appended, 0 when the constants are the ones
 * already sent or the call is rejected. A rejected call writes nothing.
 * The command words are little-endian, as vgc_constants writes them; the
 * last-sent copy stays in the guest's byte order. A part does not do this:
 * the last-sent values are state, so a decline does not send twice. */
HL_TAG_LOCKED(hostlib_mgl_consts, HL_ID_MGL_CONSTS)
HL_FN(hostlib_mgl_consts)
{
    hl_addr state, src, cmd;
    uint32_t cap, n, shader, cmdcap, nlast, ncmd, words, bytes, slen, i, same;
    uint8_t *st, *cw;
    const uint8_t *sp;
    HL_NEED_R(block, HL_MC_BLOCK);
    state = HL_ARG(HL_MC_STATE);
    cap = HL_ARG(HL_MC_CAP);
    src = HL_ARG(HL_MC_SRC);
    n = HL_ARG(HL_MC_N);
    shader = HL_ARG(HL_MC_SHADER);
    cmd = HL_ARG(HL_MC_CMD);
    cmdcap = HL_ARG(HL_MC_CMD_CAP);
    if (cap == 0 || cap > 4095 || n > cap || shader > 1) return 0;
    slen = HL_MC_HDR + cap * 16u;
    words = n * 4u + 3u;
    bytes = n * 16u;
    HL_NEED_S(state, slen);
    nlast = HL_RD32(state);
    ncmd = HL_RD32(state + 4);
    if (n && (uint64_t)src + bytes > 0xffffffffull) return 0;
    if (n && !mc_apart(src, bytes, state, slen)) return 0;
    if (n) HL_NEED_R(src, bytes);
    same = nlast == n;
    if (same && n) {
        for (i = 0; i < n * 4u; i++) {
            uint32_t a = HL_RD32(src + i * 4u), b = HL_RD32(state + HL_MC_HDR + i * 4u);
#if !defined(HL_HOST) && defined(__mc68000__)
            /* a in a data register: two memory longs compared make GCC emit
             * CMPM.L (gfopenrtg #246), which older AC090 JITs interpret */
            __asm__("" : "+d"(a));
#endif
            if (a != b) { same = 0; break; }
        }
    }
    if (same) return 0;
    if ((uint64_t)ncmd + words > cmdcap) return 0;
    if ((uint64_t)cmd + (uint64_t)(ncmd + words) * 4u > 0xffffffffull) return 0;
    if (!mc_apart(cmd + ncmd * 4u, words * 4u, state, slen)) return 0;
    if (n && !mc_apart(src, bytes, cmd + ncmd * 4u, words * 4u)) return 0;
    HL_NEED_W(cmd + ncmd * 4u, words * 4u);
    HL_DECLARED();
#if defined(HL_HOST) && defined(HOSTLIB_TEST_STOP)
    if (hostlib_mgl_consts_stop == 0xffffffffu) HL_DECLINE();
#endif
    if (n) HL_PTR_R(sp, src, bytes);
    else sp = 0;
    HL_PTR_W(cw, cmd + ncmd * 4u, words * 4u);
    HL_PTR_W(st, state, slen);
    mc_le(cw + 0, HL_MC_VCMD | (words - 1u) << 16);
    mc_le(cw + 4, shader);
    mc_le(cw + 8, 0);
    for (i = 0; i < n * 4u; i++) {
        const uint8_t *s = sp + i * 4u;
        uint32_t w = ((uint32_t)s[0] << 24) | ((uint32_t)s[1] << 16) | ((uint32_t)s[2] << 8) | s[3];
        mc_le(cw + 12 + i * 4u, w);
        st[HL_MC_HDR + i * 4u] = s[0]; st[HL_MC_HDR + i * 4u + 1] = s[1];
        st[HL_MC_HDR + i * 4u + 2] = s[2]; st[HL_MC_HDR + i * 4u + 3] = s[3];
#if defined(HL_HOST) && defined(HOSTLIB_TEST_STOP)
        if (hostlib_mgl_consts_stop && i + 1 == hostlib_mgl_consts_stop) HL_DECLINE();
#endif
    }
    mc_be(st, n);
    mc_be(st + 4, ncmd + words);
    return 1;
}
