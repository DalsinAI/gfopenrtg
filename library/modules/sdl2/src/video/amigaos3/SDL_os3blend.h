/*
  SDL 2 for AmigaOS 3 in GFOpenGPU: the ARGB8888 blend blit as a hostlib call.
  Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
  SPDX-License-Identifier: MIT
*/
#ifndef SDL_os3blend_h_
#define SDL_os3blend_h_

/* BlitRGBtoRGBPixelAlpha's rows from the top, w pixels each, by hostlib's
   SDL2_BLEND_ARGB where the runtime runs it as host code: the rows it
   blended (0 to h); SDL's own loop does the rest. */
extern int SDL_OS3_BlendARGB(const void *src, int srcpitch, void *dst, int dstpitch, int w, int h);

#endif
