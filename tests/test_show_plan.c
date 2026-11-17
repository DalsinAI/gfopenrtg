/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * library/show_plan.h: when ORTG_ShowInstead (0.28) may ask the board to
 * show a picture in a screen's place. */
#include <stdio.h>
#include "../library/show_plan.h"

static int cases, fails;
#define CHECK(what, ok) do { cases++; if (!(ok)) { fails++; printf("FAIL: %s\n", what); } } while (0)

int main(void)
{
    struct shw_pic wb = { 1, 2, 1920, 1080, 0x100000, 7680 };
    struct shw_pic p = { 1, 2, 1920, 1080, 0x900000, 7680 }, q;
    CHECK("a friend of the shown screen on a v11 board", shw_fits(&wb, &p, 11, 1));
    CHECK("a later board too", shw_fits(&wb, &p, 12, 1));
    CHECK("a board before v11 has no such register", !shw_fits(&wb, &p, 10, 1));
    CHECK("uaegfx (version 0) neither", !shw_fits(&wb, &p, 0, 1));
    CHECK("a screen its head does not show now", !shw_fits(&wb, &p, 11, 0));
    q = p; q.monitor = 2;
    CHECK("a picture in another board's video RAM", !shw_fits(&wb, &q, 11, 1));
    q = p; q.monitor = 0;
    CHECK("a picture in fast RAM", !shw_fits(&wb, &q, 11, 1));
    q = wb; q.monitor = 0;
    CHECK("a screen in fast RAM", !shw_fits(&q, &p, 11, 1));
    q = p; q.format = 1;
    CHECK("another format", !shw_fits(&wb, &q, 11, 1));
    q = p; q.width = 1280;
    CHECK("another width", !shw_fits(&wb, &q, 11, 1));
    q = p; q.height = 1200;
    CHECK("another height", !shw_fits(&wb, &q, 11, 1));
    q = p; q.stride = 8192;
    CHECK("other bytes a row", !shw_fits(&wb, &q, 11, 1));
    q = p; q.stride = 0;
    CHECK("no bytes a row", !shw_fits(&wb, &q, 11, 1));
    q = p; q.off = 0x900002;
    CHECK("a picture not on a long", !shw_fits(&wb, &q, 11, 1));
    CHECK("the screen itself is not a picture in its place", !shw_fits(&wb, &wb, 11, 1));
    CHECK("no screen, no picture", !shw_fits(0, &p, 11, 1) && !shw_fits(&wb, 0, 11, 1));
    printf("%d/%d show_plan cases pass\n", cases - fails, cases);
    return fails != 0;
}
