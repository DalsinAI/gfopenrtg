/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * The shared pipe's transport for the PC tests (docs/design/OpenGPU-Shared-Pipe-Plan.md, 2.2 and stage M2).
 *
 * The tests are built 32-bit (their pointers are ULONGs, as on the Amiga) and run as the child of
 * tests/minigl_vg_bridge (64-bit), which carries each block to virglrenderer with acvirgl_run, reading and writing
 * the child's memory as the Amiga's. A block goes down the bridge's pipe as its address and size; the result
 * comes back. The pipe's descriptors are in MGL_VG_BRIDGE ("read,write"); without it there is no virgl.
 *
 * Moved here from tests/test_minigl.c (vg_os_open, vg_os_run) and tests/test_w3d_vg.c (os_run), which were the
 * same code. */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "../../include/gfopengpu/pipe.h"

static int bridge_r = -1, bridge_w = -1, tried;

int ogpu_pipe_xport_ready(void)
{
    if (!tried) {
        const char *e = getenv("MGL_VG_BRIDGE");
        tried = 1;
        if (!e || sscanf(e, "%d,%d", &bridge_r, &bridge_w) != 2) bridge_r = bridge_w = -1;
    }
    return bridge_r >= 0 && bridge_w >= 0;
}

LONG ogpu_pipe_xport_run(ULONG *blk, ULONG bytes)
{
    ULONG req[2];
    int r = -1;
    if (!ogpu_pipe_xport_ready()) return -1;
    req[0] = (ULONG)blk; req[1] = bytes;
    if (write(bridge_w, req, sizeof req) != (ssize_t)sizeof req || read(bridge_r, &r, sizeof r) != (ssize_t)sizeof r) return -1;
    return r;
}

void *ogpu_pipe_xport_alloc(ULONG bytes)
{
    return calloc(1, bytes ? bytes : 4);
}

void ogpu_pipe_xport_free(void *p)
{
    free(p);
}
