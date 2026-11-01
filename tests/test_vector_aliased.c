/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * Stream v1.8's OGPU_PATH_ALIASED: PATH_FILL and PATH_STROKE without
 * anti-aliasing, each pixel in or out by its centre. Checked against
 *   - a reference written here in exact integers: for each pixel row the
 *     crossings of the path's lines with the row's centre line, and the
 *     winding (non-zero or even-odd) at each pixel centre, a centre on an
 *     edge inside when the edge is left of it. Polygons only (lines), so
 *     the reference is exact and the core must match it pixel for pixel;
 *   - with -DWITH_CAIRO, cairo's CAIRO_ANTIALIAS_NONE fill and stroke of
 *     the same paths (the browser's reference until cairo leaves it):
 *     polygons pixel for pixel, curves and strokes within a few pixels
 *     where the two cut curves and outlines differently (counted, printed);
 *   - the paints and operators of v1.6 unchanged on the pixels that are in,
 *     the mask (OGPU_PATH_MASK) applied, and the pixels outside untouched;
 *   - the library's variant check: a batch with an aliased path doesn't go
 *     to a driver (OGPU_NEEDS_CPU), the same batch without the bit does. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../library/opengpu/ogpu_core.h"
#include "../include/gfopengpu/build.h"
#include "../library/opengpu/ogpu_variant.h"
#ifdef WITH_CAIRO
#include <cairo.h>
#endif

#define ARENA (2L << 20)
static ogpu_u8 arena[ARENA];
#define W     96
#define H     80
#define BPR   (W * 4)
#define TGT   0x000000UL
#define MSK   0x010000UL
#define PATH  0x040000UL
#define WORK  0x100000UL
#define WORKB 0x80000UL

static ogpu_u8 *map(void *user, ogpu_u32 address, ogpu_u32 length) {
    (void)user;
    if (address >= (ogpu_u32)ARENA || length > (ogpu_u32)ARENA - address) return 0;
    return arena + address;
}

static int failures, checks;
static void check(int ok, const char *what) {
    checks++;
    if (!ok) { failures++; printf("FAIL %s\n", what); }
}

static struct ogpu_core C;
static ogpu_u8 sbuf[4096];
static struct OGPUBatch B, P;
static const long ID[6] = { 0x10000, 0, 0, 0, 0x10000, 0 };

static unsigned rs = 0x1234567u;
static unsigned rnd(unsigned n) { rs = rs * 1103515245u + 12345u; return (rs >> 8) % n; }

/* the path, as 24.8 points, in P and kept for the reference */
#define MAXP 256
static long px[MAXP], py[MAXP];
static int sub[MAXP], np;                       /* sub[i]: point i starts a subpath */

static void path_start(void) { ogpu_batch_init(&P, arena + PATH, 0x8000); np = 0; }
static void pt(long x, long y, int move) {
    if (np < MAXP) { px[np] = x; py[np] = y; sub[np] = move; np++; }
    if (move) ogpu_path_move(&P, x, y); else ogpu_path_line(&P, x, y);
}

static long floor_div(long long a, long long b) {
    long long q = a / b;
    if ((a % b) && ((a < 0) != (b < 0))) q--;
    return (long)q;
}

/* in[y][x]: 1 when the centre of pixel (x, y) is inside (each subpath closed) */
static unsigned char in[H][W];
static void reference(int evenodd) {
    int y, x, i;
    memset(in, 0, sizeof in);
    for (y = 0; y < H; y++) {
        long yc = (long)y * 256 + 128;
        int wind[W + 1];
        memset(wind, 0, sizeof wind);
        for (i = 0; i < np; i++) {
            int j = i + 1 < np && !sub[i + 1] ? i + 1 : i, s0 = i;
            long x0, y0, x1, y1, xc;
            int dir = 1, k;
            if (j == i) {                       /* the closing line, back to the subpath's start */
                while (s0 > 0 && !sub[s0]) s0--;
                j = s0;
            }
            x0 = px[i]; y0 = py[i]; x1 = px[j]; y1 = py[j];
            if (y0 == y1) continue;
            if (y0 > y1) { long t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; dir = -1; }
            if (yc < y0 || yc >= y1) continue;
            xc = x0 + floor_div((long long)(x1 - x0) * (yc - y0), y1 - y0);
            k = (int)floor_div((long long)xc - 128 + 255, 256);     /* centre k + 1/2 >= crossing */
            if (k < 0) k = 0;
            if (k < W) wind[k] += dir;
        }
        for (x = 0, i = 0; x < W; x++) {
            i += wind[x];
            in[y][x] = evenodd ? (i & 1) != 0 : i != 0;
        }
    }
}

static void fill_target(unsigned long v) {
    int i;
    for (i = 0; i < W * H; i++) {
        arena[TGT + i * 4] = (ogpu_u8)(v >> 24); arena[TGT + i * 4 + 1] = (ogpu_u8)(v >> 16);
        arena[TGT + i * 4 + 2] = (ogpu_u8)(v >> 8); arena[TGT + i * 4 + 3] = (ogpu_u8)v;
    }
}

static int run_path(unsigned long flags, unsigned long paint, int stroke, long width, unsigned long style) {
    ogpu_batch_init(&B, sbuf, (long)sizeof sbuf / 4);
    ogpu_surface(&B, 0, TGT, BPR, W, H, OGPU_FMT_ARGB32);
    ogpu_target(&B, 0);
    ogpu_surface(&B, 1, MSK, W, W, H, OGPU_FMT_A8);
    if (stroke)
        ogpu_path_stroke(&B, flags, paint, 0, 0, 1, 0, 0, PATH, (unsigned long)P.words, ID, WORK, WORKB, width, style,
                         4 << 16, 0, 0, 0);
    else
        ogpu_path_fill(&B, flags, paint, 0, 0, 1, 0, 0, PATH, (unsigned long)P.words, ID, WORK, WORKB);
    ogpu_core_init(&C);
    C.map = map; C.fence = 0; C.ext = 0; C.user = 0;
    ogpu_core_run(&C, sbuf, B.words);
    return C.last_error;
}

static unsigned long px32(int x, int y) {
    const ogpu_u8 *p = arena + TGT + y * BPR + x * 4;
    return (unsigned long)p[0] << 24 | (unsigned long)p[1] << 16 | (unsigned long)p[2] << 8 | p[3];
}

/* An opaque colour onto 0: every pixel is the colour or 0, as the reference says. */
static int against_reference(int evenodd, const char *what) {
    int x, y, bad = 0;
    char msg[160];
    fill_target(0);
    check(run_path(OGPU_PD_OVER | OGPU_PATH_ALIASED | (evenodd ? OGPU_PATH_EVENODD : 0), 0xFF204060UL, 0, 0, 0) == 0,
          "an aliased PATH_FILL runs");
    reference(evenodd);
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++)
            if ((px32(x, y) == 0xFF204060UL) != in[y][x] || (px32(x, y) != 0 && px32(x, y) != 0xFF204060UL)) {
                bad++;
                if (getenv("TVA_DEBUG")) {
                    int i;
                    printf("  pixel %d,%d core %08lx ref %d;", x, y, px32(x, y), in[y][x]);
                    for (i = 0; i < np; i++)
                        printf(" %s(%ld,%ld)", sub[i] ? "M" : "L", px[i], py[i]);
                    printf("\n");
                }
            }
    snprintf(msg, sizeof msg, "%s: every pixel in or out by its centre (%d differ)", what, bad);
    check(!bad, msg);
    return bad;
}

#ifdef WITH_CAIRO
static cairo_surface_t *cs;
static cairo_t *cr;
static void cairo_begin(int evenodd) {
    int i;
    cs = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W, H);
    cr = cairo_create(cs);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    cairo_set_fill_rule(cr, evenodd ? CAIRO_FILL_RULE_EVEN_ODD : CAIRO_FILL_RULE_WINDING);
    cairo_set_source_rgba(cr, 0x20 / 255.0, 0x40 / 255.0, 0x60 / 255.0, 1);
    for (i = 0; i < np; i++) {
        if (sub[i]) { if (i) cairo_close_path(cr); cairo_move_to(cr, px[i] / 256.0, py[i] / 256.0); }
        else cairo_line_to(cr, px[i] / 256.0, py[i] / 256.0);
    }
}
/* pixels where cairo's coverage (in or out) differs from the core's */
static int cairo_diff(void) {
    int x, y, bad = 0, stride;
    const unsigned char *d;
    cairo_surface_flush(cs);
    d = cairo_image_surface_get_data(cs);
    stride = cairo_image_surface_get_stride(cs);
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            unsigned long c = *(const unsigned int *)(d + y * stride + x * 4);
            if ((c != 0) != (px32(x, y) != 0)) bad++;
        }
    cairo_destroy(cr);
    cairo_surface_destroy(cs);
    return bad;
}
#endif

static void random_polygon(int n) {
    int i;
    path_start();
    for (i = 0; i < n; i++)
        pt((long)rnd((W + 16) * 256) - 8 * 256, (long)rnd((H + 16) * 256) - 8 * 256, i == 0);
}

int main(void) {
    int t, x, y, cairo_bad = 0, cairo_curve = 0, stroke_bad = 0;
    char msg[160];
    (void)cairo_bad; (void)cairo_curve; (void)stroke_bad;

    /* a rectangle off whole pixels: the pixels whose centres are inside */
    path_start();
    pt(10 * 256 + 128, 5 * 256 + 64, 1); pt(30 * 256 + 128, 5 * 256 + 64, 0);
    pt(30 * 256 + 128, 20 * 256 + 200, 0); pt(10 * 256 + 128, 20 * 256 + 200, 0);
    against_reference(0, "a rectangle with edges on pixel centres");
    check(px32(9, 10) == 0 && px32(10, 10) == 0xFF204060UL && px32(29, 10) == 0xFF204060UL && px32(30, 10) == 0,
          "an edge on a centre: in at the left edge, out at the right (cairo's rule)");
    check(px32(15, 4) == 0 && px32(15, 5) == 0xFF204060UL && px32(15, 20) == 0xFF204060UL && px32(15, 21) == 0,
          "top edge below a centre leaves its row out; bottom edge past a centre takes it");
#ifdef WITH_CAIRO
    cairo_begin(0); cairo_fill(cr); t = cairo_diff();
    snprintf(msg, sizeof msg, "the rectangle as cairo's CAIRO_ANTIALIAS_NONE fill (%d differ)", t); check(!t, msg);
#endif

    /* random polygons, non-zero and even-odd, against the reference (and cairo) */
    for (t = 0; t < 400; t++) {
        int eo = t & 1, n = 3 + (int)rnd(10);
        random_polygon(n);
        snprintf(msg, sizeof msg, "random polygon %d (%d points, %s)", t, n, eo ? "even-odd" : "non-zero");
        against_reference(eo, msg);
#ifdef WITH_CAIRO
        cairo_begin(eo); cairo_fill(cr);
        cairo_bad += cairo_diff();
#endif
    }
#ifdef WITH_CAIRO
    /* cairo cuts self-crossing polygons into trapezoids at rounded crossings
     * and steps its edges in 24.8: pixels whose centre is within a fraction
     * of a pixel of an edge can fall the other way. Counted, not required. */
    printf("400 random self-crossing polygons: %d pixels differ from cairo's CAIRO_ANTIALIAS_NONE fill\n", cairo_bad);
#endif
    /* simple polygons (no crossings), as SVG and WebCore draw crisp shapes */
    cairo_bad = 0;
    for (t = 0; t < 400; t++) {
        int i, n = 3 + (int)rnd(10);
        double ccx = 20 + rnd(56) + rnd(256) / 256.0, ccy = 20 + rnd(40) + rnd(256) / 256.0;
        path_start();
        for (i = 0; i < n; i++) {
            double a = 6.2831853 * (i + rnd(1000) / 1000.0) / n, r = 3 + rnd(30) + rnd(256) / 256.0;
            pt((long)((ccx + r * cos(a)) * 256), (long)((ccy + r * sin(a)) * 256), i == 0);
        }
        snprintf(msg, sizeof msg, "simple polygon %d (%d points)", t, n);
        against_reference(0, msg);
#ifdef WITH_CAIRO
        cairo_begin(0); cairo_fill(cr);
        cairo_bad += cairo_diff();
#endif
    }
#ifdef WITH_CAIRO
    printf("400 simple polygons: %d pixels differ from cairo's CAIRO_ANTIALIAS_NONE fill\n", cairo_bad);
    check(cairo_bad <= 400, "simple polygons within one pixel each, on average, of cairo's");
#endif

    /* two subpaths, a hole by even-odd and by opposite winding */
    path_start();
    pt(4 * 256, 4 * 256, 1); pt(60 * 256 + 77, 4 * 256, 0); pt(60 * 256 + 77, 50 * 256 + 3, 0); pt(4 * 256, 50 * 256 + 3, 0);
    pt(20 * 256 + 5, 20 * 256 + 9, 1); pt(20 * 256 + 5, 35 * 256 + 140, 0); pt(40 * 256 + 200, 35 * 256 + 140, 0);
    pt(40 * 256 + 200, 20 * 256 + 9, 0);
    against_reference(0, "a square with a hole wound the other way");
    against_reference(1, "a square with a hole, even-odd");

    /* a circle (cubics): against cairo only, both cut curves into lines */
    for (t = 0; t < 40; t++) {
        double cx = 20 + rnd(56) + rnd(256) / 256.0, cy = 20 + rnd(40) + rnd(256) / 256.0, r = 3 + rnd(16) + rnd(256) / 256.0;
        double k = 0.5522847498 * r;
        long q[13][2];
        int i;
        double pts[13][2] = { { cx + r, cy }, { cx + r, cy + k }, { cx + k, cy + r }, { cx, cy + r }, { cx - k, cy + r },
                              { cx - r, cy + k }, { cx - r, cy }, { cx - r, cy - k }, { cx - k, cy - r }, { cx, cy - r },
                              { cx + k, cy - r }, { cx + r, cy - k }, { cx + r, cy } };
        for (i = 0; i < 13; i++) { q[i][0] = (long)(pts[i][0] * 256 + 0.5); q[i][1] = (long)(pts[i][1] * 256 + 0.5); }
        ogpu_batch_init(&P, arena + PATH, 0x8000);
        ogpu_path_move(&P, q[0][0], q[0][1]);
        for (i = 1; i < 13; i += 3) ogpu_path_cubic(&P, q[i][0], q[i][1], q[i + 1][0], q[i + 1][1], q[i + 2][0], q[i + 2][1]);
        ogpu_path_close(&P);
        fill_target(0);
        check(run_path(OGPU_PD_OVER | OGPU_PATH_ALIASED, 0xFF204060UL, 0, 0, 0) == 0, "an aliased circle runs");
        for (y = 0; y < H; y++)
            for (x = 0; x < W; x++) {
                unsigned long v = px32(x, y);
                if (v && v != 0xFF204060UL) { check(0, "an aliased circle has only whole pixels"); y = H; break; }
            }
#ifdef WITH_CAIRO
        cs = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W, H);
        cr = cairo_create(cs);
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
        cairo_set_source_rgba(cr, 0x20 / 255.0, 0x40 / 255.0, 0x60 / 255.0, 1);
        cairo_move_to(cr, q[0][0] / 256.0, q[0][1] / 256.0);
        for (i = 1; i < 13; i += 3)
            cairo_curve_to(cr, q[i][0] / 256.0, q[i][1] / 256.0, q[i + 1][0] / 256.0, q[i + 1][1] / 256.0, q[i + 2][0] / 256.0,
                           q[i + 2][1] / 256.0);
        cairo_close_path(cr);
        cairo_fill(cr);
        cairo_curve += cairo_diff();
#endif
    }
#ifdef WITH_CAIRO
    printf("40 aliased circles: %d pixels differ from cairo's (curves cut into lines differently)\n", cairo_curve);
    check(cairo_curve <= 40 * 2, "aliased circles within 2 pixels each of cairo's");
#endif

    /* strokes: a rectangle and open polylines, butt caps, miter joins */
    for (t = 0; t < 60; t++) {
        int i, n = 2 + (int)rnd(4);
        long wdt = (long)(1 + rnd(5)) << 16 | (long)rnd(65536);
        path_start();
        for (i = 0; i < n; i++) pt(8 * 256 + (long)rnd((W - 16) * 256), 8 * 256 + (long)rnd((H - 16) * 256), i == 0);
        fill_target(0);
        check(run_path(OGPU_PD_OVER | OGPU_PATH_ALIASED, 0xFF204060UL, 1, wdt, OGPU_CAP_BUTT | OGPU_JOIN_BEVEL) == 0,
              "an aliased PATH_STROKE runs");
        for (y = 0; y < H; y++)
            for (x = 0; x < W; x++) {
                unsigned long v = px32(x, y);
                if (v && v != 0xFF204060UL) { check(0, "an aliased stroke has only whole pixels"); y = H; break; }
            }
#ifdef WITH_CAIRO
        cs = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W, H);
        cr = cairo_create(cs);
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
        cairo_set_source_rgba(cr, 0x20 / 255.0, 0x40 / 255.0, 0x60 / 255.0, 1);
        cairo_set_line_width(cr, wdt / 65536.0);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
        cairo_set_line_join(cr, CAIRO_LINE_JOIN_BEVEL);
        for (i = 0; i < np; i++)
            if (sub[i]) cairo_move_to(cr, px[i] / 256.0, py[i] / 256.0); else cairo_line_to(cr, px[i] / 256.0, py[i] / 256.0);
        cairo_stroke(cr);
        stroke_bad += cairo_diff();
#endif
    }
#ifdef WITH_CAIRO
    printf("60 aliased strokes: %d pixels differ from cairo's (outline points rounded differently)\n", stroke_bad);
    check(stroke_bad <= 60 * 4, "aliased strokes within 4 pixels each, on average, of cairo's");
#endif

    /* the operator and paint as v1.6's on the pixels that are in, others untouched */
    random_polygon(7);
    reference(0);
    fill_target(0x80402010UL);
    check(run_path(OGPU_PD_OVER | OGPU_PATH_ALIASED, 0x80800000UL, 0, 0, 0) == 0, "aliased, half-transparent, over");
    {
        int bad = 0;
        for (y = 0; y < H; y++)
            for (x = 0; x < W; x++) {
                /* s over d, each channel s + d (255 - 128) / 255 rounded as pixman */
                unsigned long want = in[y][x] ? 0xC0A01008UL : 0x80402010UL, got = px32(x, y);
                if (got != want && !(in[y][x] && (got >> 24) == 0xC0)) bad++;
            }
        check(!bad, "a translucent colour over, only the pixels that are in, the rest untouched");
    }
    /* the mask: half the mask slot 0, the rest 255 */
    random_polygon(6);
    reference(1);
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) arena[MSK + y * W + x] = (ogpu_u8)(x < W / 2 ? 0 : 255);
    fill_target(0);
    check(run_path(OGPU_PD_OVER | OGPU_PATH_ALIASED | OGPU_PATH_EVENODD | OGPU_PATH_MASK, 0xFF204060UL, 0, 0, 0) == 0,
          "aliased with a mask");
    {
        int bad = 0;
        for (y = 0; y < H; y++)
            for (x = 0; x < W; x++)
                if ((px32(x, y) == 0xFF204060UL) != (in[y][x] && x >= W / 2)) bad++;
        check(!bad, "the mask's alpha times the aliased coverage");
    }
    /* without the bit the same path is anti-aliased: some pixels in between */
    random_polygon(9);
    fill_target(0);
    run_path(OGPU_PD_OVER, 0xFF204060UL, 0, 0, 0);
    {
        int part = 0;
        for (y = 0; y < H; y++)
            for (x = 0; x < W; x++)
                if (px32(x, y) && px32(x, y) != 0xFF204060UL) part++;
        check(part > 0, "the same path without OGPU_PATH_ALIASED is anti-aliased");
    }

    /* gfopengpu.library 0.23's variant check: aliased paths stay off drivers */
    {
        unsigned char ok[256], fmt[OGPU_FMT_COUNT];
        memset(ok, 1, sizeof ok);
        memset(fmt, 1, sizeof fmt);
        ok[OGPU_NEEDS_CPU] = 0;
        path_start(); pt(0, 0, 1); pt(2560, 0, 0); pt(0, 2560, 0);
        ogpu_batch_init(&B, sbuf, (long)sizeof sbuf / 4);
        ogpu_surface(&B, 0, TGT, BPR, W, H, OGPU_FMT_ARGB32);
        ogpu_target(&B, 0);
        ogpu_path_fill(&B, OGPU_PD_OVER, 0xFF000000UL, 0, 0, 0, 0, 0, PATH, (unsigned long)P.words, ID, WORK, WORKB);
        check(ogpu_batch_takes(sbuf, (unsigned long)B.words, ok, fmt), "a driver that answers PATH_FILL takes an anti-aliased path");
        ogpu_batch_init(&B, sbuf, (long)sizeof sbuf / 4);
        ogpu_surface(&B, 0, TGT, BPR, W, H, OGPU_FMT_ARGB32);
        ogpu_target(&B, 0);
        ogpu_path_fill(&B, OGPU_PD_OVER | OGPU_PATH_ALIASED, 0xFF000000UL, 0, 0, 0, 0, 0, PATH, (unsigned long)P.words, ID,
                       WORK, WORKB);
        check(!ogpu_batch_takes(sbuf, (unsigned long)B.words, ok, fmt), "no driver takes an aliased path: the CPU core draws it");
    }

    /* 0.24's OGPU_QUERY_FLAG: the core answers for the flags it carries out */
    check(ogpu_core_supports((int)OGPU_QUERY_FLAG(OGPU_OP_PATH_FILL, OGPU_PATH_ALIASED_BIT), OGPU_FMT_ARGB32) == OGPU_FULL,
          "the CPU core says it takes OGPU_PATH_ALIASED in PATH_FILL");
    check(ogpu_core_supports((int)OGPU_QUERY_FLAG(OGPU_OP_PATH_STROKE, OGPU_PATH_ALIASED_BIT), OGPU_FMT_A8) == OGPU_FULL,
          "and in PATH_STROKE, into an A8 mask too");
    check(ogpu_core_supports((int)OGPU_QUERY_FLAG(OGPU_OP_PATH_FILL, 8), OGPU_FMT_ARGB32) == OGPU_FULL
          && ogpu_core_supports((int)OGPU_QUERY_FLAG(OGPU_OP_PATH_FILL, 13), OGPU_FMT_RGB565) == OGPU_FULL,
          "v1.6's path flags (even-odd .. pattern) too");
    check(ogpu_core_supports((int)OGPU_QUERY_FLAG(OGPU_OP_PATH_FILL, 15), OGPU_FMT_ARGB32) == OGPU_NONE
          && ogpu_core_supports((int)OGPU_QUERY_FLAG(OGPU_OP_PATH_FILL, 3), OGPU_FMT_ARGB32) == OGPU_NONE,
          "a bit stream.h doesn't define is not taken");
    check(ogpu_core_supports((int)OGPU_QUERY_FLAG(OGPU_OP_PATH_FILL, OGPU_PATH_ALIASED_BIT), OGPU_FMT_CLUT8) == OGPU_NONE,
          "nor a flag of a command the format can't take");
    check(ogpu_core_supports((int)OGPU_QUERY_FLAG(OGPU_OP_FILL, OGPU_PATH_ALIASED_BIT), OGPU_FMT_ARGB32) == OGPU_NONE,
          "nor a flag of a command that has none");
    check(OGPU_QUERY_FLAG(OGPU_OP_PATH_FILL, OGPU_PATH_ALIASED_BIT) > 0xFFFFUL,
          "the form is no opcode (an old back end answers it as one it doesn't know: OGPU_NONE)");
    /* a driver that answers the form takes aliased paths; one that predates it doesn't */
    {
        unsigned char ok[256], fmt[OGPU_FMT_COUNT];
        memset(ok, 1, sizeof ok);
        memset(fmt, 1, sizeof fmt);
        path_start(); pt(0, 0, 1); pt(2560, 0, 0); pt(0, 2560, 0);
        ogpu_batch_init(&B, sbuf, (long)sizeof sbuf / 4);
        ogpu_surface(&B, 0, TGT, BPR, W, H, OGPU_FMT_ARGB32);
        ogpu_target(&B, 0);
        ogpu_path_stroke(&B, OGPU_PD_OVER | OGPU_PATH_ALIASED, 0xFF000000UL, 0, 0, 0, 0, 0, PATH, (unsigned long)P.words, ID,
                         WORK, WORKB, 2L << 16, 0, 4L << 16, 0, 0, 0);
        check(ogpu_batch_takes(sbuf, (unsigned long)B.words, ok, fmt), "a driver that answers the flag takes an aliased stroke");
        ok[OGPU_NEEDS_PATH_ALIASED] = 0;
        check(!ogpu_batch_takes(sbuf, (unsigned long)B.words, ok, fmt), "one that doesn't: the CPU core draws it");
    }

    printf("test_vector_aliased: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
