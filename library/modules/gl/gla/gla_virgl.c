/* Copyright (c) 2026 AmiCradle Team. OpenGPU's GL module, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * The GLA core's virgl winsys: Mesa's virgl driver (Gallium) on GFOpenGPU.
 * Every call virgl makes of its winsys becomes ACVirgl commands
 * (include/gfopengpu/virgl.h) in a request block, which the transport hands
 * to the host: on the Amiga one OGPU_OP_VIRGL batch on the ACRTG board's
 * ring, which the Cradle's runtime gives to virglrenderer; in the host
 * tests a direct call of acvirgl_run.
 *
 * The host draws in its own memory; each resource also has a copy in memory
 * the host reaches (Fast RAM on the Amiga), which virgl maps, and PUT and
 * GET move pixels between the two, turning units round on a big-endian
 * Amiga: a texture's by its format's channel or pixel size, a buffer's by
 * 32 bits (floats). Commands are queued into one block until something
 * needs their results (batching, below); everything in a block is done by
 * the time it returns, so a resource is busy, and a fence not yet done, only
 * while a command that concerns it waits in the queue.
 *
 * Written for gfopenmesa's GLA core (8 October 2026), after virgl's own
 * winsys interface (virgl_winsys.h); Mesa's vtest winsys was the reference
 * for what each call must do. */
#include "gla_virgl.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "pipe/p_defines.h"
#include "pipe/p_state.h"
#include "frontend/sw_winsys.h"
#include "util/format/u_format.h"
#include "util/u_inlines.h"
#include "util/u_memory.h"
#include "util/u_math.h"
#include "c11/threads.h"
#include "util/u_endian.h"
#include "util/box.h"
#include "util/u_surface.h"
#include "virgl/virgl_winsys.h"
#include "virtio-gpu/virgl_hw.h"
#include "virtio-gpu/virgl_protocol.h"
#include "gfopengpu/virgl.h"

/* Words in the Amiga's order (big-endian), as a request block has them. */
#if UTIL_ARCH_BIG_ENDIAN
#define util_cpu_to_be32(x) (x)
#else
#define util_cpu_to_be32(x) util_bswap32(x)
#endif
#define util_be32_to_cpu(x) util_cpu_to_be32(x)

/* The ids the host knows a context and a resource by. virglrenderer has one
 * namespace for all its clients, and every GL program on the Amiga is one,
 * so a fixed context id (and handles counted from 1) made two programs at
 * once share a context and clash on handles. With 32-bit pointers the id is
 * the address of the program's own winsys or resource: unique while it
 * lives, which is while the host knows it. Elsewhere (the host's own test,
 * one program) a context 1 and a count. */
#if UINTPTR_MAX == 0xffffffffu
#define NEW_CTX_ID(w) ((uint32_t)(uintptr_t)(w))
#define NEW_RES_HANDLE(w, res) ((uint32_t)(uintptr_t)(res))
#else
#define NEW_CTX_ID(w) 1u
#define NEW_RES_HANDLE(w, res) ((w)->next_handle++)
#endif
#define CTX_ID (w->ctx_id)

/* Batching (9 Nov 2026, Design-VIRGL-Transport option A). Each command used
 * to be a request block of its own, run and waited for at once: a frame of
 * AssaultCube was some 1,090 blocks, each a round trip through
 * gfopengpu.library, ACRTG.gpu's ring and a FENCE. Now commands queue in one
 * block, first in first out, and the block goes to the host only when
 * something must see its results:
 *   - a GET (a read back, a query's result), CAPS, a context made or gone;
 *   - a frame presented (flush_frontbuffer), before the present;
 *   - a fence waited for (glFinish, glClientWaitSync) whose commands are queued;
 *   - a resource Mesa writes while a queued PUT still reads it in place
 *     (resource_wait; resource_is_busy says so);
 *   - the queue's caps: Q_CMDS commands or Q_BYTES of data (the host's time
 *     for one block, which the 68k waits for), or no room left.
 * A PUT of Q_COPY bytes or fewer is copied into the arena, so Mesa may write
 * the resource again at once (the usual pattern: glBufferSubData over data
 * a queued draw still needs); a bigger one is read where it is, and the
 * resource is busy until its block has run. A SUBMIT's commands are copied
 * into the arena (Mesa writes its command buffer again at once). Creates
 * and unrefs queue too; an unref'd resource's memory is freed once no
 * queued PUT reads it. Results of queued commands are looked at when their
 * block has run: errors are counted (gla_virgl_batch_errors), the first kept.
 * The arena and the queue are allocated once, with the winsys.
 * Off (each command its own block, as before): ENV:GFOpenGPU/GLBatch 0, or a
 * runtime that asks for it (HELLO's ACV_HOST_NO_BATCH: AC_VIRGL_BATCH=0). */
#define Q_WORDS 8192u                   /* the queue: 32 KB of command words */
#define Q_CMDS 256u                     /* commands in one block at most */
#define Q_BYTES (384u * 1024u)          /* data one block reads at most (puts and command streams) */
#define Q_COPY (64u * 1024u)            /* puts this big or smaller are copied */
#define ARENA_BYTES (1024u * 1024u)     /* the copies */
#define NOW_WORDS 64u                   /* a command that runs at once, unbatched */

int gla_virgl_batch_wanted = -1;        /* -1 the runtime's choice, 0 off, 1 on (ENV:GFOpenGPU/GLBatch) */
unsigned long gla_virgl_batch_errors;   /* queued commands the host refused */
unsigned long gla_virgl_blocks;         /* request blocks sent */

struct gv_free { void *ptr; struct gv_free *next; };

struct gv_ws {
    struct virgl_winsys base;
    struct sw_winsys *sws;
    struct gla_virgl_transport t;
    uint32_t *q;                /* the request block, in memory the host reaches */
    unsigned at;                /* words in it */
    unsigned ncmds;             /* commands in it */
    uint32_t qbytes;            /* data its commands read */
    uint8_t *arena;             /* copies its commands read (the host reaches it) */
    uint32_t arena_used;
    uint32_t seq;               /* blocks run; the queue is block seq + 1 */
    int batch;                  /* 1 queue, 0 a block a command */
    int32_t first_err;
    struct gv_free *later;      /* memory to free once the queue has run */
    uint32_t next_handle;
    uint32_t ctx_id;
    uint32_t host_flags;        /* HELLO's: ACV_HOST_ */
    mtx_t mutex;
};

struct virgl_hw_res {
    struct pipe_reference reference;
    uint32_t res_handle;
    int num_cs_references;
    uint8_t *ptr;               /* the copy the host reaches */
    uint32_t size;
    enum pipe_format format;
    uint32_t target, bind, width, height, stride;
    struct sw_displaytarget *dt;
    unsigned dt_stride;         /* its rows: the display target's are 64-byte aligned */
    uint32_t busy_seq;          /* a queued PUT reads ptr in place until block busy_seq has run */
};

struct gv_cmd_buf {
    struct virgl_cmd_buf base;
    struct gv_ws *ws;
    struct virgl_hw_res **res;
    unsigned nres, cres;
};

/* done once block seq has run */
struct gv_fence { struct pipe_reference reference; uint32_t seq; };

static struct gv_ws *gv(struct virgl_winsys *w) { return (struct gv_ws *)w; }

/* ---- request blocks (the mutex held) ---- */

/* The queue run: its commands' results looked at, its copies and the
 * memory freed meanwhile let go. 0, or the transport's error. */
static int q_run(struct gv_ws *w)
{
    unsigned i;
    int r;
    struct gv_free *f;
    if (!w->at) return 0;
    r = w->t.run(w->t.user, w->q, w->at * 4);
    gla_virgl_blocks++;
    for (i = 0; i + 1 < w->at; ) {
        uint32_t hdr = util_be32_to_cpu(w->q[i]);
        unsigned cmd = ACV_HDR_CMD(hdr), len = ACV_HDR_WORDS(hdr);
        int32_t res = (int32_t)util_be32_to_cpu(w->q[i + 1]);
        if (len < 2) break;
        if ((r || res < 0) && cmd != ACV_HELLO && cmd != ACV_CAPS && cmd != ACV_CAPSET_INFO) {
            if (!w->first_err) w->first_err = r ? -1 : res;
            if (w->batch) gla_virgl_batch_errors++;
        }
        i += len;
    }
    w->seq++;
    w->at = 0;
    w->ncmds = 0;
    w->qbytes = 0;
    w->arena_used = 0;
    while ((f = w->later)) { w->later = f->next; w->t.free(w->t.user, f->ptr); FREE(f); }
    return r;
}

/* A command into the queue (after running it when the command doesn't fit);
 * returns where its result word is. */
static unsigned q_cmd(struct gv_ws *w, unsigned cmd, unsigned n, const uint32_t *args)
{
    unsigned here, i;
    if (w->at + n + 2 > (w->batch ? Q_WORDS : NOW_WORDS)) q_run(w);
    here = w->at;
    w->q[w->at++] = util_cpu_to_be32(ACV_HDR(cmd, n + 2));
    w->q[w->at++] = 0;
    for (i = 0; i < n; i++) w->q[w->at++] = util_cpu_to_be32(args[i]);
    w->ncmds++;
    return here + 1;
}

/* After a command that may wait: unbatched, or over a cap, the queue runs now. */
static void q_after(struct gv_ws *w)
{
    if (!w->batch || w->ncmds >= Q_CMDS || w->qbytes >= Q_BYTES) q_run(w);
}

/* Room for n bytes of copies in the arena (running the queue to make it), or NULL. */
static uint8_t *q_room(struct gv_ws *w, uint32_t n)
{
    uint8_t *p;
    n = (n + 3) & ~3u;
    if (!w->arena || n > ARENA_BYTES) return NULL;
    if (w->arena_used + n > ARENA_BYTES) q_run(w);
    p = w->arena + w->arena_used;
    w->arena_used += n;
    return p;
}

/* Room for a command of n argument words, so that q_cmd won't run the queue
 * (call it before q_room: a run then would forget the arena under a copy). */
static void q_reserve(struct gv_ws *w, unsigned n)
{
    if (w->at + n + 2 > (w->batch ? Q_WORDS : NOW_WORDS)) q_run(w);
}

/* A command that runs now: queued behind the rest, the whole block run;
 * its result (or -1 when the block didn't run) and where its words are. */
static int32_t q_now(struct gv_ws *w, unsigned cmd, unsigned n, const uint32_t *args, unsigned *at)
{
    unsigned r = q_cmd(w, cmd, n, args);
    if (at) *at = r;
    return q_run(w) ? -1 : (int32_t)util_be32_to_cpu(w->q[r]);
}

static uint32_t q_word(struct gv_ws *w, unsigned at) { return util_be32_to_cpu(w->q[at]); }

/* Memory the host may still read (a queued PUT in place): freed once the queue has run. */
static void q_free_later(struct gv_ws *w, void *p)
{
    struct gv_free *f = CALLOC_STRUCT(gv_free);
    if (!f) { q_run(w); w->t.free(w->t.user, p); return; }
    f->ptr = p;
    f->next = w->later;
    w->later = f;
}

static int res_busy(struct gv_ws *w, const struct virgl_hw_res *res) { return (int32_t)(res->busy_seq - w->seq) > 0; }

/* ---- byte order ---- */

/* The units a transfer turns round: none on a little-endian Amiga stand-in;
 * on a big-endian one a buffer's floats, or a texture's channel size (array
 * formats) or pixel size (packed ones). */
static uint32_t swap_unit(const struct virgl_hw_res *res)
{
#if UTIL_ARCH_BIG_ENDIAN
    const struct util_format_description *d;
    if (res->target == PIPE_BUFFER) return ACV_SWAP_32;
    d = util_format_description(res->format);
    if (!d || d->layout != UTIL_FORMAT_LAYOUT_PLAIN) return ACV_SWAP_NONE;
    if (d->is_array) {
        unsigned bits = d->channel[0].size;
        return bits == 16 ? ACV_SWAP_16 : bits == 32 ? ACV_SWAP_32 : bits == 64 ? ACV_SWAP_64 : ACV_SWAP_NONE;
    }
    return d->block.bits == 16 ? ACV_SWAP_16 : d->block.bits == 32 ? ACV_SWAP_32 : d->block.bits == 64 ? ACV_SWAP_32 : ACV_SWAP_NONE;
#else
    (void)res;
    return ACV_SWAP_NONE;
#endif
}

/* ---- resources ---- */

static uint32_t transfer_size(struct virgl_hw_res *res, const struct pipe_box *box, uint32_t stride, uint32_t layer_stride)
{
    uint32_t vs = util_format_get_stride(res->format, box->width), vls;
    if (stride && box->height > 1) vs = stride;
    vls = util_format_get_2d_size(res->format, vs, box->height);
    if (layer_stride && box->depth > 1) vls = layer_stride;
    return vls * box->depth;
}

static int transfer(struct gv_ws *w, struct virgl_hw_res *res, const struct pipe_box *box, uint32_t stride,
                    uint32_t layer_stride, uint32_t buf_offset, uint32_t level, int get, uint32_t unit)
{
    uint32_t a[14];
    unsigned r;
    int rc;
    a[0] = res->res_handle; a[1] = CTX_ID; a[2] = level; a[3] = stride; a[4] = layer_stride;
    a[5] = box->x; a[6] = box->y; a[7] = box->z; a[8] = box->width; a[9] = box->height; a[10] = box->depth;
    a[11] = w->t.addr(w->t.user, res->ptr + buf_offset);
    a[12] = transfer_size(res, box, stride, layer_stride);
    a[13] = unit;
    if (buf_offset + a[12] > res->size) a[12] = res->size > buf_offset ? res->size - buf_offset : 0;
    mtx_lock(&w->mutex);
    if (get) {
        /* after everything queued; the host writes res->ptr, which Mesa reads at once */
        rc = q_now(w, ACV_GET, 14, a, &r);
        mtx_unlock(&w->mutex);
        return rc;
    }
    q_reserve(w, 14);
    if (w->batch && a[12] && a[12] <= Q_COPY) {
        uint8_t *copy = q_room(w, a[12]);
        if (copy) {
            memcpy(copy, res->ptr + buf_offset, a[12]);
            a[11] = w->t.addr(w->t.user, copy);
        } else
            res->busy_seq = w->seq + 1;
    } else
        res->busy_seq = w->seq + 1;     /* read in place: busy until this block has run */
    w->qbytes += a[12];
    q_cmd(w, ACV_PUT, 14, a);
    rc = 0;
    if (!w->batch) {
        rc = q_run(w) ? -1 : (int32_t)util_be32_to_cpu(w->q[1]);
        w->first_err = 0;
    } else
        q_after(w);
    mtx_unlock(&w->mutex);
    return rc;
}

static int gv_transfer_put(struct virgl_winsys *vws, struct virgl_hw_res *res, const struct pipe_box *box,
                           uint32_t stride, uint32_t layer_stride, uint32_t buf_offset, uint32_t level)
{
    return transfer(gv(vws), res, box, stride, layer_stride, buf_offset, level, 0, swap_unit(res));
}

static int gv_transfer_get(struct virgl_winsys *vws, struct virgl_hw_res *res, const struct pipe_box *box,
                           uint32_t stride, uint32_t layer_stride, uint32_t buf_offset, uint32_t level)
{
    return transfer(gv(vws), res, box, stride, layer_stride, buf_offset, level, 1, swap_unit(res));
}

static void res_destroy(struct gv_ws *w, struct virgl_hw_res *res)
{
    uint32_t a[1] = { res->res_handle };
    mtx_lock(&w->mutex);
    q_cmd(w, ACV_RES_UNREF, 1, a);      /* after the queued draws that use it */
    if (res->ptr && res_busy(w, res)) {
        q_free_later(w, res->ptr);      /* a queued PUT reads it in place */
        res->ptr = NULL;
    }
    q_after(w);
    mtx_unlock(&w->mutex);
    if (res->dt) w->sws->displaytarget_destroy(w->sws, res->dt);
    if (res->ptr) w->t.free(w->t.user, res->ptr);
    FREE(res);
}

static struct virgl_hw_res *gv_resource_create(struct virgl_winsys *vws, enum pipe_texture_target target,
                                               const void *map_front_private, uint32_t format, uint32_t bind,
                                               uint32_t width, uint32_t height, uint32_t depth, uint32_t array_size,
                                               uint32_t last_level, uint32_t nr_samples, uint32_t flags, uint32_t size)
{
    struct gv_ws *w = gv(vws);
    struct virgl_hw_res *res = CALLOC_STRUCT(virgl_hw_res);
    uint32_t a[12];
    unsigned r;
    int rc;
    if (!res) return NULL;
    res->format = (enum pipe_format)format;
    res->target = target; res->bind = bind; res->width = width; res->height = height;
    res->size = size;
    res->stride = util_format_get_stride(res->format, width);
    if (bind & (VIRGL_BIND_DISPLAY_TARGET | VIRGL_BIND_SCANOUT)) {
        res->dt = w->sws->displaytarget_create(w->sws, bind, res->format, width, height, 64, map_front_private, &res->dt_stride);
        if (!res->dt) { FREE(res); return NULL; }
    }
    if (size && !(res->ptr = w->t.alloc(w->t.user, size))) {
        if (res->dt) w->sws->displaytarget_destroy(w->sws, res->dt);
        FREE(res);
        return NULL;
    }
    mtx_lock(&w->mutex);
    res->res_handle = NEW_RES_HANDLE(w, res);
    a[0] = res->res_handle; a[1] = target; a[2] = pipe_to_virgl_format(res->format); a[3] = bind;
    a[4] = width; a[5] = height; a[6] = depth; a[7] = array_size; a[8] = last_level; a[9] = nr_samples;
    a[10] = flags; a[11] = CTX_ID;
    if (w->batch) {
        /* queued: the host makes it before the commands that name it; a
         * refusal is counted when the block has run (q_run) */
        q_cmd(w, ACV_RES_CREATE, 12, a);
        q_after(w);
        rc = 0;
    } else
        rc = q_now(w, ACV_RES_CREATE, 12, a, &r);
    mtx_unlock(&w->mutex);
    if (rc) {
        if (res->dt) w->sws->displaytarget_destroy(w->sws, res->dt);
        if (res->ptr) w->t.free(w->t.user, res->ptr);
        FREE(res);
        return NULL;
    }
    pipe_reference_init(&res->reference, 1);
    p_atomic_set(&res->num_cs_references, 0);
    return res;
}

static void gv_resource_reference(struct virgl_winsys *vws, struct virgl_hw_res **dres, struct virgl_hw_res *sres)
{
    struct virgl_hw_res *old = *dres;
    if (pipe_reference(old ? &old->reference : NULL, sres ? &sres->reference : NULL))
        res_destroy(gv(vws), old);
    *dres = sres;
}

static void *gv_resource_map(struct virgl_winsys *vws, struct virgl_hw_res *res) { (void)vws; return res->ptr; }

/* Busy while a queued PUT reads its memory in place: Mesa waits (here, the
 * queue runs) before it writes there. */
static void gv_resource_wait(struct virgl_winsys *vws, struct virgl_hw_res *res)
{
    struct gv_ws *w = gv(vws);
    mtx_lock(&w->mutex);
    if (res_busy(w, res)) q_run(w);
    mtx_unlock(&w->mutex);
}

static bool gv_resource_is_busy(struct virgl_winsys *vws, struct virgl_hw_res *res)
{
    struct gv_ws *w = gv(vws);
    bool busy;
    mtx_lock(&w->mutex);
    busy = res_busy(w, res);
    mtx_unlock(&w->mutex);
    return busy;
}

/* ---- command buffers ---- */

static struct virgl_cmd_buf *gv_cmd_buf_create(struct virgl_winsys *vws, uint32_t size)
{
    struct gv_ws *w = gv(vws);
    struct gv_cmd_buf *cb = CALLOC_STRUCT(gv_cmd_buf);
    if (!cb) return NULL;
    cb->nres = 512;
    cb->res = CALLOC(cb->nres, sizeof *cb->res);
    cb->base.buf = w->t.alloc(w->t.user, (size_t)size * 4);
    if (!cb->res || !cb->base.buf) {
        if (cb->base.buf) w->t.free(w->t.user, cb->base.buf);
        FREE(cb->res);
        FREE(cb);
        return NULL;
    }
    cb->ws = w;
    return &cb->base;
}

static void release_all(struct gv_cmd_buf *cb)
{
    unsigned i;
    for (i = 0; i < cb->cres; i++) {
        p_atomic_dec(&cb->res[i]->num_cs_references);
        gv_resource_reference(&cb->ws->base, &cb->res[i], NULL);
    }
    cb->cres = 0;
}

static void gv_cmd_buf_destroy(struct virgl_cmd_buf *_cb)
{
    struct gv_cmd_buf *cb = (struct gv_cmd_buf *)_cb;
    release_all(cb);
    cb->ws->t.free(cb->ws->t.user, cb->base.buf);
    FREE(cb->res);
    FREE(cb);
}

static void add_res(struct gv_cmd_buf *cb, struct virgl_hw_res *res)
{
    unsigned i;
    for (i = 0; i < cb->cres; i++) if (cb->res[i] == res) return;
    if (cb->cres >= cb->nres) {
        struct virgl_hw_res **n = REALLOC(cb->res, cb->nres * sizeof *n, (cb->nres + 256) * sizeof *n);
        if (!n) return;
        cb->res = n;
        cb->nres += 256;
    }
    cb->res[cb->cres] = NULL;
    gv_resource_reference(&cb->ws->base, &cb->res[cb->cres], res);
    p_atomic_inc(&res->num_cs_references);
    cb->cres++;
}

/* A resource named in the stream: its handle, a little-endian word as the rest. */
static void gv_emit_res(struct virgl_winsys *vws, struct virgl_cmd_buf *_cb, struct virgl_hw_res *res, bool write_buf)
{
    struct gv_cmd_buf *cb = (struct gv_cmd_buf *)_cb;
    (void)vws;
    if (write_buf) cb->base.buf[cb->base.cdw++] = util_cpu_to_le32(res->res_handle);
    add_res(cb, res);
}

static bool gv_res_is_referenced(struct virgl_winsys *vws, struct virgl_cmd_buf *cb, struct virgl_hw_res *res)
{
    (void)vws; (void)cb;
    return p_atomic_read(&res->num_cs_references) != 0;
}

/* A fence done once everything queued now has run (the mutex held). */
static struct pipe_fence_handle *new_fence(struct gv_ws *w)
{
    struct gv_fence *f = CALLOC_STRUCT(gv_fence);
    if (f) {
        pipe_reference_init(&f->reference, 1);
        f->seq = w->at ? w->seq + 1 : w->seq;
    }
    return (struct pipe_fence_handle *)f;
}

static int gv_submit_cmd(struct virgl_winsys *vws, struct virgl_cmd_buf *_cb, struct pipe_fence_handle **fence)
{
    struct gv_ws *w = gv(vws);
    struct gv_cmd_buf *cb = (struct gv_cmd_buf *)_cb;
    int rc = 0;
    mtx_lock(&w->mutex);
    if (cb->base.cdw) {
        uint32_t a[3], bytes = cb->base.cdw * 4;
        uint8_t *copy = NULL;
        a[0] = CTX_ID; a[1] = w->t.addr(w->t.user, cb->base.buf); a[2] = cb->base.cdw;
        q_reserve(w, 3);
        /* Mesa writes the command buffer again as soon as this returns: a
         * queued submit reads a copy; one too big for the arena runs now */
        if (w->batch && bytes <= ARENA_BYTES / 2 && (copy = q_room(w, bytes)) != NULL) {
            memcpy(copy, cb->base.buf, bytes);
            a[1] = w->t.addr(w->t.user, copy);
        }
        w->qbytes += bytes;
        if (copy) {
            q_cmd(w, ACV_SUBMIT, 3, a);
            q_after(w);
        } else
            rc = q_now(w, ACV_SUBMIT, 3, a, NULL);
    }
    if (fence && !rc) *fence = new_fence(w);
    mtx_unlock(&w->mutex);
    release_all(cb);
    cb->base.cdw = 0;
    return rc;
}

/* ---- caps, fences, the front buffer ---- */

static int gv_get_caps(struct virgl_winsys *vws, struct virgl_drm_caps *caps)
{
    struct gv_ws *w = gv(vws);
    uint32_t a[4], set = 2, *mem, bytes, i, n;
    unsigned ri, rc;
    virgl_ws_fill_new_caps_defaults(caps);
    mtx_lock(&w->mutex);
    a[0] = 2; a[1] = 0; a[2] = 0;
    q_now(w, ACV_CAPSET_INFO, 3, a, &ri);
    if (!q_word(w, ri + 2)) {
        set = 1;
        a[0] = 1;
        q_now(w, ACV_CAPSET_INFO, 3, a, &ri);
    }
    bytes = q_word(w, ri + 3);
    if (bytes > sizeof caps->caps) bytes = sizeof caps->caps;
    mem = w->t.alloc(w->t.user, sizeof caps->caps);
    if (!mem || !bytes) { mtx_unlock(&w->mutex); if (mem) w->t.free(w->t.user, mem); return -1; }
    a[0] = set; a[1] = q_word(w, ri + 2); a[2] = w->t.addr(w->t.user, mem); a[3] = bytes;
    q_now(w, ACV_CAPS, 4, a, &rc);
    n = (int32_t)q_word(w, rc) > 0 ? q_word(w, rc) : 0;
    mtx_unlock(&w->mutex);
    /* the host gives each word in the Amiga's order: read them as words */
    for (i = 0; i < n / 4; i++) ((uint32_t *)&caps->caps)[i] = util_be32_to_cpu(mem[i]);
    w->t.free(w->t.user, mem);
#if UTIL_ARCH_BIG_ENDIAN
    /* the boolean bit-field word: bit k of the host's is field k, which a
     * big-endian compiler puts at bit 31 - k */
    {
        uint32_t *b = (uint32_t *)&caps->caps.v1.bset, v = *b, r = 0;
        for (i = 0; i < 32; i++) if (v & (1u << i)) r |= 1u << (31 - i);
        *b = r;
    }
    /* the renderer's name is bytes, not words: the word order above turned
     * each four of them round ("virgl ( DMAedaR..."), so turn them back */
    if (set == 2 && n >= offsetof(struct virgl_caps_v2, renderer) + sizeof caps->caps.v2.renderer)
        for (i = 0; i < sizeof caps->caps.v2.renderer; i += 4) {
            uint32_t *p = (uint32_t *)&caps->caps.v2.renderer[i];
            *p = util_bswap32(*p);
        }
#endif
    /* copies through staging resources need memory the host maps: not here */
    caps->caps.v2.capability_bits_v2 &= ~VIRGL_CAP_V2_COPY_TRANSFER_BOTH_DIRECTIONS;
    caps->caps.v2.capability_bits &= ~(VIRGL_CAP_COPY_TRANSFER | VIRGL_CAP_TRANSFER);
    return n ? 0 : -1;
}

static struct pipe_fence_handle *gv_cs_create_fence(struct virgl_winsys *vws, int fd)
{
    struct gv_ws *w = gv(vws);
    struct pipe_fence_handle *f;
    (void)fd;
    mtx_lock(&w->mutex);
    f = new_fence(w);
    mtx_unlock(&w->mutex);
    return f;
}

/* Done once its block has run: a fence on queued commands runs the queue,
 * whatever the timeout (a program polling with 0 would otherwise wait for
 * ever on commands nothing else sends). */
static bool gv_fence_wait(struct virgl_winsys *vws, struct pipe_fence_handle *_f, uint64_t timeout)
{
    struct gv_ws *w = gv(vws);
    struct gv_fence *f = (struct gv_fence *)_f;
    (void)timeout;
    mtx_lock(&w->mutex);
    if (f && (int32_t)(f->seq - w->seq) > 0) q_run(w);
    mtx_unlock(&w->mutex);
    return true;
}
static void gv_fence_reference(struct virgl_winsys *vws, struct pipe_fence_handle **dst, struct pipe_fence_handle *src)
{
    struct gv_fence *old = (struct gv_fence *)*dst, *s = (struct gv_fence *)src;
    (void)vws;
    if (pipe_reference(old ? &old->reference : NULL, s ? &s->reference : NULL)) FREE(old);
    *dst = src;
}

/* A finished frame. When the host can (ACV_HOST_PRESENT) and the present
 * hook has a way, the host writes it straight into the display; otherwise
 * it comes back from the host into the resource's copy, then into the
 * display target, which shows it (the GLA core's present hook).
 * `drawable` is the buffer's present hook. */
static void gv_flush_frontbuffer(struct virgl_winsys *vws, struct virgl_cmd_buf *cb, struct virgl_hw_res *res,
                                 unsigned level, unsigned layer, void *drawable, struct pipe_box *sub_box)
{
    struct gv_ws *w = gv(vws);
    const struct gla_present *p = drawable;
    gla_present_virgl_fn pv = gla_present_virgl_of(p);
    struct pipe_box box;
    uint32_t offset = 0;
    void *map;
    (void)cb;
    if (!res->dt) return;
    mtx_lock(&w->mutex);
    q_run(w);                           /* the frame's commands before its present */
    mtx_unlock(&w->mutex);
    gla_counts.frames++;
    gla_counts.direct_state = !(w->host_flags & ACV_HOST_PRESENT) ? GLA_DIRECT_NO_HOST
                            : !pv ? GLA_DIRECT_NO_HOOK : GLA_DIRECT_REFUSED;
    if (gla_counts.direct_state == GLA_DIRECT_REFUSED && level == 0 && layer == 0) {
        struct gla_virgl_frame f;
        long n;
        f.context = CTX_ID;
        f.handle = res->res_handle;
        f.w = res->width; f.h = res->height;
        if (sub_box) {
            f.x0 = sub_box->x; f.y0 = sub_box->y;
            f.x1 = sub_box->x + sub_box->width; f.y1 = sub_box->y + sub_box->height;
        } else {
            f.x0 = 0; f.y0 = 0; f.x1 = (int)res->width; f.y1 = (int)res->height;
        }
        if ((n = pv(p->user, &f)) >= 0) {     /* on -1 the hook may name why (GLA_DIRECT_) */
            gla_counts.direct_state = GLA_DIRECT_ON;
            gla_counts.direct++;
            gla_counts.direct_pixels += (unsigned long)n;
            return;
        }
    }
    gla_counts.readback++;
    if (sub_box) {
        box = *sub_box;
        offset = box.y * res->stride + box.x * util_format_get_blocksize(res->format);
    } else u_box_3d(0, 0, layer, res->width, res->height, 1, &box);
    transfer(w, res, &box, res->stride, 0, offset, level, 1, swap_unit(res));
    map = w->sws->displaytarget_map(w->sws, res->dt, PIPE_MAP_WRITE);
    if (map) {
        util_copy_rect(map, res->format, res->dt_stride, box.x, box.y, box.width, box.height, res->ptr, res->stride, box.x, box.y);
        w->sws->displaytarget_unmap(w->sws, res->dt);
    }
    gla_counts.readback_pixels += (unsigned long)box.width * (unsigned long)box.height;
    w->sws->displaytarget_display(w->sws, res->dt, drawable, sub_box ? 1 : 0, sub_box);
}

static void gv_destroy(struct virgl_winsys *vws)
{
    struct gv_ws *w = gv(vws);
    uint32_t a[1] = { CTX_ID };
    q_now(w, ACV_CTX_DESTROY, 1, a, NULL);      /* after everything queued */
    w->t.free(w->t.user, w->q);
    if (w->arena) w->t.free(w->t.user, w->arena);
    mtx_destroy(&w->mutex);
    FREE(w);
}

/* Batching on unless the program (ENV:GFOpenGPU/GLBatch 0) or the runtime
 * (AC_VIRGL_BATCH=0: HELLO's ACV_HOST_NO_BATCH) says no. */
static int want_batch(uint32_t host_flags)
{
    if (gla_virgl_batch_wanted == 0) return 0;
#ifdef ACV_HOST_NO_BATCH
    if ((host_flags & ACV_HOST_NO_BATCH) && gla_virgl_batch_wanted != 1) return 0;
#else
    (void)host_flags;
#endif
    return 1;
}

struct virgl_winsys *gla_virgl_winsys_create(struct sw_winsys *sws, const struct gla_virgl_transport *t)
{
    struct gv_ws *w = CALLOC_STRUCT(gv_ws);
    static const char name[] = "GLA";
    uint32_t a[3];
    unsigned rh, rc;
    int ran;
    char *nm;
    if (!w) { gla_note_why("no memory for virgl's winsys"); return NULL; }
    w->sws = sws;
    w->t = *t;
    w->next_handle = 1;
    w->ctx_id = NEW_CTX_ID(w);
    (void)mtx_init(&w->mutex, mtx_plain);
    if (!(w->q = w->t.alloc(w->t.user, Q_WORDS * 4 + 64))) { gla_note_why("no memory for virgl's request block"); FREE(w); return NULL; }
    nm = (char *)(w->q + Q_WORDS);
    memcpy(nm, name, sizeof name);
    a[0] = 0;
    rh = q_cmd(w, ACV_HELLO, 1, a);
    a[0] = CTX_ID; a[1] = w->t.addr(w->t.user, nm); a[2] = sizeof name - 1;
    rc = q_cmd(w, ACV_CTX_CREATE, 3, a);
    ran = q_run(w) == 0;
    w->first_err = 0;
    if (!ran || (int32_t)q_word(w, rh) != ACV_PROTOCOL || q_word(w, rc) != 0) {
        /* Why, for the program's notice (gla_core.h, gla_note_why). */
        if (!ran) gla_note_why("virgl's first request block didn't run (gfopengpu.library or the ACRTG ring failed it)");
        else if ((int32_t)q_word(w, rh) != ACV_PROTOCOL) gla_note_why("the runtime's ACVirgl speaks another protocol (ACV_HELLO)");
        else gla_note_why("the runtime's virglrenderer refused a context (ACV_CTX_CREATE)");
        w->t.free(w->t.user, w->q);
        FREE(w);
        return NULL;
    }
    w->host_flags = q_word(w, rh + 1);
    /* the arena for queued copies, once; without it, each command its own block */
    if (want_batch(w->host_flags) && (w->arena = w->t.alloc(w->t.user, ARENA_BYTES)) != NULL) w->batch = 1;
    w->base.transfer_put = gv_transfer_put;
    w->base.transfer_get = gv_transfer_get;
    w->base.resource_create = gv_resource_create;
    w->base.resource_reference = gv_resource_reference;
    w->base.resource_map = gv_resource_map;
    w->base.resource_wait = gv_resource_wait;
    w->base.resource_is_busy = gv_resource_is_busy;
    w->base.cmd_buf_create = gv_cmd_buf_create;
    w->base.cmd_buf_destroy = gv_cmd_buf_destroy;
    w->base.emit_res = gv_emit_res;
    w->base.submit_cmd = gv_submit_cmd;
    w->base.res_is_referenced = gv_res_is_referenced;
    w->base.get_caps = gv_get_caps;
    w->base.cs_create_fence = gv_cs_create_fence;
    w->base.fence_wait = gv_fence_wait;
    w->base.fence_reference = gv_fence_reference;
    w->base.flush_frontbuffer = gv_flush_frontbuffer;
    w->base.destroy = gv_destroy;
    w->base.supports_fences = 0;
    w->base.supports_encoded_transfers = 0;
    w->base.supports_coherent = 0;
    return &w->base;
}
