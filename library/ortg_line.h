/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * One line as graphics.library's Draw and PolyDraw draw it: which pixels,
 * in which order, and what each one does. Pure C, so the host checks it
 * (tests/test_lines.c); library/screens.c draws with it (0.25).
 *
 * The reference is OpenGfx's core (openamigagfx library/core/gfx_rp.c,
 * line() and PolyDraw at 6c8b6d9), which OpenGfxCheck compares byte for
 * byte with OS 3.2's graphics.library on planar bitmaps, every DrawMode,
 * LinePtrn, linpatcnt and FRST_DOT:
 *   - Bresenham along the longer axis from the pen position; pixel i takes
 *     LinePtrn's bit (linpatcnt - i) & 15, and linpatcnt goes down by the
 *     line's length (a BYTE, not kept to 0-15);
 *   - with FRST_DOT clear the first pixel is left out (it is the last one
 *     of the line before), so a COMPLEMENT line drawn on from another does
 *     not invert their corner twice; Draw clears FRST_DOT, Move sets it
 *     (and linpatcnt to 15);
 *   - INVERSVID inverts the pattern, not the pens; JAM2 draws BgPen where
 *     the bit is 0; COMPLEMENT inverts where it is 1, and every pixel with
 *     JAM2;
 *   - PolyDraw's first line keeps to FRST_DOT; every later one draws its
 *     first pixel, but in COMPLEMENT without JAM2 a corner ends up inverted
 *     once whatever LinePtrn holds there: the later line's first pixel
 *     inverts it only where LinePtrn's bit is 0 (ORTG_LINE_JOINT).
 * The pixels are the ones OpenRTG's line() drew before 0.25 (the same
 * Bresenham, checked on the host for every line within 40 pixels). */
#ifndef ORTG_LINE_H
#define ORTG_LINE_H

#include <exec/types.h>

/* what a pixel does */
#define ORTG_LINE_NONE  0       /* left alone */
#define ORTG_LINE_FG    1       /* FgPen */
#define ORTG_LINE_BG    2       /* BgPen: JAM2 where the bit is 0 */
#define ORTG_LINE_XOR   3       /* inverted */

/* what the first pixel does */
#define ORTG_LINE_SKIP  0       /* left out: FRST_DOT clear */
#define ORTG_LINE_FIRST 1       /* drawn as its bit says */
#define ORTG_LINE_JOINT 2       /* a later PolyDraw line in COMPLEMENT without JAM2 */

/* graphics/rastport.h's DrawMode bits */
#define ORTG_LINE_JAM2       1
#define ORTG_LINE_COMPLEMENT 2
#define ORTG_LINE_INVERSVID  4

struct ortg_line {
    LONG x, y;                  /* pixel i */
    LONG i, len;                /* pixels 0 to len */
    LONG dx, dy, sx, sy, mn, err;
    ULONG ptrn;                 /* LinePtrn as the mode reads it */
    UWORD raw;                  /* LinePtrn as it is (ORTG_LINE_JOINT) */
    LONG cnt;                   /* linpatcnt at pixel 0 */
    UBYTE xor, jam2, first;
};

static inline void ortg_line_init(struct ortg_line *l, LONG x0, LONG y0, LONG x1, LONG y1,
                                  UWORD lineptrn, BYTE linpatcnt, UBYTE drawmode, int first)
{
    l->dx = x1 > x0 ? x1 - x0 : x0 - x1;
    l->dy = y1 > y0 ? y1 - y0 : y0 - y1;
    l->sx = x1 >= x0 ? 1 : -1;
    l->sy = y1 >= y0 ? 1 : -1;
    l->len = l->dx > l->dy ? l->dx : l->dy;
    l->mn = l->dx > l->dy ? l->dy : l->dx;
    l->err = 2 * l->mn - l->len;
    l->x = x0; l->y = y0; l->i = 0;
    l->raw = lineptrn;
    l->ptrn = drawmode & ORTG_LINE_INVERSVID ? ~(ULONG)lineptrn & 0xFFFF : lineptrn;
    l->cnt = linpatcnt;
    l->xor = (drawmode & ORTG_LINE_COMPLEMENT) != 0;
    l->jam2 = (drawmode & ORTG_LINE_JAM2) != 0;
    if (l->xor && l->jam2) l->ptrn = 0xFFFF;
    l->first = (UBYTE)first;
}

/* What the first pixel of a call's line k does: Draw's only line and
 * PolyDraw's first (k 0) keep to FRST_DOT (frst_dot, the RastPort's flag). */
static inline int ortg_line_first_of(UBYTE drawmode, int frst_dot, LONG k)
{
    if (!k) return frst_dot ? ORTG_LINE_FIRST : ORTG_LINE_SKIP;
    return (drawmode & (ORTG_LINE_COMPLEMENT | ORTG_LINE_JAM2)) == ORTG_LINE_COMPLEMENT ? ORTG_LINE_JOINT : ORTG_LINE_FIRST;
}

/* What pixel l->i does. */
static inline int ortg_line_what(const struct ortg_line *l)
{
    ULONG on = l->ptrn >> ((l->cnt - l->i) & 15) & 1;
    if (!l->i) {
        if (l->first == ORTG_LINE_SKIP) return ORTG_LINE_NONE;
        if (l->first == ORTG_LINE_JOINT) on = !(l->raw >> (l->cnt & 15) & 1);
    }
    if (l->xor) return on ? ORTG_LINE_XOR : ORTG_LINE_NONE;
    return on ? ORTG_LINE_FG : l->jam2 ? ORTG_LINE_BG : ORTG_LINE_NONE;
}

/* On to pixel i + 1 (while i < len). */
static inline void ortg_line_next(struct ortg_line *l)
{
    if (l->err >= 0) {
        if (l->dx > l->dy) l->y += l->sy; else l->x += l->sx;
        l->err -= 2 * l->len;
    }
    l->err += 2 * l->mn;
    if (l->dx > l->dy) l->x += l->sx; else l->y += l->sy;
    l->i++;
}

/* linpatcnt after the line. */
static inline BYTE ortg_line_cnt_after(const struct ortg_line *l) { return (BYTE)(l->cnt - l->len); }

/* What every pixel after the first does when they all do the same (any
 * LinePtrn phase), else -1. */
static inline int ortg_line_rest(const struct ortg_line *l)
{
    if (l->ptrn == 0xFFFF) return l->xor ? ORTG_LINE_XOR : ORTG_LINE_FG;
    if (l->ptrn == 0) return l->xor || !l->jam2 ? ORTG_LINE_NONE : ORTG_LINE_BG;
    return -1;
}

#endif
