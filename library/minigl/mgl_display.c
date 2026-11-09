/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * minigl.library on AmigaOS: where a context draws, and the mgl* calls.
 *   - A screen of its own (the default): an RTG screen with two or more
 *     buffers (ScreenBuffers); a frame is drawn into the hidden one and
 *     shown by mglSwitchDisplay. A borderless backdrop window takes the
 *     input.
 *   - A window (mglChooseWindowMode, mglCreateContextFromWindow): frames
 *     are drawn off screen and copied into the window.
 *   - A bitmap the program owns (mglCreateContextFromBitMap).
 * R5G6B5 and A8R8G8B8 are drawn in place; other formats through an ARGB32
 * copy written back with WritePixelArray. mglMainLoop gives the program's
 * key, mouse and idle functions their turn.
 *
 * What each mgl* call does follows the clean-room spec's section 9.2
 * (docs/design/MiniGL-Compatible-Spec.md; see PROVENANCE.md). */
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/gfx.h>
#include <graphics/displayinfo.h>
#include <graphics/modeid.h>
#include <devices/inputevent.h>
#include <dos/dos.h>
#include <cybergraphx/cybergraphics.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/cybergraphics.h>

#include "mgl_internal.h"

extern struct Library *CyberGfxBase;

enum { MODE_SCREEN, MODE_WINDOW, MODE_APPWINDOW, MODE_BITMAP };

struct mgl_display {
    int mode;
    struct Screen *screen;
    struct Window *window;
    struct ScreenBuffer *sb[3];
    int nbuf, back;                 /* buffers, the one drawn into */
    struct BitMap *off;             /* window modes: the frame, drawn off screen */
    ULONG *argb;                    /* formats the core can't draw into: an ARGB32 copy */
    ULONG argbbpr;
    int direct;
    struct BitMap *appbm;
    APTR lock;                      /* LockBitMapTags while a batch runs */
    ULONG *lockbuf;                 /* mglLockBack's copy of the frame (R, G, B, A bytes), or 0 */
    int locked;                     /* mglLockDisplay's flag */
    UWORD *blank;                   /* the empty pointer (chip memory) */
    int ox, oy;                     /* the frame's place in the window */
    int waits;                      /* buffers asked for (mglChooseNumberOfBuffers): more than 1 waits */
    int closed_wb;                  /* mglProposeCloseDesktop closed the Workbench */
};

/* The bitmap the next frame is drawn into. */
static struct BitMap *back_bitmap(struct mgl_display *d)
{
    switch (d->mode) {
    case MODE_SCREEN:
        if (!d->screen) return 0;           /* a resize that failed: no display */
        return d->off ? d->off : d->nbuf ? d->sb[d->back]->sb_BitMap : d->screen->RastPort.BitMap;
    case MODE_BITMAP: return d->appbm;
    }
    return d->off;
}

static int ogpu_format_of(struct BitMap *bm, int *direct)
{
    ULONG pf = GetCyberMapAttr(bm, CYBRMATTR_PIXFMT);
    *direct = pf == PIXFMT_RGB16 || pf == PIXFMT_ARGB32;
    return pf == PIXFMT_RGB16 ? OGPU_FMT_RGB565 : OGPU_FMT_ARGB32;
}

int mgl_disp_lock(GLcontext c)
{
    struct mgl_display *d = c->disp;
    struct BitMap *bm = back_bitmap(d);
    if (!bm) return 0;
    if (!d->direct) {
        struct RastPort rp;
        InitRastPort(&rp);
        rp.BitMap = bm;
        ReadPixelArray(d->argb, 0, 0, d->argbbpr, &rp, 0, 0, c->width, c->height, RECTFMT_ARGB);
        c->base = (UBYTE *)d->argb;
        c->bpr = d->argbbpr;
        return 1;
    }
    {
        ULONG base = 0, bpr = 0;
        d->lock = LockBitMapTags(bm, LBMI_BASEADDRESS, (ULONG)&base, LBMI_BYTESPERROW, (ULONG)&bpr, TAG_DONE);
        if (!d->lock) return 0;
        c->base = (UBYTE *)base;
        c->bpr = bpr;
    }
    return 1;
}

void mgl_disp_unlock(GLcontext c)
{
    struct mgl_display *d = c->disp;
    if (d->lock) { UnLockBitMap(d->lock); d->lock = 0; }
    if (!d->direct) {
        struct RastPort rp;
        InitRastPort(&rp);
        rp.BitMap = back_bitmap(d);
        WritePixelArray(d->argb, 0, 0, d->argbbpr, &rp, 0, 0, c->width, c->height, RECTFMT_ARGB);
    }
}

/* ---- opening -------------------------------------------------------------------------------- */

/* What the library opened for the display (not what the program gave it). */
static void close_parts(struct mgl_display *d)
{
    int i;
    mgl_free(d->lockbuf);
    d->lockbuf = 0;
    if (d->window && d->mode != MODE_APPWINDOW) {
        ClearPointer(d->window);
        CloseWindow(d->window);
    }
    if (d->mode != MODE_BITMAP) d->window = 0;
    if (d->screen && d->mode == MODE_SCREEN) {
        for (i = d->nbuf - 1; i >= 0; i--) if (d->sb[i]) FreeScreenBuffer(d->screen, d->sb[i]);
        CloseScreen(d->screen);
        d->screen = 0;
    }
    d->nbuf = 0;
    if (d->off) { WaitBlit(); FreeBitMap(d->off); d->off = 0; }
    mgl_free(d->argb);
    d->argb = 0;
}

static void close_display(struct mgl_display *d)
{
    if (!d) return;
    close_parts(d);
    if (d->closed_wb) OpenWorkBench();
    if (d->blank) FreeVec(d->blank);
    mgl_free(d);
}

/* The area's format: drawn in place, or through an ARGB32 copy. */
static int setup_area(GLcontext c)
{
    struct mgl_display *d = c->disp;
    struct BitMap *bm = back_bitmap(d);
    if (!bm || !GetCyberMapAttr(bm, CYBRMATTR_ISCYBERGFX)) { mgl_log("mglCreateContext: not an RTG bitmap (%lx)", (unsigned long)bm); return 0; }
    c->fmt = ogpu_format_of(bm, &d->direct);
    if (!d->direct) {
        d->argbbpr = (ULONG)c->width * 4;
        d->argb = mgl_alloc(d->argbbpr * (ULONG)c->height);
        if (!d->argb) return 0;
    }
    return 1;
}

/* The input window's messages, added by the main loops (spec 9.2: the window
 * opens with none; a program that reads it itself adds its own). */
void mgl_input_on(GLcontext c)
{
    struct Window *w = c->disp->window;
    ULONG f = IDCMP_VANILLAKEY | IDCMP_RAWKEY | IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE;
    if (!w) return;
    if (w->Flags & WINDOWCLOSE) f |= IDCMP_CLOSEWINDOW;
    if ((w->IDCMPFlags & f) != f) ModifyIDCMP(w, w->IDCMPFlags | f);
}

static int open_screen(GLcontext c, struct mgl_display *d, ULONG id, int w, int h)
{
    int depth = 32, i;                          /* mglChoosePixelDepth is accepted and ignored */
    if (id == (ULONG)INVALID_ID || !id || id == MGL_SCREENMODE_BEST)
        id = BestCModeIDTags(CYBRBIDTG_NominalWidth, w, CYBRBIDTG_NominalHeight, h, CYBRBIDTG_Depth, depth, TAG_DONE);
    if (id == (ULONG)INVALID_ID) { mgl_log("mglCreateContext: no %ldx%ld %ld-bit mode", (long)w, (long)h, (long)depth); return 0; }
    if (IsCyberModeID(id)) depth = (int)GetCyberIDAttr(CYBRIDATTR_DEPTH, id);
    if (depth > 24) depth = 24;                 /* RTG screens: 24 opens the 32-bit modes */
    {
        ULONG err = 0;
        d->screen = OpenScreenTags(0, SA_DisplayID, id, SA_Width, w, SA_Height, h, SA_Depth, depth,
                                   SA_Quiet, TRUE, SA_ShowTitle, FALSE, SA_Type, CUSTOMSCREEN,
                                   SA_Title, (ULONG)"AmiCradle GL", SA_ErrorCode, (ULONG)&err, TAG_DONE);
        if (!d->screen) {
            mgl_log("mglCreateContext: no screen (mode %lx, depth %ld, error %ld)", (unsigned long)id, (long)depth, (long)err);
            return 0;
        }
    }
    d->window = OpenWindowTags(0, WA_CustomScreen, (ULONG)d->screen, WA_Left, 0, WA_Top, 0, WA_Width, w, WA_Height, h,
                               WA_Backdrop, TRUE, WA_Borderless, TRUE, WA_Activate, TRUE, WA_RMBTrap, TRUE,
                               WA_ReportMouse, TRUE, WA_IDCMP, 0, WA_SimpleRefresh, TRUE,
                               WA_NoCareRefresh, TRUE, TAG_DONE);
    if (!d->window) { mgl_log("mglCreateContext: no window"); return 0; }
    if (mgl_prefs.close_desktop) d->closed_wb = CloseWorkBench() != 0;
    mgl_prefs.close_desktop = 0;                /* consumed when a display opens */
    c->width = w; c->height = h;
    /* A frame is drawn off screen and copied onto the screen in one blit:
     * on RTG that is quicker than changing screen buffers, which waits for
     * the display (ENV:MiniGL/Flip = 1 asks for the buffers instead). */
    if (!mgl_prefs.flip) {
        d->off = AllocBitMap(w, h, depth, BMF_MINPLANES | BMF_CLEAR, d->screen->RastPort.BitMap);
        if (d->off) return 1;
    }
    d->nbuf = mgl_prefs.buffers < 2 ? 2 : mgl_prefs.buffers > 3 ? 3 : mgl_prefs.buffers;
    for (i = 0; i < d->nbuf; i++) {
        d->sb[i] = AllocScreenBuffer(d->screen, 0, i ? 0 : SB_SCREEN_BITMAP);
        if (!d->sb[i]) break;
        d->sb[i]->sb_DBufInfo->dbi_SafeMessage.mn_ReplyPort = 0;
        d->sb[i]->sb_DBufInfo->dbi_DispMessage.mn_ReplyPort = 0;
    }
    if (i < 2) {                                /* no buffers: draw into the screen itself */
        while (i > 0) FreeScreenBuffer(d->screen, d->sb[--i]);
        d->nbuf = 0;
    } else d->nbuf = i;
    d->back = d->nbuf ? 1 : 0;
    c->width = w; c->height = h;
    return 1;
}

static int open_window(GLcontext c, struct mgl_display *d, int x, int y, int w, int h, struct Window *app, int closable)
{
    struct Screen *pub = 0;
    if (app) {
        d->window = app;
        d->mode = MODE_APPWINDOW;
        w = app->Width - app->BorderLeft - app->BorderRight;
        h = app->Height - app->BorderTop - app->BorderBottom;
        if (w < 1 || h < 1) return 0;           /* no inner area */
    } else {
        pub = LockPubScreen(0);
        if (!pub) return 0;
        d->window = OpenWindowTags(0, WA_PubScreen, (ULONG)pub, WA_Left, x, WA_Top, y, WA_InnerWidth, w, WA_InnerHeight, h,
                                   WA_Title, (ULONG)"AmiCradle GL", WA_DragBar, TRUE, WA_DepthGadget, TRUE,
                                   WA_CloseGadget, closable ? TRUE : FALSE,
                                   WA_Activate, TRUE, WA_RMBTrap, TRUE, WA_ReportMouse, TRUE, WA_IDCMP, 0,
                                   WA_SimpleRefresh, TRUE, WA_NoCareRefresh, TRUE, TAG_DONE);
        UnlockPubScreen(0, pub);
        if (!d->window) return 0;
        d->mode = MODE_WINDOW;
    }
    d->ox = d->window->BorderLeft;
    d->oy = d->window->BorderTop;
    d->off = AllocBitMap(w, h, 16, BMF_MINPLANES | BMF_CLEAR, d->window->RPort->BitMap);
    if (!d->off) return 0;
    c->width = w; c->height = h;
    return 1;
}

static GLcontext new_context(void)
{
    GLcontext c = mgl_alloc(sizeof(struct GLcontext_t));
    if (!c) return 0;
    c->disp = mgl_alloc(sizeof(struct mgl_display));
    if (!c->disp) { mgl_free(c); return 0; }
    mgl_read_prefs();
    return c;
}

static GLcontext finish(GLcontext c)
{
    if (!setup_area(c) || !mgl_out_open(c)) {
        mgl_log("mglCreateContext: no drawing area or no memory");
        mgl_out_close(c);
        close_display(c->disp);
        mgl_free(c);
        return 0;
    }
    c->no_mip = mgl_prefs.no_mip;
    c->disp->waits = mgl_prefs.buffers;
    mgl_init_state(c);
    /* MiniGL's clear colour after creation is opaque white (spec 9.2). */
    c->clear_color[0] = c->clear_color[1] = c->clear_color[2] = c->clear_color[3] = 1.0f;
    if (c->disp->mode == MODE_SCREEN) MGLSetPointer(c);    /* the display's pointer is blanked */
    mgl_current = c;
    mgl_log("mglCreateContext %ldx%ld route %ld fmt %ld direct %ld", (long)c->width, (long)c->height, (long)c->route,
            (long)c->fmt, (long)c->disp->direct);
    return c;
}

static GLcontext create(ULONG id, int x, int y, int w, int h, int window, int closable)
{
    GLcontext c = new_context();
    int ok;
    if (!c) return 0;
    if (w < 1) w = 640;
    if (h < 1) h = 480;
    if (window) ok = open_window(c, c->disp, x, y, w, h, 0, closable);
    else { c->disp->mode = MODE_SCREEN; ok = open_screen(c, c->disp, id, w, h); }
    if (!ok) { close_display(c->disp); mgl_free(c); return 0; }
    return finish(c);
}

/* x, y: the viewport's first origin (spec 9.2); the window, when there is
 * one, opens at the Workbench's top left with a drag bar and a depth gadget. */
void *MGLCreateContext(int x, int y, int w, int h)
{
    GLcontext c = create((ULONG)INVALID_ID, 0, 0, w, h, mgl_prefs.window, 0);
    if (c && (x || y)) GLViewport(c, x, y, c->width, c->height);
    return c;
}

/* GLUT's window: where GLUT says, with a close gadget. */
GLcontext mgl_create_window(int x, int y, int w, int h)
{
    return create((ULONG)INVALID_ID, x, y, w, h, 1, 1);
}

/* The compatible answer is NULL; AmiCradle opens the modes that
 * mglGetSupportedScreenModes offers (spec 9.2). */
void *MGLCreateContextFromID(GLint id, GLint *w, GLint *h)
{
    GLcontext c;
    int ww = w && *w > 0 ? *w : 640, hh = h && *h > 0 ? *h : 480;
    if ((ULONG)id == MGL_SCREENMODE_WINDOW) c = create((ULONG)INVALID_ID, 0, 0, ww, hh, 1, 0);
    else {
        if ((ULONG)id != MGL_SCREENMODE_BEST && IsCyberModeID((ULONG)id)) {
            ww = (int)GetCyberIDAttr(CYBRIDATTR_WIDTH, (ULONG)id);
            hh = (int)GetCyberIDAttr(CYBRIDATTR_HEIGHT, (ULONG)id);
        }
        c = create((ULONG)id, 0, 0, ww, hh, 0, 0);
    }
    if (c) { if (w) *w = c->width; if (h) *h = c->height; }
    else { if (w) *w = 0; if (h) *h = 0; }
    return c;
}

void *MGLCreateContextFromWindow(struct Window *window)
{
    GLcontext c;
    if (!window) return 0;
    c = new_context();
    if (!c) return 0;
    if (!open_window(c, c->disp, 0, 0, 0, 0, window, 0)) { close_display(c->disp); mgl_free(c); return 0; }
    return finish(c);
}

void *MGLCreateContextFromBitMap(struct BitMap *bitmap)
{
    GLcontext c;
    if (!bitmap || !GetCyberMapAttr(bitmap, CYBRMATTR_ISCYBERGFX)) return 0;
    c = new_context();
    if (!c) return 0;
    c->disp->mode = MODE_BITMAP;
    c->disp->appbm = bitmap;
    c->width = (int)GetCyberMapAttr(bitmap, CYBRMATTR_WIDTH);
    c->height = (int)GetCyberMapAttr(bitmap, CYBRMATTR_HEIGHT);
    return finish(c);
}

void MGLDeleteContext(GLcontext c)
{
    if (!c) return;
    mgl_pipe_flush(c);
    mgl_flush(c);
    mgl_free_lists(c);
    mgl_free_textures(c);
    mgl_free(c->cache);
    mgl_free(c->cache_ix);
    mgl_out_close(c);
    close_display(c->disp);
    if (mgl_current == c) mgl_current = 0;
    mgl_free(c);
    mgl_prefs_defaults();                       /* one program's choices don't reach the next */
}

void mgl_disp_free(GLcontext c) { close_display(c->disp); c->disp = 0; }

/* ---- frames ----------------------------------------------------------------------------------- */

/* mglLockBack's copy back into the drawing area: R, G, B, A bytes to the
 * area's format. */
static void lockbuf_release(GLcontext c)
{
    struct mgl_display *d = c->disp;
    ULONG *p = d->lockbuf, n = (ULONG)c->width * (ULONG)c->height, i;
    int x, y;
    if (!p) return;
    for (i = 0; i < n; i++) p[i] = (p[i] >> 8) | (p[i] << 24);     /* to A, R, G, B */
    if (c->vg) { mgl_vg_writeback(c, p); mgl_free(p); d->lockbuf = 0; return; }
    if (mgl_disp_lock(c)) {
        for (y = 0; y < c->height; y++) {
            const ULONG *s = p + (ULONG)y * (ULONG)c->width;
            if (c->fmt == OGPU_FMT_RGB565) {
                UWORD *o = (UWORD *)(c->base + (ULONG)y * c->bpr);
                for (x = 0; x < c->width; x++)
                    o[x] = (UWORD)(((s[x] >> 8) & 0xF800) | ((s[x] >> 5) & 0x07E0) | ((s[x] >> 3) & 0x001F));
            } else mgl_copy(c->base + (ULONG)y * c->bpr, s, (ULONG)c->width * 4);
        }
        mgl_disp_unlock(c);
    }
    mgl_free(p);
    d->lockbuf = 0;
}

/* Rendering never needs it: a flag. */
GLboolean MGLLockDisplay(GLcontext c)
{
    if (!c) return GL_FALSE;
    c->disp->locked = 1;
    return GL_TRUE;
}

void MGLUnlockDisplay(GLcontext c)
{
    if (!c) return;
    lockbuf_release(c);
    c->disp->locked = 0;
}

/* The frame so far as R, G, B, A bytes, top row first, until
 * mglUnlockDisplay (or mglSwitchDisplay) puts it back. */
GLboolean MGLLockBack(GLcontext c, MGLLockInfo *info)
{
    struct mgl_display *d;
    ULONG n, i, *p;
    if (!c) return GL_FALSE;
    d = c->disp;
    if (info) {
        info->width = (GLuint)c->width;
        info->height = (GLuint)c->height;
        info->depth = 32;
        info->format = MGL_LOCK_FORMAT_RGBA8;
        info->address = 0;
        info->bytes_per_row = 0;
    }
    if (d->lockbuf) return GL_FALSE;            /* already held */
    mgl_pipe_flush(c);
    mgl_flush(c);
    n = (ULONG)c->width * (ULONG)c->height;
    p = mgl_alloc(n * 4);
    if (!p) return GL_FALSE;
    mgl_readback(c, 0, 0, c->width, c->height, p, c->width);
    for (i = 0; i < n; i++) p[i] = (p[i] << 8) | (p[i] >> 24);     /* A, R, G, B to R, G, B, A */
    d->lockbuf = p;
    if (info) {
        info->address = p;
        info->bytes_per_row = (GLuint)c->width * 4;
    }
    return GL_TRUE;
}

void MGLSwitchDisplay(GLcontext c)
{
    struct mgl_display *d;
    if (!c) return;
    d = c->disp;
    mgl_log("switch %ld", (long)c->frames);
    mgl_pipe_flush(c);
    mgl_flush(c);
    MGLUnlockDisplay(c);
    c->frames++;
    if (c->vg) { mgl_vg_switch(c); return; }   /* the host shows it (mgl_vg_os3.c) */
    switch (d->mode) {
    case MODE_SCREEN:
        if (!d->screen) break;
        if (d->off) {
            if (c->sync || d->waits > 1) WaitTOF();
            BltBitMapRastPort(d->off, 0, 0, &d->screen->RastPort, 0, 0, c->width, c->height, 0xC0);
        } else if (d->nbuf) {
            /* shown at the next display change; when the last change is still
             * waiting, this frame's buffer stays the one drawn into */
            ULONG t0 = mgl_trace > 0 ? mgl_millis() : 0;
            if (c->sync || d->waits > 1) WaitTOF();
            if (ChangeScreenBuffer(d->screen, d->sb[d->back])) d->back = (d->back + 1) % d->nbuf;
            if (mgl_trace > 0) mgl_log("change: %ld ms", (long)(mgl_millis() - t0));
            else mgl_log("mglSwitchDisplay: the last change is still waiting");
        }
        break;
    case MODE_WINDOW: case MODE_APPWINDOW:
        BltBitMapRastPort(d->off, 0, 0, d->window->RPort, d->ox, d->oy, c->width, c->height, 0xC0);
        break;
    default:
        break;
    }
}

/* Fullscreen: the display opened again at the new size, textures and GL
 * state kept, the viewport and depth range set again. Windows and bitmaps:
 * nothing (spec 9.2). */
GLboolean MGLResizeContext(GLcontext c, GLsizei width, GLsizei height)
{
    struct mgl_display *d;
    UBYTE *nz;
    if (!c) return GL_FALSE;
    if (width <= 0 || height <= 0) { mgl_error(c, GL_INVALID_VALUE); return GL_FALSE; }
    d = c->disp;
    if (d->mode != MODE_SCREEN) return GL_TRUE;
    mgl_pipe_flush(c);
    mgl_flush(c);
    lockbuf_release(c);
    close_parts(d);
    nz = mgl_alloc((ULONG)width * (ULONG)height * (c->zfmt == OGPU_FMT_Z32 ? 4 : 2));
    if (!nz || !open_screen(c, d, (ULONG)INVALID_ID, width, height) || !setup_area(c)) {
        mgl_free(nz);
        close_parts(d);                         /* no display until a resize works */
        return GL_FALSE;
    }
    mgl_free(c->zbuf);
    c->zbuf = nz;
    c->zbpr = (ULONG)width * (c->zfmt == OGPU_FMT_Z32 ? 4 : 2);
    c->width = width; c->height = height;
    if (!(c->en & EN_SCISSOR)) { c->sc_w = width; c->sc_h = height; }
    c->vp_x = c->vp_y = 0; c->vp_w = width; c->vp_h = height;
    c->depth_near = 0.0; c->depth_far = 1.0;
    mgl_viewport_changed(c);
    c->clip_dirty = 1;
    MGLSetPointer(c);
    return GL_TRUE;
}

void *MGLGetWindowHandle(GLcontext c) { return c ? c->disp->window : 0; }
void *MGLGetInputWindowHandle(GLcontext c) { return c ? c->disp->window : 0; }

/* mglSetPointer hides the pointer, mglClearPointer brings it back, as MiniGL has them. */
void MGLSetPointer(GLcontext c)
{
    struct mgl_display *d;
    if (!c) return;
    d = c->disp;
    if (!d) return;
    if (!d->blank) d->blank = AllocVec(16, MEMF_CHIP | MEMF_CLEAR);
    if (d->window && d->blank) SetPointer(d->window, d->blank, 1, 1, 0, 0);
}

void MGLClearPointer(GLcontext c)
{
    if (c && c->disp && c->disp->window) ClearPointer(c->disp->window);
}

/* ---- the main loop ---------------------------------------------------------------------------- */


/* One round of the window's messages; 0 when the window was closed. */
int mgl_events(GLcontext c, void (*glut_key)(int key), void (*glut_special)(int key))
{
    struct mgl_display *d = c->disp;
    struct IntuiMessage *m;
    int alive = 1;
    if (!d->window) return 1;
    while ((m = (struct IntuiMessage *)GetMsg(d->window->UserPort)) != 0) {
        ULONG cls = m->Class;
        UWORD code = m->Code, q = m->Qualifier;
        /* GL's window coordinates: y up from the bottom */
        GLint mx = m->MouseX - d->ox, my = c->height - 1 - (m->MouseY - d->oy);
        GLbitfield buttons = ((q & IEQUALIFIER_LEFTBUTTON) ? MGL_MOUSE_LEFT : 0) | ((q & IEQUALIFIER_RBUTTON) ? MGL_MOUSE_RIGHT : 0)
                           | ((q & IEQUALIFIER_MIDBUTTON) ? MGL_MOUSE_MIDDLE : 0);
        ReplyMsg(&m->ExecMessage);
        switch (cls) {
        case IDCMP_VANILLAKEY:
            if (glut_key) glut_key(code);
            else if (c->key) c->key((char)code);
            break;
        case IDCMP_RAWKEY: {
            int sp = -1;
            if (code >= 0x50 && code <= 0x59) sp = MGL_KEY_F1 + (code - 0x50);
            else if (code == 0x4C) sp = MGL_KEY_UP;
            else if (code == 0x4D) sp = MGL_KEY_DOWN;
            else if (code == 0x4E) sp = MGL_KEY_RIGHT;
            else if (code == 0x4F) sp = MGL_KEY_LEFT;
            if (sp >= 0) {
                if (glut_special) glut_special(sp);
                else if (c->special) c->special((MGLspecial)sp);
            }
            break;
        }
        case IDCMP_MOUSEBUTTONS:
        case IDCMP_MOUSEMOVE:
            if (c->mouse) c->mouse(mx, my, buttons);
            break;
        case IDCMP_CLOSEWINDOW:
            alive = 0;
            break;
        }
    }
    return alive;
}

ULONG mgl_event_signal(GLcontext c)
{
    return c->disp->window ? 1UL << c->disp->window->UserPort->mp_SigBit : 0;
}

void MGLMainLoop(GLcontext c)
{
    if (!c || !c->disp->window) return;         /* no input window: at once */
    mgl_input_on(c);
    c->quit = 0;
    while (!c->quit) {
        if (!mgl_events(c, 0, 0)) break;
        if (c->quit) break;
        if (c->idle) c->idle();
        else {
            ULONG s = mgl_event_signal(c) | SIGBREAKF_CTRL_C;
            if (Wait(s) & SIGBREAKF_CTRL_C) break;
        }
        if (SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) break;
    }
}

/* The compatible answer is "best" without a call; AmiCradle offers its RTG
 * modes until the callback takes one (spec 9.2). */
GLint MGLGetSupportedScreenModes(MGLScreenModeCallback cb)
{
    ULONG id = INVALID_ID;
    if (!cb) return (GLint)MGL_SCREENMODE_BEST;
    while ((id = NextDisplayInfo(id)) != (ULONG)INVALID_ID) {
        MGLScreenMode sm;
        struct NameInfo ni;
        int i;
        if (!IsCyberModeID(id)) continue;
        sm.bits = (GLint)GetCyberIDAttr(CYBRIDATTR_DEPTH, id);
        if (sm.bits < 15) continue;
        sm.mode_id = (GLint)id;
        sm.width = (GLint)GetCyberIDAttr(CYBRIDATTR_WIDTH, id);
        sm.height = (GLint)GetCyberIDAttr(CYBRIDATTR_HEIGHT, id);
        sm.name[0] = 0;
        if (GetDisplayInfoData(0, (UBYTE *)&ni, sizeof ni, DTAG_NAME, id) > 0)
            for (i = 0; i < MGL_MODE_NAME_LEN - 1 && ni.Name[i]; i++) sm.name[i] = (char)ni.Name[i], sm.name[i + 1] = 0;
        if (cb(&sm) == GL_TRUE) return sm.mode_id;
    }
    return (GLint)MGL_SCREENMODE_BEST;
}

/* ---- a picture of the frame -------------------------------------------------------------------- */

void MGLWriteShotPPM(GLcontext c, char *name)
{
    BPTR f;
    ULONG *row;
    UBYTE *out;
    char head[32];
    int y, x, n = 0;
    LONG v;
    if (!c || !name) return;
    row = mgl_alloc((ULONG)c->width * 4);
    out = mgl_alloc((ULONG)c->width * 3);
    f = Open((STRPTR)name, MODE_NEWFILE);
    if (!row || !out || !f) { if (f) Close(f); mgl_free(row); mgl_free(out); return; }
    /* "P6\n<w> <h>\n255\n" */
    head[n++] = 'P'; head[n++] = '6'; head[n++] = '\n';
    for (v = c->width, x = 1; v >= 10; v /= 10) x *= 10;
    for (v = c->width; x; x /= 10) head[n++] = (char)('0' + (v / x) % 10);
    head[n++] = ' ';
    for (v = c->height, x = 1; v >= 10; v /= 10) x *= 10;
    for (v = c->height; x; x /= 10) head[n++] = (char)('0' + (v / x) % 10);
    head[n++] = '\n'; head[n++] = '2'; head[n++] = '5'; head[n++] = '5'; head[n++] = '\n';
    Write(f, head, n);
    for (y = 0; y < c->height; y++) {
        mgl_readback(c, 0, y, c->width, 1, row, c->width);
        for (x = 0; x < c->width; x++) {
            out[x * 3] = (UBYTE)(row[x] >> 16); out[x * 3 + 1] = (UBYTE)(row[x] >> 8); out[x * 3 + 2] = (UBYTE)row[x];
        }
        Write(f, out, c->width * 3);
    }
    Close(f);
    mgl_free(row);
    mgl_free(out);
}

/* ---- the virgl route's target ----------------------------------------------------------------- */

#include "mgl_vg.h"

/* Where a frame the host drew goes: a screen's visible bitmap (no copy
 * through d->off), the program's bitmap, or a window's RastPort inside its
 * borders. */
int mgl_disp_target(GLcontext c, struct RastPort **rp, struct BitMap **bm, int *x, int *y)
{
    struct mgl_display *d = c->disp;
    *x = *y = 0;
    switch (d->mode) {
    case MODE_SCREEN:
        *rp = &d->screen->RastPort;
        *bm = d->screen->RastPort.BitMap;
        return MGL_TARGET_SCREEN;
    case MODE_BITMAP:
        *rp = 0;
        *bm = d->appbm;
        return MGL_TARGET_BITMAP;
    }
    *rp = d->window->RPort;
    *bm = d->window->RPort->BitMap;
    *x = d->ox; *y = d->oy;
    return MGL_TARGET_WINDOW;
}
