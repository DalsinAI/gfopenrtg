/*
  SDL 3 on AmigaOS 3.x: gfopengpu.library, shared by the opengpu renderer and
  the window framebuffer (SDL_os3gpu.h).
  Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).

  A window is shown by GFOpenGPU, which copies the frame into the screen's
  bitmap (on the Cradle, the PC does it), through each visible part of the
  window's layer and inside its borders. A screen GFOpenGPU has no format for,
  a planar one, or no gfopengpu.library: WritePixelArray, as SDL3_68k does.

  SDL_OS3_GPU_NO_PRESENT leaves the window's part out (it is the Amiga's
  alone): tests/sdl3/rendercompare_host.sh builds the rest on a PC.
*/
#include "SDL_internal.h"

#ifdef SDL_VIDEO_RENDER_OPENGPU

#include "SDL_os3gpu.h"
#include <proto/exec.h>

struct Library *SDL_OS3_OpenGPUBase = NULL;
static int SDL_OS3_GPU_opens = 0;
static bool SDL_OS3_GPU_v17 = false;

#ifndef SDL_OS3_GPU_NO_PRESENT
static void OS3_ORTG_Open(void);
static void OS3_ORTG_Close(void);
#endif

bool SDL_OS3_GPU_Open(void)
{
    if (!SDL_GetHintBoolean("SDL_OPENGPU", true)) {
        return false;
    }
    if (!SDL_OS3_OpenGPUBase) {
        SDL_OS3_OpenGPUBase = OpenLibrary((CONST_STRPTR)OPENGPU_NAME, OPENGPU_VERSION);
        if (!SDL_OS3_OpenGPUBase) {
            return false;
        }
        /* a library that answers PALETTE takes all of stream v1.7 */
        SDL_OS3_GPU_v17 = OGPU_ANSWER(OGPU_Query(OGPU_OP_PALETTE, OGPU_FMT_INDEX8)) != OGPU_NONE;
#ifndef SDL_OS3_GPU_NO_PRESENT
        OS3_ORTG_Open();
#endif
    }
    SDL_OS3_GPU_opens++;
    return true;
}

void SDL_OS3_GPU_Close(void)
{
    if (SDL_OS3_GPU_opens > 0 && --SDL_OS3_GPU_opens == 0 && SDL_OS3_OpenGPUBase) {
        CloseLibrary(SDL_OS3_OpenGPUBase);
        SDL_OS3_OpenGPUBase = NULL;
        SDL_OS3_GPU_v17 = false;
#ifndef SDL_OS3_GPU_NO_PRESENT
        OS3_ORTG_Close();
#endif
    }
}

bool SDL_OS3_GPU_HasV17(void)
{
    return SDL_OS3_GPU_v17;
}

int SDL_OS3_GPU_FormatOfSDL(SDL_PixelFormat format)
{
    switch (format) {
    /* SDL's names for 32-bit pixels by their bytes in memory */
    case SDL_PIXELFORMAT_ARGB32:
        return OGPU_FMT_ARGB32;
    case SDL_PIXELFORMAT_XRGB32:
        return OGPU_FMT_XRGB32;
    case SDL_PIXELFORMAT_BGRA32:
        return OGPU_FMT_BGRA32;
    case SDL_PIXELFORMAT_RGBA32:
        return SDL_OS3_GPU_v17 ? OGPU_FMT_RGBA32 : 0;
    case SDL_PIXELFORMAT_ABGR32:
        return SDL_OS3_GPU_v17 ? OGPU_FMT_ABGR32 : 0;
    /* 16-bit pixels are words in this machine's order */
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
    case SDL_PIXELFORMAT_RGB565:
        return OGPU_FMT_RGB565;
    case SDL_PIXELFORMAT_XRGB1555:
        return OGPU_FMT_RGB555;
#else
    case SDL_PIXELFORMAT_RGB565:
        return OGPU_FMT_RGB565PC;
    case SDL_PIXELFORMAT_XRGB1555:
        return OGPU_FMT_RGB555PC;
#endif
    default:
        return 0;
    }
}

/* ---- showing a window ---------------------------------------------------------- */

#ifndef SDL_OS3_GPU_NO_PRESENT

#include "SDL_os3video.h"
#include <proto/graphics.h>
#include <proto/layers.h>
#include <proto/cybergraphics.h>
#include <cybergraphx/cybergraphics.h>
#include <graphics/clip.h>
#include <graphics/layers.h>
#include <intuition/intuition.h>
#include <proto/dos.h>
#include <dos/var.h>

/* gfopenrtg.library's calls through SDL's own base (a program's
   OpenRTGBase is its own). */
#include <gfopenrtg/gfopenrtg.h>
#define OPENRTG_BASE_NAME SDL_OS3_ORTGBase
#include <inline/gfopenrtg.h>
#include "ortg_frame.h"

static struct Library *SDL_OS3_ORTGBase = NULL;
static int SDL_OS3_present_old = 0;  /* ENV:SDL3/PresentOld 1: the window copies as before 0.30 */

#ifndef PIXFMT_RGBA32
#define PIXFMT_RGBA32 13UL
#endif

int SDL_OS3_GPU_FormatOfPixfmt(unsigned long pixfmt)
{
    switch (pixfmt) {
    case PIXFMT_ARGB32:
        return OGPU_FMT_ARGB32;
    case PIXFMT_BGRA32:
        return OGPU_FMT_BGRA32;
    case PIXFMT_RGBA32:
        return SDL_OS3_GPU_v17 ? OGPU_FMT_RGBA32 : 0;
    case PIXFMT_RGB16:
        return OGPU_FMT_RGB565;
    case PIXFMT_RGB16PC:
        return OGPU_FMT_RGB565PC;
    case PIXFMT_RGB15:
        return OGPU_FMT_RGB555;
    case PIXFMT_RGB15PC:
        return OGPU_FMT_RGB555PC;
    default:
        return 0;
    }
}

#define OS3_GPU_PROP  "SDL.amigaos3.opengpu"
#define OS3_GPU_WORDS 2048

/* A window's GFOpenGPU state, made on its first frame and freed with the
   window's properties. */
typedef struct OS3_GPUWindow
{
    bool usable;
    Uint32 stream[OS3_GPU_WORDS];
} OS3_GPUWindow;

static void SDLCALL OS3_GPU_FreeWindow(void *userdata, void *value)
{
    OS3_GPUWindow *gw = (OS3_GPUWindow *)value;

    (void)userdata;
    if (gw) {
        if (gw->usable) {
            SDL_OS3_GPU_Close();
        }
        SDL_free(gw);
    }
}

static OS3_GPUWindow *OS3_GPU_Window(SDL_Window *window)
{
    SDL_PropertiesID props = SDL_GetWindowProperties(window);
    OS3_GPUWindow *gw;

    if (!props) {
        return NULL;
    }
    gw = (OS3_GPUWindow *)SDL_GetPointerProperty(props, OS3_GPU_PROP, NULL);
    if (!gw) {
        gw = (OS3_GPUWindow *)SDL_calloc(1, sizeof(*gw));
        if (!gw) {
            return NULL;
        }
        gw->usable = SDL_OS3_GPU_Open();
        /* on failure SDL frees it through the cleanup */
        if (!SDL_SetPointerPropertyWithCleanup(props, OS3_GPU_PROP, gw, OS3_GPU_FreeWindow, NULL)) {
            return NULL;
        }
    }
    return gw;
}

/* The rects inside w x h, joined into their bounding box when there are
   more than SDL_OS3_GPU_MAXRECTS. */
static int OS3_GPU_Rects(const SDL_Rect *rects, int numrects, int w, int h, SDL_Rect *out)
{
    SDL_Rect all, r;
    int i, n = 0;

    all.x = 0;
    all.y = 0;
    all.w = w;
    all.h = h;
    for (i = 0; i < numrects; i++) {
        if (!SDL_GetRectIntersection(&rects[i], &all, &r)) {
            continue;
        }
        if (n < SDL_OS3_GPU_MAXRECTS) {
            out[n++] = r;
        } else {
            SDL_GetRectUnion(&out[0], &r, &out[0]);
        }
    }
    if (n == SDL_OS3_GPU_MAXRECTS && numrects > SDL_OS3_GPU_MAXRECTS) {
        for (i = 1; i < n; i++) {
            SDL_GetRectUnion(&out[0], &out[i], &out[0]);
        }
        n = 1;
    }
    return n;
}

int SDL_OS3_GPU_PresentAppend(SDL_Window *window, SDL_Surface *surface, struct OGPUBatch *b,
                              int dst_slot, int src_slot, const SDL_Rect *rects, int numrects,
                              SDL_OS3_GPUPresent *p)
{
    SDL_WindowData *d = window ? window->internal : NULL;
    OS3_GPUWindow *gw;
    struct Window *win;
    struct Layer *layer;
    struct BitMap *bm;
    struct ClipRect *cr;
    ULONG base = 0, bpr = 0, pixfmt = 0, bw, bh, iscgx;
    APTR lock;
    int fmt, sfmt, i, iw, ih, visible = 0;
    long need;

    if (!d || !d->syswin || !SDL_SurfaceValid(surface) || !surface->pixels || !CyberGfxBase) {
        return 0;
    }
    gw = OS3_GPU_Window(window);
    if (!gw || !gw->usable) {
        return 0;
    }
    win = d->syswin;
    layer = win->RPort ? win->RPort->Layer : NULL;
    bm = win->RPort ? win->RPort->BitMap : NULL;
    sfmt = SDL_OS3_GPU_FormatOfSDL(surface->format);
    if (!layer || !bm || !sfmt) {
        return 0;
    }
    /* The inside: a GZZ window's RastPort is already there; otherwise it is
       inside the borders. */
    if (win->Flags & WFLG_GIMMEZEROZERO) {
        p->ox = 0;
        p->oy = 0;
    } else {
        p->ox = win->BorderLeft;
        p->oy = win->BorderTop;
    }
    iw = win->GZZWidth;
    ih = win->GZZHeight;
    if (iw <= 0 || ih <= 0) {
        return 0;
    }
    /* Each value asked on its own: the inlines must not nest. */
    iscgx = GetCyberMapAttr(bm, CYBRMATTR_ISCYBERGFX);
    if (!iscgx) {
        return 0;
    }
    bw = GetCyberMapAttr(bm, CYBRMATTR_WIDTH);
    bh = GetCyberMapAttr(bm, CYBRMATTR_HEIGHT);

    p->window = window;
    p->layer = layer;
    p->surface = surface;
    p->bmlock = NULL;
    p->covered = false;
    /* a surface larger than the inside (the window was just made smaller)
       is cut to it, so the borders stay */
    p->numrects = OS3_GPU_Rects(rects, numrects, SDL_min(surface->w, iw), SDL_min(surface->h, ih), p->rects);
    if (p->numrects == 0) {
        return 0;
    }

    LockLayer(0, layer);
    for (cr = layer->ClipRect; cr; cr = cr->Next) {
        if (cr->obscured) {
            p->covered = true;
        } else {
            visible++;
        }
    }
    /* surfaces, target, then a clip and a copy (COMPOSITE is the larger) a
       rect for each visible part */
    need = 6 + 6 + 2 + (long)visible * (3 + 8L * p->numrects) + 4;
    if (b->cap - b->words < need) {
        UnlockLayer(layer);
        return SDL_OS3_GPU_NOROOM;
    }
    lock = LockBitMapTags(bm, LBMI_BASEADDRESS, (ULONG)&base, LBMI_BYTESPERROW, (ULONG)&bpr,
                          LBMI_PIXFMT, (ULONG)&pixfmt, TAG_DONE);
    fmt = SDL_OS3_GPU_FormatOfPixfmt(pixfmt);
    if (!lock || !base || !fmt) {
        if (lock) {
            UnLockBitMap(lock);
        }
        UnlockLayer(layer);
        return 0;
    }
    p->bmlock = lock;
    ogpu_surface(b, dst_slot, base, bpr, (int)bw, (int)bh, fmt);
    ogpu_target(b, dst_slot);
    ogpu_surface(b, src_slot, (unsigned long)surface->pixels, (unsigned long)surface->pitch,
                 surface->w, surface->h, sfmt);
    for (cr = layer->ClipRect; cr; cr = cr->Next) {
        if (cr->obscured) {
            continue;
        }
        ogpu_clip(b, cr->bounds.MinX, cr->bounds.MinY,
                  cr->bounds.MaxX - cr->bounds.MinX + 1, cr->bounds.MaxY - cr->bounds.MinY + 1);
        for (i = 0; i < p->numrects; i++) {
            const SDL_Rect *r = &p->rects[i];
            int dx = layer->bounds.MinX + p->ox + r->x, dy = layer->bounds.MinY + p->oy + r->y;
            if (fmt == sfmt) {
                ogpu_copy(b, src_slot, r->x, r->y, dx, dy, r->w, r->h);
            } else {
                /* another format: converted, opaque */
                ogpu_composite(b, src_slot, r->x, r->y, r->w, r->h, dx, dy, r->w, r->h, 255, 0);
            }
        }
    }
    return 1;
}

void SDL_OS3_GPU_PresentEnd(SDL_OS3_GPUPresent *p)
{
    struct Layer *layer = p->layer;
    SDL_WindowData *d = p->window ? p->window->internal : NULL;
    SDL_Surface *surface = p->surface;
    struct ClipRect *cr;
    int i;

    if (p->bmlock) {
        UnLockBitMap(p->bmlock);
        p->bmlock = NULL;
    }
    /* The covered parts of a smart refresh window: drawn through the
       window's own RastPort, inside a covered ClipRect only, so
       layers.library puts them in its backing store and they show when the
       window comes to the front. (A simple refresh window's, as SDL3_68k
       opens, have no backing store: the next frame draws them.) */
    if (p->covered && d && d->syswin && surface->format == SDL_PIXELFORMAT_ARGB8888) {
        struct RastPort *rp = d->syswin->RPort;
        for (cr = layer->ClipRect; cr; cr = cr->Next) {
            SDL_Rect part, r;
            if (!cr->obscured || !cr->BitMap) {
                continue;
            }
            part.x = cr->bounds.MinX - layer->bounds.MinX - p->ox;
            part.y = cr->bounds.MinY - layer->bounds.MinY - p->oy;
            part.w = cr->bounds.MaxX - cr->bounds.MinX + 1;
            part.h = cr->bounds.MaxY - cr->bounds.MinY + 1;
            for (i = 0; i < p->numrects; i++) {
                if (!SDL_GetRectIntersection(&p->rects[i], &part, &r)) {
                    continue;
                }
                WritePixelArray((APTR)surface->pixels, (UWORD)r.x, (UWORD)r.y, (UWORD)surface->pitch, rp,
                                (UWORD)(p->ox + r.x), (UWORD)(p->oy + r.y), (UWORD)r.w, (UWORD)r.h, RECTFMT_ARGB);
            }
        }
    }
    UnlockLayer(layer);
}

void SDL_OS3_GPU_Abandon(SDL_OS3_GPUPresent *p)
{
    if (p->bmlock) {
        UnLockBitMap(p->bmlock);
        p->bmlock = NULL;
    }
    if (p->layer) {
        UnlockLayer(p->layer);
        p->layer = NULL;
    }
}

/* ---- the window presents through ORTG_Present (gfopenrtg.library 0.30, GF-66 M8) ---- */

static void OS3_ORTG_Open(void)
{
    char v[4];

    SDL_OS3_ORTGBase = OpenLibrary((CONST_STRPTR)OPENRTG_NAME, 0);
    SDL_OS3_present_old = DOSBase && GetVar((CONST_STRPTR)"SDL3/PresentOld", (STRPTR)v, sizeof v, GVF_GLOBAL_ONLY) > 0 &&
                          v[0] == '1';
}

static void OS3_ORTG_Close(void)
{
    if (SDL_OS3_ORTGBase) {
        CloseLibrary(SDL_OS3_ORTGBase);
        SDL_OS3_ORTGBase = NULL;
    }
}

bool SDL_OS3_GPU_PresentsNew(void)
{
    return SDL_OS3_ORTGBase && orf_wanted(SDL_OS3_ORTGBase->lib_Version, SDL_OS3_ORTGBase->lib_Revision,
                                          SDL_OS3_present_old);
}

/* gfopenrtg.library 0.32: ORTG_PRESENT_AFTER, so the opengpu renderer's
   drawing need not be waited for before its present. */
bool SDL_OS3_GPU_PresentsAfter(void)
{
    return SDL_OS3_GPU_PresentsNew() && orf_after_ok(SDL_OS3_ORTGBase->lib_Version, SDL_OS3_ORTGBase->lib_Revision);
}

/* The window's bitmap is a GFOpenRTG one (ORTG_BitMapInfo; not on a
   Picasso96 board, nor with GFOpenRTG's screens off): ORTG_Present's MEMORY
   route writes anything else through graphics.library as pens. Asked again
   only when the bitmap changes: ORTG_BitMapInfo waits for GFOpenGPU. */
static bool OS3_ORTG_Target(struct BitMap *bm)
{
    static struct BitMap *seen = NULL;
    static bool seen_ok = false;
    struct OpenRTGBitMapInfo bi;

    if (!bm) {
        return false;
    }
    if (bm != seen) {
        SDL_zero(bi);
        seen_ok = ORTG_BitMapInfo(bm, &bi) != 0;
        seen = bm;
    }
    return seen_ok;
}

static long OS3_ORTG_Call(struct ORTG_PresentInfo *in, void *ctx)
{
    (void)ctx;
    return ORTG_Present(in);
}

int SDL_OS3_GPU_PresentNew(SDL_Window *window, SDL_Surface *surface, const SDL_Rect *rects, int numrects, bool vsync)
{
    return SDL_OS3_GPU_PresentNewAfter(window, surface, rects, numrects, vsync, 0);
}

int SDL_OS3_GPU_PresentNewAfter(SDL_Window *window, SDL_Surface *surface, const SDL_Rect *rects, int numrects, bool vsync,
                                Uint32 after)
{
    SDL_WindowData *d = window ? window->internal : NULL;
    struct Window *win;
    struct BitMap *bm;
    SDL_Rect out[SDL_OS3_GPU_MAXRECTS];
    unsigned long fmt;
    long ox, oy;
    int src, w, h, n;

    if (!SDL_OS3_GPU_PresentsNew() || !d || !d->syswin || !d->syswin->RPort || !SDL_SurfaceValid(surface) ||
        !surface->pixels || !rects) {
        return 0;
    }
    win = d->syswin;
    bm = win->RPort->BitMap;
    if (!OS3_ORTG_Target(bm)) {
        return 0;
    }
    switch (surface->format) {
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
    case SDL_PIXELFORMAT_ARGB8888:
        src = ORF_SRC_ARGB32;
        break;
    case SDL_PIXELFORMAT_XRGB8888:
        src = ORF_SRC_XRGB32;
        break;
    case SDL_PIXELFORMAT_RGB565:
        src = ORF_SRC_RGB565;
        break;
#endif
    default:
        return 0; /* the other byte orders: the old present */
    }
    fmt = orf_format(src, src == ORF_SRC_RGB565 && bm && CyberGfxBase && GetCyberMapAttr(bm, CYBRMATTR_ISCYBERGFX) &&
                              GetCyberMapAttr(bm, CYBRMATTR_PIXFMT) == PIXFMT_RGB16);
    if (fmt == ORF_NONE) {
        return 0;
    }
    /* the inside, as SDL_OS3_GPU_PresentAppend has it */
    if (win->Flags & WFLG_GIMMEZEROZERO) {
        ox = 0;
        oy = 0;
    } else {
        ox = win->BorderLeft;
        oy = win->BorderTop;
    }
    w = SDL_min(surface->w, (int)win->GZZWidth);
    h = SDL_min(surface->h, (int)win->GZZHeight);
    if (w <= 0 || h <= 0) {
        return 0;
    }
    n = OS3_GPU_Rects(rects, numrects, w, h, out);
    return orf_present(OS3_ORTG_Call, NULL, win->RPort, ox, oy, surface->pixels, (unsigned long)surface->pitch, fmt,
                       w, h, (const struct orf_rect *)out, n, vsync, SDL_OS3_GPU_PresentsAfter() ? after : 0);
}

bool SDL_OS3_GPU_Present(SDL_Window *window, SDL_Surface *surface, const SDL_Rect *rects, int numrects)
{
    OS3_GPUWindow *gw = window ? OS3_GPU_Window(window) : NULL;
    SDL_OS3_GPUPresent p;
    struct OGPUBatch b;
    ULONG fence = 0;

    if (!gw || !gw->usable) {
        return false;
    }
    /* gfopenrtg.library 0.30: ORTG_Present; ENV:SDL3/PresentOld 1, or a
       format it would write on the CPU: the copy below */
    if (SDL_OS3_GPU_PresentNew(window, surface, rects, numrects, false)) {
        return true;
    }
    SDL_zero(p);
    ogpu_batch_init(&b, gw->stream, OS3_GPU_WORDS);
    if (SDL_OS3_GPU_PresentAppend(window, surface, &b, 0, 1, rects, numrects, &p) != 1) {
        return false;
    }
    if (!b.overflow && OGPU_Submit(gw->stream, (ULONG)b.words, &fence) == OGPU_OK) {
        /* done before the bitmap and layer are let go; an error there and
           the caller shows the window on the CPU */
        const LONG done = OGPU_Wait(fence);
        SDL_OS3_GPU_PresentEnd(&p);
        return done == OGPU_OK;
    }
    /* not sent: the CPU draws the window */
    SDL_OS3_GPU_Abandon(&p);
    return false;
}

#endif /* !SDL_OS3_GPU_NO_PRESENT */
#endif /* SDL_VIDEO_RENDER_OPENGPU */
