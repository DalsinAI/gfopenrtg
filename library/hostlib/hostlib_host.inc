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
static const char hostlib_manifest[] = "hostlib abi 2, manifest " HOSTLIB_MANIFEST ", functions: 14";
#include "ogfx/planar_rect.c"
#include "ogfx/chunky_rect.c"
#include "ogfx/chunky_copy.c"
#include "ogfx/planar_blit.c"
#include "text/text_strip.c"
#include "sdl/blend.c"
#include "sdl/blend_argb.c"
#include "mgl/draw.c"
#include "mgl/vertices.c"
#include "mgl/tex.c"
#include "zlib/inflate_codes.c"
#include "zlib/deflate_all.c"
#include "lzma/lzma_dec.c"

/* hostlib_ogfx_planar_rect (HL_ID_OGFX_PLANAR_RECT): the call; 0, or why it is the 68k code's */
static int hlw_hostlib_ogfx_planar_rect(ac090 *c, const uint32_t *arg, uint32_t *cost) {
    struct hl_ctx hl;
    hl_init(&hl, 0);
    uint32_t r = hostlib_ogfx_planar_rect_host(&hl, arg[0]);
    if (hl.why) { hl_restore_state(&hl); return hl.why; }
    c->d[0] = r;
    *cost = (uint32_t)(hl.bytes / 4) + 8;
    return 0;
}
/* ABI 2's dry run: preserve every disjoint write and state range. */
static int hls_hostlib_ogfx_planar_rect(const uint32_t *arg, magic_spans *out) {
    struct hl_ctx hl;
    hl_init(&hl, 1);
    (void)hostlib_ogfx_planar_rect_host(&hl, arg[0]);
    if (hl.why) return hl.why;
    if (hl.nw + hl.ns > MAGIC_SPANS) return HL_WHY_INPUT;
    for (uint32_t i = 0; i < hl.nw; i++) {
        out->s[out->n++] = (magic_span){ hl.wlo[i], hl.wn[i], MAGIC_SPAN_W };
    }
    for (uint32_t i = 0; i < hl.ns; i++) {
        out->s[out->n++] = (magic_span){ hl.slo[i], hl.sn[i], MAGIC_SPAN_S };
    }
    return 0;
}

/* hostlib_ogfx_chunky_rect (HL_ID_OGFX_CHUNKY_RECT): the call; 0, or why it is the 68k code's */
static int hlw_hostlib_ogfx_chunky_rect(ac090 *c, const uint32_t *arg, uint32_t *cost) {
    struct hl_ctx hl;
    hl_init(&hl, 0);
    uint32_t r = hostlib_ogfx_chunky_rect_host(&hl, arg[0]);
    if (hl.why) { hl_restore_state(&hl); return hl.why; }
    c->d[0] = r;
    *cost = (uint32_t)(hl.bytes / 4) + 8;
    return 0;
}
/* ABI 2's dry run: preserve every disjoint write and state range. */
static int hls_hostlib_ogfx_chunky_rect(const uint32_t *arg, magic_spans *out) {
    struct hl_ctx hl;
    hl_init(&hl, 1);
    (void)hostlib_ogfx_chunky_rect_host(&hl, arg[0]);
    if (hl.why) return hl.why;
    if (hl.nw + hl.ns > MAGIC_SPANS) return HL_WHY_INPUT;
    for (uint32_t i = 0; i < hl.nw; i++) {
        out->s[out->n++] = (magic_span){ hl.wlo[i], hl.wn[i], MAGIC_SPAN_W };
    }
    for (uint32_t i = 0; i < hl.ns; i++) {
        out->s[out->n++] = (magic_span){ hl.slo[i], hl.sn[i], MAGIC_SPAN_S };
    }
    return 0;
}

/* hostlib_ogfx_chunky_copy (HL_ID_OGFX_CHUNKY_COPY): the call; 0, or why it is the 68k code's */
static int hlw_hostlib_ogfx_chunky_copy(ac090 *c, const uint32_t *arg, uint32_t *cost) {
    struct hl_ctx hl;
    hl_init(&hl, 0);
    uint32_t r = hostlib_ogfx_chunky_copy_host(&hl, arg[0]);
    if (hl.why) { hl_restore_state(&hl); return hl.why; }
    c->d[0] = r;
    *cost = (uint32_t)(hl.bytes / 4) + 8;
    return 0;
}
/* ABI 2's dry run: preserve every disjoint write and state range. */
static int hls_hostlib_ogfx_chunky_copy(const uint32_t *arg, magic_spans *out) {
    struct hl_ctx hl;
    hl_init(&hl, 1);
    (void)hostlib_ogfx_chunky_copy_host(&hl, arg[0]);
    if (hl.why) return hl.why;
    if (hl.nw + hl.ns > MAGIC_SPANS) return HL_WHY_INPUT;
    for (uint32_t i = 0; i < hl.nw; i++) {
        out->s[out->n++] = (magic_span){ hl.wlo[i], hl.wn[i], MAGIC_SPAN_W };
    }
    for (uint32_t i = 0; i < hl.ns; i++) {
        out->s[out->n++] = (magic_span){ hl.slo[i], hl.sn[i], MAGIC_SPAN_S };
    }
    return 0;
}

/* hostlib_ogfx_planar_blit (HL_ID_OGFX_PLANAR_BLIT): the call; 0, or why it is the 68k code's */
static int hlw_hostlib_ogfx_planar_blit(ac090 *c, const uint32_t *arg, uint32_t *cost) {
    struct hl_ctx hl;
    hl_init(&hl, 0);
    uint32_t r = hostlib_ogfx_planar_blit_host(&hl, arg[0]);
    if (hl.why) { hl_restore_state(&hl); return hl.why; }
    c->d[0] = r;
    *cost = (uint32_t)(hl.bytes / 4) + 8;
    return 0;
}
/* ABI 2's dry run: preserve every disjoint write and state range. */
static int hls_hostlib_ogfx_planar_blit(const uint32_t *arg, magic_spans *out) {
    struct hl_ctx hl;
    hl_init(&hl, 1);
    (void)hostlib_ogfx_planar_blit_host(&hl, arg[0]);
    if (hl.why) return hl.why;
    if (hl.nw + hl.ns > MAGIC_SPANS) return HL_WHY_INPUT;
    for (uint32_t i = 0; i < hl.nw; i++) {
        out->s[out->n++] = (magic_span){ hl.wlo[i], hl.wn[i], MAGIC_SPAN_W };
    }
    for (uint32_t i = 0; i < hl.ns; i++) {
        out->s[out->n++] = (magic_span){ hl.slo[i], hl.sn[i], MAGIC_SPAN_S };
    }
    return 0;
}

/* hostlib_text_strip (HL_ID_OGFX_TEXT_STRIP): the call; 0, or why it is the 68k code's */
static int hlw_hostlib_text_strip(ac090 *c, const uint32_t *arg, uint32_t *cost) {
    struct hl_ctx hl;
    hl_init(&hl, 0);
    uint32_t r = hostlib_text_strip_host(&hl, arg[0]);
    if (hl.why) { hl_restore_state(&hl); return hl.why; }
    c->d[0] = r;
    *cost = (uint32_t)(hl.bytes / 4) + 8;
    return 0;
}
/* ABI 2's dry run: preserve every disjoint write and state range. */
static int hls_hostlib_text_strip(const uint32_t *arg, magic_spans *out) {
    struct hl_ctx hl;
    hl_init(&hl, 1);
    (void)hostlib_text_strip_host(&hl, arg[0]);
    if (hl.why) return hl.why;
    if (hl.nw + hl.ns > MAGIC_SPANS) return HL_WHY_INPUT;
    for (uint32_t i = 0; i < hl.nw; i++) {
        out->s[out->n++] = (magic_span){ hl.wlo[i], hl.wn[i], MAGIC_SPAN_W };
    }
    for (uint32_t i = 0; i < hl.ns; i++) {
        out->s[out->n++] = (magic_span){ hl.slo[i], hl.sn[i], MAGIC_SPAN_S };
    }
    return 0;
}

/* hostlib_sdl2_blend (HL_ID_SDL2_BLEND): the call; 0, or why it is the 68k code's */
static int hlw_hostlib_sdl2_blend(ac090 *c, const uint32_t *arg, uint32_t *cost) {
    struct hl_ctx hl;
    hl_init(&hl, 0);
    uint32_t r = hostlib_sdl2_blend_host(&hl, arg[0]);
    if (hl.why) { hl_restore_state(&hl); return hl.why; }
    c->d[0] = r;
    *cost = (uint32_t)(hl.bytes / 4) + 8;
    return 0;
}
/* ABI 2's dry run: preserve every disjoint write and state range. */
static int hls_hostlib_sdl2_blend(const uint32_t *arg, magic_spans *out) {
    struct hl_ctx hl;
    hl_init(&hl, 1);
    (void)hostlib_sdl2_blend_host(&hl, arg[0]);
    if (hl.why) return hl.why;
    if (hl.nw + hl.ns > MAGIC_SPANS) return HL_WHY_INPUT;
    for (uint32_t i = 0; i < hl.nw; i++) {
        out->s[out->n++] = (magic_span){ hl.wlo[i], hl.wn[i], MAGIC_SPAN_W };
    }
    for (uint32_t i = 0; i < hl.ns; i++) {
        out->s[out->n++] = (magic_span){ hl.slo[i], hl.sn[i], MAGIC_SPAN_S };
    }
    return 0;
}

/* hostlib_sdl2_blend_argb (HL_ID_SDL2_BLEND_ARGB): the call; 0, or why it is the 68k code's */
static int hlw_hostlib_sdl2_blend_argb(ac090 *c, const uint32_t *arg, uint32_t *cost) {
    struct hl_ctx hl;
    hl_init(&hl, 0);
    uint32_t r = hostlib_sdl2_blend_argb_host(&hl, arg[0]);
    if (hl.why) { hl_restore_state(&hl); return hl.why; }
    c->d[0] = r;
    *cost = (uint32_t)(hl.bytes / 4) + 8;
    return 0;
}
/* ABI 2's dry run: preserve every disjoint write and state range. */
static int hls_hostlib_sdl2_blend_argb(const uint32_t *arg, magic_spans *out) {
    struct hl_ctx hl;
    hl_init(&hl, 1);
    (void)hostlib_sdl2_blend_argb_host(&hl, arg[0]);
    if (hl.why) return hl.why;
    if (hl.nw + hl.ns > MAGIC_SPANS) return HL_WHY_INPUT;
    for (uint32_t i = 0; i < hl.nw; i++) {
        out->s[out->n++] = (magic_span){ hl.wlo[i], hl.wn[i], MAGIC_SPAN_W };
    }
    for (uint32_t i = 0; i < hl.ns; i++) {
        out->s[out->n++] = (magic_span){ hl.slo[i], hl.sn[i], MAGIC_SPAN_S };
    }
    return 0;
}

/* hostlib_mgl_draw (HL_ID_MGL_DRAW): the call; 0, or why it is the 68k code's */
static int hlw_hostlib_mgl_draw(ac090 *c, const uint32_t *arg, uint32_t *cost) {
    struct hl_ctx hl;
    hl_init(&hl, 0);
    uint32_t r = hostlib_mgl_draw_host(&hl, arg[0]);
    if (hl.why) { hl_restore_state(&hl); return hl.why; }
    c->d[0] = r;
    *cost = (uint32_t)(hl.bytes / 4) + 8;
    return 0;
}
/* ABI 2's dry run: preserve every disjoint write and state range. */
static int hls_hostlib_mgl_draw(const uint32_t *arg, magic_spans *out) {
    struct hl_ctx hl;
    hl_init(&hl, 1);
    (void)hostlib_mgl_draw_host(&hl, arg[0]);
    if (hl.why) return hl.why;
    if (hl.nw + hl.ns > MAGIC_SPANS) return HL_WHY_INPUT;
    for (uint32_t i = 0; i < hl.nw; i++) {
        out->s[out->n++] = (magic_span){ hl.wlo[i], hl.wn[i], MAGIC_SPAN_W };
    }
    for (uint32_t i = 0; i < hl.ns; i++) {
        out->s[out->n++] = (magic_span){ hl.slo[i], hl.sn[i], MAGIC_SPAN_S };
    }
    return 0;
}

/* hostlib_mgl_vertices (HL_ID_MGL_VERTICES): the call; 0, or why it is the 68k code's */
static int hlw_hostlib_mgl_vertices(ac090 *c, const uint32_t *arg, uint32_t *cost) {
    struct hl_ctx hl;
    hl_init(&hl, 0);
    uint32_t r = hostlib_mgl_vertices_host(&hl, arg[0]);
    if (hl.why) { hl_restore_state(&hl); return hl.why; }
    c->d[0] = r;
    *cost = (uint32_t)(hl.bytes / 4) + 8;
    return 0;
}
/* ABI 2's dry run: preserve every disjoint write and state range. */
static int hls_hostlib_mgl_vertices(const uint32_t *arg, magic_spans *out) {
    struct hl_ctx hl;
    hl_init(&hl, 1);
    (void)hostlib_mgl_vertices_host(&hl, arg[0]);
    if (hl.why) return hl.why;
    if (hl.nw + hl.ns > MAGIC_SPANS) return HL_WHY_INPUT;
    for (uint32_t i = 0; i < hl.nw; i++) {
        out->s[out->n++] = (magic_span){ hl.wlo[i], hl.wn[i], MAGIC_SPAN_W };
    }
    for (uint32_t i = 0; i < hl.ns; i++) {
        out->s[out->n++] = (magic_span){ hl.slo[i], hl.sn[i], MAGIC_SPAN_S };
    }
    return 0;
}

/* hostlib_mgl_tex (HL_ID_MGL_TEX): the call; 0, or why it is the 68k code's */
static int hlw_hostlib_mgl_tex(ac090 *c, const uint32_t *arg, uint32_t *cost) {
    struct hl_ctx hl;
    hl_init(&hl, 0);
    uint32_t r = hostlib_mgl_tex_host(&hl, arg[0]);
    if (hl.why) { hl_restore_state(&hl); return hl.why; }
    c->d[0] = r;
    *cost = (uint32_t)(hl.bytes / 4) + 8;
    return 0;
}
/* ABI 2's dry run: preserve every disjoint write and state range. */
static int hls_hostlib_mgl_tex(const uint32_t *arg, magic_spans *out) {
    struct hl_ctx hl;
    hl_init(&hl, 1);
    (void)hostlib_mgl_tex_host(&hl, arg[0]);
    if (hl.why) return hl.why;
    if (hl.nw + hl.ns > MAGIC_SPANS) return HL_WHY_INPUT;
    for (uint32_t i = 0; i < hl.nw; i++) {
        out->s[out->n++] = (magic_span){ hl.wlo[i], hl.wn[i], MAGIC_SPAN_W };
    }
    for (uint32_t i = 0; i < hl.ns; i++) {
        out->s[out->n++] = (magic_span){ hl.slo[i], hl.sn[i], MAGIC_SPAN_S };
    }
    return 0;
}

/* hostlib_zlib_inflate_codes (HL_ID_ZLIB_INFLATE_CODES): the call; 0, or why it is the 68k code's */
static int hlw_hostlib_zlib_inflate_codes(ac090 *c, const uint32_t *arg, uint32_t *cost) {
    struct hl_ctx hl;
    hl_init(&hl, 0);
    uint32_t r = hostlib_zlib_inflate_codes_host(&hl, arg[0]);
    if (hl.why) { hl_restore_state(&hl); return hl.why; }
    c->d[0] = r;
    *cost = (uint32_t)(hl.bytes / 4) + 8;
    return 0;
}
/* ABI 2's dry run: preserve every disjoint write and state range. */
static int hls_hostlib_zlib_inflate_codes(const uint32_t *arg, magic_spans *out) {
    struct hl_ctx hl;
    hl_init(&hl, 1);
    (void)hostlib_zlib_inflate_codes_host(&hl, arg[0]);
    if (hl.why) return hl.why;
    if (hl.nw + hl.ns > MAGIC_SPANS) return HL_WHY_INPUT;
    for (uint32_t i = 0; i < hl.nw; i++) {
        out->s[out->n++] = (magic_span){ hl.wlo[i], hl.wn[i], MAGIC_SPAN_W };
    }
    for (uint32_t i = 0; i < hl.ns; i++) {
        out->s[out->n++] = (magic_span){ hl.slo[i], hl.sn[i], MAGIC_SPAN_S };
    }
    return 0;
}

/* hostlib_zlib_deflate_all (HL_ID_ZLIB_DEFLATE_ALL): the call; 0, or why it is the 68k code's */
static int hlw_hostlib_zlib_deflate_all(ac090 *c, const uint32_t *arg, uint32_t *cost) {
    struct hl_ctx hl;
    hl_init(&hl, 0);
    uint32_t r = hostlib_zlib_deflate_all_host(&hl, arg[0]);
    if (hl.why) { hl_restore_state(&hl); return hl.why; }
    c->d[0] = r;
    *cost = (uint32_t)(hl.bytes / 4) + 8;
    return 0;
}
/* ABI 2's dry run: preserve every disjoint write and state range. */
static int hls_hostlib_zlib_deflate_all(const uint32_t *arg, magic_spans *out) {
    struct hl_ctx hl;
    hl_init(&hl, 1);
    (void)hostlib_zlib_deflate_all_host(&hl, arg[0]);
    if (hl.why) return hl.why;
    if (hl.nw + hl.ns > MAGIC_SPANS) return HL_WHY_INPUT;
    for (uint32_t i = 0; i < hl.nw; i++) {
        out->s[out->n++] = (magic_span){ hl.wlo[i], hl.wn[i], MAGIC_SPAN_W };
    }
    for (uint32_t i = 0; i < hl.ns; i++) {
        out->s[out->n++] = (magic_span){ hl.slo[i], hl.sn[i], MAGIC_SPAN_S };
    }
    return 0;
}

/* hostlib_lzma_decode_all (HL_ID_LZMA_DECODE_ALL): the call; 0, or why it is the 68k code's */
static int hlw_hostlib_lzma_decode_all(ac090 *c, const uint32_t *arg, uint32_t *cost) {
    struct hl_ctx hl;
    hl_init(&hl, 0);
    uint32_t r = hostlib_lzma_decode_all_host(&hl, arg[0]);
    if (hl.why) { hl_restore_state(&hl); return hl.why; }
    c->d[0] = r;
    *cost = (uint32_t)(hl.bytes / 4) + 8;
    return 0;
}
/* ABI 2's dry run: preserve every disjoint write and state range. */
static int hls_hostlib_lzma_decode_all(const uint32_t *arg, magic_spans *out) {
    struct hl_ctx hl;
    hl_init(&hl, 1);
    (void)hostlib_lzma_decode_all_host(&hl, arg[0]);
    if (hl.why) return hl.why;
    if (hl.nw + hl.ns > MAGIC_SPANS) return HL_WHY_INPUT;
    for (uint32_t i = 0; i < hl.nw; i++) {
        out->s[out->n++] = (magic_span){ hl.wlo[i], hl.wn[i], MAGIC_SPAN_W };
    }
    for (uint32_t i = 0; i < hl.ns; i++) {
        out->s[out->n++] = (magic_span){ hl.slo[i], hl.sn[i], MAGIC_SPAN_S };
    }
    return 0;
}

#define HOSTLIB_DEFS \
    { 0x00010100, "hostlib_ogfx_planar_rect", 1, hlw_hostlib_ogfx_planar_rect, NULL, NULL, NULL, 0, hls_hostlib_ogfx_planar_rect, 0 }, \
    { 0x00010101, "hostlib_ogfx_chunky_rect", 1, hlw_hostlib_ogfx_chunky_rect, NULL, NULL, NULL, 0, hls_hostlib_ogfx_chunky_rect, 0 }, \
    { 0x00010102, "hostlib_ogfx_chunky_copy", 1, hlw_hostlib_ogfx_chunky_copy, NULL, NULL, NULL, 0, hls_hostlib_ogfx_chunky_copy, 0 }, \
    { 0x00010103, "hostlib_ogfx_planar_blit", 1, hlw_hostlib_ogfx_planar_blit, NULL, NULL, NULL, 0, hls_hostlib_ogfx_planar_blit, 0 }, \
    { 0x00010105, "hostlib_text_strip", 1, hlw_hostlib_text_strip, NULL, NULL, NULL, 0, hls_hostlib_text_strip, 0 }, \
    { 0x00010106, "hostlib_sdl2_blend", 1, hlw_hostlib_sdl2_blend, NULL, NULL, NULL, 0, hls_hostlib_sdl2_blend, MAGIC_F_LOCKED }, \
    { 0x0001010a, "hostlib_sdl2_blend_argb", 1, hlw_hostlib_sdl2_blend_argb, NULL, NULL, NULL, 0, hls_hostlib_sdl2_blend_argb, MAGIC_F_LOCKED }, \
    { 0x00010107, "hostlib_mgl_draw", 1, hlw_hostlib_mgl_draw, NULL, NULL, NULL, 0, hls_hostlib_mgl_draw, MAGIC_F_LOCKED }, \
    { 0x00010108, "hostlib_mgl_vertices", 1, hlw_hostlib_mgl_vertices, NULL, NULL, NULL, 0, hls_hostlib_mgl_vertices, MAGIC_F_LOCKED }, \
    { 0x00010109, "hostlib_mgl_tex", 1, hlw_hostlib_mgl_tex, NULL, NULL, NULL, 0, hls_hostlib_mgl_tex, MAGIC_F_LOCKED }, \
    { 0x00010180, "hostlib_zlib_inflate_codes", 1, hlw_hostlib_zlib_inflate_codes, NULL, NULL, NULL, 0, hls_hostlib_zlib_inflate_codes, MAGIC_F_LOCKED }, \
    { 0x00010181, "hostlib_zlib_deflate_all", 1, hlw_hostlib_zlib_deflate_all, NULL, NULL, NULL, 0, hls_hostlib_zlib_deflate_all, 0 }, \
    { 0x00010182, "hostlib_lzma_decode_all", 1, hlw_hostlib_lzma_decode_all, NULL, NULL, NULL, 0, hls_hostlib_lzma_decode_all, 0 }, \

/* every id's contract, for the runtime's HOSTLIB_PROBE and a total function's Alert
 * (HOSTLIB-ABI.md 8): { id, version, HL_C_* flags, state_max } */
#define HOSTLIB_CONTRACTS \
    { 0x00010100, 1, 0, 0 }, \
    { 0x00010101, 1, HL_C_PARALLEL, 0 }, \
    { 0x00010102, 1, 0, 0 }, \
    { 0x00010103, 1, 0, 0 }, \
    { 0x00010105, 1, 0, 0 }, \
    { 0x00010106, 1, HL_C_LOCKED, 65536 }, \
    { 0x0001010a, 1, HL_C_LOCKED, 65536 }, \
    { 0x00010107, 1, HL_C_LOCKED, 16 }, \
    { 0x00010108, 1, HL_C_LOCKED, 16 }, \
    { 0x00010109, 1, HL_C_LOCKED, 16 }, \
    { 0x00011000, 1, HL_C_RUNTIME, 0 }, \
    { 0x00010180, 1, HL_C_LOCKED, 32828 }, \
    { 0x00010181, 1, 0, 0 }, \
    { 0x00010182, 1, 0, 0 }, \

#undef HL_HOST
