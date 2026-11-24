/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * Warp3D.library's virgl route on AmigaOS: the route's start and end for a
 * context. The transport w3d_vg.c runs its request blocks through is the
 * shared pipe's (library/pipe/pipe_xport_os3.c, stage M2 of
 * docs/design/OpenGPU-Shared-Pipe-Plan.md).
 *
 * The frame reaches the display as the host's ACV_PRESENT, which w3d_vg.c
 * puts at the end of a sync point's block: the host writes the frame
 * straight into the drawing area (the bitmap locked by w3d_batch.c while
 * the block runs), in its own format: A8R8G8B8 as it is (Warp3D programs
 * may use destination alpha), R5G6B5 through the host's RGB565 stage. */
#include <exec/memory.h>
#include <proto/exec.h>

#include "w3d_internal.h"
#include "../../include/gfopengpu/gfopengpu.h"
#include "../../include/gfopengpu/build.h"
#include "../../include/gfopengpu/virgl.h"
#include "../../include/proto/gfopengpu.h"

static void *os_alloc(void *user, ULONG bytes) { (void)user; return AllocVec(bytes ? bytes : 4, MEMF_ANY | MEMF_CLEAR); }
static void os_free(void *user, void *p) { (void)user; if (p) FreeVec(p); }
static void os_log(void *user, const char *msg) { (void)user; w3d_trace("%s", msg); }

static int os_area_lock(void *user, ULONG *address, ULONG *bpr) { return w3d_area_lock(user, address, bpr); }
static void os_area_unlock(void *user) { w3d_area_unlock(user); }

int w3d_vg_start(struct w3dctx *x, int need_chip)
{
    struct w3dvg_os os;
    struct w3dvg_info info;
    if (!OpenGPUBase || !W3DBase->gpu_virgl) return 0;
    os.alloc = os_alloc; os.free = os_free; os.log = os_log; os.user = x;
    /* the draws are calls to the route, which asks for the drawing area when a send needs it */
    os.area_lock = os_area_lock;
    os.area_unlock = os_area_unlock;
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
