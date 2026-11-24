/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * ORTG_Present's plan (library/present_plan.h): the route for each kind of
 * call, and the prepared present's command word for word as MiniGL's
 * (library/minigl/mgl_vg_os3.c, vg_os_present) and Warp3D's
 * (library/warp3d/w3d_vg.c, present_cmd) own presents wrote it, so moving
 * them onto ORTG_Present changes none of their stream. */
#include <stdio.h>
#include "../library/present_plan.h"

static int failures, checks;
static void check(int ok, const char *what)
{
    checks++;
    if (!ok) { failures++; printf("FAIL: %s\n", what); }
}

/* virgl.h's request block command: header (command, words), result 0, the arguments. */
static void blk_cmd(unsigned long *p, unsigned long cmd, unsigned long n, const unsigned long *a)
{
    unsigned long i;
    p[0] = cmd << 16 | (n + 2);
    p[1] = 0;
    for (i = 0; i < n; i++) p[2 + i] = a[i];
}

static int same(const unsigned long *a, const unsigned long *b, int n)
{
    int i;
    for (i = 0; i < n; i++) if (a[i] != b[i]) return 0;
    return 1;
}

int main(void)
{
    unsigned long want[PRS_CMD_WORDS], got[PRS_CMD_WORDS], a[12];
    struct prs_call c;
    long x0, y0, x1, y1;
    short part[4];

    /* routes */
    c.kind = 1; c.prepare = 1; c.host_present = 1; c.rtg = 1; c.layered = 0; c.boxed = 0; c.screens = 1; c.foreign_deep = 0;
    check(prs_route(&c) == PRS_IN_BLOCK, "a screen or bitmap, prepared: in the caller's block");
    c.layered = 1;
    check(prs_route(&c) == PRS_VIRGL, "a window, prepared: GFOpenRTG's own present after the block");
    c.layered = 0; c.boxed = 1;
    check(prs_route(&c) == PRS_VIRGL, "a box to stretch into: GFOpenRTG's own present");
    c.boxed = 0; c.prepare = 0;
    check(prs_route(&c) == PRS_VIRGL, "not prepared: GFOpenRTG's own present");
    c.rtg = 0; c.prepare = 1;
    check(prs_route(&c) == PRS_READBACK, "a planar target: read back");
    c.rtg = 1; c.host_present = 0;
    check(prs_route(&c) == PRS_READBACK, "no ACV_PRESENT on the host: read back");
    c.kind = 2; c.host_present = 1;
    check(prs_route(&c) == PRS_MEMORY, "a frame in memory: the CPU");
    c.kind = 3;
    check(prs_route(&c) == PRS_MEDIA, "an ACMedia frame held on the host: MEDIA, prepared or not");
    c.prepare = 0; c.host_present = 0; c.rtg = 0;
    check(prs_route(&c) == PRS_MEDIA, "MEDIA needs no virgl: MEDIA on any target");
    c.kind = 4; c.prepare = 1; c.host_present = 1; c.rtg = 1;
    check(prs_route(&c) == PRS_RETAINED, "a retained surface, GFOpenRTG's screens on: GFOpenRTG presents it");
    c.screens = 0;
    check(prs_route(&c) == PRS_NONE, "a retained surface, GFOpenRTG's screens off: -1, gfopengpu.library's own present");
    c.screens = 1; c.foreign_deep = 1;
    check(prs_route(&c) == PRS_NONE, "a retained surface into another RTG system's deep bitmap: -1, cybergraphics.library's way");
    c.foreign_deep = 0; c.kind = 5;
    check(prs_route(&c) == PRS_NONE, "a kind not handled yet: none");

    /* MiniGL's present on a screen: the frame at x, y, its whole size, XRGB32 (OGPU_FMT_XRGB32 = 6 here: any value) */
    a[0] = 0x1234; a[1] = 77; a[2] = 0;
    a[3] = ((unsigned long)(unsigned short)40 << 16) | (unsigned short)24;
    a[4] = 0x40000000UL; a[5] = 2560; a[6] = 6;
    a[7] = (640UL << 16) | 480; a[8] = 0; a[9] = 1;
    a[10] = ((unsigned long)(unsigned short)40 << 16) | (unsigned short)24;
    a[11] = (320UL << 16) | 240;
    blk_cmd(want, PRS_ACV_PRESENT, 12, a);
    check(prs_cmd(got, 0x1234, 77, 40, 24, 0x40000000UL, 2560, 6, 640, 480, 0, 40, 24, 320, 240) == PRS_CMD_WORDS
          && same(want, got, PRS_CMD_WORDS), "MiniGL's screen present, word for word");

    /* Warp3D's present into its drawing area: origin 0, the area's size, ARGB32 or RGB565 */
    a[0] = 0x5678; a[1] = 9; a[2] = 0; a[3] = 0;
    a[4] = 0x41000000UL; a[5] = 1280; a[6] = 3;
    a[7] = (320UL << 16) | 200; a[8] = 0; a[9] = 1;
    a[10] = 0; a[11] = (320UL << 16) | 200;
    blk_cmd(want, PRS_ACV_PRESENT, 12, a);
    check(prs_cmd(got, 0x5678, 9, 0, 0, 0x41000000UL, 1280, 3, 320, 200, 0, 0, 0, 320, 200) == PRS_CMD_WORDS
          && same(want, got, PRS_CMD_WORDS), "Warp3D's drawing-area present, word for word");

    /* a negative origin keeps its 16 bits, as OGPU_XY does */
    prs_cmd(got, 1, 2, -8, -3, 0, 0, 0, 1, 1, PRS_FLIP_Y, -8, -3, 1, 1);
    check(got[5] == 0xFFF8FFFDUL && got[12] == 0xFFF8FFFDUL && got[10] == PRS_FLIP_Y, "a negative origin, and the flip");

    /* the source's part */
    check(prs_part(NULL, 320, 240, &x0, &y0, &x1, &y1) && x0 == 0 && y0 == 0 && x1 == 319 && y1 == 239, "all of it");
    part[0] = -5; part[1] = 10; part[2] = 400; part[3] = 20;
    check(prs_part(part, 320, 240, &x0, &y0, &x1, &y1) && x0 == 0 && y0 == 10 && x1 == 319 && y1 == 20, "a part, clamped");
    part[0] = 330; part[1] = 0; part[2] = 340; part[3] = 5;
    check(!prs_part(part, 320, 240, &x0, &y0, &x1, &y1), "a part outside: nothing");

    printf("test_present_plan: %d checks, %d failed\n", checks, failures);
    return failures != 0;
}
