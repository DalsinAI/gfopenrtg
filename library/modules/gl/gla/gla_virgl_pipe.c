/* Copyright (c) 2026 AmiCradle Team. OpenGPU's GL module, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * The GLA core's virgl winsys on GFOpenGPU's shared pipe: Mesa's virgl driver (Gallium) writes the command stream,
 * and every call it makes of its winsys goes to the carrier (gla_carry.h), which puts it in the pipe's block
 * (library/pipe) in the order it was made. The pipe sends the block: one OGPU_OP_VIRGL batch on the ACRTG board's
 * ring on the Amiga, which the Cradle's runtime gives to virglrenderer.
 *
 * It took the place of gla_virgl.c, the winsys with request blocks, a queue and a transport of its own (the GL stage
 * of docs/design/OpenGPU-Shared-Pipe-Plan.md, 24 November 2026), which is gone.
 *
 * The host draws in its own memory; each resource also has a copy in memory the host reaches (Fast RAM on the
 * Amiga), which virgl maps, and PUT and GET move pixels between the two, turning units round on a big-endian Amiga:
 * a texture's by its format's channel or pixel size, a buffer's by 32 bits (floats). A PUT is copied by the carrier
 * or has run by the time it returns, and a stream is copied, so no resource is ever busy and a resource's memory is
 * freed with it. */
#include "gla_virgl.h"
#include "gla_carry.h"

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

/* Words in the Amiga's order (big-endian), as the host gives the caps. */
#if UTIL_ARCH_BIG_ENDIAN
#define gp_be32(x) (x)
#else
#define gp_be32(x) util_bswap32(x)
#endif

/* The id the host knows a context by: virglrenderer has one namespace for all its clients, and every GL program on
 * the Amiga is one, so with 32-bit pointers it is the address of the program's own winsys (this file). A
 * resource's handle is the pipe's. */
#if UINTPTR_MAX == 0xffffffffu
#define NEW_CTX_ID(w) ((uint32_t)(uintptr_t)(w))
#else
#define NEW_CTX_ID(w) 1u
#endif

struct gp_ws {
    struct virgl_winsys base;
    struct sw_winsys *sws;
    struct gla_virgl_transport t;
    struct gla_carry *c;
    uint32_t ctx_id;
    uint32_t host_flags;        /* HELLO's: ACV_HOST_ */
    struct virgl_drm_caps caps; /* as the open got them */
    int caps_ok;
    mtx_t mutex;
};

struct virgl_hw_res {
    struct pipe_reference reference;
    struct gla_carry_res *cr;
    uint32_t res_handle;
    int num_cs_references;
    uint8_t *ptr;               /* the copy the host reaches */
    uint32_t size;
    enum pipe_format format;
    uint32_t target, bind, width, height, stride;
    struct sw_displaytarget *dt;
    unsigned dt_stride;         /* its rows: the display target's are 64-byte aligned */
};

struct gp_cmd_buf {
    struct virgl_cmd_buf base;
    struct gp_ws *ws;
    struct virgl_hw_res **res;
    unsigned nres, cres;
};

/* done once everything queued when it was made has run */
int gla_virgl_batch_wanted = -1;        /* -1 the runtime's choice, 0 off, 1 on (ENV:GFOpenGPU/GLBatch) */
unsigned long gla_virgl_batch_errors;   /* queued commands the host refused */
unsigned long gla_virgl_blocks;         /* request blocks sent */

struct gp_fence { struct pipe_reference reference; unsigned long seq; };

static struct gp_ws *gp(struct virgl_winsys *w) { return (struct gp_ws *)w; }

/* The carrier's counts into the ones the tests and GLDraws read (the mutex held). */
static void counted(struct gp_ws *w)
{
    unsigned long blocks, refused;
    gla_carry_counts(w->c, &blocks, &refused);
    gla_virgl_blocks += blocks;
    gla_virgl_batch_errors += refused;
    gla_counts.fences += blocks;        /* each block is waited for as it is sent */
}

/* ---- byte order ---- */

/* The units a transfer turns round: none on a little-endian Amiga stand-in; on a big-endian one a buffer's floats,
 * or a texture's channel size (array formats) or pixel size (packed ones). */
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

static int transfer(struct gp_ws *w, struct virgl_hw_res *res, const struct pipe_box *box, uint32_t stride,
                    uint32_t layer_stride, uint32_t buf_offset, uint32_t level, int get, uint32_t unit)
{
    struct gla_carry_box b;
    uint32_t bytes = transfer_size(res, box, stride, layer_stride);
    int rc;
    b.level = level; b.stride = stride; b.layer_stride = layer_stride;
    b.x = box->x; b.y = box->y; b.z = box->z;
    b.w = (unsigned long)box->width; b.h = (unsigned long)box->height; b.d = (unsigned long)box->depth;
    if (buf_offset + bytes > res->size) bytes = res->size > buf_offset ? res->size - buf_offset : 0;
    mtx_lock(&w->mutex);
    if (get) rc = gla_carry_get(w->c, res->cr, &b, res->ptr + buf_offset, bytes, unit);
    else rc = gla_carry_put(w->c, res->cr, &b, res->ptr + buf_offset, bytes, unit);
    counted(w);
    mtx_unlock(&w->mutex);
    return rc;
}

static int gp_transfer_put(struct virgl_winsys *vws, struct virgl_hw_res *res, const struct pipe_box *box,
                           uint32_t stride, uint32_t layer_stride, uint32_t buf_offset, uint32_t level)
{
    return transfer(gp(vws), res, box, stride, layer_stride, buf_offset, level, 0, swap_unit(res));
}

static int gp_transfer_get(struct virgl_winsys *vws, struct virgl_hw_res *res, const struct pipe_box *box,
                           uint32_t stride, uint32_t layer_stride, uint32_t buf_offset, uint32_t level)
{
    return transfer(gp(vws), res, box, stride, layer_stride, buf_offset, level, 1, swap_unit(res));
}

static void res_destroy(struct gp_ws *w, struct virgl_hw_res *res)
{
    mtx_lock(&w->mutex);
    gla_carry_res_destroy(w->c, res->cr);       /* after the queued draws that use it */
    counted(w);
    mtx_unlock(&w->mutex);
    if (res->dt) w->sws->displaytarget_destroy(w->sws, res->dt);
    if (res->ptr) w->t.free(w->t.user, res->ptr);
    FREE(res);
}

static struct virgl_hw_res *gp_resource_create(struct virgl_winsys *vws, enum pipe_texture_target target,
                                               const void *map_front_private, uint32_t format, uint32_t bind,
                                               uint32_t width, uint32_t height, uint32_t depth, uint32_t array_size,
                                               uint32_t last_level, uint32_t nr_samples, uint32_t flags, uint32_t size)
{
    struct gp_ws *w = gp(vws);
    struct virgl_hw_res *res = CALLOC_STRUCT(virgl_hw_res);
    unsigned long shape[10];
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
#if !defined(GLA_HAVE_VIRGL) || GLA_HAVE_VIRGL
    shape[0] = target; shape[1] = pipe_to_virgl_format(res->format); shape[2] = bind;
#else
    /* GL_DRIVERS=softpipe: Mesa's virgl isn't linked, and nothing makes a virgl winsys (gla_display_create_virgl
     * refuses), so never reached */
    shape[0] = target; shape[1] = 0; shape[2] = bind;
#endif
    shape[3] = width; shape[4] = height; shape[5] = depth; shape[6] = array_size; shape[7] = last_level;
    shape[8] = nr_samples; shape[9] = flags;
    mtx_lock(&w->mutex);
    res->cr = gla_carry_res_create(w->c, shape);
    counted(w);
    mtx_unlock(&w->mutex);
    if (!res->cr) {
        if (res->dt) w->sws->displaytarget_destroy(w->sws, res->dt);
        if (res->ptr) w->t.free(w->t.user, res->ptr);
        FREE(res);
        return NULL;
    }
    res->res_handle = (uint32_t)gla_carry_res_handle(res->cr);
    pipe_reference_init(&res->reference, 1);
    p_atomic_set(&res->num_cs_references, 0);
    return res;
}

static void gp_resource_reference(struct virgl_winsys *vws, struct virgl_hw_res **dres, struct virgl_hw_res *sres)
{
    struct virgl_hw_res *old = *dres;
    if (pipe_reference(old ? &old->reference : NULL, sres ? &sres->reference : NULL))
        res_destroy(gp(vws), old);
    *dres = sres;
}

static void *gp_resource_map(struct virgl_winsys *vws, struct virgl_hw_res *res) { (void)vws; return res->ptr; }

/* Never busy: a PUT is copied or has run when it returns. */
static void gp_resource_wait(struct virgl_winsys *vws, struct virgl_hw_res *res) { (void)vws; (void)res; }
static bool gp_resource_is_busy(struct virgl_winsys *vws, struct virgl_hw_res *res) { (void)vws; (void)res; return false; }

/* ---- command buffers ---- */

static struct virgl_cmd_buf *gp_cmd_buf_create(struct virgl_winsys *vws, uint32_t size)
{
    struct gp_ws *w = gp(vws);
    struct gp_cmd_buf *cb = CALLOC_STRUCT(gp_cmd_buf);
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

static void release_all(struct gp_cmd_buf *cb)
{
    unsigned i;
    for (i = 0; i < cb->cres; i++) {
        p_atomic_dec(&cb->res[i]->num_cs_references);
        gp_resource_reference(&cb->ws->base, &cb->res[i], NULL);
    }
    cb->cres = 0;
}

static void gp_cmd_buf_destroy(struct virgl_cmd_buf *_cb)
{
    struct gp_cmd_buf *cb = (struct gp_cmd_buf *)_cb;
    release_all(cb);
    cb->ws->t.free(cb->ws->t.user, cb->base.buf);
    FREE(cb->res);
    FREE(cb);
}

static void add_res(struct gp_cmd_buf *cb, struct virgl_hw_res *res)
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
    gp_resource_reference(&cb->ws->base, &cb->res[cb->cres], res);
    p_atomic_inc(&res->num_cs_references);
    cb->cres++;
}

/* A resource named in the stream: its handle, a little-endian word as the rest. */
static void gp_emit_res(struct virgl_winsys *vws, struct virgl_cmd_buf *_cb, struct virgl_hw_res *res, bool write_buf)
{
    struct gp_cmd_buf *cb = (struct gp_cmd_buf *)_cb;
    (void)vws;
    if (write_buf) cb->base.buf[cb->base.cdw++] = util_cpu_to_le32(res->res_handle);
    add_res(cb, res);
}

static bool gp_res_is_referenced(struct virgl_winsys *vws, struct virgl_cmd_buf *cb, struct virgl_hw_res *res)
{
    (void)vws; (void)cb;
    return p_atomic_read(&res->num_cs_references) != 0;
}

/* A fence done once everything queued now has run (the mutex held). */
static struct pipe_fence_handle *new_fence(struct gp_ws *w)
{
    struct gp_fence *f = CALLOC_STRUCT(gp_fence);
    if (f) {
        pipe_reference_init(&f->reference, 1);
        f->seq = gla_carry_fence(w->c);
    }
    return (struct pipe_fence_handle *)f;
}

static int gp_submit_cmd(struct virgl_winsys *vws, struct virgl_cmd_buf *_cb, struct pipe_fence_handle **fence)
{
    struct gp_ws *w = gp(vws);
    struct gp_cmd_buf *cb = (struct gp_cmd_buf *)_cb;
    int rc = 0;
    mtx_lock(&w->mutex);
    /* Mesa writes the command buffer again as soon as this returns: the carrier copies it into the pipe's stream */
    if (cb->base.cdw) rc = gla_carry_submit(w->c, cb->base.buf, cb->base.cdw);
    if (fence && !rc) *fence = new_fence(w);
    counted(w);
    mtx_unlock(&w->mutex);
    release_all(cb);
    cb->base.cdw = 0;
    return rc;
}

/* ---- caps, fences, the front buffer ---- */

/* The caps came with the open (the pipe's screen asks for them in the open's blocks). */
static int gp_get_caps(struct virgl_winsys *vws, struct virgl_drm_caps *caps)
{
    struct gp_ws *w = gp(vws);
    *caps = w->caps;
    return w->caps_ok ? 0 : -1;
}

static struct pipe_fence_handle *gp_cs_create_fence(struct virgl_winsys *vws, int fd)
{
    struct gp_ws *w = gp(vws);
    struct pipe_fence_handle *f;
    (void)fd;
    mtx_lock(&w->mutex);
    f = new_fence(w);
    mtx_unlock(&w->mutex);
    return f;
}

/* Done once its block has run: a fence on queued commands sends them, whatever the timeout (a program polling with
 * 0 would otherwise wait for ever on commands nothing else sends). */
static bool gp_fence_wait(struct virgl_winsys *vws, struct pipe_fence_handle *_f, uint64_t timeout)
{
    struct gp_ws *w = gp(vws);
    struct gp_fence *f = (struct gp_fence *)_f;
    (void)timeout;
    mtx_lock(&w->mutex);
    if (f) gla_carry_wait(w->c, f->seq);
    counted(w);
    mtx_unlock(&w->mutex);
    return true;
}

static void gp_fence_reference(struct virgl_winsys *vws, struct pipe_fence_handle **dst, struct pipe_fence_handle *src)
{
    struct gp_fence *old = (struct gp_fence *)*dst, *s = (struct gp_fence *)src;
    (void)vws;
    if (pipe_reference(old ? &old->reference : NULL, s ? &s->reference : NULL)) FREE(old);
    *dst = src;
}

/* A finished frame. When the host can (ACV_HOST_PRESENT) and the present hook has a way, the host writes it
 * straight into the display; otherwise it comes back from the host into the resource's copy, then into the display
 * target, which shows it (the GLA core's present hook). `drawable` is the buffer's present hook. Until the plan's
 * M8 the present is the hook's own block, after the frame's (as before the pipe). */
static void gp_flush_frontbuffer(struct virgl_winsys *vws, struct virgl_cmd_buf *cb, struct virgl_hw_res *res,
                                 unsigned level, unsigned layer, void *drawable, struct pipe_box *sub_box)
{
    struct gp_ws *w = gp(vws);
    const struct gla_present *p = drawable;
    gla_present_virgl_fn pv = gla_present_virgl_of(p);
    struct pipe_box box;
    uint32_t offset = 0;
    void *map;
    (void)cb;
    if (!res->dt) return;
    mtx_lock(&w->mutex);
    gla_carry_flush(w->c);              /* the frame's commands before its present */
    counted(w);
    mtx_unlock(&w->mutex);
    gla_counts.frames++;
    gla_counts.direct_state = !(w->host_flags & ACV_HOST_PRESENT) ? GLA_DIRECT_NO_HOST
                            : !pv ? GLA_DIRECT_NO_HOOK : GLA_DIRECT_REFUSED;
    if (gla_counts.direct_state == GLA_DIRECT_REFUSED && level == 0 && layer == 0) {
        struct gla_virgl_frame f;
        long n;
        f.context = w->ctx_id;
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
#if UTIL_ARCH_BIG_ENDIAN
        /* The transfer has already turned each pixel round. B,G,R,X becomes the Amiga's X,R,G,B. R,G,B,X becomes
         * X,B,G,R, so red and blue swap. */
        if (res->format == PIPE_FORMAT_R8G8B8A8_UNORM || res->format == PIPE_FORMAT_R8G8B8X8_UNORM) {
            unsigned y, x;
            for (y = 0; y < box.height; y++) {
                uint8_t *px = (uint8_t *)map + (size_t)(box.y + y) * res->dt_stride + (size_t)box.x * 4;
                for (x = 0; x < box.width; x++, px += 4) {
                    uint8_t b = px[1], r = px[3];
                    px[1] = r;
                    px[3] = b;
                }
            }
        }
#endif
        w->sws->displaytarget_unmap(w->sws, res->dt);
    }
    gla_counts.readback_pixels += (unsigned long)box.width * (unsigned long)box.height;
    w->sws->displaytarget_display(w->sws, res->dt, drawable, sub_box ? 1 : 0, sub_box);
}

static void gp_destroy(struct virgl_winsys *vws)
{
    struct gp_ws *w = gp(vws);
    gla_carry_close(w->c);              /* everything queued, then the context's end */
    mtx_destroy(&w->mutex);
    FREE(w);
}

/* The carrier's memory is the transport's. */
static void *cm_alloc(void *user, unsigned long bytes) { struct gp_ws *w = user; return w->t.alloc(w->t.user, bytes); }
static void cm_free(void *user, void *p) { struct gp_ws *w = user; w->t.free(w->t.user, p); }

struct virgl_winsys *gla_virgl_winsys_create(struct sw_winsys *sws, const struct gla_virgl_transport *t)
{
    struct gp_ws *w = CALLOC_STRUCT(gp_ws);
    struct gla_carry_mem m;
    unsigned long flags = 0;
    uint32_t *mem, i, words = sizeof w->caps.caps / 4;
    int status = 0;
    if (!w) { gla_note_why("no memory for virgl's winsys"); return NULL; }
    w->sws = sws;
    w->t = *t;
    w->ctx_id = NEW_CTX_ID(w);
    /* The caps: Mesa's defaults first, in the host's room as the host writes them (each word in the Amiga's order),
     * so the words a shorter set doesn't have stay the defaults. */
    virgl_ws_fill_new_caps_defaults(&w->caps);
    if (!(mem = w->t.alloc(w->t.user, sizeof w->caps.caps))) { gla_note_why("no memory for virgl's request block"); FREE(w); return NULL; }
    for (i = 0; i < words; i++) mem[i] = gp_be32(((uint32_t *)&w->caps.caps)[i]);
    m.alloc = cm_alloc; m.free = cm_free; m.user = w;
    w->c = gla_carry_open(&m, w->ctx_id, gla_virgl_batch_wanted, mem, sizeof w->caps.caps, &flags, &w->caps_ok, &status);
    if (!w->c) {
        /* Why, for the program's notice (gla_core.h, gla_note_why). */
        gla_note_why(status == GLA_CARRY_NO_MEMORY ? "no memory for virgl's request block"
                   : status == GLA_CARRY_NO_TRANSPORT ? "gfopengpu.library has no driver that carries virgl"
                   : status == GLA_CARRY_PROTOCOL ? "the runtime's ACVirgl speaks another protocol (its hello's answer)"
                   : status == GLA_CARRY_NO_CONTEXT ? "the runtime's virglrenderer refused a context"
                   : status == GLA_CARRY_NO_PIPE ? "GL.module's shared pipe is older than its winsys (a build mistake)"
                   : "virgl's first request block didn't run (gfopengpu.library or the ACRTG ring failed it)");
        w->t.free(w->t.user, mem);
        FREE(w);
        return NULL;
    }
    w->host_flags = (uint32_t)flags;
    for (i = 0; i < words; i++) ((uint32_t *)&w->caps.caps)[i] = gp_be32(mem[i]);
    w->t.free(w->t.user, mem);
#if UTIL_ARCH_BIG_ENDIAN
    /* the boolean bit-field word: bit k of the host's is field k, which a big-endian compiler puts at bit 31 - k */
    {
        uint32_t *b = (uint32_t *)&w->caps.caps.v1.bset, v = *b, r = 0;
        for (i = 0; i < 32; i++) if (v & (1u << i)) r |= 1u << (31 - i);
        *b = r;
    }
    /* the renderer's name is bytes, not words: the word order above turned each four of them round, so turn them
     * back (all 0 when the host has only the first set) */
    for (i = 0; i < sizeof w->caps.caps.v2.renderer; i += 4) {
        uint32_t *p = (uint32_t *)&w->caps.caps.v2.renderer[i];
        *p = util_bswap32(*p);
    }
#endif
    /* copies through staging resources need memory the host maps: not here */
    w->caps.caps.v2.capability_bits_v2 &= ~VIRGL_CAP_V2_COPY_TRANSFER_BOTH_DIRECTIONS;
    w->caps.caps.v2.capability_bits &= ~(VIRGL_CAP_COPY_TRANSFER | VIRGL_CAP_TRANSFER);
    (void)mtx_init(&w->mutex, mtx_plain);
    counted(w);
    w->base.transfer_put = gp_transfer_put;
    w->base.transfer_get = gp_transfer_get;
    w->base.resource_create = gp_resource_create;
    w->base.resource_reference = gp_resource_reference;
    w->base.resource_map = gp_resource_map;
    w->base.resource_wait = gp_resource_wait;
    w->base.resource_is_busy = gp_resource_is_busy;
    w->base.cmd_buf_create = gp_cmd_buf_create;
    w->base.cmd_buf_destroy = gp_cmd_buf_destroy;
    w->base.emit_res = gp_emit_res;
    w->base.submit_cmd = gp_submit_cmd;
    w->base.res_is_referenced = gp_res_is_referenced;
    w->base.get_caps = gp_get_caps;
    w->base.cs_create_fence = gp_cs_create_fence;
    w->base.fence_wait = gp_fence_wait;
    w->base.fence_reference = gp_fence_reference;
    w->base.flush_frontbuffer = gp_flush_frontbuffer;
    w->base.destroy = gp_destroy;
    w->base.supports_fences = 0;
    w->base.supports_encoded_transfers = 0;
    w->base.supports_coherent = 0;
    return &w->base;
}
