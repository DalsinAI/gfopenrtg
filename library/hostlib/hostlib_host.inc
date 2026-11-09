/* SPDX-License-Identifier: MIT */
/* Written by hostlib/gen.py from hostlib.toml: change that, then run it.
 * The runtime's side of hostlib (HOSTLIB-ABI.md): included by gfosmotherboards'
 * amiga-native-bodies (amiga_bodies.c) after jit_bodies.h, with hostlib/ vendored
 * beside it. Defines HOSTLIB_DEFS, the table's entries. */
#define HL_HOST 1
static uint8_t *hl_host_mem(void) { return jit_bodies_mem; }
static int hl_host_plain(uint32_t at, uint32_t n, int write) { return jb_plain(at, n, write); }
static int hl_host_aliased(uint32_t at, uint32_t n) { return jb_aliased(at, n); }
static void hl_host_written(uint32_t at, uint32_t n) { jit_guest_written(at, n); }
#include "hl.h"
#include "hl_ids.h"
static const char hostlib_manifest[] = "hostlib abi 1, manifest " HOSTLIB_MANIFEST ", functions: 5";
#include "ogfx/planar_rect.c"
#include "ogfx/chunky_rect.c"
#include "ogfx/chunky_copy.c"
#include "ogfx/planar_blit.c"
#include "text/text_strip.c"

/* hostlib_ogfx_planar_rect (HL_ID_OGFX_PLANAR_RECT): the call; 0, or why it is the 68k code's */
static int hlw_hostlib_ogfx_planar_rect(ac090 *c, const uint32_t *arg, uint32_t *cost) {
    struct hl_ctx hl;
    hl_init(&hl, 0);
    uint32_t r = hostlib_ogfx_planar_rect_host(&hl, arg[0]);
    if (hl.why) return hl.why;
    c->d[0] = r;
    *cost = (uint32_t)(hl.bytes / 4) + 8;
    return 0;
}
/* what it writes, for AC090_MAGIC=verify: every declared write range, as one */
static void hlm_hostlib_ogfx_planar_rect(const uint32_t *arg, uint32_t *at, uint32_t *n) {
    struct hl_ctx hl;
    uint64_t lo = ~0ull, hi = 0;
    hl_init(&hl, 1);
    (void)hostlib_ogfx_planar_rect_host(&hl, arg[0]);
    for (uint32_t i = 0; i < hl.nw; i++) {
        if (hl.wlo[i] < lo) lo = hl.wlo[i];
        if ((uint64_t)hl.wlo[i] + hl.wn[i] > hi) hi = (uint64_t)hl.wlo[i] + hl.wn[i];
    }
    *at = hi > lo ? (uint32_t)lo : 0;
    *n = hi > lo ? (uint32_t)(hi - lo) : 0;
}

/* hostlib_ogfx_chunky_rect (HL_ID_OGFX_CHUNKY_RECT): the call; 0, or why it is the 68k code's */
static int hlw_hostlib_ogfx_chunky_rect(ac090 *c, const uint32_t *arg, uint32_t *cost) {
    struct hl_ctx hl;
    hl_init(&hl, 0);
    uint32_t r = hostlib_ogfx_chunky_rect_host(&hl, arg[0]);
    if (hl.why) return hl.why;
    c->d[0] = r;
    *cost = (uint32_t)(hl.bytes / 4) + 8;
    return 0;
}
/* what it writes, for AC090_MAGIC=verify: every declared write range, as one */
static void hlm_hostlib_ogfx_chunky_rect(const uint32_t *arg, uint32_t *at, uint32_t *n) {
    struct hl_ctx hl;
    uint64_t lo = ~0ull, hi = 0;
    hl_init(&hl, 1);
    (void)hostlib_ogfx_chunky_rect_host(&hl, arg[0]);
    for (uint32_t i = 0; i < hl.nw; i++) {
        if (hl.wlo[i] < lo) lo = hl.wlo[i];
        if ((uint64_t)hl.wlo[i] + hl.wn[i] > hi) hi = (uint64_t)hl.wlo[i] + hl.wn[i];
    }
    *at = hi > lo ? (uint32_t)lo : 0;
    *n = hi > lo ? (uint32_t)(hi - lo) : 0;
}

/* hostlib_ogfx_chunky_copy (HL_ID_OGFX_CHUNKY_COPY): the call; 0, or why it is the 68k code's */
static int hlw_hostlib_ogfx_chunky_copy(ac090 *c, const uint32_t *arg, uint32_t *cost) {
    struct hl_ctx hl;
    hl_init(&hl, 0);
    uint32_t r = hostlib_ogfx_chunky_copy_host(&hl, arg[0]);
    if (hl.why) return hl.why;
    c->d[0] = r;
    *cost = (uint32_t)(hl.bytes / 4) + 8;
    return 0;
}
/* what it writes, for AC090_MAGIC=verify: every declared write range, as one */
static void hlm_hostlib_ogfx_chunky_copy(const uint32_t *arg, uint32_t *at, uint32_t *n) {
    struct hl_ctx hl;
    uint64_t lo = ~0ull, hi = 0;
    hl_init(&hl, 1);
    (void)hostlib_ogfx_chunky_copy_host(&hl, arg[0]);
    for (uint32_t i = 0; i < hl.nw; i++) {
        if (hl.wlo[i] < lo) lo = hl.wlo[i];
        if ((uint64_t)hl.wlo[i] + hl.wn[i] > hi) hi = (uint64_t)hl.wlo[i] + hl.wn[i];
    }
    *at = hi > lo ? (uint32_t)lo : 0;
    *n = hi > lo ? (uint32_t)(hi - lo) : 0;
}

/* hostlib_ogfx_planar_blit (HL_ID_OGFX_PLANAR_BLIT): the call; 0, or why it is the 68k code's */
static int hlw_hostlib_ogfx_planar_blit(ac090 *c, const uint32_t *arg, uint32_t *cost) {
    struct hl_ctx hl;
    hl_init(&hl, 0);
    uint32_t r = hostlib_ogfx_planar_blit_host(&hl, arg[0]);
    if (hl.why) return hl.why;
    c->d[0] = r;
    *cost = (uint32_t)(hl.bytes / 4) + 8;
    return 0;
}
/* what it writes, for AC090_MAGIC=verify: every declared write range, as one */
static void hlm_hostlib_ogfx_planar_blit(const uint32_t *arg, uint32_t *at, uint32_t *n) {
    struct hl_ctx hl;
    uint64_t lo = ~0ull, hi = 0;
    hl_init(&hl, 1);
    (void)hostlib_ogfx_planar_blit_host(&hl, arg[0]);
    for (uint32_t i = 0; i < hl.nw; i++) {
        if (hl.wlo[i] < lo) lo = hl.wlo[i];
        if ((uint64_t)hl.wlo[i] + hl.wn[i] > hi) hi = (uint64_t)hl.wlo[i] + hl.wn[i];
    }
    *at = hi > lo ? (uint32_t)lo : 0;
    *n = hi > lo ? (uint32_t)(hi - lo) : 0;
}

/* hostlib_text_strip (HL_ID_OGFX_TEXT_STRIP): the call; 0, or why it is the 68k code's */
static int hlw_hostlib_text_strip(ac090 *c, const uint32_t *arg, uint32_t *cost) {
    struct hl_ctx hl;
    hl_init(&hl, 0);
    uint32_t r = hostlib_text_strip_host(&hl, arg[0]);
    if (hl.why) return hl.why;
    c->d[0] = r;
    *cost = (uint32_t)(hl.bytes / 4) + 8;
    return 0;
}
/* what it writes, for AC090_MAGIC=verify: every declared write range, as one */
static void hlm_hostlib_text_strip(const uint32_t *arg, uint32_t *at, uint32_t *n) {
    struct hl_ctx hl;
    uint64_t lo = ~0ull, hi = 0;
    hl_init(&hl, 1);
    (void)hostlib_text_strip_host(&hl, arg[0]);
    for (uint32_t i = 0; i < hl.nw; i++) {
        if (hl.wlo[i] < lo) lo = hl.wlo[i];
        if ((uint64_t)hl.wlo[i] + hl.wn[i] > hi) hi = (uint64_t)hl.wlo[i] + hl.wn[i];
    }
    *at = hi > lo ? (uint32_t)lo : 0;
    *n = hi > lo ? (uint32_t)(hi - lo) : 0;
}

#define HOSTLIB_DEFS \
    { 0x00010100, "hostlib_ogfx_planar_rect", 1, hlw_hostlib_ogfx_planar_rect, hlm_hostlib_ogfx_planar_rect, NULL, NULL }, \
    { 0x00010101, "hostlib_ogfx_chunky_rect", 1, hlw_hostlib_ogfx_chunky_rect, hlm_hostlib_ogfx_chunky_rect, NULL, NULL }, \
    { 0x00010102, "hostlib_ogfx_chunky_copy", 1, hlw_hostlib_ogfx_chunky_copy, hlm_hostlib_ogfx_chunky_copy, NULL, NULL }, \
    { 0x00010103, "hostlib_ogfx_planar_blit", 1, hlw_hostlib_ogfx_planar_blit, hlm_hostlib_ogfx_planar_blit, NULL, NULL }, \
    { 0x00010105, "hostlib_text_strip", 1, hlw_hostlib_text_strip, hlm_hostlib_text_strip, NULL, NULL }, \

#undef HL_HOST
