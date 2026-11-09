/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * What an ACV_PRESENT costs, against virglrenderer on this host's GPU:
 * a WxH B8G8R8X8 display target with a depth/stencil buffer (BENCH_ZFMT, virgl format, default S8_UINT_Z24_UNORM), cleared (colour,
 * depth, stencil) every frame as a game does, a full-screen triangle drawn
 * (BENCH_DRAWS of them, depth tested), then presented whole into an
 * XRGB32 "video RAM". Prints the present's mean time, frames a second, and
 * acvirgl's own parts (AC_VIRGL_PARTS=1 for the GPU wait apart).
 *
 *   acvirgl_present_bench [frames [width height [now|behind|defer]]]
 *
 * AC_VIRGL_STAGE (1, 0), AC_VIRGL_ZDISCARD (0) and AC_VIRGL_DIRECT (1, 0)
 * choose as in the runtime; the "video RAM" is a sealed memfd, as the
 * runtime's, so direct presents (acvirgl_video_ram) may write it.
 *
 * Built like test_acvirgl.c (virgl_protocol.h from virglrenderer's source):
 *   cc -O2 -I$VIRGL_SRC tests/acvirgl_present_bench.c host/virgl/acvirgl.c -ldl
 * ACVIRGL_LIB names the library. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
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
static int vram_fd = -1;

#define BLK 0x1000u
#define CMDS 0x8000u
#define NAME 0x30000u
#define VRAM 0x100000u
static uint8_t *arena;
static uint32_t arena_size;
static uint8_t *map(void *u, uint32_t a, uint32_t len) { (void)u; return a < arena_size && len <= arena_size - a ? arena + a : NULL; }
static int ok_all(void *u, uint32_t a, uint32_t n) { (void)u; (void)a; (void)n; return 1; }

static uint32_t at, cn;
static void be(uint32_t a, uint32_t v) { arena[a] = (uint8_t)(v >> 24); arena[a + 1] = (uint8_t)(v >> 16); arena[a + 2] = (uint8_t)(v >> 8); arena[a + 3] = (uint8_t)v; }
static uint32_t rbe(uint32_t a) { return (uint32_t)arena[a] << 24 | (uint32_t)arena[a + 1] << 16 | (uint32_t)arena[a + 2] << 8 | arena[a + 3]; }
static uint32_t cmd(unsigned c, int nargs, const uint32_t *args)
{
    uint32_t here = at;
    int i;
    be(at, ACV_HDR(c, nargs + 2)); be(at + 4, 0xDEADBEEFu);
    for (i = 0; i < nargs; i++) be(at + 8 + i * 4, args[i]);
    at += (uint32_t)(nargs + 2) * 4;
    return here;
}
static void le(uint32_t v) { uint32_t a = CMDS + cn * 4; memcpy(arena + a, &v, 4); cn++; }   /* a little-endian host */
static uint32_t f32(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
/* a TGSI shader object: handle, type, text (NUL-terminated, padded to words) */
static void shader(uint32_t handle, uint32_t type, const char *text)
{
    uint32_t n = (uint32_t)strlen(text) + 1, words = (n + 3) / 4, i;
    le(VIRGL_CMD0(VIRGL_CCMD_CREATE_OBJECT, VIRGL_OBJECT_SHADER, 5 + words));
    le(handle); le(type); le(n); le(300); le(0);
    for (i = 0; i < words; i++) { uint32_t w = 0; memcpy(&w, text + i * 4, i * 4 + 4 <= n ? 4 : n - i * 4); le(w); }
}
static uint64_t ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec; }
static void run(struct acvirgl *v) { acvirgl_run(v, map, NULL, BLK, at - BLK); }

int main(int argc, char **argv)
{
    char err[256];
    int frames = argc > 1 ? atoi(argv[1]) : 300, f;
    uint32_t w = argc > 3 ? (uint32_t)atoi(argv[2]) : 640, h = argc > 3 ? (uint32_t)atoi(argv[3]) : 480;
    const char *mode = argc > 4 ? argv[4] : "now";
    struct acvirgl *v;
    int draws = getenv("BENCH_DRAWS") ? atoi(getenv("BENCH_DRAWS")) : 1;   /* full-screen triangles a frame */
    uint32_t cf = getenv("BENCH_CFMT") ? (uint32_t)atoi(getenv("BENCH_CFMT")) : 2;   /* colour: B8G8R8X8 */
    uint32_t zf = getenv("BENCH_ZFMT") ? (uint32_t)atoi(getenv("BENCH_ZFMT")) : 20;   /* S8_UINT_Z24_UNORM */
    struct acvirgl_stats s0, s1;
    uint64_t t_pres = 0, t_all, t_max = 0;
    uint32_t c, bad = 0;
    arena_size = (VRAM + w * h * 4 + 4096 + 4095) & ~4095u;
#if defined(ACVIRGL_HAS_VRAM) && defined(__linux__)
    /* the "video RAM" a file, as the runtime's (a sealed memfd): direct
     * presents (AC_VIRGL_DIRECT) may write it */
    vram_fd = (int)syscall(SYS_memfd_create, "bench-vram", 1u /* MFD_CLOEXEC */ | 2u /* MFD_ALLOW_SEALING */);
    if (vram_fd >= 0 && !ftruncate(vram_fd, arena_size) && fcntl(vram_fd, 1033 /* F_ADD_SEALS */, 2 /* F_SEAL_SHRINK */) == 0) {
        void *m = mmap(NULL, arena_size, PROT_READ | PROT_WRITE, MAP_SHARED, vram_fd, 0);
        arena = m == MAP_FAILED ? NULL : m;
    }
#endif
    if (!arena && !(arena = calloc(1, arena_size))) return 1;
    if (!(v = acvirgl_create(NULL, err, sizeof err))) { printf("no virgl: %s\n", err); return 0; }
#if defined(ACVIRGL_HAS_VRAM) && defined(__linux__)
    if (vram_fd >= 0) acvirgl_video_ram(v, vram_fd, 0, arena_size, NULL, NULL);
#endif
    printf("bench: %s on %s, %ux%u, %d frames, present %s\n", acvirgl_api(v), acvirgl_renderer(v), w, h, frames, mode);
    if (!strcmp(mode, "behind")) acvirgl_behind_presents(v, ok_all);
    else if (!strcmp(mode, "defer")) acvirgl_defer_presents(v, ok_all);

    at = BLK;
    memcpy(arena + NAME, "present bench", 13);
    { uint32_t a[3] = { 1, NAME, 13 }; cmd(ACV_CTX_CREATE, 3, a); }
    /* colour: B8G8R8X8, render target + sampler + display target; depth: Z24S8 */
    { uint32_t a[12] = { 1, 2, cf, 2 | 8 | 128, w, h, 1, 1, 0, 0, 0, 1 }; cmd(ACV_RES_CREATE, 12, a); }
    { uint32_t a[12] = { 2, 2, zf, 1, w, h, 1, 1, 0, 0, 0, 1 }; cmd(ACV_RES_CREATE, 12, a); }
    run(v);
    cn = 0;
    le(VIRGL_CMD0(VIRGL_CCMD_CREATE_SUB_CTX, 0, 1)); le(1);
    le(VIRGL_CMD0(VIRGL_CCMD_SET_SUB_CTX, 0, 1)); le(1);
    le(VIRGL_CMD0(VIRGL_CCMD_CREATE_OBJECT, VIRGL_OBJECT_SURFACE, VIRGL_OBJ_SURFACE_SIZE)); le(10); le(1); le(cf); le(0); le(0);
    le(VIRGL_CMD0(VIRGL_CCMD_CREATE_OBJECT, VIRGL_OBJECT_SURFACE, VIRGL_OBJ_SURFACE_SIZE)); le(11); le(2); le(zf); le(0); le(0);
    le(VIRGL_CMD0(VIRGL_CCMD_SET_FRAMEBUFFER_STATE, 0, VIRGL_SET_FRAMEBUFFER_STATE_SIZE(1))); le(1); le(11); le(10);
    /* what a game's draw needs: shaders, blend (all colours), depth test and
     * write, a rasterizer, the viewport, one triangle covering the screen */
    shader(20, 0, "VERT\nDCL IN[0]\nDCL OUT[0], POSITION\n  0: MOV OUT[0], IN[0]\n  1: END\n");
    shader(21, 1, "FRAG\nPROPERTY FS_COLOR0_WRITES_ALL_CBUFS 1\nDCL OUT[0], COLOR\nIMM[0] FLT32 { 0.25, 0.5, 0.75, 1.0 }\n  0: MOV OUT[0], IMM[0]\n  1: END\n");
    le(VIRGL_CMD0(VIRGL_CCMD_BIND_SHADER, 0, 2)); le(20); le(0);
    le(VIRGL_CMD0(VIRGL_CCMD_BIND_SHADER, 0, 2)); le(21); le(1);
    le(VIRGL_CMD0(VIRGL_CCMD_CREATE_OBJECT, VIRGL_OBJECT_BLEND, VIRGL_OBJ_BLEND_SIZE)); le(22); le(0); le(0);
    { int k; le(VIRGL_OBJ_BLEND_S2_RT_COLORMASK(0xF)); for (k = 1; k < VIRGL_MAX_COLOR_BUFS; k++) le(0); }
    le(VIRGL_CMD0(VIRGL_CCMD_BIND_OBJECT, VIRGL_OBJECT_BLEND, 1)); le(22);
    le(VIRGL_CMD0(VIRGL_CCMD_CREATE_OBJECT, VIRGL_OBJECT_DSA, VIRGL_OBJ_DSA_SIZE)); le(23);
    le(VIRGL_OBJ_DSA_S0_DEPTH_ENABLE(1) | VIRGL_OBJ_DSA_S0_DEPTH_WRITEMASK(1) | VIRGL_OBJ_DSA_S0_DEPTH_FUNC(3 /* LEQUAL */)); le(0); le(0); le(0);
    le(VIRGL_CMD0(VIRGL_CCMD_BIND_OBJECT, VIRGL_OBJECT_DSA, 1)); le(23);
    le(VIRGL_CMD0(VIRGL_CCMD_CREATE_OBJECT, VIRGL_OBJECT_RASTERIZER, VIRGL_OBJ_RS_SIZE)); le(24);
    le(VIRGL_OBJ_RS_S0_DEPTH_CLIP(1) | VIRGL_OBJ_RS_S0_HALF_PIXEL_CENTER(1)); le(f32(1.0f)); le(0); le(f32(1.0f)); le(0); le(0); le(0); le(0);
    le(VIRGL_CMD0(VIRGL_CCMD_BIND_OBJECT, VIRGL_OBJECT_RASTERIZER, 1)); le(24);
    le(VIRGL_CMD0(VIRGL_CCMD_SET_VIEWPORT_STATE, 0, VIRGL_SET_VIEWPORT_STATE_SIZE(1))); le(0);
    le(f32(w / 2.0f)); le(f32(h / 2.0f)); le(f32(0.5f)); le(f32(w / 2.0f)); le(f32(h / 2.0f)); le(f32(0.5f));
    le(VIRGL_CMD0(VIRGL_CCMD_CREATE_OBJECT, VIRGL_OBJECT_VERTEX_ELEMENTS, VIRGL_OBJ_VERTEX_ELEMENTS_SIZE(1))); le(25);
    le(0); le(0); le(0); le(31 /* R32G32B32A32_FLOAT */);
    le(VIRGL_CMD0(VIRGL_CCMD_BIND_OBJECT, VIRGL_OBJECT_VERTEX_ELEMENTS, 1)); le(25);
    le(VIRGL_CMD0(VIRGL_CCMD_SET_VERTEX_BUFFERS, 0, VIRGL_SET_VERTEX_BUFFERS_SIZE(1))); le(16); le(0); le(3);
    {
        /* the triangle's vertices, into buffer resource 3 */
        static const float tri[12] = { -1, -1, 0.5f, 1,  3, -1, 0.5f, 1,  -1, 3, 0.5f, 1 };
        uint32_t a_buf[12] = { 3, 0 /* PIPE_BUFFER */, 64 /* R8_UNORM */, 1u << 4 /* VERTEX_BUFFER */, 48, 1, 1, 1, 0, 0, 0, 1 };
        uint32_t a_put[14] = { 3, 1, 0, 0, 0, 0, 0, 0, 48, 1, 1, NAME + 64, 48, ACV_SWAP_NONE };
        memcpy(arena + NAME + 64, tri, 48);
        at = BLK; cmd(ACV_RES_CREATE, 12, a_buf); cmd(ACV_PUT, 14, a_put); run(v);
    }
    at = BLK;
    { uint32_t a[3] = { 1, CMDS, cn }; c = cmd(ACV_SUBMIT, 3, a); }
    run(v);
    if ((int32_t)rbe(c + 4) != 0) { printf("setup: submit %d\n", (int32_t)rbe(c + 4)); return 1; }
    acvirgl_stats(v, &s0);
    t_all = ns();
    for (f = 0; f < frames; f++) {
        uint32_t cp, sub;
        int32_t sr;
        int k;
        uint64_t t0, t1;
        float g = (float)(f % 256) / 255.0f;
        acvirgl_settle(v, map, NULL, ACVIRGL_READY);
        cn = 0;
        le(VIRGL_CMD0(VIRGL_CCMD_CLEAR, 0, VIRGL_OBJ_CLEAR_SIZE)); le(1 << 2 | 1 | 2);     /* colour 0, depth, stencil */
        le(f32(0.25f)); le(f32(g)); le(f32(0.75f)); le(f32(1.0f));
        { double d = 1.0; uint32_t w2[2]; memcpy(w2, &d, 8); le(w2[0]); le(w2[1]); }
        le(0);
        for (k = 0; k < draws; k++) {
            le(VIRGL_CMD0(VIRGL_CCMD_DRAW_VBO, 0, VIRGL_DRAW_VBO_SIZE));
            le(0); le(3); le(4 /* TRIANGLES */); le(0); le(1); le(0); le(0); le(0); le(0); le(0); le(2); le(0);
        }
        at = BLK;
        { uint32_t a[3] = { 1, CMDS, cn }; sub = cmd(ACV_SUBMIT, 3, a); }
        run(v);
        sr = (int32_t)rbe(sub + 4);
        at = BLK;
        { uint32_t a[12] = { 1, 1, 0, 0, VRAM, w * 4, OGPU_FMT_XRGB32, w << 16 | h, 0, 1, 0, w << 16 | h }; cp = cmd(ACV_PRESENT, 12, a); }
        t0 = ns();
        run(v);
        t1 = ns();
        t_pres += t1 - t0;
        if (t1 - t0 > t_max) t_max = t1 - t0;
        if (sr != 0 || (int32_t)rbe(cp + 4) != (int32_t)(w * h)) {
            if (!bad) printf("bench: frame %d: submit %d, present %d\n", f, sr, (int32_t)rbe(cp + 4));
            bad++;
        }
    }
    acvirgl_settle(v, map, NULL, ACVIRGL_ALL);
    t_all = ns() - t_all;
    acvirgl_stats(v, &s1);
    {
        /* the last frame's colour, as the 68k reads it: X 0, R 0x40, G, B 0xBF */
        const uint8_t *p = arena + VRAM + (h / 2 * w + w / 2) * 4;
        uint64_t n = s1.presents - s0.presents ? s1.presents - s0.presents : 1;
        if (draws) printf("bench: pixel %02x %02x %02x %02x (want 00 3f-40 7f-80 bf-c0)%s\n", p[0], p[1], p[2], p[3], bad ? "  ERRORS" : "");
        else printf("bench: pixel %02x %02x %02x %02x (want 00 3f-40 %02x bf-c0)%s\n", p[0], p[1], p[2], p[3],
                    (unsigned)((frames - 1) % 256), bad ? "  ERRORS" : "");
        printf("bench: present (the 68k waits) %.0f us mean, %.0f max; %.1f frames/s\n", t_pres / (double)frames / 1e3, t_max / 1e3,
               frames / (t_all / 1e9));
        printf("bench: acvirgl's own: present %.0f us: GPU wait %.0f, read back %.0f (waited %.0f), conversion %.0f, issue %.0f; behind %llu\n",
               (s1.present_ns - s0.present_ns) / (double)n / 1e3, (s1.present_gpu_ns - s0.present_gpu_ns) / (double)n / 1e3,
               (s1.present_read_ns - s0.present_read_ns) / (double)n / 1e3, (s1.present_wait_ns - s0.present_wait_ns) / (double)n / 1e3,
               (s1.present_conv_ns - s0.present_conv_ns) / (double)n / 1e3, (s1.present_issue_ns - s0.present_issue_ns) / (double)n / 1e3,
               (unsigned long long)(s1.behind - s0.behind));
#ifdef ACVIRGL_HAS_STAGE
        printf("bench: staged %llu, depth/stencil not stored %llu\n", (unsigned long long)(s1.staged - s0.staged),
               (unsigned long long)(s1.zs_discards - s0.zs_discards));
#endif
#ifdef ACVIRGL_HAS_VRAM
        printf("bench: written straight into video RAM by the GPU %llu\n", (unsigned long long)(s1.direct - s0.direct));
#endif
    }
    acvirgl_destroy(v);
    return bad != 0;
}
