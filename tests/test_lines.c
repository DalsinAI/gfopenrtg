/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * Draw and PolyDraw on GFOpenRTG's 8-bit bitmaps (library/ortg_line.h, 0.25)
 * against graphics.library's, on the host. The reference is GFOpenGfx's core
 * (gfopengfx library/core/gfx_rp.c, line(), Draw, PolyDraw and Move at
 * 6c8b6d9), which OpenGfxCheck compares byte for byte with OS 3.2's
 * graphics.library on planar bitmaps; it is copied here onto a byte per
 * pixel. The other side is library/screens.c's lines(), line_gpu() and
 * lines_piece() as they use ortg_line.h: the lines GFOpenGPU takes drawn by
 * the GFOpenGPU core (CLIP and LINE, as line_piece sends them), the others
 * per pixel, both through clip pieces with a hidden part as a layer's.
 * Random calls in every DrawMode, LinePtrn, linpatcnt, FRST_DOT and Mask;
 * the pixels, cp, linpatcnt and Flags must match after every call.
 * Side by side with them: GFOpenRTG's Draw and PolyDraw before 0.25 (main at
 * 4b13804: line() and any_line()), counted where they differ from
 * graphics.library; and the lines the CPU draws a pixel at a time, a layer
 * lock each (0.25's first commit) against a run at a time (its second),
 * which must be the same pixels. Then cases spelt out pixel by pixel:
 * a COMPLEMENT box's corners, a pattern going on from line to line, JAM2's
 * BgPen, INVERSVID, COMPLEMENT with JAM2. Then the pixels against line()
 * before 0.25 for every line within 40 pixels: the same Bresenham.
 * Built for the 68k too (qemu-m68k in tests/run.sh; tests/amiga/lines
 * builds it for AmigaOS as OpenRTGLineModel). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <exec/types.h>
#include "../library/ortg_line.h"
#include "../library/opengpu/ogpu_core.h"
#include "../include/gfopengpu/build.h"

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

/* ---- the reference: GFOpenGfx's core ---------------------------------------------------- */

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

/* ---- GFOpenRTG: screens.c's lines() on ortg_line.h ------------------------------------------ */

#define ARENA (1L << 16)
#define T0 0x1000
#define T1 0x4000                   /* the same drawn a pixel at a time */
#define T2 0x7000                   /* the same drawn by GFOpenRTG before 0.25 */
static ogpu_u8 arena[ARENA];
static long tgt = T0;               /* where ortg_lines() draws */
static int batched = 1;             /* a run at a time (0.25), or a pixel at a time */
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
    UBYTE *p = &arena[tgt + y * W + x];
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

/* the first commit's line_cpu(): a pixel at a time, through every piece */
static void cpu_dots(const struct rp *rp, struct ortg_line *l)
{
    for (;;) {
        int w = ortg_line_what(l);
        if (w != ORTG_LINE_NONE)
            for (int pc = 0; pc < 3; pc++)
                if (l->x >= piece[pc].x0 && l->x <= piece[pc].x1 && l->y >= piece[pc].y0 && l->y <= piece[pc].y1) plot8(l->x, l->y, rp, w);
        if (l->i == l->len) break;
        ortg_line_next(l);
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
        ogpu_surface(&b, 0, (ogpu_u32)tgt, W, W, H, OGPU_FMT_CLUT8);
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
        CHECK(C.last_error == OGPU_OK, "GFOpenGPU core: error %ld", (long)C.last_error);
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
        int g = line_gpu(rp, &l, ex, ey, 1);
        if (!g && !batched) {
            cpu_dots(rp, &l);
            cpu_lines++;
        } else if (!g) {
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

/* ---- GFOpenRTG before 0.25 (main at 4b13804) ---------------------------------------------
 * line() for every line (any_line()'s GFOpenGPU path drew the same pixels):
 * every first pixel drawn, the pattern from bit 15 for each line, JAM2
 * dropped, INVERSVID swapping the pens; linpatcnt and Flags left alone. */

static void old_lines(struct rp *rp, const WORD *xy, LONG n)
{
    UBYTE pen = rp->DrawMode & INVERSVID ? rp->BgPen : rp->FgPen;
    for (LONG j = 0; j < n; j++) {
        LONG x0 = rp->cp_x, y0 = rp->cp_y, x1 = xy[2 * j], y1 = xy[2 * j + 1];
        LONG ddx = x1 > x0 ? x1 - x0 : x0 - x1, ddy = -(y1 > y0 ? y1 - y0 : y0 - y1);
        LONG sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = ddx + ddy;
        UWORD pat = rp->LinePtrn;
        int k = 0;
        for (;;) {
            if ((pat == 0xFFFF || ((pat >> (15 - (k & 15))) & 1)) && x0 >= 0 && y0 >= 0 && x0 < W && y0 < H && visible(x0, y0) && rp->Mask) {
                UBYTE *p = &arena[T2 + y0 * W + x0];
                if (rp->DrawMode & COMPLEMENT) *p ^= rp->Mask;
                else *p = (UBYTE)((*p & ~rp->Mask) | (pen & rp->Mask));
            }
            k++;
            if (x0 == x1 && y0 == y1) break;
            LONG e2 = 2 * err;
            if (e2 >= ddy) { err += ddy; x0 += sx; }
            if (e2 <= ddx) { err += ddx; y0 += sy; }
        }
        rp->cp_x = (WORD)x1; rp->cp_y = (WORD)y1;
    }
}

/* ---- the checks ------------------------------------------------------------------------- */

static LONG coord(LONG span)
{
    /* mostly inside, some off the edges; some lines straight or at 45 degrees */
    return (LONG)(rnd() % (unsigned long)(span + 16)) - 8;
}

static long calls, old_pixels_wrong, old_state_wrong;

static void random_calls(int gpu, int rounds)
{
    static const UWORD pats[] = { 0xFFFF, 0xFFFF, 0x0000, 0xF0F0, 0xAAAA, 0xCCCC, 0x8001, 0xFF00, 0x1234 };
    gpu_on = gpu;
    for (int r = 0; r < rounds && failures < 20; r++) {
        struct rp a, b, d, o;
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
            if (what < 5) n = 1;
            memcpy(arena + T1, ref, W * H); memcpy(arena + T2, ref, W * H);
            d = b; o = b;
            if (what < 5) ref_draw(&a, xy[0], xy[1]);
            else ref_polydraw(&a, n, xy);
            tgt = T0; batched = 1; ortg_lines(&b, xy, n);
            tgt = T1; batched = 0; ortg_lines(&d, xy, n);
            tgt = T0; batched = 1;
            old_lines(&o, xy, n);
            calls++;
            if (memcmp(ref, arena + T2, W * H)) old_pixels_wrong++;
            if (o.linpatcnt != a.linpatcnt || o.Flags != a.Flags) old_state_wrong++;
            CHECK(memcmp(arena + T0, arena + T1, W * H) == 0 && memcmp(&b, &d, sizeof b) == 0,
                  "round %d call %d: a run at a time and a pixel at a time differ", r, c);
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

/* ---- cases spelt out --------------------------------------------------------------------
 * Each drawn on 0 by graphics.library's reference, 0.25 (with GFOpenGPU's
 * core and without) and GFOpenRTG before 0.25; one row read back as pen
 * numbers ('.' 0, 'X' 255). */

static struct rp ca, cb, co;

static void case_start(UBYTE mode, UWORD ptrn, UBYTE fg, UBYTE bg, LONG x, LONG y)
{
    memset(ref, 0, W * H); memset(arena + T0, 0, W * H); memset(arena + T2, 0, W * H);
    memset(&ca, 0, sizeof ca);
    ca.DrawMode = mode; ca.LinePtrn = ptrn; ca.FgPen = fg; ca.BgPen = bg; ca.Mask = 0xFF;
    ref_move(&ca, x, y);
    cb = ca; co = ca;
}

static void case_draw(const WORD *xy, LONG n, int poly)
{
    if (poly) ref_polydraw(&ca, n, xy);
    else for (LONG i = 0; i < n; i++) ref_draw(&ca, xy[2 * i], xy[2 * i + 1]);
    tgt = T0; batched = 1;
    if (poly) ortg_lines(&cb, xy, n);
    else for (LONG i = 0; i < n; i++) ortg_lines(&cb, xy + 2 * i, 1);
    old_lines(&co, xy, n);
}

static void row(const UBYTE *p, LONG y, LONG x0, LONG x1, char *out)
{
    for (LONG x = x0; x <= x1; x++) {
        UBYTE v = p[y * W + x];
        *out++ = v == 0 ? '.' : v == 0xFF ? 'X' : v < 10 ? (char)('0' + v) : '?';
    }
    *out = 0;
}

static void case_check(const char *name, LONG y, LONG x0, LONG x1, const char *want, const char *old_want, int cnt)
{
    char r[W + 1], n[W + 1], o[W + 1];
    row(ref, y, x0, x1, r); row(arena + T0, y, x0, x1, n); row(arena + T2, y, x0, x1, o);
    CHECK(!strcmp(r, want), "%s: graphics.library's reference draws %s, the case says %s", name, r, want);
    CHECK(!strcmp(n, want) && !memcmp(ref, arena + T0, W * H), "%s: 0.25 draws %s, graphics.library %s", name, n, want);
    CHECK(!strcmp(o, old_want), "%s: before 0.25 drew %s, the case says %s", name, o, old_want);
    CHECK(cb.linpatcnt == cnt && ca.linpatcnt == cnt && !(cb.Flags & FRST_DOT), "%s: linpatcnt %d (graphics %d), want %d", name, cb.linpatcnt, ca.linpatcnt, cnt);
    if (!gpu_on) printf("test_lines: %-34s graphics.library and 0.25 %s, before 0.25 %s\n", name, n, o);
}

static void cases(int gpu)
{
    static const WORD two[4] = { 15, 5, 25, 5 };
    static const WORD eight[2] = { 7, 8 };
    static const WORD box[8] = { 90, 2, 90, 12, 62, 12, 62, 2 };
    gpu_on = gpu;
    /* LinePtrn F0F0 from Move: 15 to 10 for the first line's six pixels; the
     * second leaves out its first (FRST_DOT clear) and goes on at bit 9 */
    case_start(JAM1, 0xF0F0, 1, 2, 10, 5);
    case_draw(two, 2, 0);
    case_check("pattern on from line to line", 5, 10, 25, "1111....1111....", "1111.1111....111", 0);
    case_start(JAM2, 0xF0F0, 1, 2, 10, 5);
    case_draw(two, 2, 0);
    case_check("JAM2: BgPen where the bit is 0", 5, 10, 25, "1111222211112222", "1111.1111....111", 0);
    case_start(JAM1 | INVERSVID, 0xFFFF, 1, 2, 0, 8);
    case_draw(eight, 1, 0);
    case_check("INVERSVID JAM1 solid: nothing", 8, 0, 7, "........", "22222222", 8);
    case_start(COMPLEMENT | JAM2, 0xF0F0, 1, 2, 0, 8);
    case_draw(eight, 1, 0);
    case_check("COMPLEMENT JAM2: every pixel", 8, 0, 7, "XXXXXXXX", "XXXX....", 8);
    /* a COMPLEMENT box, Move and four Draws, then the same as a PolyDraw:
     * every corner inverted once but the start, which the last line inverts again */
    case_start(COMPLEMENT, 0xFFFF, 1, 2, 62, 2);
    case_draw(box, 4, 0);
    case_check("COMPLEMENT box, Draws: top", 2, 62, 66, ".XXXX", ".XXXX", (BYTE)(15 - 28 - 10 - 28 - 10));
    case_check("COMPLEMENT box, Draws: bottom", 12, 62, 66, "XXXXX", ".XXXX", (BYTE)(15 - 28 - 10 - 28 - 10));
    case_check("COMPLEMENT box, Draws: right", 12, 86, 90, "XXXXX", "XXXX.", (BYTE)(15 - 28 - 10 - 28 - 10));
    case_check("COMPLEMENT box, Draws: top right", 2, 86, 90, "XXXXX", "XXXX.", (BYTE)(15 - 28 - 10 - 28 - 10));
    case_start(COMPLEMENT, 0xFFFF, 1, 2, 62, 2);
    case_draw(box, 4, 1);
    case_check("COMPLEMENT box, PolyDraw: top", 2, 62, 66, ".XXXX", ".XXXX", (BYTE)(15 - 28 - 10 - 28 - 10));
    case_check("COMPLEMENT box, PolyDraw: bottom", 12, 62, 66, "XXXXX", ".XXXX", (BYTE)(15 - 28 - 10 - 28 - 10));
    case_check("COMPLEMENT box, PolyDraw: top right", 2, 86, 90, "XXXXX", "XXXX.", (BYTE)(15 - 28 - 10 - 28 - 10));
}

/* GFOpenRTG's line() before 0.25 */
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
    printf("test_lines: %ld random Draw and PolyDraw calls the same as graphics.library, a run at a time and a pixel at a time; "
           "before 0.25, %ld of them drew other pixels and %ld left linpatcnt or FRST_DOT wrong\n", calls, old_pixels_wrong, old_state_wrong);
    CHECK(old_pixels_wrong > calls / 10, "the old code should differ often");
    complement_box(0);
    complement_box(1);
    cases(0);
    cases(1);
    printf("test_lines: %ld lines through the GFOpenGPU core, %ld on the CPU; %s\n", gpu_lines, cpu_lines, failures ? "FAILED" : "all the same as graphics.library");
    return failures != 0;
}
