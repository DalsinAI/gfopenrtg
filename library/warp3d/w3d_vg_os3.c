/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * Warp3D.library's virgl route on AmigaOS: the route's start and end for a
 * context. The transport w3d_vg.c runs its request blocks through is the
 * shared pipe's (library/pipe/pipe_xport_os3.c, stage M2 of
 * docs/design/OpenGPU-Shared-Pipe-Plan.md).
 *
 * The frame reaches the display through gfopenrtg.library's ORTG_Present
 * (0.30, stage M8 of the plan): prepared as a sync point's block is ended,
 * so a GFOpenRTG bitmap gets its ACV_PRESENT in that block, and done after
 * it ran (any other bitmap: the frame read back). The frame's alpha goes
 * too (Warp3D programs may use destination alpha). Without 0.30, with a
 * W3D_Bitmap, or with ENV:Warp3D/PresentOld set (kept for one release),
 * the route's own way: the host's ACV_PRESENT, which w3d_vg.c
 * puts at the end of a sync point's block: the host writes the frame
 * straight into the drawing area (the bitmap locked by w3d_batch.c while
 * the block runs), in its own format: A8R8G8B8 as it is (Warp3D programs
 * may use destination alpha), R5G6B5 through the host's RGB565 stage. */
#include <exec/memory.h>
#include <graphics/rastport.h>
#include <proto/exec.h>
#include <proto/graphics.h>

#include "w3d_internal.h"
#include "../../include/gfopengpu/gfopengpu.h"
#include "../../include/gfopengpu/build.h"
#include "../../include/gfopengpu/virgl.h"
#include "../../include/proto/gfopengpu.h"
#define OPENRTG_BASE_NAME w3d_ortg_base
#include "../../include/proto/gfopenrtg.h"

static struct Library *w3d_ortg_base;
static int ortg_tried;

static void *os_alloc(void *user, ULONG bytes) { (void)user; return AllocVec(bytes ? bytes : 4, MEMF_ANY | MEMF_CLEAR); }
static void os_free(void *user, void *p) { (void)user; if (p) FreeVec(p); }
static void os_log(void *user, const char *msg) { (void)user; w3d_trace("%s", msg); }

static int os_area_lock(void *user, ULONG *address, ULONG *bpr) { return w3d_area_lock(user, address, bpr); }
static void os_area_unlock(void *user) { w3d_area_unlock(user); }

/* gfopenrtg.library 0.30 or later, opened once (closed with Warp3D.library: w3d_vg_expunge). */
static int ortg30(void)
{
    if (!w3d_ortg_base && !ortg_tried) {
        ortg_tried = 1;
        if ((w3d_ortg_base = OpenLibrary((CONST_STRPTR)"gfopenrtg.library", 0)) != 0
            && w3d_ortg_base->lib_Version == 0 && w3d_ortg_base->lib_Revision < 30) {
            CloseLibrary(w3d_ortg_base);
            w3d_ortg_base = 0;
        }
    }
    return w3d_ortg_base != 0;
}

void w3d_vg_expunge(void)
{
    if (w3d_ortg_base) CloseLibrary(w3d_ortg_base);
    w3d_ortg_base = 0;
    ortg_tried = 0;
}

/* The frame at the drawing area's place in its bitmap: ORTG_Present, prepared (into the block at p). -1: the
 * route's own present. */
static LONG os_present_prepare(void *user, ULONG ctx, ULONG handle, ULONG *p, ULONG room)
{
    struct w3dctx *x = user;
    W3D_Context *c = x->ctx;
    struct ORTG_PresentInfo in;
    LONG n;
    x->present_token = 0;
    if (W3DBase->present_old || c->w3dbitmap || !x->direct || !c->drawregion || !ortg30()) return -1;
    InitRastPort(&x->present_rp);
    x->present_rp.BitMap = c->drawregion;
    w3d_clear(&in, sizeof in);
    in.size = sizeof in;
    in.kind = ORTG_SURF_VIRGL;
    in.context = ctx; in.handle = handle;
    in.width = (UWORD)c->width; in.height = (UWORD)c->height;
    in.rp = &x->present_rp; in.x = 0; in.y = (long)c->yoffset;
    in.flags = ORTG_PRESENT_PREPARE | ORTG_PRESENT_ALPHA;
    in.blk = p; in.room = room;
    n = ORTG_Present(&in);
    if (n < 0 || !in.token) return -1;
    x->present_token = in.token;
    return n;
}

static void os_present_done(void *user, const ULONG *blk, ULONG at)
{
    struct w3dctx *x = user;
    ORTG_PresentDone(x->present_token, blk, at);
    x->present_token = 0;
}

int w3d_vg_start(struct w3dctx *x, int need_chip)
{
    struct w3dvg_os os;
    struct w3dvg_info info;
    if (!OpenGPUBase || !W3DBase->gpu_virgl) return 0;
    os.alloc = os_alloc; os.free = os_free; os.log = os_log; os.user = x;
    /* the draws are calls to the route, which asks for the drawing area when a send needs it */
    os.area_lock = os_area_lock;
    os.area_unlock = os_area_unlock;
    os.present_prepare = os_present_prepare;
    os.present_done = os_present_done;
    info.host_flags = info.caps_bits = 0;
    x->vg = w3dvg_open(&os, &info);
    if (!x->vg) { w3d_trace("virgl: the host has no virgl for Warp3D"); return 0; }
    W3DBase->virgl_chip = (info.host_flags & ACV_HOST_GPU) != 0;
    w3d_trace("virgl: host flags %lx", (unsigned long)info.host_flags);
    if (need_chip && !W3DBase->virgl_chip) {
        /* Auto: the host draws virgl without a graphics chip, so GFOpenGPU */
        w3dvg_close(x->vg);
        x->vg = 0;
        return 0;
    }
    if (!(info.host_flags & ACV_HOST_PRESENT)) {
        /* the frame could only come back the slow way: not worth the route */
        w3d_trace("virgl: the host has no ACV_PRESENT");
        w3dvg_close(x->vg);
        x->vg = 0;
        return 0;
    }
    x->vg_hold = 0;
    x->vg_errors = 0;
    return 1;
}

void w3d_vg_stop(struct w3dctx *x)
{
    struct w3dvg_stats st;
    if (!x->vg) return;
    w3dvg_stats(x->vg, &st);
    w3d_trace("virgl: %ld commands not drawn; last stats: %ld blocks %ld draws %ld presents %ld loads %ld refused %ld declined",
              (long)x->vg_errors, (long)st.blocks, (long)st.draws, (long)st.presents, (long)st.loads, (long)st.refused, (long)st.declined);
    w3dvg_close(x->vg);
    x->vg = 0;
    x->vg_hold = 0;
}
