/* Copyright (c) 2026 AmiCradle Team. OpenGPU's GL module, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 * The GLA core's virgl winsys (gla_virgl.c). */
#ifndef GLA_VIRGL_H
#define GLA_VIRGL_H

#include "gla_core.h"

struct sw_winsys;
struct virgl_winsys;

/* Display targets come from sws; everything the host reads or writes from
 * t's memory; request blocks go through t. NULL when the host says no. */
struct virgl_winsys *gla_virgl_winsys_create(struct sw_winsys *sws, const struct gla_virgl_transport *t);

/* The route (the GL stage of the shared pipe, 24 Nov 2026). 0 (the default): the winsys on GFOpenGPU's shared pipe
 * (gla_virgl_pipe.c, gla_carry_pipe.c, library/pipe), which gla_virgl_winsys_create then makes. 1: gla_virgl.c's own
 * request blocks, as before, kept for one release; on OS 3.2 ENV:GFOpenGPU/GLPipeOld 1 sets it. Read when a winsys
 * is made. On the pipe the transport's alloc, free and addr are used and its run is not (the pipe has its own);
 * blocks are never sent without waiting (gla_virgl_send, below, is the old route's). */
extern int gla_virgl_pipe_old;
struct virgl_winsys *gla_virgl_pipe_winsys_create(struct sw_winsys *sws, const struct gla_virgl_transport *t);

/* Request blocks batched (gla_virgl.c, 9 Nov 2026): -1 (the default) as the
 * runtime asks (HELLO's ACV_HOST_NO_BATCH, AC_VIRGL_BATCH=0), 0 never, 1
 * always; read when a winsys is made. On OS 3.2 ENV:GFOpenGPU/GLBatch sets it. */
extern int gla_virgl_batch_wanted;
extern unsigned long gla_virgl_batch_errors;    /* queued commands the host refused */
extern unsigned long gla_virgl_blocks;          /* request blocks sent */

/* Pipelined (with batching): a block sent where nothing needs its results
 * yet is not waited for. A transport that can do that sets send (0: sent,
 * *ticket to wait for it with) and wait (0, or an error: the block failed);
 * blocks run in the order sent, run or not. gla_virgl_pipeline_wanted as
 * gla_virgl_batch_wanted (HELLO's ACV_HOST_NO_PIPELINE, AC_VIRGL_PIPELINE=0;
 * on OS 3.2 ENV:GFOpenGPU/GLPipeline). */
extern int gla_virgl_pipeline_wanted;
extern int (*gla_virgl_send)(void *user, void *block, unsigned long bytes, unsigned long *ticket);
extern int (*gla_virgl_wait)(void *user, unsigned long ticket);
extern unsigned long gla_virgl_unwaited;        /* blocks sent without waiting */

#endif
