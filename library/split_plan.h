/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * A dragged screen's split (0.27, row 11 C3; acrtg-v10's SPLIT_Y, UNDER_AT
 * and UNDER_ROW): what a head shows above a front screen intuition has
 * moved down, plain C so the host tests build the same code
 * (tests/test_split_plan.c).
 *
 * The board shows the head's first y rows from the picture at `at` (`row`
 * bytes a row, 0: the one row at `at` on each) and the rest from the front
 * screen's first rows, as the chipset's copper shows the screen behind a
 * dragged one. Above the split is the screen behind on the same head, from
 * its top, when it is in the front's format, at least as wide and at least
 * y rows tall; otherwise a blank row. A CLUT8 front never splits: the
 * head has one palette. y is the front's drag offset, at most the head's
 * height less one row; 0 is no split, as before 0.27.
 */
#ifndef ORTG_SPLIT_PLAN_H
#define ORTG_SPLIT_PLAN_H

/* A picture in a board's video RAM: format (ORTG_ format), size, where. */
struct spl_pic { unsigned char format, bpp; unsigned long width, height, off, stride; };
struct spl_split { unsigned long y, at, row; };

/* front at dy rows down on a head h rows tall; behind, the next screen on
 * that head or 0; blank, the offset of a blank row of at least the front's
 * width (~0UL: none to be had) */
static inline struct spl_split spl_plan(const struct spl_pic *front, long dy, unsigned long h, const struct spl_pic *behind,
                                        unsigned long blank)
{
    struct spl_split s = { 0, 0, 0 };
    if (!front || dy <= 0 || !h || front->bpp < 2) return s;
    s.y = (unsigned long)dy < h ? (unsigned long)dy : h - 1;
    if (!s.y) return s;
    if (behind && behind->format == front->format && behind->width >= front->width && behind->height >= s.y && behind->stride) {
        s.at = behind->off;
        s.row = behind->stride;
    } else if (blank != ~0UL) {
        s.at = blank;
        s.row = 0;
    } else
        s.y = 0;
    return s;
}

#endif
