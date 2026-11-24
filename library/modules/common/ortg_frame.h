/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * A software frame (pixels in memory) shown through ORTG_Present, kind
 * MEMORY (gfopenrtg.library 0.30): SDL2's and SDL3's window presents, GF-66
 * M8 step 2.5 (docs/design/OpenGPU-Shared-Pipe-Plan.md). GFOpenRTG then
 * writes it through the window's layer itself: the visible parts with
 * GFOpenGPU's PIXELS, the covered parts of a smart refresh window into the
 * layer's backing store, a planar screen with the CPU.
 *
 * Plain C, so tests/test_ortg_frame.c builds it on the PC with a fake
 * ORTG_Present. The old present (the module's own GFOpenGPU copy) stays
 * for one release behind ENV:SDL2/PresentOld and ENV:SDL3/PresentOld. */
#ifndef ORTG_FRAME_H
#define ORTG_FRAME_H

#include <graphics/gfx.h>               /* struct Rectangle */
#include <gfopenrtg/gfopenrtg.h>

/* The source's pixels as they are in memory. */
#define ORF_SRC_ARGB32  1               /* A, R, G, B bytes (SDL's ARGB8888 on the Amiga) */
#define ORF_SRC_XRGB32  2               /* x, R, G, B bytes (SDL2's RGB888, SDL3's XRGB8888) */
#define ORF_SRC_RGB565  3               /* big-endian RGB565 words */
#define ORF_NONE        0xFFFFFFFFUL

/* The new route is taken: gfopenrtg.library 0.30 or later, and the
 * variable that keeps the old one is not 1. */
static inline int orf_wanted(unsigned version, unsigned revision, int present_old)
{
    return !present_old && (version > 0 || revision >= 30);
}

/* The ORTG_PIX_ format of a source, or ORF_NONE where ORTG_Present would put
 * it on the CPU while the old present has GFOpenGPU for it: ARGB and 0RGB
 * take PIXELS on 16 and 32-bit; RGB565 only into a target of that very
 * format (16-bit RGB565, the bitmap's own pixels: RAW). Every other source
 * stays on the old present. */
static inline unsigned long orf_format(int src, int target_rgb565)
{
    switch (src) {
    case ORF_SRC_ARGB32: return ORTG_PIX_ARGB;
    case ORF_SRC_XRGB32: return ORTG_PIX_0RGB;
    case ORF_SRC_RGB565: return target_rgb565 ? ORTG_PIX_RAW : ORF_NONE;
    default: return ORF_NONE;
    }
}

/* ORTG_Present itself (its base is the caller's), as tests/test_ortg_frame.c fakes it. */
typedef long (*orf_call)(struct ORTG_PresentInfo *in, void *ctx);

/* SDL_Rect's layout. */
struct orf_rect { int x, y, w, h; };

/* The frame's rects (in its own pixels, already inside w x h) through
 * ORTG_Present, one call each: pixel 0, 0 of the frame at x, y of rp. vsync:
 * the first call waits for the display's next frame. 1: all were shown; 0:
 * one was refused (-1), and the caller shows the frame the old way (the
 * rects already written are written again, the same pixels). */
static inline int orf_present(orf_call call, void *ctx, struct RastPort *rp, long x, long y,
                              void *pixels, unsigned long pitch, unsigned long format, int w, int h,
                              const struct orf_rect *rects, int n, int vsync)
{
    struct ORTG_PresentInfo in;
    struct Rectangle src;
    int i;
    unsigned b;

    if (!call || !rp || !pixels || format == ORF_NONE || w <= 0 || h <= 0 || w > 65535 || h > 65535) return 0;
    for (i = 0; i < n; i++) {
        const struct orf_rect *r = &rects[i];
        if (r->w <= 0 || r->h <= 0) continue;
        for (b = 0; b < sizeof in; b++) ((unsigned char *)&in)[b] = 0;
        in.size = sizeof in;
        in.kind = ORTG_SURF_MEMORY;
        in.memory = pixels;
        in.stride = pitch;
        in.format = format;
        in.width = (unsigned short)w;
        in.height = (unsigned short)h;
        in.rp = rp;
        in.x = x;
        in.y = y;
        src.MinX = (WORD)r->x;
        src.MinY = (WORD)r->y;
        src.MaxX = (WORD)(r->x + r->w - 1);
        src.MaxY = (WORD)(r->y + r->h - 1);
        in.src = &src;
        if (vsync) {
            in.flags = ORTG_PRESENT_VSYNC;
            vsync = 0;
        }
        if (call(&in, ctx) < 0) return 0;
    }
    return 1;
}

#endif
