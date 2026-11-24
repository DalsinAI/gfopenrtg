/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * SDL2's and SDL3's window presents through ORTG_Present (library/modules/common/ortg_frame.h,
 * GF-66 M8 step 2.5) on the PC, with a fake ORTG_Present that writes the frame's part into a
 * screen the way GFOpenRTG's MEMORY route does (the source's part at x, y of the target).
 * Exit 0 when every check passes. */
#include <stdio.h>
#include <string.h>
#include <exec/types.h>
#include <graphics/gfx.h>
#include <gfopenrtg/gfopenrtg.h>
#include "ortg_frame.h"

#define SW 40
#define SH 30
#define FW 16
#define FH 12

static unsigned long screen[SH][SW];
static unsigned long frame[FH][FW + 4];         /* 4 longs of padding a row: the stride isn't the width */
static int calls, bad_info, vsyncs, vsync_not_first, refuse_at = -1, zero_at = -1;
static struct RastPort rastport;
static int fails;

static void check(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL %s\n", what);
        fails++;
    }
}

static long fake_present(struct ORTG_PresentInfo *in, void *ctx)
{
    long x, y, n = 0;
    (void)ctx;
    if (calls == zero_at) { calls++; return 0; }  /* GFOpenRTG's screens off: nothing written */
    if (calls++ == refuse_at) return -1;
    if (in->size != sizeof *in || in->kind != ORTG_SURF_MEMORY || in->memory != (void *)frame
        || in->stride != sizeof frame[0] || in->width != FW || in->height != FH || in->rp != &rastport
        || !in->src || in->dst || in->blk || in->context || in->handle) bad_info++;
    if (in->flags & ORTG_PRESENT_VSYNC) {
        vsyncs++;
        if (calls != 1) vsync_not_first++;
    }
    for (y = in->src->MinY; y <= in->src->MaxY; y++)
        for (x = in->src->MinX; x <= in->src->MaxX; x++) {
            const unsigned long *row = (const unsigned long *)((const unsigned char *)in->memory + y * in->stride);
            if (in->y + y >= 0 && in->y + y < SH && in->x + x >= 0 && in->x + x < SW) screen[in->y + y][in->x + x] = row[x];
            n++;
        }
    return n;
}

static void reset(void)
{
    int x, y;
    for (y = 0; y < SH; y++) for (x = 0; x < SW; x++) screen[y][x] = 0xDEAD;
    for (y = 0; y < FH; y++) for (x = 0; x < FW + 4; x++) frame[y][x] = (unsigned long)(y << 8 | x);
    calls = bad_info = vsyncs = vsync_not_first = 0;
    refuse_at = zero_at = -1;
}

/* The screen holds the frame's rects at ox, oy and nothing else. */
static int screen_is(const struct orf_rect *r, int n, int ox, int oy)
{
    int x, y, i;
    for (y = 0; y < SH; y++)
        for (x = 0; x < SW; x++) {
            int fx = x - ox, fy = y - oy, in = 0;
            for (i = 0; i < n; i++)
                if (fx >= r[i].x && fx < r[i].x + r[i].w && fy >= r[i].y && fy < r[i].y + r[i].h) in = 1;
            if (screen[y][x] != (in ? (unsigned long)(fy << 8 | fx) : 0xDEADUL)) return 0;
        }
    return 1;
}

int main(void)
{
    struct orf_rect all = { 0, 0, FW, FH };
    struct orf_rect two[3] = { { 1, 2, 3, 4 }, { 5, 5, 0, 7 }, { 10, 0, 6, 12 } };   /* the middle one is empty */

    /* the route: gfopenrtg.library 0.30 and later, and not ENV:SDLn/PresentOld 1 */
    check(!orf_wanted(0, 29, 0), "0.29 takes the old present");
    check(orf_wanted(0, 30, 0), "0.30 takes ORTG_Present");
    check(orf_wanted(1, 0, 0), "1.0 takes ORTG_Present");
    check(!orf_wanted(0, 30, 1), "PresentOld 1 keeps the old present");

    /* the formats: only those GFOpenRTG puts on GFOpenGPU */
    check(orf_format(ORF_SRC_ARGB32, 0) == ORTG_PIX_ARGB, "ARGB8888 is ORTG_PIX_ARGB");
    check(orf_format(ORF_SRC_XRGB32, 0) == ORTG_PIX_0RGB, "XRGB8888 is ORTG_PIX_0RGB");
    check(orf_format(ORF_SRC_RGB565, 1) == ORTG_PIX_RAW, "RGB565 onto an RGB565 screen is RAW");
    check(orf_format(ORF_SRC_RGB565, 0) == ORF_NONE, "RGB565 onto another screen stays on the old present");
    check(orf_format(0, 1) == ORF_NONE, "any other source stays on the old present");

    /* the whole frame inside a window's borders, with vsync */
    reset();
    check(orf_present(fake_present, NULL, &rastport, 4, 11, frame, sizeof frame[0], ORTG_PIX_ARGB, FW, FH, &all, 1, 1) == 1,
          "a whole frame is shown");
    check(calls == 1 && !bad_info, "one call, the info as GFOpenRTG reads it");
    check(vsyncs == 1, "vsync asked once");
    check(screen_is(&all, 1, 4, 11), "the frame lands at the inside's corner");

    /* damage: each rect a call, the empty one none, vsync on the first only */
    reset();
    check(orf_present(fake_present, NULL, &rastport, 0, 0, frame, sizeof frame[0], ORTG_PIX_0RGB, FW, FH, two, 3, 1) == 1,
          "rects are shown");
    check(calls == 2 && !bad_info, "a call each non-empty rect");
    check(vsyncs == 1 && !vsync_not_first, "vsync only on the first call");
    check(screen_is(two, 3, 0, 0), "only the rects are written");

    /* without vsync none asks for it */
    reset();
    orf_present(fake_present, NULL, &rastport, 0, 0, frame, sizeof frame[0], ORTG_PIX_ARGB, FW, FH, two, 3, 0);
    check(vsyncs == 0, "no vsync when not asked");

    /* a refusal: 0, the rest not tried, and the caller shows the frame the old way */
    reset();
    refuse_at = 0;
    check(orf_present(fake_present, NULL, &rastport, 0, 0, frame, sizeof frame[0], ORTG_PIX_ARGB, FW, FH, two, 3, 0) == 0,
          "a refused call is 0");
    check(calls == 1, "nothing tried after a refusal");
    reset();
    refuse_at = 1;
    check(orf_present(fake_present, NULL, &rastport, 0, 0, frame, sizeof frame[0], ORTG_PIX_ARGB, FW, FH, two, 3, 0) == 0,
          "a refusal after the first rect is 0");

    /* 0 pixels for a non-empty rect: refused too (GFOpenRTG's screens off) */
    reset();
    zero_at = 0;
    check(orf_present(fake_present, NULL, &rastport, 0, 0, frame, sizeof frame[0], ORTG_PIX_ARGB, FW, FH, &all, 1, 0) == 0,
          "0 pixels written is refused");

    /* nothing called for what ORTG_Present can't take */
    reset();
    check(orf_present(fake_present, NULL, &rastport, 0, 0, frame, sizeof frame[0], ORF_NONE, FW, FH, &all, 1, 0) == 0
          && calls == 0, "no format: not called");
    check(orf_present(fake_present, NULL, NULL, 0, 0, frame, sizeof frame[0], ORTG_PIX_ARGB, FW, FH, &all, 1, 0) == 0
          && calls == 0, "no RastPort: not called");
    check(orf_present(fake_present, NULL, &rastport, 0, 0, NULL, sizeof frame[0], ORTG_PIX_ARGB, FW, FH, &all, 1, 0) == 0
          && calls == 0, "no pixels: not called");
    check(orf_present(fake_present, NULL, &rastport, 0, 0, frame, sizeof frame[0], ORTG_PIX_ARGB, 0, FH, &all, 1, 0) == 0
          && calls == 0, "no width: not called");
    check(orf_present(fake_present, NULL, &rastport, 0, 0, frame, sizeof frame[0], ORTG_PIX_ARGB, FW, FH, &all, 0, 0) == 1
          && calls == 0, "no rects: shown, nothing called");

    if (fails) return 1;
    printf("test_ortg_frame: all passed\n");
    return 0;
}
