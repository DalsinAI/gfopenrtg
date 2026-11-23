/* Copyright (c) 2026 AmiCradle Team. OpenGPU's GL module, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * The carrier: how the GLA core's virgl winsys (gla_virgl_pipe.c) hands its work to GFOpenGPU's shared pipe
 * (library/pipe, include/gfopengpu/pipe.h). Mesa's virgl driver writes the command stream; the winsys turns its
 * calls into these; the carrier (gla_carry_pipe.c) puts them in the pipe's block in the order they were made and
 * decides when a block goes. Plain C on both sides: the winsys includes Mesa's headers and never the Amiga's, the
 * carrier the pipe's and never Mesa's, so the carrier is tested on a PC without Mesa (tests/pipe/test_gla_carry.sh).
 *
 * The GL stage of docs/design/OpenGPU-Shared-Pipe-Plan.md (24 November 2026). */
#ifndef GLA_CARRY_H
#define GLA_CARRY_H

struct gla_carry;
struct gla_carry_res;

/* Memory the host reaches (the transport's: AllocVec on OS 3.2). */
struct gla_carry_mem {
    void *(*alloc)(void *user, unsigned long bytes);
    void (*free)(void *user, void *p);
    void *user;
};

/* Why an open failed (gla_carry_open's *status) */
enum {
    GLA_CARRY_OK = 0,
    GLA_CARRY_NO_MEMORY,
    GLA_CARRY_NO_PIPE,          /* the pipe linked here is older than this file needs */
    GLA_CARRY_NO_TRANSPORT,     /* gfopengpu.library has no driver that carries virgl */
    GLA_CARRY_NO_RUN,           /* the first request block didn't run */
    GLA_CARRY_PROTOCOL,         /* the runtime's ACVirgl speaks another protocol */
    GLA_CARRY_NO_CONTEXT        /* virglrenderer refused a context */
};

/* The host's context ctx_id, opened. caps: caps_bytes of room the host reaches; the caps arrive there word by word
 * in the Amiga's order, as many as the host has (the rest is left as it was), *caps_ok says whether they came.
 * batch_wanted: -1 as the runtime asks, 0 a block a command, 1 batched. 0 with *status saying why. */
struct gla_carry *gla_carry_open(const struct gla_carry_mem *mem, unsigned long ctx_id, int batch_wanted,
                                 void *caps, unsigned long caps_bytes,
                                 unsigned long *host_flags, int *caps_ok, int *status);
/* Everything queued sent, the context destroyed. */
void gla_carry_close(struct gla_carry *c);

/* A resource as virgl makes it. shape: target, format (virgl's), bind, width, height, depth, array size, last
 * level, samples, flags. Queued before anything that names it; unbatched it is made now and 0 says the host refused. */
struct gla_carry_res *gla_carry_res_create(struct gla_carry *c, const unsigned long shape[10]);
unsigned long gla_carry_res_handle(const struct gla_carry_res *r);
/* Let go of after the queued draws that use it. Its memory may be freed as soon as this returns. */
void gla_carry_res_destroy(struct gla_carry *c, struct gla_carry_res *r);

struct gla_carry_box {
    unsigned long level, stride, layer_stride;
    long x, y, z;
    unsigned long w, h, d;
};
/* Amiga memory to the host, after everything made so far. Small ones are copied (mem may be written again at
 * once); a bigger one is read where it is and has run by the time this returns. swap: ACV_SWAP_. 0, or the host's
 * error when it ran now. */
int gla_carry_put(struct gla_carry *c, struct gla_carry_res *r, const struct gla_carry_box *b,
                  const void *mem, unsigned long bytes, unsigned long swap);
/* The host's pixels back, after everything made so far: done when it returns. 0 when they came. */
int gla_carry_get(struct gla_carry *c, struct gla_carry_res *r, const struct gla_carry_box *b,
                  void *mem, unsigned long bytes, unsigned long swap);
/* A command stream (little-endian words), copied: the caller writes its buffer again at once. 0, or -1. */
int gla_carry_submit(struct gla_carry *c, const void *words, unsigned long n);

/* What a fence made now waits for, and the wait (everything up to it has run when it returns). */
unsigned long gla_carry_fence(struct gla_carry *c);
void gla_carry_wait(struct gla_carry *c, unsigned long fence);
/* Everything queued sent (a frame's commands before its present). */
void gla_carry_flush(struct gla_carry *c);

/* Counts since the last call: request blocks sent, queued commands the host refused. */
void gla_carry_counts(struct gla_carry *c, unsigned long *blocks, unsigned long *refused);

#endif
