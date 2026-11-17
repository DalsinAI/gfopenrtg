/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * A picture shown in a screen's place (0.28; acrtg-v11's SHOW_AT, SHOW_FENCE
 * and SHOWN): whether the board can show one bitmap where it shows another,
 * plain C so the host tests build the same code (tests/test_show_plan.c).
 *
 * The board changes only where the head's picture starts: the mode, the
 * rows' bytes and the palette stay the screen's. So the picture is in the
 * same board's video RAM as the screen, in its format, of its size and its
 * bytes a row, the screen is the one that head shows now, and the board is
 * acrtg-v11 or later (never the uaegfx backend, which has no such thing).
 */
#ifndef ORTG_SHOW_PLAN_H
#define ORTG_SHOW_PLAN_H

/* A bitmap: the board whose video RAM it is in (0: none), format, size, where. */
struct shw_pic { unsigned char monitor, format; unsigned long width, height, off, stride; };

#define SHW_BOARD_VERSION 11UL

/* 1: the board may show pic where it shows screen. version: the board's
 * (0 for uaegfx); front: screen is what its head shows now. */
static inline int shw_fits(const struct shw_pic *screen, const struct shw_pic *pic, unsigned long version, int front)
{
    if (!screen || !pic || !front || version < SHW_BOARD_VERSION) return 0;
    if (!screen->monitor || pic->monitor != screen->monitor) return 0;
    if (pic->format != screen->format || pic->width != screen->width || pic->height != screen->height) return 0;
    if (!pic->stride || pic->stride != screen->stride || (pic->off & 3)) return 0;
    return pic->off != screen->off;
}

#endif
