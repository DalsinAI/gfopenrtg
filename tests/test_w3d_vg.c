/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * Warp3D.library's virgl route (library/warp3d/w3d_vg.c) against the CPU
 * core (library/opengpu/ogpu_3d.c), on the PC. Built 32-bit (W3D_HOST: its
 * addresses are ULONGs, as on the Amiga) and run as the child of
 * minigl_vg_bridge, which carries its request blocks to virglrenderer.
 *
 * Each scene (tests/scenes3d.c) is built twice into its own memory: once
 * the core draws it, once the route does and presents into the target. The
 * targets are then compared. A GPU never matches an integer core bit for
 * bit, so this reports, per scene, the worst channel difference and how
 * many pixels are more than TOL (8 of 255) away, with and without a
 * pixel's shift allowed (edges and texel steps round differently). Fog,
 * chroma key and alpha test are reported on their own, as the places
 * differences show. Scenes the route leaves to the CPU (CLUT8 targets,
 * texture formats it doesn't take, SDL's form) are counted as declined.
 *
 * The mixed scene: GPU 3D, then the CPU draws 2D into the bitmap, then 3D
 * again over it. The 2D must sit between the two in the picture.
 *   minigl_vg_bridge test_w3d_vg [rounds] [-o dir] */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../library/opengpu/ogpu_core.h"
#include "../library/opengpu/ogpu_3d.h"
#include "../include/gfopengpu/build3d.h"
#include "../library/warp3d/w3d_vg.h"
#include "../include/gfopengpu/pipe.h"
#include "scenes3d.h"

#define REG OGPU_SCENE3D_REGION
#define TOL 8

static int failures, checks;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* ---- the platform: the transport is the pipe's (library/pipe/pipe_xport_host.c: the bridge's pipe) ---- */

static void *os_alloc(void *user, ULONG bytes) { (void)user; return calloc(1, bytes ? bytes : 4); }
static void os_free(void *user, void *p) { (void)user; free(p); }
static void os_log(void *user, const char *m) { (void)user; if (getenv("W3D_LOG")) printf("  w3dvg: %s\n", m); }

/* ---- memory: [0] the core's, [1] the route's; each video RAM and Fast RAM ------------------- */

static ogpu_u8 *mem[2][2];
static struct ogpu_scene3d_env env[2];

static ogpu_u8 *map_ref(void *u, ogpu_u32 a, ogpu_u32 len)
{
    int r;
    (void)u;
    for (r = 0; r < 2; r++) {
        ogpu_u32 b = (ogpu_u32)(unsigned long)mem[0][r];
        if (a >= b && a - b <= (ogpu_u32)REG && len <= (ogpu_u32)REG - (a - b)) return mem[0][r] + (a - b);
    }
    return 0;
}

static struct ogpu_core C;
static struct ogpu3d T;

static void core_fresh(void)
{
    ogpu_core_init(&C); C.map = map_ref; C.fence = 0; C.ext = 0; C.user = 0;
    ogpu3d_init(&T, &C); C.d3 = &T;
}

static ULONG rd32(const ogpu_u8 *p) { return ((ULONG)p[0] << 24) | ((ULONG)p[1] << 16) | ((ULONG)p[2] << 8) | p[3]; }

/* ---- the stream's target, and what it uses ------------------------------------------------- */

struct shape { ULONG addr, bpr; int w, h, fmt, ok; ULONG enables_or; int fogmode_or; };

static struct shape look(const ogpu_u8 *s, long words)
{
    struct shape sh;
    ULONG slot[16][4];
    long at = 0;
    memset(&sh, 0, sizeof sh);
    memset(slot, 0, sizeof slot);
    while (at < words) {
        ULONG h = rd32(s + at * 4), op = OGPU_HDR_OP(h), n = OGPU_HDR_WORDS(h);
        const ogpu_u8 *a = s + at * 4 + 4;
        if (!n || at + (long)n > words) break;
        if (op == OGPU_OP_SURFACE && rd32(a) < 16) {
            ULONG *t = slot[rd32(a)];
            t[0] = rd32(a + 4); t[1] = rd32(a + 8); t[2] = rd32(a + 12); t[3] = rd32(a + 16);
        } else if (op == OGPU_OP_TARGET && rd32(a) < 16) {
            ULONG *t = slot[rd32(a)];
            sh.addr = t[0]; sh.bpr = t[1]; sh.w = (int)(t[2] >> 16); sh.h = (int)(t[2] & 0xFFFF); sh.fmt = (int)t[3]; sh.ok = 1;
        } else if (op == OGPU_OP_RENDER3D) {
            sh.enables_or |= rd32(a);
            sh.fogmode_or |= (int)rd32(a + 28);
        }
        at += (long)n;
    }
    return sh;
}

/* ---- comparing --------------------------------------------------------------------------------- */

static void rgb_at(const ogpu_u8 *base, ULONG bpr, int fmt, int x, int y, int *c)
{
    const ogpu_u8 *p = base + (unsigned long)y * bpr;
    if (fmt == OGPU_FMT_RGB565) {
        unsigned v = ((unsigned)p[x * 2] << 8) | p[x * 2 + 1];
        c[0] = (int)((v >> 11) & 31) * 255 / 31; c[1] = (int)((v >> 5) & 63) * 255 / 63; c[2] = (int)(v & 31) * 255 / 31;
    } else {
        c[0] = p[x * 4 + 1]; c[1] = p[x * 4 + 2]; c[2] = p[x * 4 + 3];
    }
}

struct diff { int worst; long over, over_shift, pixels; int worst_box; long box_over, boxes; double mean; };

static struct diff compare(const struct shape *sh, int region, const char *ppm_dir, const char *name)
{
    struct diff d;
    long off = (long)(sh->addr - (ULONG)(unsigned long)env[0].mem[region]);
    const ogpu_u8 *a = mem[0][region] + off, *b = mem[1][region] + off;
    int x, y;
    memset(&d, 0, sizeof d);
    for (y = 0; y < sh->h; y++)
        for (x = 0; x < sh->w; x++) {
            int ca[3], cb[3], k, w = 0, near_one = 0, dx, dy;
            rgb_at(a, sh->bpr, sh->fmt, x, y, ca);
            rgb_at(b, sh->bpr, sh->fmt, x, y, cb);
            for (k = 0; k < 3; k++) { int e = abs(ca[k] - cb[k]); if (e > w) w = e; }
            if (w > d.worst) d.worst = w;
            d.pixels++;
            if (w <= TOL) continue;
            d.over++;
            for (dy = -1; dy <= 1 && !near_one; dy++)
                for (dx = -1; dx <= 1 && !near_one; dx++) {
                    int cc[3], ok = 1;
                    if (x + dx < 0 || x + dx >= sh->w || y + dy < 0 || y + dy >= sh->h) continue;
                    rgb_at(a, sh->bpr, sh->fmt, x + dx, y + dy, cc);
                    for (k = 0; k < 3; k++) if (abs(cc[k] - cb[k]) > TOL) ok = 0;
                    if (ok) near_one = 1;
                }
            if (!near_one) d.over_shift++;
        }
    /* Aliasing aside: the mean of each 4x4 block, compared. A texture
     * squeezed below a texel a pixel with no mip levels aliases on both, and
     * a sub-texel rounding difference then flips single pixels. */
    {
        double sum = 0;
        int bx, by;
        for (by = 0; by + 4 <= sh->h; by += 4)
            for (bx = 0; bx + 4 <= sh->w; bx += 4) {
                int ta[3] = { 0, 0, 0 }, tb[3] = { 0, 0, 0 }, k, w = 0;
                for (y = by; y < by + 4; y++)
                    for (x = bx; x < bx + 4; x++) {
                        int ca[3], cb[3];
                        rgb_at(a, sh->bpr, sh->fmt, x, y, ca);
                        rgb_at(b, sh->bpr, sh->fmt, x, y, cb);
                        for (k = 0; k < 3; k++) { ta[k] += ca[k]; tb[k] += cb[k]; sum += abs(ca[k] - cb[k]); }
                    }
                for (k = 0; k < 3; k++) { int e2 = abs(ta[k] - tb[k]) / 16; if (e2 > w) w = e2; }
                if (w > d.worst_box) d.worst_box = w;
                if (w > TOL) d.box_over++;
                d.boxes++;
            }
        d.mean = d.pixels ? sum / (3.0 * (double)d.pixels) : 0;
    }
    if (ppm_dir) {
        char path[512];
        int i;
        for (i = 0; i < 2; i++) {
            FILE *f;
            snprintf(path, sizeof path, "%s/w3d-%s-%s.ppm", ppm_dir, name, i ? "virgl" : "cpu");
            f = fopen(path, "wb");
            if (!f) continue;
            fprintf(f, "P6\n%d %d\n255\n", sh->w, sh->h);
            for (y = 0; y < sh->h; y++)
                for (x = 0; x < sh->w; x++) {
                    int c[3];
                    unsigned char o[3];
                    rgb_at(i ? b : a, sh->bpr, sh->fmt, x, y, c);
                    o[0] = (unsigned char)c[0]; o[1] = (unsigned char)c[1]; o[2] = (unsigned char)c[2];
                    fwrite(o, 1, 3, f);
                }
            fclose(f);
        }
    }
    return d;
}

static int region_of(ULONG addr)
{
    int r;
    for (r = 0; r < 2; r++) {
        ULONG b = (ULONG)(unsigned long)env[0].mem[r];
        if (addr >= b && addr - b < (ULONG)REG) return r;
    }
    return -1;
}

/* ---- the groups reported ---------------------------------------------------------------------------- */

enum { G_ALL, G_FOG, G_CHROMA, G_ALPHA, G_PLAIN, G_N };
static const char *gname[G_N] = { "all", "fog", "chroma key", "alpha test", "none of those" };
struct group { long scenes, pixels, over, over_shift; int worst; long worst_seed; double worst_frac; long worst_frac_seed; };
static struct group grp[G_N];
static long declined_scenes, refused_scenes, ran_scenes;

static void add(int g, const struct diff *d, long seed)
{
    struct group *p = &grp[g];
    double frac = d->pixels ? (double)d->over_shift / (double)d->pixels : 0;
    p->scenes++; p->pixels += d->pixels; p->over += d->over; p->over_shift += d->over_shift;
    if (d->worst > p->worst) { p->worst = d->worst; p->worst_seed = seed; }
    if (frac > p->worst_frac) { p->worst_frac = frac; p->worst_frac_seed = seed; }
}

static struct w3dvg *V;

/* ---- direct calls (stage M7 part 2) ------------------------------------------------------------------ */

/* W3D_DIRECT=1: every batch reaches the route as Warp3D's draw calls now make it (w3dvg_begin, the calls,
 * w3dvg_end) instead of being read back by w3dvg_run, and the drawing area's memory is only had through the lock
 * hooks. The pictures must be the same as the reader's. The lock is counted: taken once for each load and each
 * present, given back each time, and never held when a call returns. */
static int direct, direct_shown;
static struct w3dvg_stats direct_last;      /* the last batch's counters (direct calls read them themselves) */
static int lock_held, lock_refuse;
static long lock_calls, lock_taken, unlock_calls, lock_bad, lock_sends, lock_presents;
static ULONG area_addr, area_bpr;
static ULONG replay_declined;
static struct w3dvg_stats direct_sum;

static int t_area_lock(void *user, ULONG *address, ULONG *bpr)
{
    (void)user;
    lock_calls++;
    if (lock_held) lock_bad++;
    if (lock_refuse) return 0;
    lock_held = 1;
    lock_taken++;
    *address = area_addr; *bpr = area_bpr;
    return 1;
}

static void t_area_unlock(void *user)
{
    (void)user;
    if (!lock_held) lock_bad++;
    lock_held = 0;
    unlock_calls++;
}

#define NOT_HELD() do { if (lock_held) { lock_bad++; lock_held = 0; } } while (0)

static LONG batch(struct w3dvg *v, const ogpu_u8 *s, long words, ULONG vbase, ULONG vlen, int present)
{
    static ogpu_u8 loose[1 << 21];      /* indexed draws, their vertices in the order drawn */
    ULONG slot[16][5], used = 0;
    struct w3dvg_stats st;
    long at = 0;
    LONG r;
    int shown = 0;
    if (!direct) return w3dvg_run(v, s, words, vbase, vlen, present);
    memset(slot, 0, sizeof slot);
    replay_declined = 0;
    w3dvg_begin(v, vbase, vlen);
    NOT_HELD();
    while (at < words) {
        ULONG h = rd32(s + at * 4), op = OGPU_HDR_OP(h), n = OGPU_HDR_WORDS(h);
        const ogpu_u8 *a = s + at * 4 + 4;
        long args = (long)n - 1;
        if (!n || at + (long)n > words) break;
        switch (op) {
        case OGPU_OP_NOP: case OGPU_OP_FENCE: break;
        case OGPU_OP_SURFACE:
            if (rd32(a) < 16) {
                ULONG *t = slot[rd32(a)];
                t[0] = rd32(a + 4); t[1] = rd32(a + 8); t[2] = rd32(a + 12) >> 16; t[3] = rd32(a + 12) & 0xFFFF; t[4] = rd32(a + 16);
            }
            break;
        case OGPU_OP_TARGET:
            if (rd32(a) < 16) {
                const ULONG *t = slot[rd32(a)];
                area_addr = t[0]; area_bpr = t[1];
                w3dvg_area(v, t[0], 0, (LONG)t[2], (LONG)t[3], t[4]);   /* the address names the area; its memory is the hook's */
            } else replay_declined++;
            break;
        case OGPU_OP_CLIP: {
            ULONG xy = rd32(a), wh = rd32(a + 4);
            w3dvg_clip(v, (WORD)(xy >> 16), (WORD)(xy & 0xFFFF), (LONG)(wh >> 16), (LONG)(wh & 0xFFFF));
            break;
        }
        case OGPU_OP_FILL: {
            ULONG xy = rd32(a), wh = rd32(a + 4);
            w3dvg_fill(v, (WORD)(xy >> 16), (WORD)(xy & 0xFFFF), (LONG)(wh >> 16), (LONG)(wh & 0xFFFF), rd32(a + 8));
            break;
        }
        case OGPU_OP_RENDER3D: {
            ULONG r3[OGPU_R3D_ARGS];
            int i;
            if (args < OGPU_R3D_ARGS) { replay_declined++; break; }
            for (i = 0; i < OGPU_R3D_ARGS; i++) r3[i] = rd32(a + i * 4);
            w3dvg_render3d(v, r3);
            break;
        }
        case OGPU_OP_TEXENV:
            if (rd32(a) == 0) w3dvg_texenv(v, rd32(a + 4), rd32(a + 8));
            else replay_declined++;
            break;
        case OGPU_OP_TEXTURE: {
            ULONG lv[OGPU_TEX_LEVELS], levels = rd32(a + 40), i;
            if (!levels) levels = 1;
            if (rd32(a) || levels > OGPU_TEX_LEVELS || (long)(10 + levels) > args) {
                if (!rd32(a)) w3dvg_texture(v, 0, 0, 1, 1, OGPU_FMT_ARGB32, 0, 1, 1, 1, 1, 0, 1, 0);
                replay_declined++;
                break;
            }
            for (i = 1; i < levels; i++) lv[i - 1] = rd32(a + 44 + (i - 1) * 4);
            w3dvg_texture(v, rd32(a + 4), rd32(a + 8), (LONG)(rd32(a + 12) >> 16), (LONG)(rd32(a + 12) & 0xFFFF), rd32(a + 16),
                          rd32(a + 24), rd32(a + 28) & 255, rd32(a + 28) >> 8, rd32(a + 32) & 255, rd32(a + 32) >> 8,
                          rd32(a + 36), levels, lv);
            break;
        }
        case OGPU_OP_DEPTH: {
            ULONG wh = rd32(a + 12);
            w3dvg_depth(v, rd32(a), rd32(a + 4), rd32(a + 8), (LONG)(wh >> 16), (LONG)(wh & 0xFFFF), rd32(a + 16));
            break;
        }
        case OGPU_OP_CLEAR3D: {
            ULONG xy = rd32(a + 4), wh = rd32(a + 8);
            w3dvg_clear3d(v, rd32(a), (WORD)(xy >> 16), (WORD)(xy & 0xFFFF), (LONG)(wh >> 16), (LONG)(wh & 0xFFFF), rd32(a + 12));
            break;
        }
        case OGPU_OP_TRIANGLES: {
            ULONG vaddr = rd32(a), vcount = rd32(a + 4), stride = rd32(a + 8), iaddr = rd32(a + 12), icount = rd32(a + 16);
            ULONG isize = rd32(a + 20), tex = rd32(a + 24) & 0xFFFF, layout = args >= 9 ? rd32(a + 32) : 0;
            if (args < 9) { replay_declined++; break; }
            if (iaddr) {
                /* Warp3D's draw calls never index: the vertices in the order drawn, as it would send them. An index
                 * past the vertices ends the command there (the core's rule). */
                const ogpu_u8 *ip = (const ogpu_u8 *)(unsigned long)iaddr, *vp = (const ogpu_u8 *)(unsigned long)vaddr;
                ogpu_u8 *d;
                ULONG i, vi, k = 0;
                if ((isize != 1 && isize != 2 && isize != 4) || !stride || stride > 64) { replay_declined++; break; }
                used = (used + 3) & ~3UL;
                if (used + icount * stride > sizeof loose) { replay_declined++; break; }
                d = loose + used;
                for (i = 0; i < icount; i++) {
                    vi = isize == 1 ? ip[i] : isize == 2 ? ((ULONG)ip[i * 2] << 8 | ip[i * 2 + 1]) : rd32(ip + i * 4);
                    if (vi >= vcount) break;
                    memcpy(d + k * stride, vp + vi * stride, stride);
                    k++;
                }
                used += k * stride;
                if ((layout & OGPU_LAY_PRIM_MASK) == OGPU_LAY_LIST) {   /* in draws no longer than Warp3D's (64K of vertices) */
                    ULONG done = 0, most = (64UL * 1024 / stride) / 3 * 3;
                    while (done < k) {
                        ULONG part = k - done > most ? most : k - done;
                        w3dvg_triangles(v, (ULONG)(unsigned long)(d + done * stride), part, stride, tex, layout);
                        done += part;
                    }
                } else
                    w3dvg_triangles(v, (ULONG)(unsigned long)d, k, stride, tex, layout);
            } else
                w3dvg_triangles(v, vaddr, vcount, stride, tex, layout);
            break;
        }
        default: replay_declined++;
        }
        NOT_HELD();
        at += (long)n;
    }
    r = w3dvg_end(v, present, &shown);
    NOT_HELD();
    /* the sends that touched the drawing area in this batch: one lock each */
    w3dvg_stats(v, &st);
    lock_sends += (long)(st.loads + st.presents);       /* (loads counts Z loads too, which take no lock) */
    lock_presents += (long)st.presents;
    direct_last = st;
    direct_shown = shown;
    {
        ULONG *sum = (ULONG *)&direct_sum, *one = (ULONG *)&st;
        unsigned i;
        for (i = 0; i < sizeof st / sizeof(ULONG); i++) sum[i] += one[i];
    }
    return r + (LONG)replay_declined;
}

/* The counters since the last call; after direct batches, the ones batch() already read (added up). */
static void stats_of(struct w3dvg_stats *st)
{
    if (!direct) { w3dvg_stats(V, st); return; }
    *st = direct_sum;
    memset(&direct_sum, 0, sizeof direct_sum);
}

/* One stream on both; the route presents at its end. Returns the route's
 * declined + refused count (0: drawn). */
static LONG run_both(const ogpu_u8 *s_ref, long w_ref, const ogpu_u8 *s_vg, long w_vg)
{
    struct w3dvg_stats st;
    LONG r;
    if (getenv("W3D_LIST")) {           /* command by command, each one's result */
        long at = 0;
        while (at < w_ref) {
            ULONG n = OGPU_HDR_WORDS(rd32(s_ref + at * 4));
            if (!n) break;
            C.last_error = 0;
            ogpu3d_run(&T, s_ref + at * 4, (long)n);
            if (C.last_error) printf("  core: word %04lx: error %d\n", (unsigned long)at, (int)C.last_error);
            at += (long)n;
        }
    } else ogpu3d_run(&T, s_ref, w_ref);
    r = batch(V, s_vg, w_vg, (ULONG)(unsigned long)mem[1][1], 256 * 1024, 1);
    stats_of(&st);
    if (st.declined || replay_declined) declined_scenes++;
    else if (st.refused || r) refused_scenes++;
    return r;
}

static ogpu_u8 sref[1 << 16];

static void random_scene(unsigned long seed)
{
    long w0, w1;
    const ogpu_u8 *s;
    struct shape sh;
    struct diff d;
    LONG r;
    int region;
    memset(mem[0][0], 0, REG); memset(mem[0][1], 0, REG); memset(mem[1][0], 0, REG); memset(mem[1][1], 0, REG);
    s = ogpu_scene3d_random(&env[0], seed, &w0);
    memcpy(sref, s, (size_t)w0 * 4);
    s = ogpu_scene3d_random(&env[1], seed, &w1);
    sh = look(sref, w0);
    if (getenv("W3D_LIST")) {           /* the stream, one command a line */
        long at = 0;
        while (at < w0) {
            ULONG h = rd32(sref + at * 4), n = OGPU_HDR_WORDS(h), j;
            printf("  %04lx op %02lx:", (unsigned long)at, (unsigned long)OGPU_HDR_OP(h));
            for (j = 1; j < n && j < 24; j++) printf(" %08lx", (unsigned long)rd32(sref + (at + (long)j) * 4));
            printf("\n");
            if (!n) break;
            at += (long)n;
        }
    }
    core_fresh();
    w3dvg_stale(V);
    w3dvg_forget_all(V);
    w3dvg_reset(V);
    r = run_both(sref, w0, s, w1);
    if (r || !sh.ok || (sh.fmt != OGPU_FMT_ARGB32 && sh.fmt != OGPU_FMT_RGB565) || (region = region_of(sh.addr)) < 0) return;
    ran_scenes++;
    {
        char nm[32];
        snprintf(nm, sizeof nm, "seed%lu", seed);
        d = compare(&sh, region, getenv("W3D_SEED_PPM"), nm);
    }
    add(G_ALL, &d, (long)seed);
    if ((sh.enables_or & OGPU_R3D_FOG) && sh.fogmode_or) add(G_FOG, &d, (long)seed);
    if (sh.enables_or & OGPU_R3D_CHROMA) add(G_CHROMA, &d, (long)seed);
    if (sh.enables_or & OGPU_R3D_ALPHATEST) add(G_ALPHA, &d, (long)seed);
    if (!(sh.enables_or & (OGPU_R3D_FOG | OGPU_R3D_CHROMA | OGPU_R3D_ALPHATEST))) add(G_PLAIN, &d, (long)seed);
    if (getenv("W3D_SEEDS") && C.last_error) printf("  seed %lu: the core stopped with error %d at word %ld\n", seed, (int)C.last_error, (long)C.error_word);
    if (getenv("W3D_SEEDS")) printf("  seed %lu: worst %d, %ld over %d (%ld with a shift) of %ld\n", seed, d.worst, d.over, TOL, d.over_shift, d.pixels);
}

static void bench_scene(int which, const char *ppm_dir)
{
    long w0, w1, off;
    const ogpu_u8 *s;
    const char *name;
    struct shape sh;
    struct diff d;
    struct w3dvg_stats st;
    LONG r;
    int region, w, h, fmt;
    char nm[64];
    memset(mem[0][0], 0, REG); memset(mem[0][1], 0, REG); memset(mem[1][0], 0, REG); memset(mem[1][1], 0, REG);
    s = ogpu_scene3d_bench(&env[0], which, &w0, &name);
    memcpy(sref, s, (size_t)w0 * 4);
    s = ogpu_scene3d_bench(&env[1], which, &w1, &name);
    ogpu_scene3d_bench_target(which, &region, &off, &w, &h, &fmt);
    if (getenv("W3D_UVTEX")) {          /* texels that name themselves, nearest, REPLACE, no fog */
        int side, q, at;
        for (side = 0; side < 2; side++) {
            ogpu_u8 *t = mem[side][1] + 0x100000L, *st = side ? (ogpu_u8 *)s : sref;
            long ww = side ? w1 : w0;
            for (q = 0; q < 256 * 256; q++) { t[q * 4] = 255; t[q * 4 + 1] = (ogpu_u8)(q & 255); t[q * 4 + 2] = (ogpu_u8)(q >> 8); t[q * 4 + 3] = 0; }
            for (at = 0; at < ww; ) {
                ULONG hh = rd32(st + at * 4), op = OGPU_HDR_OP(hh), n = OGPU_HDR_WORDS(hh);
                ogpu_u8 *a = st + at * 4 + 4;
                if (!n) break;
                if (op == OGPU_OP_TEXTURE) { a[28] = 0; a[29] = 0; a[30] = 1; a[31] = 1; }
                if (op == OGPU_OP_TEXENV) { a[4] = a[5] = a[6] = 0; a[7] = 1; }
                if (op == OGPU_OP_RENDER3D) a[3] &= (ogpu_u8)~OGPU_R3D_FOG;
                at += (long)n;
            }
        }
    }
    if (getenv("W3D_DUMP")) {
        int q;
        const ogpu_u8 *vb = mem[0][1] + 0x180000L;
        for (q = 0; q < 6; q++) {
            const ogpu_u8 *p = vb + q * 32;
            union { ULONG u; float f; } wf;
            wf.u = rd32(p + 24);
            printf("v%d x %.2f y %.2f c %08lx u %.2f v %.2f z %.4f w %.4f s %08lx\n", q, (LONG)rd32(p) / 65536.0, (LONG)rd32(p + 4) / 65536.0,
                   (unsigned long)rd32(p + 8), (LONG)rd32(p + 12) / 65536.0, (LONG)rd32(p + 16) / 65536.0, rd32(p + 20) / 4294967296.0, wf.f, (unsigned long)rd32(p + 28));
        }
    }
    sh = look(sref, w0);
    core_fresh();
    w3dvg_stale(V);
    w3dvg_forget_all(V);
    w3dvg_reset(V);
    ogpu3d_run(&T, sref, w0);
    r = batch(V, s, w1, (ULONG)(unsigned long)mem[1][1], 256 * 1024, 1);
    stats_of(&st);
    if (r || st.declined) { printf("w3d virgl bench %-28s declined (%ld) or refused (%ld)\n", name, (long)st.declined, (long)st.refused); return; }
    sh.addr = (ULONG)(unsigned long)env[0].mem[region] + (ULONG)off;
    if (!sh.bpr) sh.bpr = (ULONG)w * (fmt == OGPU_FMT_RGB565 ? 2 : 4);
    sh.w = w; sh.h = h; sh.fmt = fmt;
    snprintf(nm, sizeof nm, "bench%d", which);
    d = compare(&sh, region, ppm_dir, nm);
    printf("w3d virgl bench %-16s %dx%d %s: pixels worst %3d, mean %.2f, %.2f%% over %d (%.2f%% with a shift); 4x4 blocks worst %3d, %.2f%% over %d; %ld draws for %ld triangles\n",
           name, w, h, fmt == OGPU_FMT_RGB565 ? "RGB565" : "ARGB32", d.worst, d.mean, 100.0 * (double)d.over / (double)d.pixels, TOL,
           100.0 * (double)d.over_shift / (double)d.pixels, d.worst_box, 100.0 * (double)d.box_over / (double)d.boxes, TOL,
           (long)st.draws, (long)st.triangles);
    /* reported, not passed or failed (texture aliasing and the core's own
     * perspective steps differ from a GPU's); untextured scenes must match */
    if (!(sh.enables_or & OGPU_R3D_TEXTURE))
        CHECK(d.worst <= 2, "bench %s (untextured): worst channel %d", name, d.worst);
}

/* ---- the mixed scene: GPU 3D, CPU 2D into the bitmap, GPU 3D again -------------------------------- */

#define MX_W 320
#define MX_H 240
static int mx565;                               /* the mixed scene on an RGB565 bitmap (BlitzQuake's and W3DTest's 16 bits) */

static ULONG mx_build(ogpu_u8 *buf, int side, int pass)
{
    struct OGPUBatch b;
    ULONG tgt = (ULONG)(unsigned long)mem[side][0], vtx = (ULONG)(unsigned long)mem[side][1] + 0x1000UL * (ULONG)(pass + 1);
    ogpu_u8 *v = mem[side][1] + 0x1000 * (pass + 1);
    unsigned long r3d[OGPU_R3D_ARGS];
    static const int tri[2][3][2] = { { { 20, 20 }, { 300, 40 }, { 60, 220 } }, { { 140, 10 }, { 310, 230 }, { 100, 200 } } };
    static const ULONG col[2][3] = { { 0xFFFF0000UL, 0xFF00FF00UL, 0xFF0000FFUL }, { 0x80FFFFFFUL, 0x80FFFF00UL, 0x8000FFFFUL } };
    int i;
    ogpu_batch_init(&b, buf, 4096);
    ogpu_surface(&b, 0, tgt, MX_W * (mx565 ? 2 : 4), MX_W, MX_H, mx565 ? OGPU_FMT_RGB565 : OGPU_FMT_ARGB32);
    ogpu_target(&b, 0);
    if (!pass) ogpu_fill(&b, 0, 0, MX_W, MX_H, mx565 ? 0x2188UL : 0xFF202040UL);
    memset(r3d, 0, sizeof r3d);
    r3d[0] = OGPU_R3D_GOURAUD | (pass ? OGPU_R3D_BLEND : 0);
    r3d[1] = 8; r3d[2] = 8; r3d[3] = pass ? (7 | 8UL << 16) : (2 | 1UL << 16);  /* W3D_SRC_ALPHA, W3D_ONE_MINUS_SRC_ALPHA on the second */
    r3d[5] = 0xF; r3d[6] = 4; r3d[14] = 1; r3d[17] = 0xFFFFFFFFUL;
    r3d[12] = 2 | 0xFFUL << 16; r3d[13] = 1 | 1UL << 8 | 1UL << 16 | 0xFFUL << 24;
    ogpu_render3d(&b, r3d);
    for (i = 0; i < 3; i++) {
        ogpu_u8 *p = v + i * 20;
        ULONG x = (ULONG)tri[pass][i][0] << 16, y = (ULONG)tri[pass][i][1] << 16, c = col[pass][i];
        ULONG w4[5] = { x, y, c, 0, 0 };
        int k;
        for (k = 0; k < 5; k++) { p[k * 4] = (ogpu_u8)(w4[k] >> 24); p[k * 4 + 1] = (ogpu_u8)(w4[k] >> 16); p[k * 4 + 2] = (ogpu_u8)(w4[k] >> 8); p[k * 4 + 3] = (ogpu_u8)w4[k]; }
    }
    ogpu_triangles3d(&b, vtx, 3, 20, 0, 0, 2, OGPU_TRI_NOTEX, 0, OGPU_LAY_LIST);
    return (ULONG)b.words;
}

static void mx_cpu_2d(int side)
{
    /* what a program's own RectFill/WritePixelArray does between batches: straight into the bitmap */
    int x, y;
    for (y = 60; y < 180; y++)
        for (x = 100; x < 220; x++) {
            int on = ((x / 8) ^ (y / 8)) & 1;
            if (mx565) {
                ogpu_u8 *p = mem[side][0] + (y * MX_W + x) * 2;
                p[0] = on ? 0xFE : 0x00; p[1] = 0x00;           /* big-endian RGB565 */
            } else {
                ogpu_u8 *p = mem[side][0] + (y * MX_W + x) * 4;
                p[0] = 0xFF; p[1] = on ? 0xFF : 0x00; p[2] = on ? 0xC0 : 0x00; p[3] = 0x00;
            }
        }
}

static void mixed_scene(const char *ppm_dir)
{
    static ogpu_u8 b0[4096 * 4], b1[4096 * 4];
    struct shape sh;
    struct diff d;
    struct w3dvg_stats st;
    ULONG n0, n1;
    LONG r = 0;
    int pass, side;
    memset(mem[0][0], 0, REG); memset(mem[0][1], 0, REG); memset(mem[1][0], 0, REG); memset(mem[1][1], 0, REG);
    core_fresh();
    w3dvg_stale(V);
    w3dvg_forget_all(V);
    w3dvg_reset(V);
    for (pass = 0; pass < 2; pass++) {
        n0 = mx_build(b0, 0, pass);
        n1 = mx_build(b1, 1, pass);
        ogpu3d_run(&T, b0, (long)n0);
        r |= batch(V, b1, (long)n1, (ULONG)(unsigned long)mem[1][1], 256 * 1024, 1);
        if (!pass) {
            for (side = 0; side < 2; side++) mx_cpu_2d(side);
            w3dvg_stale(V);                 /* UnLockHardware, the CPU drew, LockHardware again */
        }
    }
    stats_of(&st);
    memset(&sh, 0, sizeof sh);
    sh.addr = (ULONG)(unsigned long)env[0].mem[0]; sh.bpr = MX_W * (mx565 ? 2 : 4); sh.w = MX_W; sh.h = MX_H;
    sh.fmt = mx565 ? OGPU_FMT_RGB565 : OGPU_FMT_ARGB32;
    d = compare(&sh, 0, ppm_dir, mx565 ? "mixed565" : "mixed");
    printf("w3d virgl mixed 2D/3D scene%s:", mx565 ? " (RGB565)" : "");
    printf(" worst %d, %ld of %ld over %d, %ld with a pixel's shift; %ld loads, %ld presents, %ld declined, %ld refused\n",
           d.worst, d.over, d.pixels, TOL, d.over_shift, (long)st.loads, (long)st.presents, (long)st.declined, (long)st.refused);
    CHECK(!r && !st.declined, "mixed scene ran on the route");
    CHECK(st.loads >= 1, "the CPU's 2D was loaded before the second 3D batch (%ld loads)", (long)st.loads);
    CHECK(d.over_shift * 1000 <= d.pixels, "mixed scene matches the core (%ld off)", d.over_shift);
}

/* ---- the hardware lock: batches that keep the frame on the host, then a sync with nothing new -- */

static void hold_scene(const char *ppm_dir)
{
    static ogpu_u8 b0[4096 * 4], b1[4096 * 4], pre[64];
    struct OGPUBatch b;
    struct shape sh;
    struct diff d;
    struct w3dvg_stats st;
    LONG r = 0;
    int pass;
    memset(mem[0][0], 0, REG); memset(mem[0][1], 0, REG); memset(mem[1][0], 0, REG); memset(mem[1][1], 0, REG);
    core_fresh();
    w3dvg_stale(V);
    w3dvg_forget_all(V);
    w3dvg_reset(V);
    for (pass = 0; pass < 2; pass++) {
        ULONG n0 = mx_build(b0, 0, pass), n1 = mx_build(b1, 1, pass);
        ogpu3d_run(&T, b0, (long)n0);
        r |= batch(V, b1, (long)n1, (ULONG)(unsigned long)mem[1][1], 256 * 1024, 0);    /* full batches under the lock */
    }
    /* W3D_UnLockHardware with the batch empty: SURFACE and TARGET only, and the present */
    ogpu_batch_init(&b, pre, 16);
    ogpu_surface(&b, 0, (ULONG)(unsigned long)mem[1][0], MX_W * 4, MX_W, MX_H, OGPU_FMT_ARGB32);
    ogpu_target(&b, 0);
    r |= batch(V, pre, b.words, 0, 0, 1);
    stats_of(&st);
    memset(&sh, 0, sizeof sh);
    sh.addr = (ULONG)(unsigned long)env[0].mem[0]; sh.bpr = MX_W * 4; sh.w = MX_W; sh.h = MX_H; sh.fmt = OGPU_FMT_ARGB32;
    d = compare(&sh, 0, ppm_dir, "hold");
    printf("w3d virgl held frame: worst %d, %ld with a pixel's shift; %ld loads, %ld presents\n",
           d.worst, d.over_shift, (long)st.loads, (long)st.presents);
    CHECK(!r && st.presents == 1 && st.loads == 0, "held frame: one present, no loads (%ld, %ld)", (long)st.presents, (long)st.loads);
    CHECK(d.over_shift * 1000 <= d.pixels, "held frame matches the core (%ld off)", d.over_shift);
}

/* ---- Z and stencil back to memory (W3D_ReadZSpan ...), and written by the CPU ------------------ */

#define ZS_Z   0x80000
#define ZS_S   0x100000

static ULONG zs_build(ogpu_u8 *buf, int side, int pass, int zfmt, int stencil)
{
    struct OGPUBatch b;
    ogpu_u8 *fast = mem[side][1];
    ULONG base = (ULONG)(unsigned long)fast, vtx = base + 0x1000UL * (ULONG)(pass + 1);
    ogpu_u8 *v = fast + 0x1000 * (pass + 1);
    unsigned long r3d[OGPU_R3D_ARGS];
    static const int tri[2][3][2] = { { { 10, 10 }, { 310, 30 }, { 40, 230 } }, { { 150, 5 }, { 315, 235 }, { 60, 190 } } };
    static const ULONG zz[2][3] = { { 0x20000000UL, 0xE0000000UL, 0x80000000UL }, { 0xF0000000UL, 0x10000000UL, 0x60000000UL } };
    int i;
    ogpu_batch_init(&b, buf, 4096);
    ogpu_surface(&b, 0, (ULONG)(unsigned long)mem[side][0], MX_W * 4, MX_W, MX_H, OGPU_FMT_ARGB32);
    ogpu_target(&b, 0);
    ogpu_depth(&b, OGPU_KIND_DEPTH, base + ZS_Z, MX_W * (zfmt == OGPU_FMT_Z16 ? 2 : 4), MX_W, MX_H, zfmt);
    if (stencil) ogpu_depth(&b, OGPU_KIND_STENCIL, base + ZS_S, MX_W, MX_W, MX_H, OGPU_FMT_S8);
    memset(r3d, 0, sizeof r3d);
    r3d[0] = OGPU_R3D_GOURAUD | OGPU_R3D_ZTEST | OGPU_R3D_ZWRITE | (stencil ? OGPU_R3D_STENCIL : 0);
    r3d[1] = 2;                         /* W3D_Z_LESS */
    r3d[2] = 8; r3d[3] = 2 | 1UL << 16;
    r3d[5] = 0xF; r3d[6] = 4; r3d[14] = 1; r3d[17] = 0xFFFFFFFFUL;
    r3d[12] = 8 | (ULONG)(0x40 + pass * 0x31) << 8 | 0xFFUL << 16;   /* always, the ref */
    r3d[13] = 1 | 1UL << 8 | 3UL << 16 | 0xFFUL << 24;               /* keep, keep, replace */
    ogpu_render3d(&b, r3d);
    for (i = 0; i < 3; i++) {
        ogpu_u8 *p = v + i * 24;
        ULONG w6[6] = { (ULONG)tri[pass][i][0] << 16, (ULONG)tri[pass][i][1] << 16, 0xFF000000UL | (0x55UL << (8 * i)) | (pass ? 0x808080UL : 0), 0, 0, zz[pass][i] };
        int k;
        for (k = 0; k < 6; k++) { p[k * 4] = (ogpu_u8)(w6[k] >> 24); p[k * 4 + 1] = (ogpu_u8)(w6[k] >> 16); p[k * 4 + 2] = (ogpu_u8)(w6[k] >> 8); p[k * 4 + 3] = (ogpu_u8)w6[k]; }
    }
    ogpu_triangles3d(&b, vtx, 3, 24, 0, 0, 2, OGPU_TRI_NOTEX, 0, OGPU_LAY_LIST | OGPU_LAY_Z);
    return (ULONG)b.words;
}

/* What W3D_WriteZSpan and a program's own stencil writes do: straight into memory. */
static void zs_cpu_write(int side, int zfmt, int stencil)
{
    int x, y;
    for (y = 100; y < 140; y++)
        for (x = 0; x < MX_W; x++) {
            ogpu_u8 *z = mem[side][1] + ZS_Z + (y * MX_W + x) * (zfmt == OGPU_FMT_Z16 ? 2 : 4);
            z[0] = 0x05; z[1] = 0x00;
            if (zfmt != OGPU_FMT_Z16) { z[2] = 0; z[3] = 0; }
            if (stencil) mem[side][1][ZS_S + y * MX_W + x] = 0xEE;
        }
}

/* The worst difference in the Z buffer's top 16 bits, and stencil bytes that differ. */
static void zs_compare(int zfmt, int stencil, int *zworst, long *sdiff)
{
    int x, y, bpp = zfmt == OGPU_FMT_Z16 ? 2 : 4;
    *zworst = 0; *sdiff = 0;
    for (y = 0; y < MX_H; y++)
        for (x = 0; x < MX_W; x++) {
            const ogpu_u8 *a = mem[0][1] + ZS_Z + (y * MX_W + x) * bpp, *b = mem[1][1] + ZS_Z + (y * MX_W + x) * bpp;
            int d = ((int)a[0] << 8 | a[1]) - ((int)b[0] << 8 | b[1]);
            if (d < 0) d = -d;
            if (d > *zworst) *zworst = d;
            if (stencil && mem[0][1][ZS_S + y * MX_W + x] != mem[1][1][ZS_S + y * MX_W + x]) (*sdiff)++;
        }
}

static void z_scene(int zfmt, int stencil, const char *ppm_dir)
{
    static ogpu_u8 b0[4096 * 4], b1[4096 * 4];
    struct shape sh;
    struct diff d;
    struct w3dvg_stats st;
    LONG r = 0;
    int pass, side, zworst[2];
    long sdiff[2];
    const char *name = zfmt == OGPU_FMT_Z16 ? (stencil ? "z16+stencil" : "z16") : (stencil ? "z32+stencil" : "z32");
    char tag[32];
    for (side = 0; side < 2; side++) {
        memset(mem[side][0], 0, REG); memset(mem[side][1], 0, REG);
        /* a cleared Z buffer (W3D_ClearZBuffer(1.0) on the CPU) */
        memset(mem[side][1] + ZS_Z, 0xFF, MX_W * MX_H * 4);
    }
    core_fresh();
    w3dvg_stale(V);
    w3dvg_forget_all(V);
    w3dvg_reset(V);
    for (pass = 0; pass < 2; pass++) {
        ULONG n0 = zs_build(b0, 0, pass, zfmt, stencil), n1 = zs_build(b1, 1, pass, zfmt, stencil);
        ogpu3d_run(&T, b0, (long)n0);
        r |= batch(V, b1, (long)n1, (ULONG)(unsigned long)mem[1][1], 256 * 1024, 1);
        w3dvg_read_z(V);                /* W3D_ReadZSpan after the batch */
        zs_compare(zfmt, stencil, &zworst[pass], &sdiff[pass]);
        if (!pass) {
            for (side = 0; side < 2; side++) zs_cpu_write(side, zfmt, stencil);
            w3dvg_stale_z(V);           /* W3D_WriteZSpan */
        }
    }
    stats_of(&st);
    memset(&sh, 0, sizeof sh);
    sh.addr = (ULONG)(unsigned long)env[0].mem[0]; sh.bpr = MX_W * 4; sh.w = MX_W; sh.h = MX_H; sh.fmt = OGPU_FMT_ARGB32;
    snprintf(tag, sizeof tag, "zread-%s", name);
    d = compare(&sh, 0, ppm_dir, tag);
    printf("w3d virgl Z read %-12s: Z worst %d/%d (top 16 bits), stencil %ld/%ld off; colour worst %d, %ld with a shift; %ld refused\n",
           name, zworst[0], zworst[1], sdiff[0], sdiff[1], d.worst, d.over_shift, (long)st.refused);
    CHECK(!r && !st.refused, "Z scene %s ran on the route", name);
    CHECK(zworst[0] <= 2 && zworst[1] <= 2, "Z %s back in memory matches the core (%d, %d)", name, zworst[0], zworst[1]);
    CHECK(!sdiff[0] && !sdiff[1], "stencil %s back in memory matches the core (%ld, %ld)", name, sdiff[0], sdiff[1]);
    CHECK(d.over_shift * 1000 <= d.pixels, "Z scene %s colour matches the core (%ld off)", name, d.over_shift);
}

/* Direct calls with the drawing area not to be had (the bitmap isn't visible): nothing is loaded, so the draws over
 * the old frame are left out; a frame that starts by filling the area is drawn, and stays on the host until the area
 * can be had, when one present brings it. */
static void lock_scene(void)
{
    static ogpu_u8 b1[4096 * 4], pre[64];
    struct OGPUBatch b;
    struct shape sh;
    struct diff d;
    ULONG n1;
    long taken = lock_taken, asked = lock_calls;
    LONG r;
    memset(mem[0][0], 0, REG); memset(mem[0][1], 0, REG); memset(mem[1][0], 0, REG); memset(mem[1][1], 0, REG);
    core_fresh();
    w3dvg_stale(V);
    w3dvg_forget_all(V);
    w3dvg_reset(V);
    lock_refuse = 1;
    /* a batch over the old frame: the load is refused */
    n1 = mx_build(b1, 1, 1);
    r = batch(V, b1, (long)n1, (ULONG)(unsigned long)mem[1][1], 256 * 1024, 1);
    CHECK(r > 0, "draws over a frame that can't be loaded are left out (%ld)", (long)r);
    CHECK(!direct_last.loads && !direct_last.presents, "nothing loaded or presented without the area (%ld, %ld)",
          (long)direct_last.loads, (long)direct_last.presents);
    /* the frame from its start (it fills the area): drawn on the host, not shown */
    {
        static ogpu_u8 b0[4096 * 4];
        ULONG n0 = mx_build(b0, 0, 0);
        ogpu3d_run(&T, b0, (long)n0);
        n0 = mx_build(b0, 0, 1);
        ogpu3d_run(&T, b0, (long)n0);
    }
    n1 = mx_build(b1, 1, 0);
    r = batch(V, b1, (long)n1, (ULONG)(unsigned long)mem[1][1], 256 * 1024, 1);
    CHECK(!r && !direct_shown && !direct_last.presents, "a frame drawn with the area not to be had stays on the host (%ld, %d, %ld)",
          (long)r, direct_shown, (long)direct_last.presents);
    n1 = mx_build(b1, 1, 1);
    r = batch(V, b1, (long)n1, (ULONG)(unsigned long)mem[1][1], 256 * 1024, 1);
    CHECK(!r && !direct_shown, "and so does the next batch of it (%ld, %d)", (long)r, direct_shown);
    CHECK(lock_taken == taken && lock_calls > asked, "the area was asked for and never had (%ld asked)", lock_calls - asked);
    /* visible again: an empty batch presents what the host holds */
    lock_refuse = 0;
    ogpu_batch_init(&b, pre, 16);
    ogpu_surface(&b, 0, (ULONG)(unsigned long)mem[1][0], MX_W * 4, MX_W, MX_H, mx565 ? OGPU_FMT_RGB565 : OGPU_FMT_ARGB32);
    ogpu_target(&b, 0);
    r = batch(V, pre, b.words, 0, 0, 1);
    CHECK(!r && direct_shown && direct_last.presents == 1 && !direct_last.loads, "one present brings the held frame (%ld, %d, %ld, %ld)",
          (long)r, direct_shown, (long)direct_last.presents, (long)direct_last.loads);
    CHECK(lock_taken == taken + 1 && unlock_calls == lock_taken, "that present took the lock once and gave it back (%ld, %ld)",
          lock_taken - taken, unlock_calls - lock_taken);
    /* drawing on over the frame the program now has: one lock for the load, one for the present */
    {
        static ogpu_u8 b0[4096 * 4];
        ULONG n0 = mx_build(b0, 0, 1);
        ogpu3d_run(&T, b0, (long)n0);
    }
    n1 = mx_build(b1, 1, 1);
    r = batch(V, b1, (long)n1, (ULONG)(unsigned long)mem[1][1], 256 * 1024, 1);
    CHECK(!r && direct_shown && direct_last.loads == 1 && direct_last.presents == 1 && lock_taken == taken + 3 && unlock_calls == lock_taken,
          "a batch over the shown frame: one lock to load it, one to present (%ld, %d, %ld loads, %ld presents, %ld locks)",
          (long)r, direct_shown, (long)direct_last.loads, (long)direct_last.presents, lock_taken - taken - 1);
    memset(&sh, 0, sizeof sh);
    sh.addr = (ULONG)(unsigned long)env[0].mem[0]; sh.bpr = MX_W * 4; sh.w = MX_W; sh.h = MX_H; sh.fmt = OGPU_FMT_ARGB32;
    d = compare(&sh, 0, 0, "lock");
    printf("w3d virgl frame held for the lock: worst %d, %ld with a pixel's shift\n", d.worst, d.over_shift);
    CHECK(d.over_shift == 0, "the held frame is the CPU's picture (%ld pixels over with a shift)", d.over_shift);
}

int main(int argc, char **argv)
{
    const char *e = getenv("MGL_VG_BRIDGE"), *ppm_dir = 0;
    long rounds = 300, i;
    struct w3dvg_os os;
    struct w3dvg_info info;
    int k, g;
    for (k = 1; k < argc; k++) {
        if (!strcmp(argv[k], "-o") && k + 1 < argc) ppm_dir = argv[++k];
        else rounds = atol(argv[k]);
    }
    setvbuf(stdout, 0, _IOLBF, 0);
    if (!e || !ogpu_pipe_xport_ready()) { printf("test_w3d_vg: run me under minigl_vg_bridge\n"); return 2; }
    for (k = 0; k < 2; k++) {
        mem[k][0] = aligned_alloc(4096, REG); mem[k][1] = aligned_alloc(4096, REG);
        if (!mem[k][0] || !mem[k][1]) { printf("out of memory\n"); return 1; }
        env[k].mem[0] = mem[k][0]; env[k].mem[1] = mem[k][1];
        env[k].addr[0] = (unsigned long)mem[k][0]; env[k].addr[1] = (unsigned long)mem[k][1];
    }
    memset(&os, 0, sizeof os);
    os.alloc = os_alloc; os.free = os_free; os.log = os_log;
    os.pipe_old = getenv("W3D_PIPE_OLD") && atoi(getenv("W3D_PIPE_OLD"));   /* M6: the route before the shared pipe */
    direct = !os.pipe_old && getenv("W3D_DIRECT") && atoi(getenv("W3D_DIRECT"));   /* M7 part 2: the calls, not the reader */
    if (direct) { os.area_lock = t_area_lock; os.area_unlock = t_area_unlock; }
    V = w3dvg_open(&os, &info);
    CHECK(V != 0, "the route opens on the host");
    if (!V) return 1;
    printf("w3d virgl: host flags %#lx\n", (unsigned long)info.host_flags);

    if (getenv("W3D_SEED")) { random_scene(strtoul(getenv("W3D_SEED"), 0, 0)); w3dvg_close(V); return 0; }
    if (getenv("W3D_ZONLY")) { z_scene(OGPU_FMT_Z16, 0, ppm_dir); z_scene(OGPU_FMT_Z32, 0, ppm_dir); z_scene(OGPU_FMT_Z16, 1, ppm_dir); z_scene(OGPU_FMT_Z32, 1, ppm_dir); w3dvg_close(V); printf("w3d virgl: %d checks, %d failed\n", checks, failures); return failures ? 1 : 0; }
    if (getenv("W3D_BENCH")) { bench_scene(atoi(getenv("W3D_BENCH")), ppm_dir); w3dvg_close(V); return 0; }
    mixed_scene(ppm_dir);
    mx565 = 1; mixed_scene(ppm_dir); mx565 = 0;
    hold_scene(ppm_dir);
    z_scene(OGPU_FMT_Z16, 0, ppm_dir);
    z_scene(OGPU_FMT_Z32, 0, ppm_dir);
    z_scene(OGPU_FMT_Z16, 1, ppm_dir);
    z_scene(OGPU_FMT_Z32, 1, ppm_dir);
    for (k = 0; k < OGPU_SCENE3D_BENCHES; k++) bench_scene(k, ppm_dir);
    for (i = 1; i <= rounds; i++) random_scene((unsigned long)i);
    printf("w3d virgl random scenes: %ld run, %ld declined (left to the CPU), %ld refused by the host\n", ran_scenes, declined_scenes, refused_scenes);
    CHECK(!refused_scenes, "no scene refused by the host (%ld)", refused_scenes);
    for (g = 0; g < G_N; g++) {
        struct group *p = &grp[g];
        if (!p->scenes) continue;
        printf("  %-14s %4ld scenes: worst channel %3d (seed %ld); %.3f%% of pixels over %d, %.3f%% with a shift; worst scene %.2f%% (seed %ld)\n",
               gname[g], p->scenes, p->worst, p->worst_seed, 100.0 * (double)p->over / (double)p->pixels, TOL,
               100.0 * (double)p->over_shift / (double)p->pixels, 100.0 * p->worst_frac, p->worst_frac_seed);
    }
    if (direct) {
        lock_scene();
        printf("w3d virgl direct calls: the drawing area locked %ld times (%ld presents, %ld loads with Z's), given back %ld times\n",
               lock_taken, lock_presents, lock_sends - lock_presents, unlock_calls);
        CHECK(lock_taken >= lock_presents && lock_taken <= lock_sends, "a lock for each present and each load of the area, no more (%ld for %ld to %ld)",
              lock_taken, lock_presents, lock_sends);
        CHECK(unlock_calls == lock_taken, "every lock given back (%ld of %ld)", unlock_calls, lock_taken);
        CHECK(!lock_bad, "the lock never held when a call returns, never taken twice (%ld times)", lock_bad);
    }
    w3dvg_close(V);
    printf("w3d virgl: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
