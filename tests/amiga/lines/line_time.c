/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * OpenRTGLineTime: what gfopenrtg.library 0.25's batched lines save on a 68k,
 * measured without the library (a ROM's gfopenrtg.library can't be swapped
 * in a lab). It does the work screens.c does for the lines the CPU draws,
 * both ways, against a real layer (a smart refresh window on the Workbench
 * screen, partly covered by a second), into memory of its own:
 *   before (0.24 and 0.25's first commit): for every pixel, the layer's
 *     lock, a walk of its ClipRects, OGPU_Wait(OGPU_FENCE_ALL) (as
 *     ortg_ogpu_settle) and the pixel;
 *   after (0.25): for every line, the lock and the walk once, the wait once
 *     before its first pixel, and the pixels of each piece (lines_piece).
 * Both walk library/ortg_line.h, so they must leave the same pixels; it
 * checks that, then prints the milliseconds for 1000 slanted lines.
 *   OpenRTGLineTime [NOWAIT]   NOWAIT leaves the GFOpenGPU wait out of both. */
#include <exec/types.h>
#include <exec/memory.h>
#include <devices/timer.h>
#include <intuition/intuition.h>
#include <graphics/gfx.h>
#include <graphics/clip.h>
#include <graphics/layers.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/timer.h>
#include <proto/gfopengpu.h>
#include <gfopengpu/gfopengpu.h>
#include <stdio.h>
#include <string.h>
#include "../../../library/ortg_line.h"

struct Library *OpenGPUBase;
struct Device *TimerBase;
static struct timerequest treq;
static ULONG eclock_khz;

#define W 360
#define H 220
static UBYTE *mem;                  /* the layer's pixels, W by H, layer coordinates */
static int wait_gpu;
static ULONG waits;

static ULONG now_ticks(void)
{
    struct EClockVal e;
    eclock_khz = ReadEClock(&e) / 1000;
    return e.ev_lo;
}

static void settle(void)
{
    if (wait_gpu) { OGPU_Wait(OGPU_FENCE_ALL); waits++; }
}

static void plot(LONG x, LONG y, int what)
{
    UBYTE *p = mem + y * W + x;
    if (what == ORTG_LINE_XOR) *p ^= 0xFF;
    else *p = what == ORTG_LINE_BG ? 2 : 1;
}

/* screens.c's pieces_then over the box (layer coordinates), fn per piece in layer coordinates */
typedef void (*piece_fn)(void *ctx, LONG x0, LONG y0, LONG x1, LONG y1);
static void pieces(struct Layer *l, LONG x0, LONG y0, LONG x1, LONG y1, piece_fn fn, void *ctx)
{
    LONG ox = l->bounds.MinX - l->Scroll_X, oy = l->bounds.MinY - l->Scroll_Y;
    struct ClipRect *cr;
    ObtainSemaphore(&l->Lock);
    for (cr = l->ClipRect; cr; cr = cr->Next) {
        LONG a0 = x0 + ox > cr->bounds.MinX ? x0 + ox : cr->bounds.MinX, b0 = y0 + oy > cr->bounds.MinY ? y0 + oy : cr->bounds.MinY;
        LONG a1 = x1 + ox < cr->bounds.MaxX ? x1 + ox : cr->bounds.MaxX, b1 = y1 + oy < cr->bounds.MaxY ? y1 + oy : cr->bounds.MaxY;
        if (a0 > a1 || b0 > b1) continue;
        if (!cr->obscured || cr->BitMap) fn(ctx, a0 - ox, b0 - oy, a1 - ox, b1 - oy);
    }
    ReleaseSemaphore(&l->Lock);
}

/* before: a pixel a time */
static void dot_piece(void *c, LONG x0, LONG y0, LONG x1, LONG y1)
{
    settle();
    plot(x0, y0, *(int *)c);
}

static void line_dots(struct Layer *ly, struct ortg_line *l)
{
    for (;;) {
        int w = ortg_line_what(l);
        if (w != ORTG_LINE_NONE && l->x >= 0 && l->y >= 0 && l->x < W && l->y < H) pieces(ly, l->x, l->y, l->x, l->y, dot_piece, &w);
        if (l->i == l->len) break;
        ortg_line_next(l);
    }
}

/* after: a line a time */
struct run { struct ortg_line l; int settled; };
static void run_piece(void *c, LONG x0, LONG y0, LONG x1, LONG y1)
{
    struct run *r = c;
    struct ortg_line l = r->l;
    if (x1 >= W) x1 = W - 1;
    if (y1 >= H) y1 = H - 1;
    for (;;) {
        int w = ortg_line_what(&l);
        if (w != ORTG_LINE_NONE && l.x >= x0 && l.x <= x1 && l.y >= y0 && l.y <= y1) {
            if (!r->settled) { settle(); r->settled = 1; }
            plot(l.x, l.y, w);
        }
        if (l.i == l.len) break;
        ortg_line_next(&l);
    }
}

static void line_run(struct Layer *ly, struct ortg_line *l, LONG x0, LONG y0, LONG x1, LONG y1)
{
    struct run r;
    r.l = *l; r.settled = 0;
    pieces(ly, x0 < x1 ? x0 : x1, y0 < y1 ? y0 : y1, x0 < x1 ? x1 : x0, y0 < y1 ? y1 : y0, run_piece, &r);
}

static ULONG lines(struct Layer *ly, int batched, UBYTE mode, UWORD ptrn)
{
    ULONG t0 = now_ticks(), t;
    BYTE cnt = 15;
    int i;
    for (i = 0; i < 1000; i++) {
        struct ortg_line l;
        LONG x0 = (i * 37) % (W / 2), y0 = (i * 13) % (H / 3);
        LONG x1 = x0 + W / 2 - 1 - (i % 50), y1 = y0 + H / 2 + (i % 40);
        ortg_line_init(&l, x0, y0, x1, y1, ptrn, cnt, mode, ORTG_LINE_FIRST);
        if (batched) line_run(ly, &l, x0, y0, x1, y1);
        else line_dots(ly, &l);
        cnt = ortg_line_cnt_after(&l);
    }
    t = now_ticks() - t0;
    return t / eclock_khz * 10 + t % eclock_khz * 10 / eclock_khz;   /* tenths of a ms */
}

int main(int argc, char **argv)
{
    static const struct { const char *name; UBYTE mode; UWORD ptrn; } kinds[] = {
        { "JAM1 solid", 0, 0xFFFF }, { "COMPLEMENT", ORTG_LINE_COMPLEMENT, 0xFFFF }, { "JAM2 F0F0", ORTG_LINE_JAM2, 0xF0F0 },
    };
    struct Screen *wb;
    struct Window *win, *cover;
    UBYTE *before, *after;
    int k, fails = 0;

    if (OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_ECLOCK, &treq.tr_node, 0)) { printf("OpenRTGLineTime: no timer.device\n"); return 20; }
    TimerBase = treq.tr_node.io_Device;
    OpenGPUBase = OpenLibrary((CONST_STRPTR)OPENGPU_NAME, 0);
    wait_gpu = OpenGPUBase && OpenGPUBase->lib_Revision >= OGPU_FENCE_ALL_REVISION && !(argc > 1 && !strcmp(argv[1], "NOWAIT"));
    wb = LockPubScreen(NULL);
    win = wb ? OpenWindowTags(NULL, WA_PubScreen, (ULONG)wb, WA_Left, 40, WA_Top, 40, WA_Width, W, WA_Height, H, WA_Borderless, TRUE,
                              WA_SmartRefresh, TRUE, WA_Activate, FALSE, TAG_DONE) : NULL;
    cover = win ? OpenWindowTags(NULL, WA_PubScreen, (ULONG)wb, WA_Left, 40 + 120, WA_Top, 40 + 70, WA_Width, 90, WA_Height, 60,
                                 WA_Borderless, TRUE, WA_SmartRefresh, TRUE, WA_Activate, FALSE, TAG_DONE) : NULL;
    mem = AllocVec(W * H, MEMF_ANY | MEMF_CLEAR);
    before = AllocVec(W * H, MEMF_ANY);
    after = AllocVec(W * H, MEMF_ANY);
    if (!win || !cover || !mem || !before || !after) { printf("OpenRTGLineTime: no windows or memory\n"); return 20; }
    printf("OpenRTGLineTime: a %dx%d smart refresh window, partly covered; GFOpenGPU wait each settle: %s\n", W, H,
           wait_gpu ? "yes (OGPU_Wait(OGPU_FENCE_ALL))" : "no");
    for (k = 0; k < 3; k++) {
        ULONG tb, ta, wb_, wa;
        memset(mem, 0, W * H); waits = 0;
        tb = lines(win->RPort->Layer, 0, kinds[k].mode, kinds[k].ptrn);
        wb_ = waits;
        memcpy(before, mem, W * H);
        memset(mem, 0, W * H); waits = 0;
        ta = lines(win->RPort->Layer, 1, kinds[k].mode, kinds[k].ptrn);
        wa = waits;
        memcpy(after, mem, W * H);
        if (memcmp(before, after, W * H)) fails++;
        printf("TIME 1000 slanted lines, %-10s: before %6lu.%lu ms (%lu waits), after %6lu.%lu ms (%lu waits): pixels %s\n", kinds[k].name,
               (unsigned long)(tb / 10), (unsigned long)(tb % 10), (unsigned long)wb_, (unsigned long)(ta / 10), (unsigned long)(ta % 10),
               (unsigned long)wa, memcmp(before, after, W * H) ? "DIFFERENT" : "the same");
    }
    CloseWindow(cover); CloseWindow(win);
    UnlockPubScreen(NULL, wb);
    FreeVec(mem); FreeVec(before); FreeVec(after);
    if (OpenGPUBase) CloseLibrary(OpenGPUBase);
    CloseDevice(&treq.tr_node);
    return fails ? 10 : 0;
}
