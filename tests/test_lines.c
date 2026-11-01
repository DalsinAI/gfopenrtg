/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * Draw and PolyDraw on OpenRTG's 8-bit bitmaps (library/ortg_line.h, 0.25)
 * against graphics.library's, on the host. The reference is OpenGfx's core
 * (openamigagfx library/core/gfx_rp.c, line(), Draw, PolyDraw and Move at
 * 6c8b6d9), which OpenGfxCheck compares byte for byte with OS 3.2's
 * graphics.library on planar bitmaps; it is copied here onto a byte per
 * pixel. The other side is library/screens.c's lines(), line_gpu() and
 * lines_piece() as they use ortg_line.h: the lines OpenGPU takes drawn by
 * the OpenGPU core (CLIP and LINE, as line_piece sends them), the others
 * per pixel, both through clip pieces with a hidden part as a layer's.
 * Random calls in every DrawMode, LinePtrn, linpatcnt, FRST_DOT and Mask;
 * the pixels, cp, linpatcnt and Flags must match after every call.
 * Then the pixels against OpenRTG's line() before 0.25, for every line
 * within 40 pixels: the same Bresenham. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <exec/types.h>
#include "../library/ortg_line.h"
#include "../library/opengpu/ogpu_core.h"
#include "../include/opengpu/build.h"

#define W 96
#define H 64
#define JAM1 0
#define JAM2 1
#define COMPLEMENT 2
#define INVERSVID 4
#define FRST_DOT 1

struct rp { WORD cp_x, cp_y; BYTE linpatcnt; UBYTE Flags, DrawMode, FgPen, BgPen, Mask; UWORD LinePtrn; };

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static unsigned long rnd_state = 20261101;
static unsigned long rnd(void) { rnd_state = (rnd_state * 1103515245UL + 12345UL) & 0xFFFFFFFFUL; return (rnd_state >> 8) & 0xFFFFFFUL; }

/* The pieces: three visible rectangles and a hidden one (no backing store),
 * as a window partly covered. Inclusive. */
struct box { LONG x0, y0, x1, y1; };
static const struct box piece[3] = { { 0, 0, W - 1, 19 }, { 0, 20, 39, H - 1 }, { 60, 20, W - 1, H - 1 } };
static int visible(LONG x, LONG y)
{
    for (int i = 0; i < 3; i++)
        if (x >= piece[i].x0 && x <= piece[i].x1 && y >= piece[i].y0 && y <= piece[i].y1) return 1;
    return 0;
}

/* ---- the reference: OpenGfx's core ---------------------------------------------------- */

static UBYTE ref[W * H];
enum { GFX_JAM1, GFX_JAM2, GFX_XOR };

static void ref_put(LONG x, LONG y, UBYTE pen, UBYTE mask, int invert)
{
    UBYTE *p;
    if (!visible(x, y)) return;
    p = &ref[y * W + x];
    *p = invert ? (UBYTE)(*p ^ mask) : (UBYTE)((*p & ~mask) | (pen & mask));
}

static int pat_mode(struct rp *rp, ULONG *pat)
{
    UBYTE mode = rp->DrawMode;
    if (mode & INVERSVID) *pat = ~*pat & 0xFFFF;
    if (mode & COMPLEMENT) {
        if (mode & JAM2) *pat = 0xFFFF;
        return GFX_XOR;
    }
    return mode & JAM2 ? GFX_JAM2 : GFX_JAM1;
}

static void ref_move(struct rp *rp, LONG x, LONG y)
{
    rp->cp_x = (WORD)x; rp->cp_y = (WORD)y;
    rp->Flags |= FRST_DOT;
    rp->linpatcnt = 15;
}

#define JOINT 2
static void ref_line(struct rp *rp, LONG x, LONG y, int first)
{
    WORD x0 = rp->cp_x, y0 = rp->cp_y, x1 = (WORD)x, y1 = (WORD)y;
    LONG dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y1 - y0 : y0 - y1;
    LONG sx = x1 >= x0 ? 1 : -1, sy = y1 >= y0 ? 1 : -1;
    LONG len = dx > dy ? dx : dy, mn = dx > dy ? dy : dx, err = 2 * mn - len, i;
    LONG px = x0, py = y0;
    ULONG ptrn = rp->LinePtrn;
    int cnt = rp->linpatcnt, mode = pat_mode(rp, &ptrn);

    for (i = 0; i <= len; i++) {
        ULONG on = ptrn >> ((cnt - i) & 15) & 1;
        if (!i && first == JOINT) on = !(rp->LinePtrn >> (cnt & 15) & 1);
        if ((i || first) && px >= 0 && py >= 0 && px < W && py < H) {
            if (mode == GFX_XOR) { if (on) ref_put(px, py, 0, rp->Mask, 1); }
            else if (on) ref_put(px, py, rp->FgPen, rp->Mask, 0);
            else if (mode == GFX_JAM2) ref_put(px, py, rp->BgPen, rp->Mask, 0);
        }
        if (err >= 0) {
            if (dx > dy) py += sy; else px += sx;
            err -= 2 * len;
        }
        err += 2 * mn;
        if (dx > dy) px += sx; else py += sy;
    }
    rp->linpatcnt = (BYTE)(cnt - len);
    rp->Flags &= ~FRST_DOT;
    rp->cp_x = x1;
    rp->cp_y = y1;
}

static void ref_draw(struct rp *rp, LONG x, LONG y) { ref_line(rp, x, y, (rp->Flags & FRST_DOT) != 0); }

static void ref_polydraw(struct rp *rp, LONG count, const WORD *xy)
{
    int again = (rp->DrawMode & (COMPLEMENT | JAM2)) != COMPLEMENT ? 1 : JOINT;
    for (LONG i = 0; i < count; i++, xy += 2) ref_line(rp, xy[0], xy[1], i ? again : (rp->Flags & FRST_DOT) != 0);
}

/* ---- OpenRTG: screens.c's lines() on ortg_line.h ------------------------------------------ */

#define ARENA (1L << 16)
#define T0 0x1000
static ogpu_u8 arena[ARENA];
static unsigned char sbuf[1 << 12];
static struct ogpu_core C;
static int gpu_on;
static long gpu_lines, cpu_lines;

static ogpu_u8 *map(void *user, ogpu_u32 address, ogpu_u32 length)
{
    (void)user;
    if (address >= ARENA || length > (ogpu_u32)ARENA - address) return 0;
    return arena + address;
}

/* plot() for 8 bits */
static void plot8(LONG x, LONG y, const struct rp *rp, int what)
{
    UBYTE *p = &arena[T0 + y * W + x];
    if (!rp->Mask) return;
    if (what == ORTG_LINE_XOR) { *p ^= rp->Mask; return; }
    *p = (UBYTE)((*p & ~rp->Mask) | ((what == ORTG_LINE_BG ? rp->BgPen : rp->FgPen) & rp->Mask));
}

/* lines_piece over every piece (the bitmap is the screen's: dx, dy 0) */
static void cpu_run(const struct rp *rp, LONG x0, LONG y0, const WORD *xy, LONG n, LONG k, BYTE cnt0, int frst)
{
    for (int pc = 0; pc < 3; pc++) {
        LONG px = x0, py = y0;
        BYTE cnt = cnt0;
        for (LONG j = 0; j < n; j++) {
            struct ortg_line l;
            LONG ex = xy[2 * j], ey = xy[2 * j + 1];
            ortg_line_init(&l, px, py, ex, ey, rp->LinePtrn, cnt, rp->DrawMode, ortg_line_first_of(rp->DrawMode, frst, k + j));
            for (;;) {
                int w = ortg_line_what(&l);
                if (w != ORTG_LINE_NONE && l.x >= piece[pc].x0 && l.x <= piece[pc].x1 && l.y >= piece[pc].y0 && l.y <= piece[pc].y1)
                    plot8(l.x, l.y, rp, w);
                if (l.i == l.len) break;
                ortg_line_next(&l);
            }
            cnt = ortg_line_cnt_after(&l);
            px = ex; py = ey;
        }
    }
}

static int line_gpu(const struct rp *rp, const struct ortg_line *l, LONG x1, LONG y1, int go)
{
    int w0 = ortg_line_what(l), kind = l->len ? ortg_line_rest(l) : w0;
    LONG x0 = l->x, y0 = l->y;
    if (w0 == ORTG_LINE_NONE && kind == ORTG_LINE_NONE) return 1;
    if (kind <= ORTG_LINE_NONE || (w0 != kind && w0 != ORTG_LINE_NONE)) return 0;
    if (!gpu_on || (l->dx && l->dy && l->dx != l->dy)) return 0;
    if (rp->Mask != 0xFF) return 0;
    if (!go) return 1;
    if (w0 == ORTG_LINE_NONE) { x0 += (x1 > x0) - (x1 < x0); y0 += (y1 > y0) - (y1 < y0); }
    {
        struct OGPUBatch b;
        LONG bx0 = x0 < x1 ? x0 : x1, by0 = y0 < y1 ? y0 : y1, bx1 = x0 < x1 ? x1 : x0, by1 = y0 < y1 ? y1 : y0;
        ogpu_batch_init(&b, sbuf, sizeof sbuf / 4);
        ogpu_surface(&b, 0, T0, W, W, H, OGPU_FMT_CLUT8);
        ogpu_target(&b, 0);
        for (int pc = 0; pc < 3; pc++) {
            /* pieces_then: the box within the piece and the bitmap */
            LONG a0 = bx0 > piece[pc].x0 ? bx0 : piece[pc].x0, b0 = by0 > piece[pc].y0 ? by0 : piece[pc].y0;
            LONG a1 = bx1 < piece[pc].x1 ? bx1 : piece[pc].x1, b1 = by1 < piece[pc].y1 ? by1 : piece[pc].y1;
            if (a0 > a1 || b0 > b1) continue;
            ogpu_clip(&b, a0, b0, a1 - a0 + 1, b1 - b0 + 1);
            ogpu_line(&b, x0, y0, x1, y1, kind == ORTG_LINE_BG ? rp->BgPen : rp->FgPen, kind == ORTG_LINE_XOR ? OGPU_COMPLEMENT : OGPU_JAM1);
        }
        ogpu_core_init(&C);
        C.map = map; C.user = 0;
        ogpu_core_run(&C, sbuf, b.words);
        CHECK(C.last_error == OGPU_OK, "OpenGPU core: error %ld", (long)C.last_error);
        gpu_lines++;
        return 1;
    }
}

static void ortg_lines(struct rp *rp, const WORD *xy, LONG n)
{
    UBYTE mode = rp->DrawMode, frst = (rp->Flags & FRST_DOT) != 0;
    UWORD ptrn = rp->LinePtrn;
    LONG cx = rp->cp_x, cy = rp->cp_y, j = 0;
    BYTE cnt = rp->linpatcnt;
    if (n <= 0) return;
    while (j < n) {
        struct ortg_line l;
        LONG ex = xy[2 * j], ey = xy[2 * j + 1];
        ortg_line_init(&l, cx, cy, ex, ey, ptrn, cnt, mode, ortg_line_first_of(mode, frst, j));
        if (!line_gpu(rp, &l, ex, ey, 1)) {
            LONG m = 1;
            BYTE c2 = ortg_line_cnt_after(&l);
            while (j + m < n) {
                struct ortg_line t;
                LONG tx = xy[2 * (j + m)], ty = xy[2 * (j + m) + 1];
                ortg_line_init(&t, ex, ey, tx, ty, ptrn, c2, mode, ortg_line_first_of(mode, frst, j + m));
                if (line_gpu(rp, &t, tx, ty, 0)) break;
                c2 = ortg_line_cnt_after(&t);
                ex = tx; ey = ty; m++;
            }
            cpu_run(rp, cx, cy, xy + 2 * j, m, j, cnt, frst);
            cpu_lines += m;
            j += m; cx = ex; cy = ey; cnt = c2;
            continue;
        }
        cnt = ortg_line_cnt_after(&l);
        cx = ex; cy = ey; j++;
    }
    rp->linpatcnt = cnt;
    rp->Flags &= ~FRST_DOT;
    rp->cp_x = (WORD)cx; rp->cp_y = (WORD)cy;
}

/* ---- the checks ------------------------------------------------------------------------- */

static LONG coord(LONG span)
{
    /* mostly inside, some off the edges; some lines straight or at 45 degrees */
    return (LONG)(rnd() % (unsigned long)(span + 16)) - 8;
}

static void random_calls(int gpu, int rounds)
{
    static const UWORD pats[] = { 0xFFFF, 0xFFFF, 0x0000, 0xF0F0, 0xAAAA, 0xCCCC, 0x8001, 0xFF00, 0x1234 };
    gpu_on = gpu;
    for (int r = 0; r < rounds && failures < 20; r++) {
        struct rp a, b;
        for (int i = 0; i < W * H; i++) ref[i] = arena[T0 + i] = (UBYTE)rnd();
        memset(&a, 0, sizeof a);
        a.FgPen = (UBYTE)rnd(); a.BgPen = (UBYTE)rnd();
        a.Mask = rnd() & 3 ? 0xFF : (UBYTE)rnd();
        a.LinePtrn = rnd() & 1 ? pats[rnd() % (sizeof pats / sizeof pats[0])] : (UWORD)rnd();
        a.DrawMode = (UBYTE)(rnd() & 7);
        a.Flags = rnd() & 1 ? FRST_DOT : 0;
        a.linpatcnt = (BYTE)rnd();
        a.cp_x = (WORD)coord(W); a.cp_y = (WORD)coord(H);
        b = a;
        for (int c = 0; c < 12; c++) {
            int what = (int)(rnd() % 8);
            WORD xy[16];
            LONG n = 1 + (LONG)(rnd() % 8);
            if (what == 0) {                        /* Move */
                LONG x = coord(W), y = coord(H);
                ref_move(&a, x, y); ref_move(&b, x, y);
                continue;
            }
            if (what == 1) {                        /* the pens and the mode change */
                a.DrawMode = b.DrawMode = (UBYTE)(rnd() & 7);
                a.LinePtrn = b.LinePtrn = rnd() & 1 ? pats[rnd() % (sizeof pats / sizeof pats[0])] : (UWORD)rnd();
                continue;
            }
            for (LONG i = 0; i < n; i++) {
                LONG px = i ? xy[2 * i - 2] : a.cp_x, py = i ? xy[2 * i - 1] : a.cp_y, s = (LONG)(rnd() % 4);
                xy[2 * i] = (WORD)coord(W); xy[2 * i + 1] = (WORD)coord(H);
                if (s == 0) xy[2 * i] = (WORD)px;                       /* vertical */
                else if (s == 1) xy[2 * i + 1] = (WORD)py;              /* horizontal */
                else if (s == 2) { LONG d = coord(W) - px; xy[2 * i] = (WORD)(px + d); xy[2 * i + 1] = (WORD)(py + (rnd() & 1 ? d : -d)); }
            }
            if (what < 5) { ref_draw(&a, xy[0], xy[1]); ortg_lines(&b, xy, 1); n = 1; }
            else { ref_polydraw(&a, n, xy); ortg_lines(&b, xy, n); }
            CHECK(memcmp(ref, arena + T0, W * H) == 0, "round %d call %d (%s of %ld, mode %d, LinePtrn %04x, mask %02x, gpu %d): pixels differ",
                  r, c, n == 1 && what < 5 ? "Draw" : "PolyDraw", (long)n, a.DrawMode, a.LinePtrn, a.Mask, gpu);
            CHECK(a.cp_x == b.cp_x && a.cp_y == b.cp_y && a.linpatcnt == b.linpatcnt && a.Flags == b.Flags,
                  "round %d call %d: cp %d,%d cnt %d flags %d, expected %d,%d %d %d", r, c, b.cp_x, b.cp_y, b.linpatcnt, b.Flags,
                  a.cp_x, a.cp_y, a.linpatcnt, a.Flags);
            if (failures >= 20) return;
            memcpy(ref, arena + T0, W * H);         /* go on from the same pixels */
        }
    }
}

/* A COMPLEMENT box from Move and four Draws: three corners inverted once,
 * the closing one (the start, drawn twice) back as it was. */
static void complement_box(int gpu)
{
    struct rp b;
    static const WORD xy[8] = { 90, 10, 90, 30, 62, 30, 62, 10 };
    gpu_on = gpu;
    memset(arena + T0, 0, W * H);
    memset(&b, 0, sizeof b);
    b.DrawMode = COMPLEMENT; b.Mask = 0xFF; b.LinePtrn = 0xFFFF; b.FgPen = 1;
    ref_move(&b, 62, 10);
    for (int i = 0; i < 4; i++) ortg_lines(&b, xy + 2 * i, 1);
    CHECK(arena[T0 + 10 * W + 90] == 0xFF && arena[T0 + 30 * W + 90] == 0xFF && arena[T0 + 30 * W + 62] == 0xFF, "box corners (gpu %d)", gpu);
    CHECK(arena[T0 + 10 * W + 62] == 0, "box's closing corner inverted twice (gpu %d)", gpu);
    CHECK(arena[T0 + 10 * W + 75] == 0xFF && arena[T0 + 20 * W + 90] == 0xFF, "box sides (gpu %d)", gpu);
}

/* OpenRTG's line() before 0.25 */
static int old_line(LONG x0, LONG y0, LONG x1, LONG y1, LONG *pts)
{
    LONG ddx = x1 > x0 ? x1 - x0 : x0 - x1, ddy = -(y1 > y0 ? y1 - y0 : y0 - y1);
    LONG sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = ddx + ddy;
    int n = 0;
    for (;;) {
        pts[2 * n] = x0; pts[2 * n + 1] = y0; n++;
        if (x0 == x1 && y0 == y1) break;
        LONG e2 = 2 * err;
        if (e2 >= ddy) { err += ddy; x0 += sx; }
        if (e2 <= ddx) { err += ddx; y0 += sy; }
    }
    return n;
}

static void same_pixels_as_before(void)
{
    static LONG pts[2 * 200];
    long lines = 0;
    for (LONG x1 = -40; x1 <= 40; x1++)
        for (LONG y1 = -40; y1 <= 40; y1++) {
            struct ortg_line l;
            int n = old_line(3, -7, 3 + x1, -7 + y1, pts), ok = 1;
            ortg_line_init(&l, 3, -7, 3 + x1, -7 + y1, 0xFFFF, 15, JAM1, ORTG_LINE_FIRST);
            if (l.len + 1 != n) ok = 0;
            for (int i = 0; ok && i < n; i++) {
                if (l.x != pts[2 * i] || l.y != pts[2 * i + 1]) ok = 0;
                if (i < n - 1) ortg_line_next(&l);
            }
            CHECK(ok, "line to %ld,%ld: not line()'s pixels", (long)x1, (long)y1);
            lines++;
        }
    printf("test_lines: %ld lines the same pixels as line() before 0.25\n", lines);
}

int main(void)
{
    same_pixels_as_before();
    random_calls(0, 3000);
    random_calls(1, 3000);
    complement_box(0);
    complement_box(1);
    printf("test_lines: %ld lines through the OpenGPU core, %ld on the CPU; %s\n", gpu_lines, cpu_lines, failures ? "FAILED" : "all the same as graphics.library");
    return failures != 0;
}
