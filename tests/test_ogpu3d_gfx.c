/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * Stream3D's TRIANGLES on the GPU's own graphics pipeline
 * (host/vulkan/ogpu3d_gfx.c, OGPU_VK_3D=gfx) against the C core and
 * against the exact picture. stream3d.h gives 3D pictures a tolerance, +-2
 * a channel with edge pixels excepted; that holds where there is no
 * texture. With one it can't: the core walks texture coordinates in 24.8
 * fixed point (exact every 8 pixels under perspective), so they drift up
 * to a texel across a 640-pixel row, while the GPU works each pixel's out
 * in floats; on these benches' busy textures (a texel pattern finer than
 * the pixels) a fraction of a texel changes a pixel a lot. So:
 *  - Untextured benches: within +-2 of the core (a 565 target's own step,
 *    1 in 31 or 63, in 8-bit terms: 9), edge pixels excepted (one whose
 *    core colour differs from a neighbour's by more than 48 in a channel):
 *    at most 0.5% of the pixels off the edges, and 3% in all, out of it.
 *  - The two 640x480 quads (bilinear, with and without perspective and
 *    fog): against the exact picture, worked out here in doubles from the
 *    vertices and the texture; the GPU's mean error at most 1.5 a channel,
 *    and no more than the core's.
 *  - The other textured benches (W3DTest, Cow3D, MGLTest shapes): against
 *    the core, a pixel passing when one within two pixels of it in the
 *    core's picture is within 16: at most 5% may fail (a triangle drawn
 *    wrong, or missing, fails it by far).
 *  - Random scenes (every mode stream v1.2 has; the graphics pipeline
 *    declines what it can't draw, and the core draws that exactly): the
 *    same results and command counts as the core, with no crash; the bytes
 *    they leave are counted, not judged (Z buffers' low bits differ).
 * OGPU_GFX_PPM=dir writes each bench's two pictures there; OGPU_GFX_TIME=n
 * times each bench n times on the core and on the pipeline (frames a second,
 * the frame and its buffers in memory before and after each, as in a game).
 *   test_ogpu3d_gfx [random-rounds] */
#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../host/vulkan/ogpu_vk.h"
#include "../include/gfopengpu/stream3d.h"
#include "../library/opengpu/ogpu_3d.h"
#include "scenes3d.h"

#define RAM   (4L << 20)            /* "Fast RAM": Amiga addresses 0 .. RAM */
#define VBASE 0x40000000UL
#define VSIZE (4UL << 20)

static ogpu_u8 ram_ref[RAM], ram_vk[RAM], vram_ref[VSIZE];
static ogpu_u8 *vram_vk;
static unsigned char sbuf[1 << 20];
static long words;
static struct ogpu_core C;
static struct ogpu3d T_ref;
static struct ogpu_vk *V;
static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static ogpu_u8 *map_ref(void *user, ogpu_u32 a, ogpu_u32 len) {
    (void)user;
    if (a < RAM && len <= (ogpu_u32)RAM - a) return ram_ref + a;
    if (a >= VBASE && a - VBASE <= VSIZE && len <= VSIZE - (a - VBASE)) return vram_ref + (a - VBASE);
    return 0;
}
static ogpu_u8 *map_vk(void *user, ogpu_u32 a, ogpu_u32 len) {
    (void)user;
    if (a < RAM && len <= (ogpu_u32)RAM - a) return ram_vk + a;
    return 0;
}
static void no_fence(void *user, ogpu_u32 id) { (void)user; (void)id; }

static void ref_afresh(void) {
    ogpu_core_init(&C);
    C.map = map_ref; C.fence = no_fence; C.user = 0;
    ogpu3d_init(&T_ref, &C);
    C.d3 = &T_ref;
}

static void envs(struct ogpu_scene3d_env *er, struct ogpu_scene3d_env *ev) {
    er->mem[0] = vram_ref; er->addr[0] = VBASE; er->mem[1] = ram_ref + 0x10; er->addr[1] = 0x10;
    ev->mem[0] = vram_vk; ev->addr[0] = VBASE; ev->mem[1] = ram_vk + 0x10; ev->addr[1] = 0x10;
}

/* Both through the stream; 1 when they agree on what was done and said. */
static int run_both(const char *what) {
    long d_ref = ogpu_core_run(&C, sbuf, words), d_vk = ogpu_vk_run(V, sbuf, words);
    if (d_ref != d_vk || C.last_error != ogpu_vk_last_error(V)) {
        CHECK(0, "%s: done %ld/%ld, error %d/%d", what, d_ref, d_vk, C.last_error, ogpu_vk_last_error(V));
        return 0;
    }
    return 1;
}

/* A pixel as 8-bit channels R, G, B, A (A 255 without alpha). */
static void px(const ogpu_u8 *p, int fmt, int *c) {
    switch (fmt) {
    case OGPU_FMT_RGB565: {
        int r5 = p[0] >> 3, g6 = ((p[0] & 7) << 3) | (p[1] >> 5), b5 = p[1] & 31;
        c[0] = (r5 << 3) | (r5 >> 2); c[1] = (g6 << 2) | (g6 >> 4); c[2] = (b5 << 3) | (b5 >> 2); c[3] = 255;
        return;
    }
    case OGPU_FMT_ARGB32: c[3] = p[0]; c[0] = p[1]; c[1] = p[2]; c[2] = p[3]; return;
    default: c[0] = c[1] = c[2] = p[0]; c[3] = 255;
    }
}

static int bpp(int fmt) { return fmt == OGPU_FMT_RGB565 ? 2 : fmt == OGPU_FMT_ARGB32 ? 4 : 1; }

static void ppm(const char *dir, const char *name, const char *side, const ogpu_u8 *t, int w, int h, int fmt) {
    char path[512];
    FILE *f;
    int x, y, c[4];
    snprintf(path, sizeof path, "%s/%s-%s.ppm", dir, name, side);
    for (x = (int)strlen(dir) + 1; path[x]; x++) if (path[x] == ' ' || path[x] == '/' || path[x] == ',') path[x] = '_';
    if (!(f = fopen(path, "wb"))) return;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            px(t + (long)y * w * bpp(fmt) + (long)x * bpp(fmt), fmt, c);
            fputc(c[0], f); fputc(c[1], f); fputc(c[2], f);
        }
    fclose(f);
}

static int edge(const ogpu_u8 *t, int w, int h, int fmt, int x, int y) {
    static const int dx[4] = { 1, -1, 0, 0 }, dy[4] = { 0, 0, 1, -1 };
    int c[4], n[4], k, j;
    px(t + ((long)y * w + x) * bpp(fmt), fmt, c);
    for (k = 0; k < 4; k++) {
        int xx = x + dx[k], yy = y + dy[k];
        if (xx < 0 || yy < 0 || xx >= w || yy >= h) continue;
        px(t + ((long)yy * w + xx) * bpp(fmt), fmt, n);
        for (j = 0; j < 4; j++) if (abs(c[j] - n[j]) > 48) return 1;
    }
    return 0;
}

/* The exact colour of bench 4 (perspective, linear fog) or 6 (neither) at
 * pixel x, y: the 640x480 quad of tests/scenes3d.c, bilinear over its
 * 256x256 ARGB32 texture (tex), modulated by Gouraud colour. */
static double tex_ch(const ogpu_u8 *tex, int X, int Y, int k) {
    const ogpu_u8 *p = tex + (((long)(Y & 255) * 256 + (X & 255)) * 4);
    return p[k == 3 ? 0 : k + 1];
}

static void exact_px(const ogpu_u8 *tex, int bench, int x, int y, double *out) {
    static const double vx[4] = { 0, 640, 0, 640 }, vy[4] = { 0, 0, 480, 480 }, vw[4] = { 0.08, 0.08, 1, 1 };
    static const double vu[4] = { 0, 6, 0, 6 }, vv[4] = { 0, 0, 6, 6 };
    static const int tri[2][3] = { { 0, 1, 2 }, { 1, 3, 2 } };
    double px = x + 0.5, py = y + 0.5, b[3], U = 0, V = 0, sw = 0, w = 0, col[3] = { 0, 0, 0 }, T[3], fx, fy;
    int t = px / 640 + py / 480 <= 1 ? 0 : 1, i, k, X, Y;
    const int *v = tri[t];
    double d = (vx[v[1]] - vx[v[0]]) * (vy[v[2]] - vy[v[0]]) - (vx[v[2]] - vx[v[0]]) * (vy[v[1]] - vy[v[0]]);
    b[1] = ((px - vx[v[0]]) * (vy[v[2]] - vy[v[0]]) - (vx[v[2]] - vx[v[0]]) * (py - vy[v[0]])) / d;
    b[2] = ((vx[v[1]] - vx[v[0]]) * (py - vy[v[0]]) - (px - vx[v[0]]) * (vy[v[1]] - vy[v[0]])) / d;
    b[0] = 1 - b[1] - b[2];
    for (i = 0; i < 3; i++) {
        unsigned long c = 0xFFFFFFFFUL - (unsigned long)v[i] * 0x101000UL;
        double pw = bench == 4 ? vw[v[i]] : 1;
        U += b[i] * pw * vu[v[i]]; V += b[i] * pw * vv[v[i]]; sw += b[i] * pw;
        w += b[i] * vw[v[i]];
        col[0] += b[i] * (double)((c >> 16) & 255); col[1] += b[i] * (double)((c >> 8) & 255); col[2] += b[i] * (double)(c & 255);
    }
    U = U / sw * 256 - 0.5; V = V / sw * 256 - 0.5;
    X = (int)U - (U < 0); Y = (int)V - (V < 0); fx = U - X; fy = V - Y;
    for (k = 0; k < 3; k++) {
        T[k] = (tex_ch(tex, X, Y, k) * (1 - fx) + tex_ch(tex, X + 1, Y, k) * fx) * (1 - fy)
             + (tex_ch(tex, X, Y + 1, k) * (1 - fx) + tex_ch(tex, X + 1, Y + 1, k) * fx) * fy;
        out[k] = col[k] * T[k] / 255;
    }
    if (bench == 4) {
        static const double fogc[3] = { 0x30, 0x38, 0x48 };
        double f = (w - 0.05) / (0.9 - 0.05);
        if (f < 0) f = 0;
        if (f > 1) f = 1;
        for (k = 0; k < 3; k++) out[k] = out[k] * f + fogc[k] * (1 - f);
    }
}

/* The mean distance a channel from the exact picture. */
static double exact_mae(const ogpu_u8 *tex, int bench, const ogpu_u8 *t) {
    double sum = 0, e[3];
    int x, y, k, c[4];
    for (y = 0; y < 480; y++)
        for (x = 0; x < 640; x++) {
            exact_px(tex, bench, x, y, e);
            px(t + ((long)y * 640 + x) * 4, OGPU_FMT_ARGB32, c);
            for (k = 0; k < 3; k++) sum += c[k] > e[k] ? c[k] - e[k] : e[k] - c[k];
        }
    return sum / (640.0 * 480 * 3);
}

/* Pixels of v with no pixel within two of it in r that is within 16. */
static long near_misses(const ogpu_u8 *r, const ogpu_u8 *v, int w, int h, int fmt) {
    long bad = 0;
    int x, y, dx, dy, k;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            int cv[4], cr[4], ok = 0;
            px(v + ((long)y * w + x) * bpp(fmt), fmt, cv);
            for (dy = -2; dy <= 2 && !ok; dy++)
                for (dx = -2; dx <= 2 && !ok; dx++) {
                    int d = 0;
                    if (x + dx < 0 || y + dy < 0 || x + dx >= w || y + dy >= h) continue;
                    px(r + ((long)(y + dy) * w + x + dx) * bpp(fmt), fmt, cr);
                    for (k = 0; k < 3; k++) if (abs(cr[k] - cv[k]) > d) d = abs(cr[k] - cv[k]);
                    ok = d <= 16;
                }
            bad += !ok;
        }
    return bad;
}

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void time_bench(const char *name, int n) {
    double t0, tc, tg;
    struct ogpu_vk_stats a, b;
    int i;
    t0 = now_s();
    for (i = 0; i < n; i++) ogpu_core_run(&C, sbuf, words);
    tc = now_s() - t0;
    ogpu_vk_stats(V, &a);
    t0 = now_s();
    for (i = 0; i < n; i++) ogpu_vk_run(V, sbuf, words);
    tg = now_s() - t0;
    ogpu_vk_stats(V, &b);
    printf("opengpu gfx: %-28s the core %.1f fps, the graphics pipeline %.1f fps (a frame: before %.2f ms, rows in %.2f, "
           "submit and wait %.2f, rows out %.2f)\n", name, n / tc, n / tg, (b.gfx_before_ms - a.gfx_before_ms) / n,
           (b.gfx_in_ms - a.gfx_in_ms) / n, (b.gfx_wait_ms - a.gfx_wait_ms) / n, (b.gfx_out_ms - a.gfx_out_ms) / n);
}

static void test_benches(void) {
    struct ogpu_scene3d_env er, ev;
    const unsigned char *s;
    int b;
    envs(&er, &ev);
    for (b = 0; b < OGPU_SCENE3D_BENCHES; b++) {
        const char *name;
        int region, w, h, fmt, x, y, tol, k;
        long off, bad = 0, bad_inner = 0, worst = 0, total;
        const ogpu_u8 *tr, *tv;
        struct ogpu_vk_stats s0, s1;
        ogpu_scene3d_bench(&er, b, &words, &name);
        s = ogpu_scene3d_bench(&ev, b, &words, &name);
        if ((size_t)words * 4 > sizeof sbuf) { CHECK(0, "%s: too big", name); continue; }
        memcpy(sbuf, s, (size_t)words * 4);
        ogpu_vk_reset(V);
        ref_afresh();
        ogpu_vk_stats(V, &s0);
        if (!run_both(name)) continue;
        ogpu_vk_stats(V, &s1);
        ogpu_scene3d_bench_target(b, &region, &off, &w, &h, &fmt);
        tr = er.mem[region] + off;
        tv = ev.mem[region] + off;
        tol = fmt == OGPU_FMT_RGB565 ? 9 : 2;
        for (y = 0; y < h; y++)
            for (x = 0; x < w; x++) {
                int cr[4], cv[4], d = 0;
                px(tr + ((long)y * w + x) * bpp(fmt), fmt, cr);
                px(tv + ((long)y * w + x) * bpp(fmt), fmt, cv);
                for (k = 0; k < 4; k++) if (abs(cr[k] - cv[k]) > d) d = abs(cr[k] - cv[k]);
                if (d > worst) worst = d;
                if (d <= tol) continue;
                bad++;
                if (!edge(tr, w, h, fmt, x, y) && !edge(tv, w, h, fmt, x, y)) bad_inner++;
            }
        total = (long)w * h;
        printf("opengpu gfx: %-28s %dx%d: %lu TRIANGLES on the pipeline, %lu declined; %ld of %ld pixels out of +-%d "
               "(%.2f%%), %ld off the edges (%.3f%%), worst %ld\n",
               name, w, h, s1.tri_gfx - s0.tri_gfx, s1.tri_gfx_declined - s0.tri_gfx_declined, bad, total, tol,
               100.0 * bad / total, bad_inner, 100.0 * bad_inner / total, worst);
        CHECK(s1.tri_gfx > s0.tri_gfx, "%s: the graphics pipeline drew nothing", name);
        if (b == 3 || b == 7) {                 /* no texture: the tolerance itself */
            CHECK(bad_inner * 200 <= total, "%s: %ld pixels off the edges out of tolerance", name, bad_inner);
            CHECK(bad * 100 <= total * 3, "%s: %ld pixels out of tolerance", name, bad);
        } else if (b == 4 || b == 6) {          /* the exact picture */
            const ogpu_u8 *tex = ev.mem[1] + 0x100000L;
            double eg = exact_mae(tex, b, tv), ec = exact_mae(tex, b, tr);
            printf("opengpu gfx: %-28s against the exact picture: the graphics pipeline %.2f a channel, the core %.2f\n", name, eg, ec);
            CHECK(eg <= 1.5 && eg <= ec, "%s: %.2f a channel from the exact picture (the core %.2f)", name, eg, ec);
        } else {
            long nm = near_misses(tr, tv, w, h, fmt);
            printf("opengpu gfx: %-28s %ld pixels (%.2f%%) with nothing within 16 two pixels round in the core's\n", name, nm,
                   100.0 * nm / total);
            CHECK(nm * 20 <= total, "%s: %ld pixels unlike the core's round them", name, nm);
        }
        if (getenv("OGPU_GFX_TIME") && atoi(getenv("OGPU_GFX_TIME")) > 0) time_bench(name, atoi(getenv("OGPU_GFX_TIME")));
        if (getenv("OGPU_GFX_PPM")) {
            ppm(getenv("OGPU_GFX_PPM"), name, "core", tr, w, h, fmt);
            ppm(getenv("OGPU_GFX_PPM"), name, "gfx", tv, w, h, fmt);
        }
    }
}

static void test_random(int rounds) {
    struct ogpu_scene3d_env er, ev;
    struct ogpu_vk_stats s0, s1;
    const unsigned char *s;
    long same = 0, bytes = 0, far = 0;
    int n;
    envs(&er, &ev);
    ogpu_vk_stats(V, &s0);
    for (n = 0; n < rounds; n++) {
        char what[64];
        long i;
        memset(vram_ref, 0, OGPU_SCENE3D_REGION); memset(vram_vk, 0, OGPU_SCENE3D_REGION);
        memset(ram_ref, 0, OGPU_SCENE3D_REGION + 0x10); memset(ram_vk, 0, OGPU_SCENE3D_REGION + 0x10);
        ogpu_scene3d_random(&er, (unsigned long)n, &words);
        s = ogpu_scene3d_random(&ev, (unsigned long)n, &words);
        memcpy(sbuf, s, (size_t)words * 4);
        ogpu_vk_reset(V);
        ref_afresh();
        snprintf(what, sizeof what, "random 3D scene %d", n);
        if (!run_both(what)) continue;
        if (!memcmp(vram_ref, vram_vk, OGPU_SCENE3D_REGION) && !memcmp(ram_ref, ram_vk, OGPU_SCENE3D_REGION + 0x10)) same++;
        for (i = 0; i < OGPU_SCENE3D_REGION; i++) {
            if (vram_ref[i] != vram_vk[i]) { bytes++; if (abs(vram_ref[i] - vram_vk[i]) > 8) far++; }
            if (ram_ref[i] != ram_vk[i]) { bytes++; if (abs(ram_ref[i] - ram_vk[i]) > 8) far++; }
        }
    }
    ogpu_vk_stats(V, &s1);
    printf("opengpu gfx: %d random scenes: %lu TRIANGLES on the pipeline, %lu declined to the core, %lu groups, "
           "%lu textures sent, %lu pipelines; %ld scenes byte for byte, %ld bytes differ (%ld by more than 8)\n",
           rounds, s1.tri_gfx - s0.tri_gfx, s1.tri_gfx_declined - s0.tri_gfx_declined, s1.gfx_groups - s0.gfx_groups,
           s1.gfx_tex_uploads - s0.gfx_tex_uploads, s1.gfx_pipelines - s0.gfx_pipelines, same, bytes, far);
    CHECK(s1.tri_gfx > s0.tri_gfx, "random scenes: the graphics pipeline drew nothing");
}

/* Routing by cost (the back end as a host starts it): a few tiny triangles
 * are drawn by the core, as it is quicker than a group's copies and submit,
 * so the picture is the core's byte for byte; a 640x480 quad is drawn by the
 * graphics pipeline. */
static void test_route(int b, int on_gpu) {
    struct ogpu_scene3d_env er, ev;
    const unsigned char *s;
    const char *name;
    int region, w, h, fmt;
    long off;
    struct ogpu_vk_stats s0, s1;
    envs(&er, &ev);
    ogpu_scene3d_bench(&er, b, &words, &name);
    s = ogpu_scene3d_bench(&ev, b, &words, &name);
    memcpy(sbuf, s, (size_t)words * 4);
    ogpu_vk_reset(V);
    ref_afresh();
    ogpu_vk_stats(V, &s0);
    if (!run_both(name)) return;
    ogpu_vk_stats(V, &s1);
    ogpu_scene3d_bench_target(b, &region, &off, &w, &h, &fmt);
    printf("opengpu gfx: by cost, %-19s %lu TRIANGLES on the pipeline, %lu by the core as quicker\n", name,
           s1.tri_gfx - s0.tri_gfx, s1.tri_gfx_routed - s0.tri_gfx_routed);
    if (on_gpu) {
        CHECK(s1.tri_gfx > s0.tri_gfx && s1.tri_gfx_routed == s0.tri_gfx_routed, "%s by cost: not on the pipeline", name);
    } else {
        CHECK(s1.tri_gfx == s0.tri_gfx && s1.tri_gfx_routed > s0.tri_gfx_routed, "%s by cost: not left to the core", name);
        CHECK(!memcmp(er.mem[region] + off, ev.mem[region] + off, (size_t)w * h * bpp(fmt)),
              "%s by cost: the core's picture isn't the core's", name);
    }
    if (getenv("OGPU_GFX_TIME") && atoi(getenv("OGPU_GFX_TIME")) > 0) time_bench(name, atoi(getenv("OGPU_GFX_TIME")));
}

static struct ogpu_vk *start(struct ogpu_vk_config *cfg, int route, char *err, int errlen) {
    struct ogpu_vk *v;
    cfg->route = route;
    v = ogpu_vk_create(cfg, err, errlen);
    if (v) vram_vk = ogpu_vk_vram(v);
    return v;
}

int main(int argc, char **argv) {
    struct ogpu_vk_config cfg;
    struct ogpu_vk_stats st;
    char err[200];
    int rounds = argc > 1 ? atoi(argv[1]) : 300;
    setenv("OGPU_VK_3D", "gfx", 1);
    memset(&cfg, 0, sizeof cfg);
    cfg.vram_base = VBASE;
    cfg.vram_size = VSIZE;
    cfg.arena_size = 1u << 20;
    cfg.map = map_vk;
    cfg.fence = no_fence;
    unsetenv("OGPU_GFX_ROUTE");
    unsetenv("OGPU_VK_ROUTE");
    V = start(&cfg, OGPU_VK_ROUTE_GPU, err, sizeof err);
    if (!V) {
        int none = !strcmp(err, "vkCreateInstance (VkResult -9)") || !strcmp(err, "no Vulkan device (VkResult 0)")
                   || !strncmp(err, "no device with compute", 22);
        printf("opengpu gfx: %s (%s)\n", none ? "no device; skipped" : "FAILED to start", err);
        return none ? 0 : 1;
    }
    ogpu_vk_stats(V, &st);
    printf("opengpu gfx: on %s\n", ogpu_vk_device(V));
    CHECK(st.gfx, "the graphics pipeline was set up");
    if (st.gfx) {
        test_benches();
        test_random(rounds);
        ogpu_vk_destroy(V);
        V = start(&cfg, OGPU_VK_ROUTE_COST, err, sizeof err);
        CHECK(V != 0, "the back end by cost started (%s)", V ? "" : err);
        if (!V) return 1;
        test_route(3, 0);       /* tiny */
        test_route(4, 1);       /* fill640 */
        test_route(7, 0);       /* fill640-gouraud: no texture, so the core is quicker on the starting costs */
    }
    printf("opengpu gfx: %s\n", failures ? "FAILED" : "all tests passed");
    ogpu_vk_destroy(V);
    return failures != 0;
}
