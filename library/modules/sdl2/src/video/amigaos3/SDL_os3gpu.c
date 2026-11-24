/*
  SDL 2 on AmigaOS 3.x: gfopengpu.library, shared by the opengpu renderer and
  the window present.
  Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
*/
#include "../../SDL_internal.h"

#if SDL_VIDEO_RENDER_OPENGPU

#include "SDL_hints.h"
#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/var.h>
#include <graphics/rastport.h>
#include <cybergraphx/cybergraphics.h>
#include <proto/cybergraphics.h>

#include "SDL_os3gpu.h"
#include "SDL_os3vram.h"
#include <intuition/intuition.h>

/* gfopenrtg.library's calls through SDL's own base (a program's
   OpenRTGBase is its own), as SDL_os3vram.c's go through a local one. */
#include <gfopenrtg/gfopenrtg.h>
#define OPENRTG_BASE_NAME SDL_OS3_ORTGBase
#include <inline/gfopenrtg.h>
#include "ortg_frame.h"

struct Library *SDL_OS3_OpenGPUBase = NULL;
static int SDL_OS3_GPU_opens = 0;
static struct Library *SDL_OS3_ORTGBase = NULL;
static int SDL_OS3_present_old = 0;  /* ENV:SDL2/PresentOld 1: the window copies as before 0.30 */

SDL_bool SDL_OS3_GPU_Open(void)
{
    if (!SDL_GetHintBoolean("SDL_OPENGPU", SDL_TRUE)) {
        return SDL_FALSE;
    }
    if (!SDL_OS3_OpenGPUBase) {
        SDL_OS3_OpenGPUBase = OpenLibrary((CONST_STRPTR)OPENGPU_NAME, OPENGPU_VERSION);
        if (!SDL_OS3_OpenGPUBase) {
            return SDL_FALSE;
        }
        /* GF-66 M8: the window presents go to ORTG_Present on 0.30 (below) */
        SDL_OS3_ORTGBase = OpenLibrary((CONST_STRPTR)OPENRTG_NAME, 0);
        {
            char v[4];
            SDL_OS3_present_old = DOSBase && GetVar((CONST_STRPTR)"SDL2/PresentOld", (STRPTR)v, sizeof v, GVF_GLOBAL_ONLY) > 0 &&
                                  v[0] == '1';
        }
    }
    SDL_OS3_GPU_opens++;
    return SDL_TRUE;
}

void SDL_OS3_GPU_Close(void)
{
    if (SDL_OS3_GPU_opens > 0 && --SDL_OS3_GPU_opens == 0 && SDL_OS3_OpenGPUBase) {
        CloseLibrary(SDL_OS3_OpenGPUBase);
        SDL_OS3_OpenGPUBase = NULL;
        if (SDL_OS3_ORTGBase) {
            CloseLibrary(SDL_OS3_ORTGBase);
            SDL_OS3_ORTGBase = NULL;
        }
    }
}

/* ---- the window presents through ORTG_Present (gfopenrtg.library 0.30) ---- */

SDL_bool SDL_OS3_GPU_PresentsNew(void)
{
    return SDL_OS3_ORTGBase && orf_wanted(SDL_OS3_ORTGBase->lib_Version, SDL_OS3_ORTGBase->lib_Revision,
                                          SDL_OS3_present_old) ? SDL_TRUE : SDL_FALSE;
}

static long OS3_ORTG_Call(struct ORTG_PresentInfo *in, void *ctx)
{
    (void)ctx;
    return ORTG_Present(in);
}

int SDL_OS3_GPU_PresentNew(SDL_Window *window, SDL_Surface *surface, const SDL_Rect *rects, int numrects,
                           SDL_bool vsync)
{
    struct Window *win = SDL_OS3_IntuitionWindow(window);
    struct RastPort *rp = win ? win->RPort : NULL;
    SDL_Rect all, r, out[SDL_OS3_GPU_MAXRECTS];
    unsigned long fmt;
    int i, n = 0, src, iw, ih;

    if (!SDL_OS3_GPU_PresentsNew() || !rp || !surface || !surface->pixels || !rects) {
        return 0;
    }
    /* the drawing area (OS3_InnerSize): a window larger than its surface
       is scaled, which stays the old present's */
    if (win->Flags & WFLG_GIMMEZEROZERO) {
        iw = win->GZZWidth;
        ih = win->GZZHeight;
    } else {
        iw = win->Width;
        ih = win->Height;
    }
    if (iw > surface->w || ih > surface->h) {
        return 0;
    }
    switch (surface->format->format) {
    case SDL_PIXELFORMAT_ARGB8888:
        src = ORF_SRC_ARGB32;
        break;
    case SDL_PIXELFORMAT_RGB888: /* XRGB */
        src = ORF_SRC_XRGB32;
        break;
    case SDL_PIXELFORMAT_RGB565:
        src = ORF_SRC_RGB565;
        break;
    default:
        return 0; /* RGB555, pens, BGRA: the old present */
    }
    fmt = orf_format(src, src == ORF_SRC_RGB565 && rp->BitMap && CyberGfxBase &&
                              GetCyberMapAttr(rp->BitMap, CYBRMATTR_ISCYBERGFX) &&
                              GetCyberMapAttr(rp->BitMap, CYBRMATTR_PIXFMT) == PIXFMT_RGB16);
    if (fmt == ORF_NONE) {
        return 0;
    }
    /* the rects inside w x h, joined into one when there are too many */
    all.x = 0;
    all.y = 0;
    all.w = surface->w;
    all.h = surface->h;
    for (i = 0; i < numrects; i++) {
        if (!SDL_IntersectRect(&rects[i], &all, &r)) {
            continue;
        }
        if (n < SDL_OS3_GPU_MAXRECTS) {
            out[n++] = r;
        } else {
            for (n--; n > 0; n--) {
                SDL_UnionRect(&out[0], &out[n], &out[0]);
            }
            SDL_UnionRect(&out[0], &r, &out[0]);
            n = 1;
        }
    }
    /* SDL's windows draw from the inside's corner: a GimmeZeroZero
       window's RastPort, or a borderless one */
    return orf_present(OS3_ORTG_Call, NULL, rp, 0, 0, surface->pixels, (unsigned long)surface->pitch, fmt,
                       all.w, all.h, (const struct orf_rect *)out, n, vsync);
}

int SDL_OS3_GPU_FormatOfPixfmt(unsigned long pixfmt)
{
    switch (pixfmt) {
    case PIXFMT_ARGB32:
        return OGPU_FMT_ARGB32;
    case PIXFMT_RGB16:
        return OGPU_FMT_RGB565;
    case PIXFMT_RGB16PC:
        return OGPU_FMT_RGB565PC;
    case PIXFMT_RGB15:
        return OGPU_FMT_RGB555;
    case PIXFMT_RGB15PC:
        return OGPU_FMT_RGB555PC;
    case PIXFMT_BGRA32:
        return OGPU_FMT_BGRA32;
    default:
        return 0;
    }
}

int SDL_OS3_GPU_FormatOfSDL(Uint32 format)
{
    switch (format) {
    case SDL_PIXELFORMAT_ARGB8888:
    case SDL_PIXELFORMAT_RGB888: /* XRGB: the byte order is ARGB32's */
        return OGPU_FMT_ARGB32;
    case SDL_PIXELFORMAT_RGB565:
        return OGPU_FMT_RGB565;
    case SDL_PIXELFORMAT_RGB555:
        return OGPU_FMT_RGB555;
    case SDL_PIXELFORMAT_BGRA8888:
        return OGPU_FMT_BGRA32;
    default:
        return 0;
    }
}

#endif /* SDL_VIDEO_RENDER_OPENGPU */
