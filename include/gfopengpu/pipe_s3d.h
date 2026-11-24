/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * The shared pipe's stream3d emitter (stage M7, part 1; library/pipe/pipe_emit_s3d.c): one builder of a
 * stream3d batch for the front ends that draw on the 68k or through gfopengpu.library's 3D ops. Warp3D
 * (w3d_batch.c) and MiniGL (mgl_out.c) each had this code; the words it writes are theirs, one for one
 * (tests/pipe/test_s3d_same.c).
 *
 * A batch is the commands, with room in front of them for SURFACE and TARGET (written when it is finished,
 * since the drawing area's address is known only once it is locked), and a data area for vertices. The front
 * end owns both pieces of memory, decides when a batch is sent and where it runs, and keeps what it has sent
 * (its state caches); the emitter keeps the batch.
 *
 * It has a table of its own (ogpu_pipe_s3d_ops), apart from struct OGPUPipeOps: it needs no screen and no
 * context, and a front end that never draws this way (GL.module) doesn't link it or gfopengpu's builders. New
 * calls only ever go at its end, with a new version. */
#ifndef GFOPENGPU_PIPE_S3D_H
#define GFOPENGPU_PIPE_S3D_H

#include <exec/types.h>
#include "build3d.h"

/* A buffer of the drawing area: the colour surface, depth or stencil. */
struct OGPUPipeS3DBuf {
    unsigned long address;
    ULONG bpr;
    LONG w, h;
    LONG format;                /* OGPU_FMT_ */
};

struct OGPUPipeS3D {
    struct OGPUBatch b;         /* the commands, from cmd + prefix_words */
    ULONG *cmd;                 /* batch_words of memory */
    LONG batch_words, prefix_words;
    UBYTE *vmem;                /* the data area */
    ULONG vcap, vused;
    LONG body;                  /* the batch has commands */
    LONG list_at;               /* the word of the last TRIANGLES list, or -1: the next list right after it, with
                                 * nothing sent between, makes it longer. The front end sets -1 when it changes
                                 * state the list was drawn with. */
    ULONG list_vend;            /* where that list's vertices end */
};

#define OGPU_PIPE_S3D_VERSION 1

struct OGPUPipeS3DOps {
    ULONG version;              /* first, always */

    /* A batch over the front end's memory; nothing in it. */
    void  (*init)(struct OGPUPipeS3D *, ULONG *cmd, LONG batch_words, LONG prefix_words, UBYTE *vmem, ULONG vert_bytes);
    /* 1 when `words` more command words and `vbytes` more data fit; 0: send the batch first. */
    LONG  (*room)(const struct OGPUPipeS3D *, LONG words, ULONG vbytes);
    /* The batch started if it hasn't: DEPTH for each buffer given (0: none). 1 when this call started it: the
     * front end then has nothing sent (its texture, clip and RENDER3D go again). */
    LONG  (*begin)(struct OGPUPipeS3D *, const struct OGPUPipeS3DBuf *depth, const struct OGPUPipeS3DBuf *stencil);
    /* bytes of the data area (rounded up to a long), or 0 when it is full. */
    UBYTE *(*data)(struct OGPUPipeS3D *, ULONG bytes);
    void  (*clip)(struct OGPUPipeS3D *, LONG x, LONG y, LONG w, LONG h);
    void  (*render3d)(struct OGPUPipeS3D *, const unsigned long *args);     /* OGPU_R3D_ARGS of them */
    void  (*texenv)(struct OGPUPipeS3D *, LONG unit, LONG mode, unsigned long colour);
    void  (*texture)(struct OGPUPipeS3D *, LONG unit, unsigned long address, unsigned long bpr, LONG w, LONG h,
                     unsigned long format, unsigned long table, unsigned long generation, LONG min, LONG mag,
                     LONG wrap_s, LONG wrap_t, unsigned long border, LONG levels, const unsigned long *level_addrs);
    /* n vertices at vaddr (the data area's, as the stream names addresses) as a list, strip or fan (OGPU_LAY_):
     * TRIANGLES, or a longer last list. tex: OGPU_TRI_UNIT0 or OGPU_TRI_NOTEX. layout without the primitive. */
    void  (*emit)(struct OGPUPipeS3D *, unsigned long vaddr, ULONG n, ULONG stride, LONG prim, LONG tex, unsigned long layout);
    void  (*fill)(struct OGPUPipeS3D *, LONG x, LONG y, LONG w, LONG h, unsigned long raw);
    void  (*clear3d)(struct OGPUPipeS3D *, LONG kind, LONG x, LONG y, LONG w, LONG h, unsigned long value);
    /* The batch as a stream: SURFACE (slot 0) and TARGET put in front of the commands. Answers the words and
     * where they start; the batch has no body afterwards, and reset() empties it once it has run. With no
     * commands it is SURFACE and TARGET alone (a present of what the host holds). */
    LONG  (*finish)(struct OGPUPipeS3D *, const struct OGPUPipeS3DBuf *surface, UBYTE **stream);
    void  (*reset)(struct OGPUPipeS3D *);

    /* new calls only ever added here, at the end */
};

/* The table, or 0 when this pipe is older than the version asked for. */
const struct OGPUPipeS3DOps *ogpu_pipe_s3d_ops(ULONG version);

#endif
