/* Copyright (c) 2026 AmigaChrome Team. Mesa for AmigaOS 3.2 (Open), MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * How a finished GL frame reaches the display on OS 3.2. Mesa draws into a
 * buffer in Fast RAM (gla_core.c); this file shows it, through the Open
 * stack rather than the bare CPU:
 *
 *  - A window, or anything with layers: WritePixelArray (RECTFMT_ARGB) into
 *    the RastPort. That is OpenRTG's cybergraphics call, which clips to the
 *    layers and draws through OpenGPU (one PIXELS command per visible part).
 *  - A whole screen of its own (an SDL full-screen game): with
 *    opengpu.library open, one SURFACE + PIXELS + FENCE batch straight into
 *    the screen's bitmap, locked only while the batch is built and sent. On
 *    a plain 68040 that runs on OpenGPU's CPU back end; on AmigaChrome on
 *    the host's GPU. present() does not wait for the batch: it waits for
 *    the one before (the previous frame's), and the GLA core draws the next
 *    frame into its other texture (gla_present_set_async); a single buffer,
 *    a resize and the buffer's end wait for it (settle). With
 *    opengpu.library before 0.20 it is waited for at once, as before.
 *  - A frame virgl drew on the host's graphics chip (gla_virgl.c): with
 *    openrtg.library 0.15, ORTG_PresentVirgl, which hands the host the
 *    visible rectangles and lets it write the frame straight in (ACV_PRESENT),
 *    a window's or a screen's alike. Otherwise it is read back and comes
 *    through one of the two above.
 *  - No RTG (AGA only): not in phase 1. GL there needs a chunky-to-planar
 *    step, which comes with the AGA back end (OpenGPU G5).
 *
 * Big-endian builds draw GLA_ARGB, which is RECTFMT_ARGB and OGPU_FMT_ARGB32,
 * so no frame is converted on the way. */
#include <exec/types.h>
#include <graphics/rastport.h>
#include <graphics/gfx.h>
#include <intuition/screens.h>
#include <cybergraphx/cybergraphics.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/dos.h>
#include <dos/var.h>
#include <proto/cybergraphics.h>

#include <opengpu/opengpu.h>
#include <opengpu/build.h>
#include <proto/opengpu.h>
#define OPENRTG_BASE_NAME gla_openrtg_base   /* not OpenRTGBase: a program linking this may have its own */
#include <proto/openrtg.h>

#include "../gla_core.h"
#include "gla_present_os3.h"

extern struct Library *CyberGfxBase;
extern struct Library *OpenGPUBase;
static struct Library *gla_openrtg_base;
static int openrtg_tried;

/* SURFACE 6 + TARGET 2 + PIXELS 7 + FENCE 2 words, with room to spare. */
#define BATCH_WORDS 24

/* Batches sent and not yet waited for, one a target at most. They are kept
 * here, not in struct gla_os3_target: the program owns that struct, and its
 * size is the SDK's ABI. A slot is taken while a batch runs; with none free
 * the batch is waited for at once, as before. */
#define PENDING_SLOTS 4
static struct {
    struct gla_os3_target *t;
    ULONG fence;
} pending[PENDING_SLOTS];

static int pending_slot(const struct gla_os3_target *t)
{
    int i;
    for (i = 0; i < PENDING_SLOTS; i++)
        if (pending[i].t == t) return i;
    return -1;
}

/* The target's last batch, if it is still being carried out, is done: its
 * pixels can be drawn into again. No bitmap is locked here. */
static void settle(void *user)
{
    int i = pending_slot(user);
    if (i >= 0) {
        ULONG fence = pending[i].fence;
        pending[i].t = NULL;
        OGPU_Wait(fence);
    }
}

static int running(void *user)
{
    return user && pending_slot(user) >= 0;
}

/* opengpu.library 0.20 or later: OGPU_Wait(OGPU_FENCE_ALL) lets OpenRTG wait
 * for our batch too at its hazards (the CPU on the bitmap, freeing it). An
 * older one: the batch is waited for at once, as before. */
static int may_leave_running(void)
{
    return OpenGPUBase->lib_Version > 0 || OpenGPUBase->lib_Revision >= OGPU_FENCE_ALL_REVISION;
}

static int present_gpu(struct gla_os3_target *t, const struct gla_frame *f)
{
    ULONG words[BATCH_WORDS], fence = 0, base = 0, bpr = 0, fmt = 0;
    struct OGPUBatch b;
    APTR lock;
    int w = f->x1 - f->x0, h = f->y1 - f->y0, ofmt, slot;
    LONG err;

    lock = LockBitMapTags(t->bitmap, LBMI_BASEADDRESS, (ULONG)&base,
                          LBMI_BYTESPERROW, (ULONG)&bpr, LBMI_PIXFMT, (ULONG)&fmt, TAG_DONE);
    if (!lock)
        return 0;
    if (fmt == PIXFMT_ARGB32)           /* OpenRTG's 32-bit pixels have alpha 0: XRGB32 (v1.3) gives it */
        ofmt = OGPU_ANSWER(OGPU_Query(OGPU_OP_PIXELS, OGPU_FMT_XRGB32)) != OGPU_NONE ? OGPU_FMT_XRGB32 : OGPU_FMT_ARGB32;
    else if (fmt == PIXFMT_RGB16)
        ofmt = OGPU_FMT_RGB565;
    else {
        UnLockBitMap(lock);
        return 0;                       /* other layouts: WritePixelArray converts */
    }
    ogpu_batch_init(&b, words, BATCH_WORDS);
    ogpu_surface(&b, 0, base, bpr, GetCyberMapAttr(t->bitmap, CYBRMATTR_WIDTH),
                 GetCyberMapAttr(t->bitmap, CYBRMATTR_HEIGHT), ofmt);
    ogpu_target(&b, 0);
    ogpu_pixels(&b, (unsigned long)((const UBYTE *)f->pixels + f->y0 * f->stride + f->x0 * 4),
                f->stride, OGPU_FMT_ARGB32, 0, t->left + f->x0, t->top + f->y0, w, h);
    ogpu_fence(&b, 1);
    err = b.overflow ? -1 : OGPU_Submit(words, b.words, &fence);
    slot = err == OGPU_OK && may_leave_running() ? pending_slot(NULL) : -1;
    if (err == OGPU_OK && slot < 0)
        OGPU_Wait(fence);               /* as before 0.20, or with no slot free */
    /* Unlocked once the batch is sent: OpenRTG's bitmaps keep their memory,
     * and OpenRTG waits for running batches itself before the CPU or a new
     * lock touches a bitmap (ortg_ogpu_settle, OGPU_FENCE_ALL). */
    UnLockBitMap(lock);
    settle(t);                          /* the frame before: its texture is drawn into next */
    if (err != OGPU_OK)
        return 0;
    if (slot >= 0) {
        pending[slot].t = t;
        pending[slot].fence = fence;
    }
    return 1;
}

/* gla_counts.direct_state into the global variable GLAPresent when it
 * changes, so a lab can see which way virgl's frames go (GetEnv GLAPresent). */
static void publish_state(void)
{
    static const char *const words[] = {
        "", "direct: the host writes GL frames straight in",
        "read back: the runtime's host has no ACV_PRESENT",
        "read back: the program's present hook has no direct present",
        "read back: openrtg.library refused the frame (CLUT or planar screen, or no OpenGPU virgl)",
        "read back: no openrtg.library 0.15 or later",
        "read back: no RastPort or bitmap to present to" };
    static int shown = -1;
    int st = gla_counts.direct_state;
    if (st == shown || st < 1 || st > GLA_DIRECT_NO_TARGET || !DOSBase) return;
    shown = st;
    SetVar((CONST_STRPTR)"GLAPresent", (CONST_STRPTR)words[st], -1, GVF_GLOBAL_ONLY);
}

static void present(void *user, const struct gla_frame *f)
{
    struct gla_os3_target *t = user;
    publish_state();
    if (f->x1 <= f->x0 || f->y1 <= f->y0 || f->format != GLA_ARGB)
        return;
    if (t->bitmap && OpenGPUBase && CyberGfxBase && present_gpu(t, f))
        return;
    settle(t);                          /* WritePixelArray is done when it returns */
    if (CyberGfxBase && t->rp) {
        WritePixelArray((APTR)f->pixels, f->x0, f->y0, f->stride, t->rp,
                        t->left + f->x0, t->top + f->y0, f->x1 - f->x0, f->y1 - f->y0,
                        RECTFMT_ARGB);
        gla_counts.wpa_pixels += (unsigned long)(f->x1 - f->x0) * (unsigned long)(f->y1 - f->y0);
    }
}

/* openrtg.library walks the RastPort's visible pieces and the host writes
 * the frame into them; -1 when it can't (no 0.15, a CLUT or planar screen,
 * a runtime without ACV_PRESENT), and then the frame is read back. */
static long present_virgl(void *user, const struct gla_virgl_frame *f)
{
    struct gla_os3_target *t = user;
    struct OpenRTGVirglFrame of;
    struct RastPort rp, *r = t->rp;
    long n;
    if (!gla_openrtg_base) {
        /* openrtg.library is version 0 (0.15 is revision 15), so OpenLibrary
         * can't ask for 0.15: open any, then look at the revision */
        if (!openrtg_tried) {
            openrtg_tried = 1;
            if ((gla_openrtg_base = OpenLibrary((CONST_STRPTR)"openrtg.library", 0)) != NULL
                && gla_openrtg_base->lib_Version == 0 && gla_openrtg_base->lib_Revision < 15) {
                CloseLibrary(gla_openrtg_base);
                gla_openrtg_base = NULL;
            }
        }
        if (!gla_openrtg_base) {
            gla_counts.direct_state = GLA_DIRECT_NO_LIBRARY;
            return -1;
        }
    }
    if (!r) {                           /* a screen of its own: its bitmap, no layer */
        if (!t->bitmap) {
            gla_counts.direct_state = GLA_DIRECT_NO_TARGET;
            return -1;
        }
        InitRastPort(&rp);
        rp.BitMap = t->bitmap;
        r = &rp;
    }
    if (f->w > 0xFFFF || f->h > 0xFFFF || f->x1 <= f->x0 || f->y1 <= f->y0) return -1;
    of.context = f->context;
    of.handle = f->handle;
    of.width = (UWORD)f->w;
    of.height = (UWORD)f->h;
    of.damage_x = (WORD)f->x0;
    of.damage_y = (WORD)f->y0;
    of.damage_w = (UWORD)(f->x1 - f->x0);
    of.damage_h = (UWORD)(f->y1 - f->y0);
    of.flags = 0;                       /* virgl's display targets are top-down, as GLA's frames */
    settle(t);                          /* an earlier frame's batch reaches the screen first */
    n = ORTG_PresentVirgl(r, t->left, t->top, &of);
    if (n >= 0) {
        gla_counts.direct_state = GLA_DIRECT_ON;
        publish_state();
    }
    return n;
}

void gla_os3_present_init(struct gla_present *p, struct gla_os3_target *t)
{
    settle(t);                          /* nothing left running from an earlier use of this target */
    p->present = present;
    p->user = t;
    gla_present_set_virgl(present, present_virgl);
    gla_present_set_async(present, running, settle);
}

void gla_os3_present_close(void)
{
    if (gla_openrtg_base) CloseLibrary(gla_openrtg_base);
    gla_openrtg_base = NULL;
    openrtg_tried = 0;
}
