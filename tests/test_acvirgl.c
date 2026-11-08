/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * ACVirgl's host side against virglrenderer: request blocks built here, in
 * the Amiga's byte order, in an arena that stands for Amiga memory. Hello,
 * the caps (turned into the Amiga's order), a context, a texture, pixels
 * up and back with their units turned round, and a command stream (a
 * surface, a framebuffer, a clear) whose colour comes back as the 68k
 * reads it: A, R, G, B. Skipped when ACVIRGL_LIB isn't set.
 *   tests/run.sh builds it with virglrenderer's protocol header (fetched). */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include "../host/virgl/acvirgl.h"
#include "../include/gfopengpu/stream.h"
#include "../include/gfopengpu/virgl.h"
#include "virgl_protocol.h"     /* virglrenderer's, from its pinned source */

#define ARENA (1u << 20)
static uint8_t arena[ARENA];
#define MF_AT 0x100000u           /* guest addresses from here are a sealed memfd, as the runtime's video RAM is */
#define MF_SIZE (1u << 20)
static uint8_t *mf;
static int mf_fd = -1;
static uint8_t *map(void *u, uint32_t a, uint32_t len)
{
    (void)u;
    if (mf && a >= MF_AT && len <= MF_SIZE && a - MF_AT <= MF_SIZE - len) return mf + (a - MF_AT);
    return a < ARENA && len <= ARENA - a ? arena + a : NULL;
}
static int mf_file(void *u, uint32_t a, int *fd, uint64_t *off, uint64_t *size)
{
    (void)u;
    if (!mf || a < MF_AT) return 0;
    *fd = mf_fd; *off = a - MF_AT; *size = MF_SIZE;
    return 1;
}

static int defer_all(void *u, uint32_t a, uint32_t n) { (void)u; (void)a; (void)n; return 1; }
static int defer_none(void *u, uint32_t a, uint32_t n) { (void)u; (void)a; (void)n; return 0; }

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* A block at BLK, built word by word. */
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
    return here;               /* where its result word is, less 4 */
}
static int32_t result(uint32_t c) { return (int32_t)rbe(c + 4); }

/* A command stream in little-endian words, as Mesa's encoder writes it. */
#define CMDS 0x8000u
static uint32_t cn;
static void le(uint32_t v) { uint32_t a = CMDS + cn * 4; arena[a] = (uint8_t)v; arena[a + 1] = (uint8_t)(v >> 8); arena[a + 2] = (uint8_t)(v >> 16); arena[a + 3] = (uint8_t)(v >> 24); cn++; }
static uint32_t f32(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

int main(void)
{
    char err[256];
    struct acvirgl *v = acvirgl_create(NULL, err, sizeof err);
    uint32_t c, i;
    if (!v) { printf("acvirgl: skipped (%s)\n", err); return 0; }
    printf("acvirgl: drawing with %s on %s\n", acvirgl_api(v), acvirgl_renderer(v));
    CHECK(strcmp(acvirgl_renderer(v), "unknown") != 0, "the host's renderer is unknown");

    begin();
    uint32_t a_hello[1] = { 0 }, a_info[3] = { 2, 0, 0 }, a_caps[4] = { 2, 2, 0x20000, 0x8000 };
    uint32_t ch = cmd(ACV_HELLO, 1, a_hello), ci = cmd(ACV_CAPSET_INFO, 3, a_info), cc = cmd(ACV_CAPS, 4, a_caps);
    memcpy(arena + 0x30000, "acvirgl test", 12);
    uint32_t a_ctx[3] = { 1, 0x30000, 12 };
    uint32_t cx = cmd(ACV_CTX_CREATE, 3, a_ctx);
    /* a 64x64 B8G8R8A8 render target and texture, attached to context 1 */
    uint32_t a_res[12] = { 1, 2 /* PIPE_TEXTURE_2D */, 1 /* B8G8R8A8_UNORM */, 2 | 8, 64, 64, 1, 1, 0, 0, 0, 1 };
    uint32_t cr = cmd(ACV_RES_CREATE, 12, a_res);
    acvirgl_run(v, map, NULL, BLK, at - BLK);
    CHECK(result(ch) == ACV_PROTOCOL, "hello: %d", result(ch));
    CHECK(rbe(ch + 8) & ACV_HOST_PRESENT, "hello says the host presents (flags %08x)", rbe(ch + 8));
    CHECK(rbe(ci + 12) >= 1 && rbe(ci + 16) > 0, "capset 2: version %u, %u bytes", rbe(ci + 12), rbe(ci + 16));
    CHECK(result(cc) > 0 && rbe(0x20000) >= 1, "caps: %d bytes, max_version %u (the first word, in the Amiga's order)", result(cc), rbe(0x20000));
    CHECK(result(cx) == 0 && result(cr) == 0, "context %d, resource %d", result(cx), result(cr));
    printf("acvirgl: capset 2 version %u, %u bytes; caps v1 max_version %u\n", rbe(ci + 12), rbe(ci + 16), rbe(0x20000));

    /* pixels up, turned round in 32-bit units, and back the same way */
    for (i = 0; i < 64 * 64 * 4; i++) arena[0x40000 + i] = (uint8_t)(i * 7 + (i >> 8));
    begin();
    uint32_t a_put[14] = { 1, 1, 0, 256, 0, 0, 0, 0, 64, 64, 1, 0x40000, 64 * 64 * 4, ACV_SWAP_32 };
    uint32_t a_get[14] = { 1, 1, 0, 256, 0, 0, 0, 0, 64, 64, 1, 0x50000, 64 * 64 * 4, ACV_SWAP_32 };
    uint32_t cp = cmd(ACV_PUT, 14, a_put), cg = cmd(ACV_GET, 14, a_get);
    acvirgl_run(v, map, NULL, BLK, at - BLK);
    CHECK(result(cp) == 0 && result(cg) == 0 && !memcmp(arena + 0x40000, arena + 0x50000, 64 * 64 * 4), "pixels up and back (%d, %d)", result(cp), result(cg));
    /* up as they are, back turned round: each pixel's bytes reversed */
    a_put[13] = ACV_SWAP_NONE;
    begin(); cp = cmd(ACV_PUT, 14, a_put); cg = cmd(ACV_GET, 14, a_get);
    acvirgl_run(v, map, NULL, BLK, at - BLK);
    CHECK(arena[0x50000] == arena[0x40003] && arena[0x50003] == arena[0x40000], "a 32-bit unit turned round once");

    /* a surface on the texture, the framebuffer, a clear to 0.25, 0.5, 0.75, 1 */
    cn = 0;
    le(VIRGL_CMD0(VIRGL_CCMD_CREATE_SUB_CTX, 0, 1)); le(1);
    le(VIRGL_CMD0(VIRGL_CCMD_SET_SUB_CTX, 0, 1)); le(1);
    le(VIRGL_CMD0(VIRGL_CCMD_CREATE_OBJECT, VIRGL_OBJECT_SURFACE, VIRGL_OBJ_SURFACE_SIZE)); le(10); le(1); le(1); le(0); le(0);
    le(VIRGL_CMD0(VIRGL_CCMD_SET_FRAMEBUFFER_STATE, 0, VIRGL_SET_FRAMEBUFFER_STATE_SIZE(1))); le(1); le(0); le(10);
    le(VIRGL_CMD0(VIRGL_CCMD_CLEAR, 0, VIRGL_OBJ_CLEAR_SIZE)); le(1 << 2);
    le(f32(0.25f)); le(f32(0.5f)); le(f32(0.75f)); le(f32(1.0f));
    { double d = 1.0; uint32_t w2[2]; memcpy(w2, &d, 8); le(w2[0]); le(w2[1]); }
    le(0);
    begin();
    uint32_t a_sub[3] = { 1, CMDS, cn };
    uint32_t cs = cmd(ACV_SUBMIT, 3, a_sub);
    cg = cmd(ACV_GET, 14, a_get);
    acvirgl_run(v, map, NULL, BLK, at - BLK);
    c = 0x50000 + (10 * 64 + 10) * 4;
    printf("acvirgl: the cleared pixel reads %02x %02x %02x %02x\n", arena[c], arena[c + 1], arena[c + 2], arena[c + 3]);
    CHECK(result(cs) == 0 && result(cg) == 0, "submit %d, get %d", result(cs), result(cg));
    CHECK(arena[c] == 0xFF && arena[c + 1] >= 0x3F && arena[c + 1] <= 0x40 && arena[c + 2] >= 0x7F && arena[c + 2] <= 0x80
          && arena[c + 3] >= 0xBF && arena[c + 3] <= 0xC0, "the clear comes back as A, R, G, B");


    /* ACV_PRESENT: a known picture into the resource (bytes as they are,
     * so B, G, R, A as B8G8R8A8 keeps them), then straight into "video RAM". */
    for (i = 0; i < 64 * 64 * 4; i++) arena[0x40000 + i] = (uint8_t)(i * 13 + (i >> 9) + 5);
    a_put[13] = ACV_SWAP_NONE;
    begin(); cp = cmd(ACV_PUT, 14, a_put);
    acvirgl_run(v, map, NULL, BLK, at - BLK);
    CHECK(result(cp) == 0, "the picture went up (%d)", result(cp));
    {
        /* a 100x80 XRGB32 target at 0x60000, the resource at (10, 5) in it;
         * rectangles: one inside, one that runs off the resource and the
         * target, one wholly outside the resource */
        enum { TW = 100, TH = 80, TB = TW * 4, T = 0x60000, T16 = 0x70000 };
        uint32_t pa[16] = { 1, 1, 0, (10u << 16) | 5u, T, TB, OGPU_FMT_XRGB32, ((uint32_t)TW << 16) | TH, 0, 3,
                            (12u << 16) | 7u, (20u << 16) | 10u,
                            (60u << 16) | 60u, (40u << 16) | 40u,
                            (80u << 16) | 0u, (20u << 16) | 4u };
        uint32_t x, y, ok = 1, outside_ok = 1, pixels;
        memset(arena + T, 0xEE, TB * TH);
        begin(); c = cmd(ACV_PRESENT, 16, pa);
        acvirgl_run(v, map, NULL, BLK, at - BLK);
        /* 20x10 + (74-60)x(69-60) = 200 + 126 */
        pixels = 20 * 10 + 14 * 9;
        CHECK(result(c) == (int32_t)pixels, "present wrote %d pixels, want %u", result(c), pixels);
        for (y = 0; y < TH; y++)
            for (x = 0; x < TW; x++) {
                const uint8_t *d = arena + T + y * TB + x * 4;
                int in = (x >= 12 && x < 32 && y >= 7 && y < 17) || (x >= 60 && x < 74 && y >= 60 && y < 69);
                if (in) {
                    const uint8_t *p = arena + 0x40000 + ((y - 5) * 64 + (x - 10)) * 4;
                    if (d[0] != 0 || d[1] != p[2] || d[2] != p[1] || d[3] != p[0]) ok = 0;
                } else if (d[0] != 0xEE || d[1] != 0xEE || d[2] != 0xEE || d[3] != 0xEE) outside_ok = 0;
            }
        CHECK(ok, "present: X 0, R, G, B from the resource");
        CHECK(outside_ok, "present: nothing outside the rectangles changed");

        /* the same into RGB565, flipped */
        pa[4] = T16; pa[5] = TW * 2; pa[6] = OGPU_FMT_RGB565; pa[8] = ACV_PRESENT_FLIP_Y; pa[9] = 1;
        memset(arena + T16, 0, TW * 2 * TH);
        begin(); c = cmd(ACV_PRESENT, 12, pa);
        acvirgl_run(v, map, NULL, BLK, at - BLK);
        ok = result(c) == 200;
        for (y = 7; y < 17; y++)
            for (x = 12; x < 32; x++) {
                const uint8_t *d = arena + T16 + y * TW * 2 + x * 2;
                const uint8_t *p = arena + 0x40000 + ((63 - (y - 5)) * 64 + (x - 10)) * 4;
                if (d[0] != ((p[2] & 0xF8) | (p[1] >> 5)) || d[1] != (((p[1] & 0x1C) << 3) | (p[0] >> 3))) ok = 0;
            }
        CHECK(ok, "present: RGB565, flipped (%d)", result(c));

        /* refusals: out of reach, an unknown resource, a bad format, bad flags */
        pa[4] = ARENA - 16; pa[5] = TB; pa[6] = OGPU_FMT_XRGB32; pa[8] = 0;
        begin(); c = cmd(ACV_PRESENT, 12, pa);
        pa[4] = T; pa[1] = 99; x = cmd(ACV_PRESENT, 12, pa);
        pa[1] = 1; pa[6] = OGPU_FMT_CLUT8; y = cmd(ACV_PRESENT, 12, pa);
        acvirgl_run(v, map, NULL, BLK, at - BLK);
        CHECK(result(c) == ACV_ERR_NOMAP && result(x) == ACV_ERR_RENDERER && result(y) == ACV_ERR_BADCMD,
              "present refusals: %d %d %d", result(c), result(x), result(y));
        pa[6] = OGPU_FMT_XRGB32; pa[8] = 2;
        begin(); c = cmd(ACV_PRESENT, 12, pa);
        acvirgl_run(v, map, NULL, BLK, at - BLK);
        CHECK(result(c) == ACV_ERR_BADCMD, "present: unknown flags (%d)", result(c));
        /* a count longer than the command */
        pa[8] = 0; pa[9] = 3;
        begin(); c = cmd(ACV_PRESENT, 12, pa);
        acvirgl_run(v, map, NULL, BLK, at - BLK);
        CHECK(result(c) == ACV_ERR_BADCMD, "present: a short rectangle list (%d)", result(c));
        /* a target past 32 bits: bpr 0x01000000 and 257 rows wrap to one
         * row in 32 bits; refused, not written 16 MiB a row past the map */
        pa[5] = 0x01000000u; pa[7] = ((uint32_t)TW << 16) | 257u; pa[9] = 1;
        begin(); c = cmd(ACV_PRESENT, 12, pa);
        acvirgl_run(v, map, NULL, BLK, at - BLK);
        CHECK(result(c) == ACV_ERR_NOMAP, "present: a target past 32 bits (%d)", result(c));
    }

    /* a wrong length: BADCMD, and nothing after it runs */
    begin();
    be(at, ACV_HDR(ACV_RES_UNREF, 2)); be(at + 4, 0); at += 8;
    cg = cmd(ACV_HELLO, 1, a_hello);
    acvirgl_run(v, map, NULL, BLK, at - BLK);
    CHECK(result(BLK) == ACV_ERR_BADCMD && rbe(cg + 4) == 0xDEADBEEFu, "a short command stops the block");

    {
        struct acvirgl_stats s;
        acvirgl_stats(v, &s);
        printf("acvirgl: %llu blocks, %llu submits, %llu puts, %llu gets, %llu errors\n", (unsigned long long)s.blocks,
               (unsigned long long)s.submits, (unsigned long long)s.puts, (unsigned long long)s.gets, (unsigned long long)s.errors);
        printf("acvirgl: %llu presents, %llu rectangles, %llu pixels; %llu frame bytes read back\n", (unsigned long long)s.presents,
               (unsigned long long)s.present_rects, (unsigned long long)s.present_pixels, (unsigned long long)s.frame_get_bytes);
        CHECK(s.presents == 2 && s.present_rects == 3 && s.present_pixels == 526 && s.frame_get_bytes == 0, "present counters");
    }

    /* Deferred presents (acvirgl_defer_presents): the result at once, the
     * pixels once settled; the context's next command, the resource's
     * unref and acvirgl_settle(ACVIRGL_ALL) finish them first; a drop
     * writes nothing. */
    {
        enum { TW = 100, TH = 80, TB = TW * 4, T = 0x60000 };
        uint32_t pa[12] = { 1, 1, 0, 0, T, TB, OGPU_FMT_XRGB32, ((uint32_t)TW << 16) | TH, 0, 1, (2u << 16) | 3u, (16u << 16) | 8u };
        struct acvirgl_stats s0, s1;
        uint32_t x, y, ok;
        int n;
        acvirgl_stats(v, &s0);
        acvirgl_defer_presents(v, defer_all);
        memset(arena + T, 0xEE, TB * TH);
        begin(); c = cmd(ACV_PRESENT, 12, pa);
        acvirgl_run(v, map, NULL, BLK, at - BLK);
        CHECK(result(c) == 16 * 8, "deferred present: its pixels told at once (%d)", result(c));
        if (acvirgl_pending(v)) {
            CHECK(arena[T + 3 * TB + 2 * 4 + 1] == 0xEE, "deferred present: nothing written yet");
            n = acvirgl_settle(v, map, NULL, ACVIRGL_ALL);
            CHECK(n == 1 && !acvirgl_pending(v), "settle all finishes it (%d)", n);
        } else
            printf("acvirgl: this virglrenderer has no per-context fences; presents are not deferred\n");
        for (ok = 1, y = 3; y < 11; y++)
            for (x = 2; x < 18; x++) {
                const uint8_t *d = arena + T + y * TB + x * 4, *p = arena + 0x40000 + (y * 64 + x) * 4;
                if (d[0] != 0 || d[1] != p[2] || d[2] != p[1] || d[3] != p[0]) ok = 0;
            }
        CHECK(ok && arena[T + 2 * TB + 2 * 4 + 1] == 0xEE, "deferred present: the same pixels as at once, and only there");

        /* the context's next command finishes it first */
        memset(arena + T, 0xEE, TB * TH);
        begin(); c = cmd(ACV_PRESENT, 12, pa); cg = cmd(ACV_GET, 14, a_get);
        acvirgl_run(v, map, NULL, BLK, at - BLK);
        CHECK(!acvirgl_pending(v) && arena[T + 3 * TB + 2 * 4 + 1] != 0xEE, "a GET of the context finishes its present first");
        /* the fence: once signalled, a settle for the ready ones finishes it */
        memset(arena + T, 0xEE, TB * TH);
        begin(); c = cmd(ACV_PRESENT, 12, pa);
        acvirgl_run(v, map, NULL, BLK, at - BLK);
        for (n = 0, x = 0; acvirgl_pending(v) && x < 2000; x++) {
            struct timespec ts = { 0, 1000000 };
            n += acvirgl_settle(v, map, NULL, ACVIRGL_READY);
            if (acvirgl_pending(v)) nanosleep(&ts, NULL);
        }
        CHECK(!acvirgl_pending(v) && arena[T + 3 * TB + 2 * 4 + 1] != 0xEE, "the frame's fence signals and it is finished (%d tries)", (int)x);
        /* dropped: nothing written */
        memset(arena + T, 0xEE, TB * TH);
        begin(); c = cmd(ACV_PRESENT, 12, pa);
        acvirgl_run(v, map, NULL, BLK, at - BLK);
        acvirgl_settle(v, map, NULL, ACVIRGL_DROP);
        CHECK(!acvirgl_pending(v) && arena[T + 3 * TB + 2 * 4 + 1] == 0xEE, "dropped presents write nothing");
        /* the target not allowed: at once, as before */
        acvirgl_defer_presents(v, defer_none);
        begin(); c = cmd(ACV_PRESENT, 12, pa);
        acvirgl_run(v, map, NULL, BLK, at - BLK);
        CHECK(!acvirgl_pending(v) && arena[T + 3 * TB + 2 * 4 + 1] != 0xEE, "a target the runtime refuses is written at once");
        acvirgl_defer_presents(v, NULL);
        acvirgl_stats(v, &s1);
        printf("acvirgl: deferred %llu, finished after the fence %llu, before it %llu\n", (unsigned long long)(s1.deferred - s0.deferred),
               (unsigned long long)(s1.deferred_ready - s0.deferred_ready), (unsigned long long)(s1.deferred_forced - s0.deferred_forced));
        CHECK(s1.presents - s0.presents == 4, "deferred presents count as presents when finished (%llu)", (unsigned long long)(s1.presents - s0.presents));
    }

    /* Direct presents (acvirgl_direct_presents): the GPU writes the frame into
     * guest memory itself. The same bytes as an ordinary present: XRGB32 and
     * ARGB32 go direct, RGB565 falls back, flipped and not, from a resource
     * stored either way up. Skipped where the GPU can't (not counted as direct). */
    {
        enum { TW = 100, TH = 80, TB = TW * 4, T = 0x60000, R = 0x80000 };
        uint32_t a_y0[12] = { 3, 2, 1, 2 | 8, 64, 64, 1, 1, 0, 0, 1 /* VIRGL_RESOURCE_Y_0_TOP */, 1 };
        uint32_t a_put3[14] = { 3, 1, 0, 256, 0, 0, 0, 0, 64, 64, 1, 0x40000, 64 * 64 * 4, ACV_SWAP_NONE };
        static const uint32_t fmts[3] = { OGPU_FMT_XRGB32, OGPU_FMT_ARGB32, OGPU_FMT_RGB565 };
        struct acvirgl_stats s0, s1;
        uint32_t h, f, y;
        int same = 1;
        acvirgl_stats(v, &s0);
        begin(); c = cmd(ACV_RES_CREATE, 12, a_y0); cp = cmd(ACV_PUT, 14, a_put3);
        acvirgl_run(v, map, NULL, BLK, at - BLK);
        CHECK(result(c) == 0 && result(cp) == 0, "direct: a Y_0_TOP resource and its picture (%d, %d)", result(c), result(cp));
        for (h = 1; h <= 3; h += 2)
            for (f = 0; f < 6; f++) {
                uint32_t fmt = fmts[f % 3], bpr = fmt == OGPU_FMT_RGB565 ? TW * 2 : TB;
                uint32_t pa[16] = { 1, h, 0, (10u << 16) | 5u, T, bpr, fmt, ((uint32_t)TW << 16) | TH, f >= 3 ? ACV_PRESENT_FLIP_Y : 0, 3,
                                    (12u << 16) | 7u, (20u << 16) | 10u, (60u << 16) | 60u, (40u << 16) | 40u, (80u << 16) | 0u, (20u << 16) | 4u };
                int32_t r0, r1;
                acvirgl_direct_presents(v, NULL, NULL, NULL);
                memset(arena + R, 0xEE, TB * TH);
                pa[4] = R;
                begin(); c = cmd(ACV_PRESENT, 16, pa);
                acvirgl_run(v, map, NULL, BLK, at - BLK);
                r0 = result(c);
                acvirgl_direct_presents(v, defer_all, NULL, NULL);
                memset(arena + T, 0xEE, TB * TH);
                pa[4] = T;
                begin(); c = cmd(ACV_PRESENT, 16, pa);
                acvirgl_run(v, map, NULL, BLK, at - BLK);
                r1 = result(c);
                if (r0 != r1 || r0 <= 0 || memcmp(arena + R, arena + T, TB * TH)) {
                    same = 0;
                    for (y = 0; y < TB * TH && arena[R + y] == arena[T + y]; y++) ;
                    printf("acvirgl: direct differs: resource %u, case %u: %d %d, byte %u: %02x %02x\n", h, f, r0, r1, y, arena[R + y], arena[T + y]);
                }
            }
        /* the same, into a sealed memfd (udmabuf), as the runtime's guarded video RAM is */
        {
            struct acvirgl_stats m0, m1;
            mf_fd = memfd_create("acvirgl-test", MFD_CLOEXEC | MFD_ALLOW_SEALING);
            if (mf_fd >= 0 && !ftruncate(mf_fd, MF_SIZE) && !fcntl(mf_fd, F_ADD_SEALS, F_SEAL_SHRINK)
                && (mf = mmap(NULL, MF_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, mf_fd, 0)) != MAP_FAILED) {
                uint32_t pa[16] = { 1, 1, 0, (10u << 16) | 5u, T, TB, OGPU_FMT_XRGB32, ((uint32_t)TW << 16) | TH, 0, 3,
                                    (12u << 16) | 7u, (20u << 16) | 10u, (60u << 16) | 60u, (40u << 16) | 40u, (80u << 16) | 0u, (20u << 16) | 4u };
                int32_t r0, r1;
                acvirgl_stats(v, &m0);
                acvirgl_direct_presents(v, NULL, NULL, NULL);
                memset(arena + R, 0xEE, TB * TH);
                pa[4] = R;
                begin(); c = cmd(ACV_PRESENT, 16, pa);
                acvirgl_run(v, map, NULL, BLK, at - BLK);
                r0 = result(c);
                acvirgl_direct_presents(v, defer_all, mf_file, NULL);
                memset(mf + 0x8000, 0xEE, TB * TH);
                pa[4] = MF_AT + 0x8000;
                begin(); c = cmd(ACV_PRESENT, 16, pa);
                acvirgl_run(v, map, NULL, BLK, at - BLK);
                r1 = result(c);
                acvirgl_stats(v, &m1);
                printf("acvirgl: direct into a memfd: %llu of %llu presents\n", (unsigned long long)(m1.direct - m0.direct), (unsigned long long)(m1.presents - m0.presents));
                CHECK(r0 == r1 && r0 > 0 && !memcmp(arena + R, mf + 0x8000, TB * TH), "direct: into a memfd, the same bytes as an ordinary present (%d %d)", r0, r1);
                /* a let-go, then the pages changing: the next present still reaches the file */
                acvirgl_direct_forget(v);
                memset(mf + 0x8000, 0xEE, TB * TH);
                begin(); c = cmd(ACV_PRESENT, 16, pa);
                acvirgl_run(v, map, NULL, BLK, at - BLK);
                CHECK(result(c) == r0 && !memcmp(arena + R, mf + 0x8000, TB * TH), "direct: after acvirgl_direct_forget, the file still written");
                acvirgl_direct_presents(v, NULL, NULL, NULL);
                munmap(mf, MF_SIZE); mf = NULL;
            }
            if (mf_fd >= 0) close(mf_fd);
            mf_fd = -1;
        }
        acvirgl_direct_presents(v, NULL, NULL, NULL);
        CHECK(same, "direct: the same bytes as an ordinary present");
        begin(); h = 3; cmd(ACV_RES_UNREF, 1, &h);
        acvirgl_run(v, map, NULL, BLK, at - BLK);
        acvirgl_stats(v, &s1);
        printf("acvirgl: direct %llu of %llu presents\n", (unsigned long long)(s1.direct - s0.direct), (unsigned long long)(s1.presents - s0.presents));
        CHECK(s1.direct - s0.direct >= 8 || s1.direct == s0.direct, "direct: all eight 32-bit presents, or none on a GPU that can't (%llu)", (unsigned long long)(s1.direct - s0.direct));
    }

    /* Presents one frame behind (acvirgl_behind_presents): the GPU copies the
     * frame into a staging buffer at the present, and it is written once
     * settled. The same pixels as at once, for 32-bit and RGB565, flipped
     * and not, from a resource stored either way up (VIRGL_RESOURCE_Y_0_TOP);
     * the frame as it was presented, though the resource is drawn again
     * after it; the context's later commands don't wait for it; a drop
     * writes nothing. */
    {
        enum { TW = 100, TH = 80, TB = TW * 4, T = 0x60000, R = 0x80000 };
        uint32_t a_y0[12] = { 3, 2, 1, 2 | 8, 64, 64, 1, 1, 0, 0, 1 /* VIRGL_RESOURCE_Y_0_TOP */, 1 };
        uint32_t a_put3[14] = { 3, 1, 0, 256, 0, 0, 0, 0, 64, 64, 1, 0x40000, 64 * 64 * 4, ACV_SWAP_NONE };
        uint32_t a_put1[14] = { 1, 1, 0, 256, 0, 0, 0, 0, 64, 64, 1, 0x40000, 64 * 64 * 4, ACV_SWAP_NONE };
        struct acvirgl_stats s0, s1;
        uint32_t h, f, x;
        int n, same = 1, pend = 1;
        acvirgl_stats(v, &s0);
        begin(); c = cmd(ACV_RES_CREATE, 12, a_y0); cp = cmd(ACV_PUT, 14, a_put3);
        acvirgl_run(v, map, NULL, BLK, at - BLK);
        CHECK(result(c) == 0 && result(cp) == 0, "a Y_0_TOP resource and its picture (%d, %d)", result(c), result(cp));
        for (h = 1; h <= 3; h += 2)
            for (f = 0; f < 4; f++) {
                /* f: bit 0 RGB565, bit 1 flipped */
                uint32_t fmt = f & 1 ? OGPU_FMT_RGB565 : OGPU_FMT_XRGB32, bpr = f & 1 ? TW * 2 : TB;
                uint32_t pa[16] = { 1, h, 0, (10u << 16) | 5u, T, bpr, fmt, ((uint32_t)TW << 16) | TH, f & 2 ? ACV_PRESENT_FLIP_Y : 0, 3,
                                    (12u << 16) | 7u, (20u << 16) | 10u, (60u << 16) | 60u, (40u << 16) | 40u, (80u << 16) | 0u, (20u << 16) | 4u };
                int32_t r0, r1;
                acvirgl_behind_presents(v, NULL);
                memset(arena + R, 0xEE, TB * TH);
                pa[4] = R;
                begin(); c = cmd(ACV_PRESENT, 16, pa);
                acvirgl_run(v, map, NULL, BLK, at - BLK);
                r0 = result(c);
                acvirgl_behind_presents(v, defer_all);
                memset(arena + T, 0xEE, TB * TH);
                pa[4] = T;
                /* presented, then drawn over and read back by the program: the present keeps the frame it had */
                memset(arena + 0x48000, 0x11, 64 * 64 * 4);
                a_put1[0] = h; a_put1[11] = 0x48000;
                begin(); c = cmd(ACV_PRESENT, 16, pa); cp = cmd(ACV_PUT, 14, a_put1); cg = cmd(ACV_GET, 14, a_get);
                acvirgl_run(v, map, NULL, BLK, at - BLK);
                r1 = result(c);
                if (!acvirgl_pending(v)) pend = 0;
                n = acvirgl_settle(v, map, NULL, ACVIRGL_ALL);
                if (r0 != r1 || r0 <= 0 || n != 1 || memcmp(arena + R, arena + T, TB * TH)) {
                    same = 0;
                    printf("acvirgl: behind differs: resource %u, case %u: %d %d %d\n", h, f, r0, r1, n);
                }
                /* the picture back in the resource */
                a_put1[11] = 0x40000;
                if (h == 3) { begin(); cmd(ACV_PUT, 14, a_put3); acvirgl_run(v, map, NULL, BLK, at - BLK); }
                else { begin(); cmd(ACV_PUT, 14, a_put1); acvirgl_run(v, map, NULL, BLK, at - BLK); }
            }
        CHECK(pend, "behind: presents left to finish over the context's next commands");
        CHECK(same, "behind: the same pixels as at once, the frame as presented");
        /* ready ones finish on a settle once their fence signals */
        {
            uint32_t pa[12] = { 1, 1, 0, 0, T, TB, OGPU_FMT_XRGB32, ((uint32_t)TW << 16) | TH, 0, 1, (2u << 16) | 3u, (16u << 16) | 8u };
            memset(arena + T, 0xEE, TB * TH);
            begin(); c = cmd(ACV_PRESENT, 12, pa);
            acvirgl_run(v, map, NULL, BLK, at - BLK);
            for (x = 0; acvirgl_pending(v) && x < 2000; x++) {
                struct timespec ts = { 0, 1000000 };
                acvirgl_settle(v, map, NULL, ACVIRGL_READY);
                if (acvirgl_pending(v)) nanosleep(&ts, NULL);
            }
            CHECK(!acvirgl_pending(v) && arena[T + 3 * TB + 2 * 4 + 1] != 0xEE, "behind: the fence signals and it is finished (%d tries)", (int)x);
            /* a newer present over the same rectangles writes the older first */
            begin(); c = cmd(ACV_PRESENT, 12, pa); x = cmd(ACV_PRESENT, 12, pa);
            acvirgl_run(v, map, NULL, BLK, at - BLK);
            CHECK(acvirgl_pending(v) == 1, "behind: a present over an older one finishes it first (%d left)", acvirgl_pending(v));
            /* dropped: nothing written */
            acvirgl_settle(v, map, NULL, ACVIRGL_ALL);
            memset(arena + T, 0xEE, TB * TH);
            begin(); c = cmd(ACV_PRESENT, 12, pa);
            acvirgl_run(v, map, NULL, BLK, at - BLK);
            acvirgl_settle(v, map, NULL, ACVIRGL_DROP);
            CHECK(!acvirgl_pending(v) && arena[T + 3 * TB + 2 * 4 + 1] == 0xEE, "behind: dropped presents write nothing");
        }
        acvirgl_behind_presents(v, NULL);
        begin(); h = 3; cmd(ACV_RES_UNREF, 1, &h);
        acvirgl_run(v, map, NULL, BLK, at - BLK);
        acvirgl_stats(v, &s1);
        printf("acvirgl: behind %llu of %llu presents\n", (unsigned long long)(s1.behind - s0.behind), (unsigned long long)(s1.presents - s0.presents));
        CHECK(s1.behind - s0.behind == 12, "behind: twelve presents one frame behind (%llu)", (unsigned long long)(s1.behind - s0.behind));
    }

    /* Handles as the Amiga makes them: GLA's resource addresses (sparse,
     * above 1 MiB). 600 display targets, every other one dropped again;
     * the rest present and count their GETs as frames read back. */
    {
        enum { N = 600, T = 0x60000 };
        struct acvirgl_stats s0, s1;
        uint32_t k, bad = 0;
        acvirgl_stats(v, &s0);
        for (k = 0; k < N; k += 20) {
            begin();
            for (i = k; i < k + 20; i++) {
                uint32_t a_dt[12] = { 0x40001000u + i * 0x48u, 2, 1, 2 | (1u << 7), 8, 8, 1, 1, 0, 0, 0, 1 };
                c = cmd(ACV_RES_CREATE, 12, a_dt);
            }
            acvirgl_run(v, map, NULL, BLK, at - BLK);
            for (i = 0, c = BLK; i < 20; i++, c += 14 * 4) if (result(c) != 0) bad++;
        }
        CHECK(!bad, "600 resources at Amiga addresses (%u refused)", bad);
        for (k = 0; k < N; k += 2) {
            uint32_t h = 0x40001000u + k * 0x48u;
            begin(); cmd(ACV_RES_UNREF, 1, &h);
            acvirgl_run(v, map, NULL, BLK, at - BLK);
        }
        for (k = 0, bad = 0; k < N; k++) {
            uint32_t pa[12] = { 1, 0x40001000u + k * 0x48u, 0, 0, T, 8 * 4, OGPU_FMT_XRGB32, (8u << 16) | 8u, 0, 1,
                                0, (4u << 16) | 4u };
            int32_t want = k & 1 ? 16 : ACV_ERR_RENDERER;
            begin(); c = cmd(ACV_PRESENT, 12, pa);
            acvirgl_run(v, map, NULL, BLK, at - BLK);
            if (result(c) != want) bad++;
        }
        CHECK(!bad, "present by Amiga-address handles: the kept ones draw, the dropped ones refuse (%u wrong)", bad);
        {
            uint32_t a_g[14] = { 0x40001000u + 599 * 0x48u, 1, 0, 32, 0, 0, 0, 0, 8, 8, 1, 0x50000, 8 * 8 * 4, ACV_SWAP_NONE };
            begin(); c = cmd(ACV_GET, 14, a_g);
            acvirgl_run(v, map, NULL, BLK, at - BLK);
        }
        acvirgl_stats(v, &s1);
        CHECK(result(c) == 0 && s1.frame_get_bytes - s0.frame_get_bytes == 8 * 8 * 4 && s1.presents - s0.presents == N / 2,
              "a display target's GET counts as a frame read back (%d, %llu bytes, %llu presents)", result(c),
              (unsigned long long)(s1.frame_get_bytes - s0.frame_get_bytes), (unsigned long long)(s1.presents - s0.presents));
    }
    acvirgl_destroy(v);
    printf("acvirgl: %d of %d checks passed\n", checks - fails, checks);
    return fails != 0;
}
