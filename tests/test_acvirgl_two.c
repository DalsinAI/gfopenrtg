/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * Two GL programs at once against virglrenderer (17 Nov 2026: on the Pi 5
 * the program started first went black when a second one started; its
 * context had died on the host, "context error ... 1286", "Illegal command
 * buffer"). Each "program" here is a context with a colour and a depth
 * buffer that clears and presents a frame, as GL.module's do. The first
 * draws, the second starts and draws beside it, the second goes, and the
 * first must draw on throughout: every SUBMIT and PRESENT answers without
 * an error and the first's frames carry its own colour.
 * Skipped when ACVIRGL_LIB isn't set. tests/run.sh builds and runs it. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../host/virgl/acvirgl.h"
#include "../include/gfopengpu/stream.h"
#include "../include/gfopengpu/virgl.h"
#include "virgl_protocol.h"

#if defined(__linux__)
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#endif

#define ARENA (16u << 20)
static uint8_t *arena;
static int vram_fd = -1;
static uint8_t *map(void *u, uint32_t a, uint32_t len) { (void)u; return a < ARENA && len <= ARENA - a ? arena + a : NULL; }
static int vram_ok(void *u, uint32_t a, uint32_t n) { (void)u; (void)a; (void)n; return 1; }

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define BLK 0x1000u
static uint32_t at;
static void be(uint32_t a, uint32_t v) { arena[a] = (uint8_t)(v >> 24); arena[a + 1] = (uint8_t)(v >> 16); arena[a + 2] = (uint8_t)(v >> 8); arena[a + 3] = (uint8_t)v; }
static uint32_t rbe(uint32_t a) { return (uint32_t)arena[a] << 24 | (uint32_t)arena[a + 1] << 16 | (uint32_t)arena[a + 2] << 8 | arena[a + 3]; }
static void begin(void) { at = BLK; }
static uint32_t cmd(unsigned c, int nargs, const uint32_t *args)
{
    uint32_t here = at;
    int i;
    be(at, ACV_HDR(c, nargs + 2)); be(at + 4, 0xDEADBEEFu);
    for (i = 0; i < nargs; i++) be(at + 8 + i * 4, args[i]);
    at += (uint32_t)(nargs + 2) * 4;
    return here;
}
static int32_t result(uint32_t c) { return (int32_t)rbe(c + 4); }

#define CMDS 0x8000u
static uint32_t cn;
static void le(uint32_t v) { uint32_t a = CMDS + cn * 4; arena[a] = (uint8_t)v; arena[a + 1] = (uint8_t)(v >> 8); arena[a + 2] = (uint8_t)(v >> 16); arena[a + 3] = (uint8_t)(v >> 24); cn++; }
static uint32_t f32(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

static struct acvirgl *v;
/* the frame and its target: 64x64 in 128x80, or AC_TWO_W x AC_TWO_H (a real window's size: 720x540
 * is past the pixels a stage wants by default, so a present in pieces goes through one) */
static uint32_t RW = 64, RH = 64, TW = 128, TH = 80, TB = 128 * 4;

/* A program: its context, its colour and depth resources, its target in "video RAM". */
struct prog { uint32_t ctx, col, zs, target; float r, g, b; };

static void prog_start(const struct prog *p, const char *what)
{
    uint32_t a_ctx[3] = { p->ctx, 0x30000, 8 };
    uint32_t a_col[12] = { p->col, 2 /* PIPE_TEXTURE_2D */, 1 /* B8G8R8A8_UNORM */, 2 | 8, RW, RH, 1, 1, 0, 0, 0, p->ctx };
    uint32_t a_zs[12] = { p->zs, 2, 21 /* Z24X8_UNORM */, 1 /* depth/stencil */, RW, RH, 1, 1, 0, 0, 0, p->ctx };
    uint32_t a_sub[3] = { p->ctx, CMDS, 0 };
    uint32_t cx, c1, c2, cs;
    memcpy(arena + 0x30000, "two test", 8);
    begin(); cx = cmd(ACV_CTX_CREATE, 3, a_ctx); c1 = cmd(ACV_RES_CREATE, 12, a_col); c2 = cmd(ACV_RES_CREATE, 12, a_zs);
    acvirgl_run(v, map, NULL, BLK, at - BLK);
    CHECK(result(cx) == 0 && result(c1) == 0 && result(c2) == 0, "%s: context %d, colour %d, depth %d", what, result(cx), result(c1), result(c2));
    cn = 0;
    le(VIRGL_CMD0(VIRGL_CCMD_CREATE_SUB_CTX, 0, 1)); le(1);
    le(VIRGL_CMD0(VIRGL_CCMD_SET_SUB_CTX, 0, 1)); le(1);
    le(VIRGL_CMD0(VIRGL_CCMD_CREATE_OBJECT, VIRGL_OBJECT_SURFACE, VIRGL_OBJ_SURFACE_SIZE)); le(10); le(p->col); le(1); le(0); le(0);
    le(VIRGL_CMD0(VIRGL_CCMD_CREATE_OBJECT, VIRGL_OBJECT_SURFACE, VIRGL_OBJ_SURFACE_SIZE)); le(11); le(p->zs); le(21); le(0); le(0);
    le(VIRGL_CMD0(VIRGL_CCMD_SET_FRAMEBUFFER_STATE, 0, VIRGL_SET_FRAMEBUFFER_STATE_SIZE(1))); le(1); le(11); le(10);
    a_sub[2] = cn;
    begin(); cs = cmd(ACV_SUBMIT, 3, a_sub);
    acvirgl_run(v, map, NULL, BLK, at - BLK);
    CHECK(result(cs) == 0, "%s: its framebuffer (%d)", what, result(cs));
}

/* One frame: colour and depth cleared, presented at (10, 5) of its target; in `parts` rectangles side by
 * side (2: a window with another over its middle edge, which GFOpenRTG presents in pieces, and acvirgl
 * then through a stage or read back rather than straight into video RAM). 1 when all of it went well. */
static int prog_frame_parts(const struct prog *p, const char *what, int n, int parts)
{
    uint32_t a_sub[3] = { p->ctx, CMDS, 0 };
    uint32_t pa[14] = { p->ctx, p->col, 0, (10u << 16) | 5u, p->target, TB, OGPU_FMT_XRGB32, ((uint32_t)TW << 16) | TH, 0, (uint32_t)parts,
                        (10u << 16) | 5u, ((uint32_t)(parts == 2 ? RW / 2 : RW) << 16) | RH,
                        ((uint32_t)(10 + RW / 2) << 16) | 5u, ((uint32_t)(RW - RW / 2) << 16) | RH };
    uint32_t cs, cp;
    const uint8_t *d;
    int ok;
    cn = 0;
    le(VIRGL_CMD0(VIRGL_CCMD_CLEAR, 0, VIRGL_OBJ_CLEAR_SIZE)); le((1 << 2) | 1);
    le(f32(p->r)); le(f32(p->g)); le(f32(p->b)); le(f32(1.0f));
    { double z = 1.0; uint32_t w2[2]; memcpy(w2, &z, 8); le(w2[0]); le(w2[1]); }
    le(0);
    a_sub[2] = cn;
    memset(arena + p->target, 0xEE, TB * TH);
    begin(); cs = cmd(ACV_SUBMIT, 3, a_sub); cp = cmd(ACV_PRESENT, 10 + 2 * parts, pa);
    acvirgl_run(v, map, NULL, BLK, at - BLK);
    acvirgl_settle(v, map, NULL, ACVIRGL_ALL);
    d = arena + p->target + 20 * TB + 30 * 4;       /* X, R, G, B */
    ok = result(cs) == 0 && result(cp) == (int32_t)(RW * RH)
         && abs((int)d[1] - (int)(p->r * 255.0f + 0.5f)) <= 1 && abs((int)d[2] - (int)(p->g * 255.0f + 0.5f)) <= 1
         && abs((int)d[3] - (int)(p->b * 255.0f + 0.5f)) <= 1;
    CHECK(ok, "%s, frame %d: submit %d, present %d, pixel %02x %02x %02x %02x", what, n, result(cs), result(cp), d[0], d[1], d[2], d[3]);
    return ok;
}

static int prog_frame(const struct prog *p, const char *what, int n) { return prog_frame_parts(p, what, n, 1); }

static void prog_end(const struct prog *p)
{
    uint32_t a[1] = { p->ctx }, h1[1] = { p->col }, h2[1] = { p->zs };
    begin(); cmd(ACV_CTX_DESTROY, 1, a); cmd(ACV_RES_UNREF, 1, h1); cmd(ACV_RES_UNREF, 1, h2);
    acvirgl_run(v, map, NULL, BLK, at - BLK);
}

int main(void)
{
    char err[256];
    /* ids as GL.module's on the Amiga: addresses, not small counts */
    static const struct prog A = { 0x08c4a1ac, 0x08c50010, 0x08c50090, 0x100000, 0.25f, 0.5f, 0.75f };
    static const struct prog B = { 0x08dd0c7c, 0x08de0010, 0x08de0090, 0x500000, 0.75f, 0.25f, 0.5f };
    static const struct prog C = { 0x08ee0c7c, 0x08ef0010, 0x08ef0090, 0x900000, 0.5f, 0.75f, 0.25f };
    struct acvirgl_stats s0, s1;
    uint32_t a_hello[1] = { 0 };
    int i;
    if (getenv("AC_TWO_W") && getenv("AC_TWO_H")) {
        RW = (uint32_t)atoi(getenv("AC_TWO_W")); RH = (uint32_t)atoi(getenv("AC_TWO_H"));
        if (RW < 16 || RW > 960 || RH < 16 || RH > 1000) { printf("acvirgl two: AC_TWO_W x AC_TWO_H out of range\n"); return 1; }
        TW = RW + 64; TH = RH + 16; TB = TW * 4;
    }
#if defined(__linux__)
    vram_fd = (int)syscall(SYS_memfd_create, "test-vram", 1u | 2u);
    if (vram_fd >= 0 && !ftruncate(vram_fd, ARENA) && fcntl(vram_fd, 1033 /* F_ADD_SEALS */, 2 /* F_SEAL_SHRINK */) == 0) {
        void *m = mmap(NULL, ARENA, PROT_READ | PROT_WRITE, MAP_SHARED, vram_fd, 0);
        arena = m == MAP_FAILED ? NULL : m;
    }
#endif
    if (!arena) { vram_fd = -1; if (!(arena = calloc(1, ARENA))) return 1; }
    v = acvirgl_create(NULL, err, sizeof err);
    if (!v) { printf("acvirgl two: skipped (%s)\n", err); return 0; }
    printf("acvirgl two: %s on %s\n", acvirgl_api(v), acvirgl_renderer(v));
    begin(); cmd(ACV_HELLO, 1, a_hello);
    acvirgl_run(v, map, NULL, BLK, at - BLK);
    if (vram_fd >= 0) acvirgl_video_ram(v, vram_fd, 0, ARENA, vram_ok, NULL);
    acvirgl_stats(v, &s0);

    prog_start(&A, "the first");
    for (i = 0; i < 4; i++) prog_frame(&A, "the first alone", i);
    prog_start(&B, "the second");
    for (i = 0; i < 6; i++) {
        prog_frame(&A, "the first beside the second", i);
        prog_frame(&B, "the second", i);
    }
    /* the second's window over part of the first's: the first presents in two pieces
     * (a stage, or read back), the second straight into video RAM, turn about */
    for (i = 0; i < 6; i++) {
        prog_frame_parts(&A, "the first in two pieces beside the second", i, 2);
        prog_frame(&B, "the second over the first", i);
    }
    for (i = 0; i < 4; i++) {
        prog_frame(&A, "the first whole again", i);
        prog_frame_parts(&B, "the second in two pieces", i, 2);
    }
    prog_end(&B);
    for (i = 0; i < 4; i++) prog_frame(&A, "the first after the second went", i);
    prog_start(&C, "a third");
    for (i = 0; i < 4; i++) {
        prog_frame(&C, "the third", i);
        prog_frame(&A, "the first beside the third", i);
    }
    prog_end(&A);
    for (i = 0; i < 4; i++) prog_frame(&C, "the third after the first went", i);
    prog_end(&C);
    acvirgl_stats(v, &s1);
    CHECK(s1.errors == s0.errors, "%llu errors", (unsigned long long)(s1.errors - s0.errors));
    printf("acvirgl two: %llu presents, %llu of them written by the GPU, %llu through a stage\n", (unsigned long long)(s1.presents - s0.presents),
           (unsigned long long)(s1.direct - s0.direct), (unsigned long long)(s1.staged - s0.staged));
    acvirgl_destroy(v);
    printf("acvirgl two: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
