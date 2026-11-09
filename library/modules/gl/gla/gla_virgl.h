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

/* Request blocks batched (gla_virgl.c, 9 Nov 2026): -1 (the default) as the
 * runtime asks (HELLO's ACV_HOST_NO_BATCH, AC_VIRGL_BATCH=0), 0 never, 1
 * always; read when a winsys is made. On OS 3.2 ENV:GFOpenGPU/GLBatch sets it. */
extern int gla_virgl_batch_wanted;
extern unsigned long gla_virgl_batch_errors;    /* queued commands the host refused */
extern unsigned long gla_virgl_blocks;          /* request blocks sent */

#endif
