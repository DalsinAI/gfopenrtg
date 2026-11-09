/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * minigl.library's virgl route on AmigaOS: the transport and the present.
 *
 * A request block is one OGPU_OP_VIRGL command in a batch for
 * gfopengpu.library, done when OGPU_Wait returns (as GL.module's
 * gla_virgl_os3.c sends them). The host reaches Fast RAM by the Amiga's own
 * addresses, so an address is the pointer.
 *
 * The frame reaches the display (plan section 6):
 *  - a screen of its own, or the program's bitmap: ACV_PRESENT at the end
 *    of the frame's block, the host writing the frame straight into the
 *    bitmap (locked while the block runs), in its own format;
 *  - a window: ORTG_PresentVirgl (gfopenrtg.library 0.15), which hands the
 *    host the window's visible rectangles;
 *  - otherwise (no ACV_PRESENT on the host, a format the host can't
 *    write): the frame comes back (ACV_GET) and goes in with
 *    WritePixelArray. Correct, slower.
 * No display lock is held between frames. */
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <cybergraphx/cybergraphics.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/cybergraphics.h>

#include "mgl_vg.h"
#include "../../include/gfopengpu/gfopengpu.h"
#include "../../include/gfopengpu/build.h"
#include "../../include/proto/gfopengpu.h"
#define OPENRTG_BASE_NAME mgl_ortg_base
#include "../../include/proto/gfopenrtg.h"

extern struct Library *OpenGPUBase;
extern struct Library *CyberGfxBase;
struct Library *mgl_ortg_base;
static int ortg_tried;

int vg_os_open(GLcontext c)
{
    (void)c;
    if (!OpenGPUBase) { mgl_log("virgl: no gfopengpu.library"); return 0; }
    if (OGPU_ANSWER(OGPU_Query(OGPU_OP_VIRGL, 0)) != OGPU_FULL) {
        mgl_log("virgl: gfopengpu.library has no driver that carries virgl");
        return 0;
    }
    return 1;
}

void vg_os_close(GLcontext c) { (void)c; }

int vg_os_run(GLcontext c, ULONG *blk, ULONG bytes)
{
    ULONG stream[4], fence = 0;
    struct OGPUBatch b;
    LONG r;
    (void)c;
    ogpu_batch_init(&b, stream, 4);
    ogpu_virgl(&b, (unsigned long)blk, bytes);
    if (OGPU_Submit(stream, (ULONG)b.words, &fence) != OGPU_OK) return -1;
    r = OGPU_Wait(fence);
    if (r == OGPU_ERR_EXPIRED) {
        /* other programs' batches came between: ours is done, not failed
         * (gla_virgl_os3.c's rule) */
        r = OGPU_Wait(OGPU_FENCE_ALL);
        if (r == OGPU_ERR_EXPIRED) r = OGPU_OK;
    }
    return r == OGPU_OK ? 0 : -1;
}

/* ---- presenting ------------------------------------------------------------------------------ */

static APTR present_lock;               /* the bitmap's, while the frame's block runs */

static int ortg(void)
{
    if (!mgl_ortg_base && !ortg_tried) {
        ortg_tried = 1;
        if ((mgl_ortg_base = OpenLibrary((CONST_STRPTR)"gfopenrtg.library", 0)) != 0
            && mgl_ortg_base->lib_Version == 0 && mgl_ortg_base->lib_Revision < 15) {
            CloseLibrary(mgl_ortg_base);
            mgl_ortg_base = 0;
        }
    }
    return mgl_ortg_base != 0;
}

ULONG vg_os_present(GLcontext c, ULONG *p, ULONG room)
{
    struct RastPort *rp;
    struct BitMap *bm;
    int x, y, mode = mgl_disp_target(c, &rp, &bm, &x, &y);
    ULONG base = 0, bpr = 0, pf = 0, fmt, a[12];
    present_lock = 0;
    if (c->sync && mode != MGL_TARGET_BITMAP) WaitTOF();
    if (!(c->vg->host_flags & ACV_HOST_PRESENT) || !bm || mode == MGL_TARGET_WINDOW || room < 12 || !CyberGfxBase)
        return 0;
    present_lock = LockBitMapTags(bm, LBMI_BASEADDRESS, (ULONG)&base, LBMI_BYTESPERROW, (ULONG)&bpr,
                                  LBMI_PIXFMT, (ULONG)&pf, TAG_DONE);
    if (!present_lock) return 0;
    if (pf == PIXFMT_ARGB32)            /* GFOpenRTG's 32-bit pixels: XRGB32 where the host has it */
        fmt = OGPU_ANSWER(OGPU_Query(OGPU_OP_PIXELS, OGPU_FMT_XRGB32)) != OGPU_NONE ? OGPU_FMT_XRGB32 : OGPU_FMT_ARGB32;
    else if (pf == PIXFMT_RGB16) fmt = OGPU_FMT_RGB565;
    else { UnLockBitMap(present_lock); present_lock = 0; return 0; }
    a[0] = c->vg->ctx_id; a[1] = c->vg->colour->handle; a[2] = 0;
    a[3] = ((ULONG)(UWORD)x << 16) | (UWORD)y;
    a[4] = base; a[5] = bpr; a[6] = fmt;
    a[7] = (GetCyberMapAttr(bm, CYBRMATTR_WIDTH) << 16) | (GetCyberMapAttr(bm, CYBRMATTR_HEIGHT) & 0xFFFF);
    a[8] = 0; a[9] = 1;
    a[10] = ((ULONG)(UWORD)x << 16) | (UWORD)y;
    a[11] = ((ULONG)c->width << 16) | (ULONG)c->height;
    vg_blk_cmd(p, ACV_PRESENT, 12, a);
    return VG_BLK_WORDS(12);
}

/* The frame back from the host and written in: the slow way. */
static void present_readback(GLcontext c, struct RastPort *rp, int x, int y)
{
    ULONG *buf = mgl_alloc((ULONG)c->width * c->height * 4);
    if (!buf) return;
    mgl_vg_readback(c, 0, 0, c->width, c->height, buf, c->width);
    WritePixelArray(buf, 0, 0, (UWORD)(c->width * 4), rp, (UWORD)x, (UWORD)y, (UWORD)c->width, (UWORD)c->height, RECTFMT_ARGB);
    mgl_free(buf);
}

void vg_os_presented(GLcontext c, ULONG *blk, ULONG at)
{
    struct RastPort *rp, brp;
    struct BitMap *bm;
    int x, y, mode;
    if (present_lock) {
        UnLockBitMap(present_lock);
        present_lock = 0;
        if (blk && vg_result(blk, at) >= 0) return;
    }
    mode = mgl_disp_target(c, &rp, &bm, &x, &y);
    if (!rp && bm) { InitRastPort(&brp); brp.BitMap = bm; rp = &brp; }
    if (!rp) return;
    if (mode == MGL_TARGET_WINDOW && (c->vg->host_flags & ACV_HOST_PRESENT) && ortg()) {
        struct OpenRTGVirglFrame f;
        f.context = c->vg->ctx_id;
        f.handle = c->vg->colour->handle;
        f.width = (UWORD)c->width; f.height = (UWORD)c->height;
        f.damage_x = 0; f.damage_y = 0; f.damage_w = 0; f.damage_h = 0;
        f.flags = 0;                    /* top row first */
        if (ORTG_PresentVirgl(rp, x, y, &f) >= 0) return;
    }
    if (CyberGfxBase) present_readback(c, rp, x, y);
}
