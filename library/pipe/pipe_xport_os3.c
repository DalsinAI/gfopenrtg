/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * The shared pipe's transport on AmigaOS (docs/design/OpenGPU-Shared-Pipe-Plan.md, 2.2 and stage M2).
 *
 * A request block is one OGPU_OP_VIRGL command in a batch for gfopengpu.library, done when OGPU_Wait returns. The
 * host reaches Fast RAM by the Amiga's own addresses, so an address is the pointer.
 *
 * Moved here from minigl.library's mgl_vg_os3.c (vg_os_open, vg_os_run) and Warp3D.library's w3d_vg_os3.c
 * (os_run, os_alloc, os_free), which were the same code. The library that links the pipe opens gfopengpu.library
 * itself, as OpenGPUBase. */
#include <exec/memory.h>
#include <proto/exec.h>

#include "../../include/gfopengpu/pipe.h"
#include "../../include/gfopengpu/gfopengpu.h"
#include "../../include/gfopengpu/build.h"
#include "../../include/proto/gfopengpu.h"

extern struct Library *OpenGPUBase;

int ogpu_pipe_xport_ready(void)
{
    return OpenGPUBase && OGPU_ANSWER(OGPU_Query(OGPU_OP_VIRGL, 0)) == OGPU_FULL;
}

LONG ogpu_pipe_xport_run(ULONG *blk, ULONG bytes)
{
    ULONG stream[4], fence = 0;
    struct OGPUBatch b;
    LONG r;
    ogpu_batch_init(&b, stream, 4);
    ogpu_virgl(&b, (unsigned long)blk, bytes);
    if (OGPU_Submit(stream, (ULONG)b.words, &fence) != OGPU_OK) return -1;
    r = OGPU_Wait(fence);
    if (r == OGPU_ERR_EXPIRED) {
        /* other programs' batches came between: ours is done, not failed (gla_virgl_os3.c's rule) */
        r = OGPU_Wait(OGPU_FENCE_ALL);
        if (r == OGPU_ERR_EXPIRED) r = OGPU_OK;
    }
    return r == OGPU_OK ? 0 : -1;
}

void *ogpu_pipe_xport_alloc(ULONG bytes)
{
    return AllocVec(bytes ? bytes : 4, MEMF_ANY | MEMF_CLEAR);
}

void ogpu_pipe_xport_free(void *p)
{
    if (p) FreeVec(p);
}
