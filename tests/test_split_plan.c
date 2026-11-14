/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * library/split_plan.h, what screens.c's show_front asks the board for
 * when a screen is dragged (0.27, acrtg-v10's split).
 */
#include <stdio.h>
#include "../library/split_plan.h"

static int fails, cases;
#define CHECK(name, cond) do { cases++; if (!(cond)) { fails++; printf("FAIL %s\n", name); } } while (0)

int main(void)
{
    const struct spl_pic wb = { 2, 4, 1920, 1080, 0x10000, 7680 }, behind = { 2, 4, 1920, 1080, 0x800000, 7680 };
    const struct spl_pic small = { 2, 4, 1920, 50, 0x900000, 7680 }, narrow = { 2, 4, 1280, 1080, 0xA00000, 5120 };
    const struct spl_pic r16 = { 1, 2, 1920, 1080, 0xB00000, 3840 }, clut = { 0, 1, 1920, 1080, 0xC00000, 1920 };
    struct spl_split s;
    s = spl_plan(&wb, 0, 1080, &behind, 0x5000);
    CHECK("at its place: no split", s.y == 0 && s.at == 0 && s.row == 0);
    s = spl_plan(&wb, -20, 1080, &behind, 0x5000);
    CHECK("above the top (a tall screen scrolled): no split", s.y == 0);
    s = spl_plan(&wb, 200, 1080, &behind, 0x5000);
    CHECK("dragged 200 down: the screen behind above, its own rows", s.y == 200 && s.at == 0x800000 && s.row == 7680);
    s = spl_plan(&wb, 5000, 1080, &behind, 0x5000);
    CHECK("dragged past the bottom: one row of the front still shows", s.y == 1079);
    s = spl_plan(&wb, 200, 1080, 0, 0x5000);
    CHECK("nothing behind: the blank row on each row above", s.y == 200 && s.at == 0x5000 && s.row == 0);
    s = spl_plan(&wb, 200, 1080, &small, 0x5000);
    CHECK("a screen behind shorter than the split: the blank", s.at == 0x5000 && s.row == 0);
    s = spl_plan(&wb, 40, 1080, &small, 0x5000);
    CHECK("... and one tall enough for it: that screen", s.at == 0x900000 && s.row == 7680);
    s = spl_plan(&wb, 200, 1080, &narrow, 0x5000);
    CHECK("a narrower screen behind: the blank", s.at == 0x5000 && s.row == 0);
    s = spl_plan(&wb, 200, 1080, &r16, 0x5000);
    CHECK("a screen behind in another format: the blank", s.at == 0x5000 && s.row == 0);
    s = spl_plan(&wb, 200, 1080, &r16, ~0UL);
    CHECK("no blank to be had either: no split", s.y == 0);
    s = spl_plan(&r16, 100, 1080, &r16, 0x5000);
    CHECK("16-bit in front of 16-bit: split", s.y == 100 && s.at == 0xB00000 && s.row == 3840);
    s = spl_plan(&clut, 100, 1080, &clut, 0x5000);
    CHECK("CLUT8 never splits (one palette a head)", s.y == 0);
    s = spl_plan(0, 100, 1080, &behind, 0x5000);
    CHECK("no front: no split", s.y == 0);
    printf("%d/%d split_plan cases pass\n", cases - fails, cases);
    return fails != 0;
}
