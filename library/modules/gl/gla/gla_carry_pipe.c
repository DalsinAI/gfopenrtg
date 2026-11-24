/* Copyright (c) 2026 AmiCradle Team. OpenGPU's GL module, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * The carrier on GFOpenGPU's shared pipe (gla_carry.h): the GL module's virgl route, carried by library/pipe as
 * MiniGL's and Warp3D's are (the GL stage of docs/design/OpenGPU-Shared-Pipe-Plan.md, 24 November 2026).
 *
 * Mesa's virgl driver writes the whole command stream, so this takes little of the pipe: its screen (the HELLO, the
 * context, the caps), a context without arenas, its block, its transport, and from version 6 of its table resources
 * of any shape (res_create2), transfers of any box (xfer) and the stream's cut. No state objects, no fixed-function
 * shaders, no arenas: Mesa has its own, and they are the one exception the design allows.
 *
 * The order. Mesa's calls are first in, first out: a PUT made after a draw must run after it. The pipe's block is
 * pre-commands, stream, post-commands. So before every PUT the stream so far is cut (stream_cut: it becomes an
 * ACV_SUBMIT among the pre-commands) and the PUT goes after it, in the same block; a create goes ahead of the
 * streams (nothing made earlier can name it) and an unref after them all.
 *
 * When a block goes: as the winsys of before the pipe sent it (9 November 2026), so a frame is the same blocks as before:
 *   - a GET, a fence waited for, a frame's present (gla_carry_flush), the context's end;
 *   - a PUT too big to copy (over Q_COPY, or no room left in the arena): read where it is, sent at once with what
 *     is queued before it, so no resource is ever busy when a call returns;
 *   - the caps of one block: Q_CMDS commands or Q_BYTES of data;
 *   - unbatched (GLBatch 0, or the runtime's ACV_HOST_NO_BATCH): after every command.
 * Blocks are waited for as they are sent (the pipe's transport); the old winsys's pipelining, never on by default, is
 * not carried over. */
#include <exec/types.h>
#include "gfopengpu/pipe.h"
#include "gfopengpu/virgl.h"
#include "gla_carry.h"

#define Q_CMDS 256u                     /* commands in one block at most */
#define Q_BYTES (384u * 1024u)          /* data one block reads at most (puts and command streams) */
#define Q_COPY (64u * 1024u)            /* puts this big or smaller are copied */
#define ARENA_BYTES (1024u * 1024u)     /* the copies */
#define STREAM_MAX 65536u               /* stream words given to the pipe in one piece */

struct gla_carry {
    const struct OGPUPipeOps *ops;
    OGPUPipeScreen *screen;
    OGPUPipeCtx *ctx;
    struct gla_carry_mem mem;
    UBYTE *arena;                       /* the copies a block's PUTs read */
    ULONG arena_used;
    ULONG seen;                         /* the pipe's blocks sent, as last looked at */
    ULONG ncmds, qbytes;                /* in the block being built */
    ULONG blocks, refused;              /* since gla_carry_counts */
    int batch;
};

/* The pipe's resource, and its handle on the host: the one it was made with, as nothing here asks for a new
 * version (Mesa's stream names the handle). */
struct gla_carry_res { OGPUPipeRes *r; ULONG handle; };

/* A block went (asked for here, or the pipe's own when its stream filled): its refusals counted, the arena and the
 * caps start again. */
static void looked(struct gla_carry *c)
{
    ULONG sent = c->ops->sent(c->ctx);
    if (sent == c->seen) return;
    c->blocks += sent - c->seen;
    if (c->batch) c->refused += c->ops->errors(c->ctx);   /* unbatched, each command's caller has its answer */
    c->seen = sent;
    c->arena_used = 0;
    c->ncmds = 0;
    c->qbytes = 0;
}

static LONG send(struct gla_carry *c)
{
    LONG r = c->ops->flush(c->ctx, 0);
    looked(c);
    return r;
}

/* After a command that may wait: unbatched, or over a cap, the block goes now. */
static void after(struct gla_carry *c)
{
    looked(c);
    c->ncmds++;
    if (!c->batch || c->ncmds >= Q_CMDS || c->qbytes >= Q_BYTES) send(c);
}

/* Room in the block for a command among the pre-commands, so that making it sends nothing (a send then would
 * forget the arena under a copy). */
static void reserve(struct gla_carry *c, ULONG pre_words)
{
    if (!c->ops->room(c->ctx, 0, 0, 0, pre_words)) send(c);
}

struct gla_carry *gla_carry_open(const struct gla_carry_mem *mem, unsigned long ctx_id, int batch_wanted,
                                 void *caps, unsigned long caps_bytes,
                                 unsigned long *host_flags, int *caps_ok, int *status)
{
    static const char name[] = "GLA";
    struct gla_carry *c;
    struct OGPUPipeOpen o;
    struct OGPUPipeInfo info;
    ULONG *blk;
    char *nm;
    ULONG i;
    int dummy;
    if (!status) status = &dummy;
    *status = GLA_CARRY_NO_MEMORY;
    if (!mem || !caps) return 0;
    if (!(c = mem->alloc(mem->user, sizeof *c))) return 0;
    for (i = 0; i < sizeof *c; i++) ((char *)c)[i] = 0;     /* the memory comes as it is */
    c->mem = *mem;
    if (!(c->ops = ogpu_pipe_ops(6))) { *status = GLA_CARRY_NO_PIPE; goto fail; }
    /* the open's block and the context's name, where the host reaches them */
    if (!(blk = mem->alloc(mem->user, 16 * 4 + 8))) goto fail;
    nm = (char *)(blk + 16);
    for (i = 0; i < sizeof name; i++) nm[i] = name[i];
    o.size = sizeof o;
    o.emit_wanted = OGPU_EMIT_VIRGL;
    o.ctx_id = (ULONG)ctx_id;
    o.name = nm;
    o.name_len = sizeof name - 1;
    o.blk = blk;
    o.caps = caps;
    o.caps_bytes = (ULONG)caps_bytes;
    c->screen = c->ops->screen_open(&o, &info);
    mem->free(mem->user, blk);
    c->blocks = info.blocks;
    if (!c->screen) {
        *status = info.status == OGPU_PIPE_OPEN_NO_TRANSPORT ? GLA_CARRY_NO_TRANSPORT
                : info.status == OGPU_PIPE_OPEN_PROTOCOL ? GLA_CARRY_PROTOCOL
                : info.status == OGPU_PIPE_OPEN_NO_CONTEXT ? GLA_CARRY_NO_CONTEXT : GLA_CARRY_NO_RUN;
        goto fail;
    }
    if (!(c->ctx = c->ops->ctx_create(c->screen, OGPU_PIPE_CTX_NO_ARENAS))) {
        /* the host has the context (the open's block made it) and no pipe context ends it: nothing more can be
         * said to the host from here but through one, so it stays until the runtime ends the program's contexts */
        c->ops->screen_close(c->screen);
        goto fail;
    }
    c->seen = c->ops->sent(c->ctx);
    if (host_flags) *host_flags = info.host_flags;
    if (caps_ok) *caps_ok = info.status != OGPU_PIPE_OPEN_NO_CAPS;
    /* batched unless the program (0) or the runtime (ACV_HOST_NO_BATCH, when the program doesn't say 1) says no,
     * and only with the arena for the queued copies */
    if (batch_wanted != 0 && !((info.host_flags & ACV_HOST_NO_BATCH) && batch_wanted != 1)
        && (c->arena = mem->alloc(mem->user, ARENA_BYTES)) != 0)
        c->batch = 1;
    *status = GLA_CARRY_OK;
    return c;
fail:
    mem->free(mem->user, c);
    return 0;
}

void gla_carry_close(struct gla_carry *c)
{
    struct gla_carry_mem m;
    if (!c) return;
    m = c->mem;
    gla_carry_flush(c);                 /* everything queued runs first, as on the old route */
    c->ops->ctx_destroy(c->ctx);
    c->ops->screen_close(c->screen);
    if (c->arena) m.free(m.user, c->arena);
    m.free(m.user, c);
}

struct gla_carry_res *gla_carry_res_create(struct gla_carry *c, const unsigned long shape[10])
{
    struct OGPUPipeResDesc2 d;
    struct gla_carry_res *r;
    if (!(r = c->mem.alloc(c->mem.user, sizeof *r))) return 0;
    d.size = sizeof d;
    d.target = (ULONG)shape[0]; d.format = (ULONG)shape[1]; d.bind = (ULONG)shape[2];
    d.width = (ULONG)shape[3]; d.height = (ULONG)shape[4]; d.depth = (ULONG)shape[5];
    d.array_size = (ULONG)shape[6]; d.last_level = (ULONG)shape[7]; d.nr_samples = (ULONG)shape[8];
    d.flags = (ULONG)shape[9];
    reserve(c, 16);
    if (!(r->r = c->ops->res_create2(c->ctx, &d))) { c->mem.free(c->mem.user, r); return 0; }
    r->handle = c->ops->res_handle(r->r);
    if (!c->batch) {
        /* made now: a refusal is known here */
        if (send(c) || c->ops->errors(c->ctx)) {
            c->ops->res_release(c->ctx, r->r);
            send(c);
            c->mem.free(c->mem.user, r);
            return 0;
        }
        return r;
    }
    after(c);
    return r;
}

unsigned long gla_carry_res_handle(const struct gla_carry_res *r)
{
    return r ? r->handle : 0;
}

void gla_carry_res_destroy(struct gla_carry *c, struct gla_carry_res *r)
{
    if (!r) return;
    c->ops->res_release(c->ctx, r->r);  /* its unref after the block's streams */
    c->mem.free(c->mem.user, r);
    after(c);
}

static void box_of(struct OGPUPipeXfer *t, const struct gla_carry_box *b, void *mem, unsigned long bytes,
                   unsigned long swap, ULONG flags)
{
    t->size = sizeof *t;
    t->flags = flags;
    t->level = (ULONG)b->level; t->stride = (ULONG)b->stride; t->layer_stride = (ULONG)b->layer_stride;
    t->box.x = (LONG)b->x; t->box.y = (LONG)b->y; t->box.z = (LONG)b->z;
    t->box.w = (ULONG)b->w; t->box.h = (ULONG)b->h; t->box.d = (ULONG)b->d;
    t->mem = mem;
    t->bytes = (ULONG)bytes;
    t->swap = (ULONG)swap;
}

int gla_carry_put(struct gla_carry *c, struct gla_carry_res *r, const struct gla_carry_box *b,
                  const void *mem, unsigned long bytes, unsigned long swap)
{
    struct OGPUPipeXfer t;
    ULONG n = ((ULONG)bytes + 3) & ~3UL, i;
    const UBYTE *src = mem;
    int copied = 0;
    LONG rc;
    looked(c);
    reserve(c, 16 + 5);                 /* the cut's SUBMIT and the PUT */
    if (c->batch && bytes && bytes <= Q_COPY) {
        if (c->arena_used + n > ARENA_BYTES) send(c);
        {
            UBYTE *copy = c->arena + c->arena_used;
            for (i = 0; i < (ULONG)bytes; i++) copy[i] = src[i];
            c->arena_used += n;
            src = copy;
            copied = 1;
        }
    }
    (void)c->ops->stream_cut(c->ctx);   /* the draws made so far run first */
    box_of(&t, b, (void *)src, bytes, swap, 0);
    rc = c->ops->xfer(c->ctx, r->r, &t);
    c->qbytes += (ULONG)bytes;
    if (rc) { looked(c); return -1; }
    if (!c->batch || !copied) {
        /* read where it is: sent now, with what is queued before it. Left queued, its resource would be busy, and
         * Mesa then gives a buffer it rewrites whole a new resource rather than waiting (9 November) */
        c->ncmds++;
        if (send(c)) return -1;
        return !c->batch && c->ops->errors(c->ctx) ? -1 : 0;
    }
    after(c);
    return 0;
}

int gla_carry_get(struct gla_carry *c, struct gla_carry_res *r, const struct gla_carry_box *b,
                  void *mem, unsigned long bytes, unsigned long swap)
{
    struct OGPUPipeXfer t;
    LONG rc;
    looked(c);
    reserve(c, 16 + 5);
    box_of(&t, b, mem, bytes, swap, OGPU_XFER_GET);
    rc = c->ops->xfer(c->ctx, r->r, &t);
    looked(c);
    if (rc && c->refused) c->refused--; /* its own refusal is the caller's answer, not a queued command's */
    return (int)rc;
}

/* One piece of a stream into the pipe's. */
static int piece(struct gla_carry *c, const ULONG *w, ULONG n)
{
    ULONG *p = c->ops->cmd_room(c->ctx, n), i;
    looked(c);                          /* the pipe sends first when its stream is full */
    if (!p) return -1;
    for (i = 0; i < n; i++) p[i] = w[i];
    return 0;
}

static ULONG le32(ULONG v)
{
    const UBYTE *b = (const UBYTE *)&v;
    return (ULONG)b[0] | ((ULONG)b[1] << 8) | ((ULONG)b[2] << 16) | ((ULONG)b[3] << 24);
}

int gla_carry_submit(struct gla_carry *c, const void *words, unsigned long n)
{
    const ULONG *w = words;
    ULONG left = (ULONG)n;
    if (!left) return 0;
    looked(c);
    /* A stream longer than one piece goes in pieces that end where a command ends (a command's header carries its
     * length in its top 16 bits, so none is longer than a piece). */
    while (left > STREAM_MAX) {
        ULONG take = 0;
        for (;;) {
            ULONG len = (le32(w[take]) >> 16) + 1;
            if (take + len > STREAM_MAX || take + len > left) break;
            take += len;
        }
        if (!take || piece(c, w, take)) return -1;
        w += take;
        left -= take;
    }
    if (piece(c, w, left)) return -1;
    c->qbytes += (ULONG)n * 4;
    after(c);
    return 0;
}

unsigned long gla_carry_fence(struct gla_carry *c)
{
    looked(c);
    return c->seen + (c->ops->queued(c->ctx) ? 1 : 0);
}

void gla_carry_wait(struct gla_carry *c, unsigned long fence)
{
    looked(c);
    if ((LONG)((ULONG)fence - c->seen) > 0) send(c);
}

void gla_carry_flush(struct gla_carry *c)
{
    send(c);
}

void gla_carry_counts(struct gla_carry *c, unsigned long *blocks, unsigned long *refused)
{
    looked(c);
    if (blocks) *blocks = c->blocks;
    if (refused) *refused = c->refused;
    c->blocks = c->refused = 0;
}
