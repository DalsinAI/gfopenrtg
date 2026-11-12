/*
  SDL 2 for AmigaOS 3 in GFOpenGPU: the ARGB8888 blend blit as a hostlib call.
  Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
  SPDX-License-Identifier: MIT

  SDL's own blit for ARGB8888 onto (A)RGB8888 with SDL_BLENDMODE_BLEND
  (BlitRGBtoRGBPixelAlpha, SDL_blit_A.c) is hostlib's SDL2_BLEND_ARGB
  ($0001010A, library/hostlib/sdl/blend_argb.c), pixel for pixel: an opaque
  source pixel copied, a clear one skipped, R and B blended together and G
  alone with >> 8, the alpha sA + (dA * (255 - sA) >> 8). On AmiCradle's
  AC090, when the runtime runs that id as host code, the blit goes to it;
  everywhere else (another 68k, an older runtime, AC_HOSTLIB=0) SDL's own
  loop runs, as before.

  The runtime is asked once, by the probe (HOSTLIB_PROBE, $00011000), whose
  68k body answers "no" on any other machine. The destination is the call's
  state, and a call's state is 64 KB at most (HOSTLIB-ABI.md 8.1), so the
  blit goes in strips of rows whose destination span fits; a row wider than
  that, overlapping rectangles, and a call the host gives back are SDL's own
  loop's (the 68k body would give the same pixels, only slower).
*/
#include "../../SDL_internal.h"
#include "SDL_hints.h"
#include "SDL_os3blend.h"

#define HL_VENDORED 1
#include "hostlib/sdl/blend_argb.c"
#include "hostlib/probe/probe.c"

static int os3_blend_host = -1;     /* -1 not asked yet, 0 SDL's loop, 1 the host's */

static int os3_blend_ask(void)
{
    Uint32 list[2], blk[HL_PB_LONGS];
    Uint8 answer[4] = { 0, 0, 0, 0 };
    list[0] = HL_ID_SDL2_BLEND_ARGB;
    list[1] = 1;                    /* the contract version this build has */
    blk[HL_PB_N] = 1;
    blk[HL_PB_LIST] = (Uint32)list;
    blk[HL_PB_ANSWERS] = (Uint32)answer;
    blk[HL_PB_FEATURES] = 0;
    return hostlib_probe((hl_addr)blk) && (answer[0] & HL_PROBE_HOST) ? 1 : 0;
}

int SDL_OS3_BlendARGB(const void *src, int srcpitch, void *dst, int dstpitch, int w, int h)
{
    Uint32 blk[HL_SBL_LONGS];
    int rows, done = 0;
    const char *hint = SDL_GetHint("SDL_OS3_HOSTLIB_BLEND");   /* tests/sdl2/blendcheck.c: "0" SDL's loop, "force" the call even off AmiCradle */
    if (w <= 0 || h <= 0 || (hint && *hint == '0')) return 0;
    if (!(hint && !SDL_strcmp(hint, "force"))) {
        if (os3_blend_host < 0) os3_blend_host = os3_blend_ask();
        if (!os3_blend_host) return 0;
    }
    if (srcpitch < w * 4 || dstpitch < w * 4 || w * 4 > (int)HL_STATE_MAX) return 0;
    /* the rows a strip may have: (rows - 1) * dstpitch + w * 4 <= 64 KB */
    rows = 1 + (int)((HL_STATE_MAX - (Uint32)w * 4u) / (Uint32)dstpitch);
    while (h > 0) {
        int n = h < rows ? h : rows;
        blk[HL_SBL_SRC] = (Uint32)src;
        blk[HL_SBL_DST] = (Uint32)dst;
        blk[HL_SBL_SSTRIDE] = (Uint32)srcpitch;
        blk[HL_SBL_DSTRIDE] = (Uint32)dstpitch;
        blk[HL_SBL_WIDTH] = (Uint32)w;
        blk[HL_SBL_HEIGHT] = (Uint32)n;
        if (hostlib_sdl2_blend_argb((hl_addr)blk) != (uint32_t)w * (uint32_t)n)
            break;      /* overlap or a refused block: this strip is untouched, SDL's loop does it and the rest */
        src = (const Uint8 *)src + (size_t)n * srcpitch;
        dst = (Uint8 *)dst + (size_t)n * dstpitch;
        h -= n;
        done += n;
    }
    return done;
}
