/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * ACVirgl's host side (acvirgl.h): OGPU_OP_VIRGL's request blocks carried
 * out with virglrenderer. The calls used are virglrenderer's public API
 * (virglrenderer.h, MIT), declared here as it gives them, and found with
 * dlsym. */
#define _GNU_SOURCE
#include "acvirgl.h"
#include "../../include/gfopengpu/stream.h"
#include "../../include/gfopengpu/virgl.h"
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/uio.h>

/* ---- virglrenderer's API, as virglrenderer.h declares it --------------------------- */

#define VR_USE_EGL          1
#define VR_THREAD_SYNC      2
#define VR_USE_SURFACELESS  (1 << 3)
#define VR_USE_GLES         (1 << 4)

struct vr_callbacks {
    int version;
    void (*write_fence)(void *cookie, uint32_t fence);
    void *(*create_gl_context)(void *cookie, int scanout_idx, void *param);
    void (*destroy_gl_context)(void *cookie, void *ctx);
    int (*make_current)(void *cookie, int scanout_idx, void *ctx);
    int (*get_drm_fd)(void *cookie);                                    /* v2 */
    void (*write_context_fence)(void *cookie, uint32_t ctx_id, uint32_t ring_idx, uint64_t fence_id);   /* v3 */
    int (*get_server_fd)(void *cookie, uint32_t version);              /* v3 */
};

struct vr_resource_create_args {
    uint32_t handle, target, format, bind, width, height, depth, array_size, last_level, nr_samples, flags;
};

struct vr_box { uint32_t x, y, z, w, h, d; };

struct vr_api {
    int (*init)(void *cookie, int flags, struct vr_callbacks *cb);
    void (*cleanup)(void *cookie);
    void (*get_cap_set)(uint32_t set, uint32_t *max_ver, uint32_t *max_size);
    void (*fill_caps)(uint32_t set, uint32_t version, void *caps);
    int (*context_create)(uint32_t handle, uint32_t nlen, const char *name);
    void (*context_destroy)(uint32_t handle);
    int (*resource_create)(struct vr_resource_create_args *args, struct iovec *iov, uint32_t num_iovs);
    void (*resource_unref)(uint32_t handle);
    void (*ctx_attach_resource)(int ctx_id, int res_handle);
    void (*ctx_detach_resource)(int ctx_id, int res_handle);
    int (*submit_cmd)(void *buffer, int ctx_id, int ndw);
    int (*transfer_read_iov)(uint32_t handle, uint32_t ctx_id, uint32_t level, uint32_t stride, uint32_t layer_stride,
                             struct vr_box *box, uint64_t offset, struct iovec *iov, int iovec_cnt);
    int (*transfer_write_iov)(uint32_t handle, uint32_t ctx_id, int level, uint32_t stride, uint32_t layer_stride,
                              struct vr_box *box, uint64_t offset, struct iovec *iovec, unsigned int iovec_cnt);
    /* optional (deferred presents): per-context fences, and their retiring */
    int (*context_create_fence)(uint32_t ctx_id, uint32_t flags, uint32_t ring_idx, uint64_t fence_id);
    void (*poll)(void);
    /* virglrenderer's context 0 made current, its idea of the current context forgotten */
    void (*force_ctx_0)(void);
};

/* What ACV_PRESENT and the counters need of a resource, by handle. On the
 * Amiga a handle is the address of GLA's resource (gla_virgl.c), so the
 * handles are sparse 32-bit numbers: a hash table, open addressing,
 * handle 0 an empty slot. */
struct av_res { uint32_t handle, format, bind, width, height; };
#define AV_MAX_RES (1u << 20)               /* resources known at once */

/* An ACV_PRESENT checked and placed, its pixels not yet read back and
 * written: what av_present_finish needs. */
struct av_pres {
    uint32_t ctx, handle, layer, addr, bpr, fmt, span;
    int sx, sy, rh, flip, nr;
    int rect[ACV_PRESENT_MAX][4];
    struct vr_box box;
    uint64_t fence;             /* deferred: the context's fence after the frame */
    int done;                   /* deferred: that fence has signalled */
};
#define AV_PEND 16                          /* deferred presents at once */

struct acvirgl {
    void *dl;
    struct vr_api vr;
    struct vr_callbacks cb;
    uint8_t *tmp;               /* commands and pixels, in the host's order */
    size_t tmp_size;
    struct av_res *res;         /* the table: res_cap slots, a power of 2 */
    uint32_t res_cap, res_used;
    struct acvirgl_stats st;
    const char *api;            /* "GL" or "GLES": the context virglrenderer got */
    char renderer[64];          /* and the GPU it draws on, from capset 2 */
    /* deferred presents (acvirgl_defer_presents), oldest first */
    int (*defer_ok)(void *map_user, uint32_t addr, uint32_t bytes);
    struct av_pres pend[AV_PEND];
    int npend;
    uint64_t fence_seq;
};

static void av_no_fence(void *cookie, uint32_t fence) { (void)cookie; (void)fence; }

/* A context's fence signalled (virglrenderer calls this from
 * virgl_renderer_poll, on the caller's thread): its present is ready. */
static void av_context_fence(void *cookie, uint32_t ctx_id, uint32_t ring_idx, uint64_t fence_id)
{
    struct acvirgl *v = cookie;
    int i;
    (void)ring_idx;
    for (i = 0; i < v->npend; i++)
        if (v->pend[i].ctx == ctx_id && v->pend[i].fence <= fence_id) v->pend[i].done = 1;
}

static uint64_t av_ns(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec;
}

static int av_need(struct acvirgl *v, size_t n)
{
    uint8_t *p;
    if (n <= v->tmp_size) return 1;
    if (!(p = realloc(v->tmp, n + 65536))) return 0;
    v->tmp = p;
    v->tmp_size = n + 65536;
    return 1;
}

/* libepoxy aborts the program when the GL library it needs isn't there,
 * where virglrenderer would have failed: so look first. */
static int av_has_lib(const char *a, const char *b)
{
    void *h = dlopen(a, RTLD_LAZY | RTLD_LOCAL);
    if (!h && b) h = dlopen(b, RTLD_LAZY | RTLD_LOCAL);
    if (h) dlclose(h);
    return h != NULL;
}

/* The host's renderer as virglrenderer tells the guest: capset 2 (struct
 * virgl_caps_v2 in virglrenderer's virgl_hw.h, an ABI shared with Mesa)
 * carries the host GL_RENDERER at byte 696, 64 bytes, e.g. "V3D 7.1". */
#define AV_CAPS_RENDERER 696
static void av_renderer(struct acvirgl *v)
{
    uint32_t mv = 0, ms = 0;
    v->vr.get_cap_set(2, &mv, &ms);
    if (ms < AV_CAPS_RENDERER + sizeof v->renderer || !av_need(v, ms + 4096)) return;
    memset(v->tmp, 0, ms + 4096);
    v->vr.fill_caps(2, mv, v->tmp);
    memcpy(v->renderer, v->tmp + AV_CAPS_RENDERER, sizeof v->renderer - 1);
}

struct acvirgl *acvirgl_create(const char *lib, char *err, int errlen)
{
    struct acvirgl *v = calloc(1, sizeof *v);
    const char *path = lib ? lib : getenv("ACVIRGL_LIB"), *drv, *gles;
    if (!v) { snprintf(err, (size_t)errlen, "no memory"); return NULL; }
    if (!path) { snprintf(err, (size_t)errlen, "no virglrenderer (ACVIRGL_LIB not set)"); free(v); return NULL; }
    if (!(v->dl = dlopen(path, RTLD_NOW | RTLD_LOCAL))) { snprintf(err, (size_t)errlen, "%s", dlerror()); free(v); return NULL; }
#define SYM(field, name) if (!(*(void **)&v->vr.field = dlsym(v->dl, name))) { snprintf(err, (size_t)errlen, "%s: no %s", path, name); goto fail; }
    SYM(init, "virgl_renderer_init")
    SYM(cleanup, "virgl_renderer_cleanup")
    SYM(get_cap_set, "virgl_renderer_get_cap_set")
    SYM(fill_caps, "virgl_renderer_fill_caps")
    SYM(context_create, "virgl_renderer_context_create")
    SYM(context_destroy, "virgl_renderer_context_destroy")
    SYM(resource_create, "virgl_renderer_resource_create")
    SYM(resource_unref, "virgl_renderer_resource_unref")
    SYM(ctx_attach_resource, "virgl_renderer_ctx_attach_resource")
    SYM(ctx_detach_resource, "virgl_renderer_ctx_detach_resource")
    SYM(submit_cmd, "virgl_renderer_submit_cmd")
    SYM(transfer_read_iov, "virgl_renderer_transfer_read_iov")
    SYM(transfer_write_iov, "virgl_renderer_transfer_write_iov")
#undef SYM
    /* per-context fences, for presents left to finish (virglrenderer 1.0 and later) */
    *(void **)&v->vr.context_create_fence = dlsym(v->dl, "virgl_renderer_context_create_fence");
    *(void **)&v->vr.poll = dlsym(v->dl, "virgl_renderer_poll");
    *(void **)&v->vr.force_ctx_0 = dlsym(v->dl, "virgl_renderer_force_ctx_0");
    v->cb.version = v->vr.context_create_fence && v->vr.poll ? 3 : 1;
    v->cb.write_fence = av_no_fence;
    v->cb.write_context_fence = av_context_fence;
    /* AC_VIRGL_DRIVER names Mesa's driver for the context, e.g. zink (GL
     * over the host's Vulkan, as on a Pi 5's V3DV); unset: the GPU's own. */
    if ((drv = getenv("AC_VIRGL_DRIVER")) && *drv) setenv("MESA_LOADER_DRIVER_OVERRIDE", drv, 1);
    /* Desktop GL, else GLES: a Pi's V3D gives GLES 3.1 where its desktop GL
     * may not make the context virglrenderer asks for. AC_VIRGL_GLES=1
     * asks for GLES only, AC_VIRGL_GLES=0 for desktop GL only. */
    gles = getenv("AC_VIRGL_GLES");
    if ((!gles || strcmp(gles, "1")) && av_has_lib("libOpenGL.so.0", "libGL.so.1")
        && !v->vr.init(v, VR_USE_EGL | VR_USE_SURFACELESS, &v->cb)) v->api = "GL";
    else if ((!gles || strcmp(gles, "0")) && av_has_lib("libGLESv2.so.2", NULL)
             && !v->vr.init(v, VR_USE_EGL | VR_USE_SURFACELESS | VR_USE_GLES, &v->cb)) v->api = "GLES";
    else {
        snprintf(err, (size_t)errlen, "virgl_renderer_init failed (no EGL with surfaceless %s contexts, or no libOpenGL/libGLESv2?)",
                 !gles ? "GL or GLES" : !strcmp(gles, "1") ? "GLES" : "GL");
        goto fail;
    }
    av_renderer(v);
    return v;
fail:
    dlclose(v->dl);
    free(v);
    return NULL;
}

void acvirgl_destroy(struct acvirgl *v)
{
    if (!v) return;
    v->npend = 0;                           /* left unfinished: the memory they would write may be gone */
    v->vr.cleanup(v);
    /* virglrenderer keeps state at file scope; it stays loaded */
    free(v->tmp);
    free(v->res);
    free(v);
}

void acvirgl_stats(const struct acvirgl *v, struct acvirgl_stats *s) { *s = v->st; }

const char *acvirgl_api(const struct acvirgl *v) { return v->api; }
const char *acvirgl_renderer(const struct acvirgl *v) { return v->renderer[0] ? v->renderer : "unknown"; }

/* ---- the block ------------------------------------------------------------------------ */

static uint32_t av_be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static void av_put_be32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

/* n bytes from s to d, each unit (1, 2, 4 or 8 bytes) turned round. */
static void av_swap_copy(uint8_t *d, const uint8_t *s, size_t n, uint32_t unit)
{
    size_t i;
    switch (unit) {
    case ACV_SWAP_16:
        for (i = 0; i + 1 < n; i += 2) { d[i] = s[i + 1]; d[i + 1] = s[i]; }
        break;
    case ACV_SWAP_32:
        for (i = 0; i + 3 < n; i += 4) { d[i] = s[i + 3]; d[i + 1] = s[i + 2]; d[i + 2] = s[i + 1]; d[i + 3] = s[i]; }
        break;
    case ACV_SWAP_64:
        for (i = 0; i + 7 < n; i += 8) { int k; for (k = 0; k < 8; k++) d[i + k] = s[i + 7 - k]; }
        break;
    default:
        i = 0;
        break;
    }
    if (i < n) memcpy(d + i, s + i, n - i);   /* units 1 (or what is left over) as they are */
}

/* virgl_hw.h's numbers that ACV_PRESENT and the counters use. */
#define AV_BIND_DISPLAY_TARGET (1u << 7)
#define AV_BIND_SCANOUT        (1u << 18)

static uint32_t av_res_slot(const struct acvirgl *v, uint32_t handle)
{
    return (handle * 0x9E3779B1u) >> 7 & (v->res_cap - 1);
}

/* The resource with this handle, or NULL. */
static const struct av_res *av_res_find(const struct acvirgl *v, uint32_t handle)
{
    uint32_t i;
    if (!handle || !v->res_cap) return NULL;
    for (i = av_res_slot(v, handle); v->res[i].handle; i = (i + 1) & (v->res_cap - 1))
        if (v->res[i].handle == handle) return &v->res[i];
    return NULL;
}

static void av_res_put(struct acvirgl *v, const struct av_res *r)
{
    uint32_t i = av_res_slot(v, r->handle);
    while (v->res[i].handle && v->res[i].handle != r->handle) i = (i + 1) & (v->res_cap - 1);
    if (!v->res[i].handle) v->res_used++;
    v->res[i] = *r;
}

static void av_res_set(struct acvirgl *v, uint32_t handle, const struct av_res *r)
{
    struct av_res e = *r;
    if (!handle) return;                            /* ACV_PRESENT then says no for it */
    if ((v->res_used + 1) * 2 > v->res_cap) {       /* at most half full */
        struct av_res *old = v->res, *p;
        uint32_t n = v->res_cap ? v->res_cap * 2 : 256, i, oc = v->res_cap;
        if (n > 2 * AV_MAX_RES || !(p = calloc(n, sizeof *p))) return;
        v->res = p; v->res_cap = n; v->res_used = 0;
        for (i = 0; i < oc; i++) if (old[i].handle) av_res_put(v, &old[i]);
        free(old);
    }
    e.handle = handle;
    av_res_put(v, &e);
}

/* Takes the handle out, moving back the entries after it that its slot
 * displaced (linear probing's deletion, no tombstones). */
static void av_res_drop(struct acvirgl *v, uint32_t handle)
{
    uint32_t i, j, k, m = v->res_cap - 1;
    const struct av_res *f = av_res_find(v, handle);
    if (!f) return;
    i = (uint32_t)(f - v->res);
    v->res[i].handle = 0;
    v->res_used--;
    for (j = (i + 1) & m; v->res[j].handle; j = (j + 1) & m) {
        k = av_res_slot(v, v->res[j].handle);
        /* j's entry can fill the hole at i unless its home k lies cyclically in (i, j] */
        if ((j > i && (k <= i || k > j)) || (j < i && k <= i && k > j)) {
            v->res[i] = v->res[j];
            v->res[j].handle = 0;
            i = j;
        }
    }
}

/* Where R, G, B and A are in a pixel of a 32-bit colour format, as the
 * host reads it back (4 for A: none, opaque); 0 for formats ACV_PRESENT
 * doesn't take. */
static int av_layout(uint32_t format, int *r, int *g, int *b, int *a)
{
    static const struct { uint8_t f, r, g, b, a; } t[] = {
        { 1, 2, 1, 0, 3 },      /* B8G8R8A8 */
        { 2, 2, 1, 0, 4 },      /* B8G8R8X8 */
        { 3, 1, 2, 3, 0 },      /* A8R8G8B8 */
        { 4, 1, 2, 3, 4 },      /* X8R8G8B8 */
        { 67, 0, 1, 2, 3 },     /* R8G8B8A8 */
        { 68, 3, 2, 1, 4 },     /* X8B8G8R8 */
        { 121, 3, 2, 1, 0 },    /* A8B8G8R8 */
        { 134, 0, 1, 2, 4 },    /* R8G8B8X8 */
    };
    unsigned i;
    for (i = 0; i < sizeof t / sizeof t[0]; i++)
        if (t[i].f == format) { *r = t[i].r; *g = t[i].g; *b = t[i].b; *a = t[i].a; return 1; }
    return 0;
}

static int av_s16(uint32_t v) { v &= 0xFFFFu; return v >= 0x8000u ? (int)v - 0x10000 : (int)v; }

/* ACV_PRESENT: the resource read back once, then written into the target's
 * rectangles in its own format. av_present_prep checks the command and
 * places its rectangles (the pixels it will write, 0, or an ACV_ERR_);
 * av_present_finish reads back and writes. */
static int32_t av_present_prep(struct acvirgl *v, uint8_t *(*map)(void *, uint32_t, uint32_t), void *mu, const uint8_t *a,
                               unsigned nargs, struct av_pres *p)
{
    uint32_t ctx = av_be32(a), handle = av_be32(a + 4), layer = av_be32(a + 8), so = av_be32(a + 12);
    uint32_t addr = av_be32(a + 16), bpr = av_be32(a + 20), fmt = av_be32(a + 24), wh = av_be32(a + 28);
    uint32_t flags = av_be32(a + 32), n = av_be32(a + 36), i, bpp;
    int sx = av_s16(so >> 16), sy = av_s16(so), tw = (int)(wh >> 16), th = (int)(wh & 0xFFFFu);
    int lr, lg, lb, la, rw, rh, bx0 = 0x7FFFFFFF, by0 = 0x7FFFFFFF, bx1 = -1, by1 = -1, flip = (flags & ACV_PRESENT_FLIP_Y) != 0;
    const struct av_res *res;
    uint64_t pixels = 0, span;
    if (n > ACV_PRESENT_MAX || nargs < 10 + 2 * n || (flags & ~(uint32_t)ACV_PRESENT_FLIP_Y)) return ACV_ERR_BADCMD;
    bpp = fmt == OGPU_FMT_XRGB32 || fmt == OGPU_FMT_ARGB32 ? 4 : fmt == OGPU_FMT_RGB565 ? 2 : 0;
    if (!bpp) return ACV_ERR_BADCMD;
    if (!n || !tw || !th) return 0;
    if (bpr < (uint32_t)tw * bpp || bpr > 0x01000000u) return ACV_ERR_BADCMD;
    if (!(res = av_res_find(v, handle)) || !av_layout(res->format, &lr, &lg, &lb, &la))
        return ACV_ERR_RENDERER;
    rw = (int)res->width; rh = (int)res->height;
    /* The target's bytes, in 64 bits: bpr * (th - 1) passes 32 bits (bpr
     * 0x01000000, th 257), and a wrapped length would map one row while the
     * rows below it are written. Past 32 bits it is out of reach. */
    span = (uint64_t)bpr * (uint64_t)(th - 1) + (uint64_t)tw * bpp;
    if (span > 0xFFFFFFFFu || !map(mu, addr, (uint32_t)span)) return ACV_ERR_NOMAP;
    p->nr = 0;
    /* Each rectangle inside the target and over the resource; their box in the resource. */
    for (i = 0; i < n; i++) {
        uint32_t xy = av_be32(a + 40 + i * 8), rwh = av_be32(a + 44 + i * 8);
        int x0 = av_s16(xy >> 16), y0 = av_s16(xy), x1 = x0 + (int)(rwh >> 16), y1 = y0 + (int)(rwh & 0xFFFFu);
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 > tw) x1 = tw;
        if (y1 > th) y1 = th;
        if (x0 < sx) x0 = sx;
        if (y0 < sy) y0 = sy;
        if (x1 > sx + rw) x1 = sx + rw;
        if (y1 > sy + rh) y1 = sy + rh;
        if (x0 >= x1 || y0 >= y1) continue;
        p->rect[p->nr][0] = x0; p->rect[p->nr][1] = y0; p->rect[p->nr][2] = x1; p->rect[p->nr][3] = y1; p->nr++;
        pixels += (uint64_t)(x1 - x0) * (uint64_t)(y1 - y0);
        if (x0 - sx < bx0) bx0 = x0 - sx;
        if (x1 - sx > bx1) bx1 = x1 - sx;
        if (flip) {
            if (rh - (y1 - sy) < by0) by0 = rh - (y1 - sy);
            if (rh - (y0 - sy) > by1) by1 = rh - (y0 - sy);
        } else {
            if (y0 - sy < by0) by0 = y0 - sy;
            if (y1 - sy > by1) by1 = y1 - sy;
        }
    }
    if (!p->nr) return 0;
    p->ctx = ctx; p->handle = handle; p->layer = layer; p->addr = addr; p->bpr = bpr; p->fmt = fmt; p->span = (uint32_t)span;
    p->sx = sx; p->sy = sy; p->rh = rh; p->flip = flip;
    p->box.x = (uint32_t)bx0; p->box.y = (uint32_t)by0; p->box.z = layer;
    p->box.w = (uint32_t)(bx1 - bx0); p->box.h = (uint32_t)(by1 - by0); p->box.d = 1;
    p->fence = 0; p->done = 0;
    return pixels > 0x7FFFFFFF ? 0x7FFFFFFF : (int32_t)pixels;
}

/* One row of n 32-bit pixels: the resource's bytes (channels at lr, lg, lb
 * and la; la 4: none) to A, R, G, B in memory, A 0 for XRGB32 and 0xFF when
 * the resource has none. B, G, R, A (or X) resources, the usual display
 * targets, are a byte swap a pixel on a little-endian host. */
static void av_row32(uint8_t *dp, const uint8_t *sp, int n, int lr, int lg, int lb, int la, int xrgb)
{
    int x;
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    if (lr == 2 && lg == 1 && lb == 0 && (la == 3 || la == 4)) {
        uint32_t clear = xrgb || la == 4 ? 0xFFu : 0, set = !xrgb && la == 4 ? 0xFFu : 0;
        for (x = 0; x < n; x++, sp += 4, dp += 4) {
            uint32_t w;
            memcpy(&w, sp, 4);
            w = (__builtin_bswap32(w) & ~clear) | set;
            memcpy(dp, &w, 4);
        }
        return;
    }
#endif
    for (x = 0; x < n; x++, sp += 4, dp += 4) {
        dp[0] = xrgb ? 0 : la == 4 ? 0xFF : sp[la];
        dp[1] = sp[lr]; dp[2] = sp[lg]; dp[3] = sp[lb];
    }
}

static int32_t av_present_finish(struct acvirgl *v, uint8_t *(*map)(void *, uint32_t, uint32_t), void *mu, const struct av_pres *p)
{
    int lr, lg, lb, la, i;
    const struct av_res *res = av_res_find(v, p->handle);
    struct vr_box box = p->box;
    struct iovec iov;
    uint8_t *dst;
    uint64_t pixels = 0, t0 = av_ns();
    uint32_t fmt = p->fmt, bpr = p->bpr;
    if (!res || !av_layout(res->format, &lr, &lg, &lb, &la) || !av_need(v, (size_t)box.w * box.h * 4)) return ACV_ERR_RENDERER;
    if (!(dst = map(mu, p->addr, p->span))) return ACV_ERR_NOMAP;
    iov.iov_base = v->tmp;
    iov.iov_len = (size_t)box.w * box.h * 4;
    if (v->vr.transfer_read_iov(p->handle, p->ctx, 0, box.w * 4, 0, &box, 0, &iov, 1)) return ACV_ERR_RENDERER;
    for (i = 0; i < p->nr; i++) {
        int x0 = p->rect[i][0], y0 = p->rect[i][1], x1 = p->rect[i][2], y1 = p->rect[i][3], x, y;
        for (y = y0; y < y1; y++) {
            int ry = p->flip ? p->rh - 1 - (y - p->sy) : y - p->sy;
            const uint8_t *sp = v->tmp + ((size_t)(ry - (int)box.y) * box.w + (size_t)(x0 - p->sx - (int)box.x)) * 4;
            if (fmt == OGPU_FMT_RGB565) {
                uint8_t *dp = dst + (size_t)y * bpr + (size_t)x0 * 2;
                for (x = x0; x < x1; x++, sp += 4, dp += 2) {
                    dp[0] = (uint8_t)((sp[lr] & 0xF8) | (sp[lg] >> 5));
                    dp[1] = (uint8_t)(((sp[lg] & 0x1C) << 3) | (sp[lb] >> 3));
                }
            } else
                av_row32(dst + (size_t)y * bpr + (size_t)x0 * 4, sp, x1 - x0, lr, lg, lb, la, fmt == OGPU_FMT_XRGB32);
        }
        pixels += (uint64_t)(x1 - x0) * (uint64_t)(y1 - y0);
    }
    v->st.presents++; v->st.present_rects += (uint64_t)p->nr; v->st.present_pixels += pixels;
    v->st.present_ns += av_ns() - t0;
    return pixels > 0x7FFFFFFF ? 0x7FFFFFFF : (int32_t)pixels;
}

/* ---- presents left to finish (acvirgl_defer_presents) -------------------------------- */

/* Finish (or drop) the deferred presents `match` picks, oldest first. */
static int av_settle_if(struct acvirgl *v, uint8_t *(*map)(void *, uint32_t, uint32_t), void *mu,
                        int (*match)(const struct av_pres *p, const void *arg), const void *arg, int drop)
{
    int i, j, n = 0;
    for (i = 0, j = 0; i < v->npend; i++) {
        struct av_pres *p = &v->pend[i];
        if (!match(p, arg)) {
            if (j != i) v->pend[j] = *p;
            j++;
            continue;
        }
        if (!drop) {
            if (p->done) v->st.deferred_ready++;
            else v->st.deferred_forced++;
            if (av_present_finish(v, map, mu, p) < 0) v->st.errors++;
            n++;
        }
    }
    v->npend = j;
    return n;
}

static int av_m_all(const struct av_pres *p, const void *arg) { (void)p; (void)arg; return 1; }
static int av_m_done(const struct av_pres *p, const void *arg) { (void)arg; return p->done; }
static int av_m_ctx(const struct av_pres *p, const void *arg) { return p->ctx == *(const uint32_t *)arg; }
static int av_m_handle(const struct av_pres *p, const void *arg) { return p->handle == *(const uint32_t *)arg; }
/* a present into the same target whose rectangles meet the new one's */
static int av_m_overlap(const struct av_pres *p, const void *arg)
{
    const struct av_pres *q = arg;
    int i, j;
    if (p->addr != q->addr || p->bpr != q->bpr) {
        uint64_t a0 = p->addr, a1 = a0 + p->span, b0 = q->addr, b1 = b0 + q->span;
        return a0 < b1 && b0 < a1;          /* other targets that share bytes: the older first, to be safe */
    }
    for (i = 0; i < p->nr; i++)
        for (j = 0; j < q->nr; j++)
            if (p->rect[i][0] < q->rect[j][2] && q->rect[j][0] < p->rect[i][2] &&
                p->rect[i][1] < q->rect[j][3] && q->rect[j][1] < p->rect[i][3]) return 1;
    return 0;
}

void acvirgl_defer_presents(struct acvirgl *v, int (*ok)(void *map_user, uint32_t addr, uint32_t bytes))
{
    if (v) v->defer_ok = v->vr.context_create_fence && v->vr.poll ? ok : NULL;
}

int acvirgl_pending(const struct acvirgl *v) { return v ? v->npend : 0; }

int acvirgl_settle(struct acvirgl *v, uint8_t *(*map)(void *map_user, uint32_t address, uint32_t length), void *map_user, int how)
{
    if (!v || !v->npend) return 0;
    if (how == ACVIRGL_DROP) return av_settle_if(v, map, map_user, av_m_all, NULL, 1);
    if (how == ACVIRGL_ALL) return av_settle_if(v, map, map_user, av_m_all, NULL, 0);
    v->vr.poll();
    return av_settle_if(v, map, map_user, av_m_done, NULL, 0);
}

/* ACV_PRESENT. Deferred when the runtime allows it for the target: the
 * frame's fence goes in after the context's commands, and the read back and
 * the writing wait until it has signalled (acvirgl_settle), or until
 * something must see them: the context's next command, the resource going,
 * a present over the same rectangles, the runtime's own drawing. The
 * frame's GPU time then overlaps the 68k's work rather than stopping it. */
static int32_t av_present(struct acvirgl *v, uint8_t *(*map)(void *, uint32_t, uint32_t), void *mu, const uint8_t *a, unsigned nargs)
{
    struct av_pres p;
    int32_t n = av_present_prep(v, map, mu, a, nargs, &p);
    if (n <= 0) return n;
    if (v->npend) av_settle_if(v, map, mu, av_m_overlap, &p, 0);
    if (v->defer_ok && v->npend < AV_PEND && v->defer_ok(mu, p.addr, p.span)) {
        static uint32_t none[1];
        /* the fence goes in the context's own GL context: a command buffer of
         * no words makes it the current one */
        if (!v->vr.submit_cmd(none, (int)p.ctx, 0)) {
            p.fence = ++v->fence_seq;
            if (!v->vr.context_create_fence(p.ctx, 0, 0, p.fence)) {
                v->pend[v->npend++] = p;
                v->st.deferred++;
                return n;
            }
        }
    }
    return av_present_finish(v, map, mu, &p);
}

/* The words a command has after its header and result word. */
static int av_args_of(unsigned cmd)
{
    switch (cmd) {
    case ACV_HELLO: return 1;
    case ACV_CAPSET_INFO: return 3;
    case ACV_CAPS: return 4;
    case ACV_CTX_CREATE: return 3;
    case ACV_CTX_DESTROY: return 1;
    case ACV_RES_CREATE: return 12;
    case ACV_RES_UNREF: return 1;
    case ACV_ATTACH: case ACV_DETACH: return 2;
    case ACV_SUBMIT: return 3;
    case ACV_PUT: case ACV_GET: return 14;
    case ACV_PRESENT: return 10;            /* and two a rectangle */
    }
    return -1;
}

static int32_t av_transfer(struct acvirgl *v, uint8_t *(*map)(void *, uint32_t, uint32_t), void *mu, const uint8_t *a, int get)
{
    uint32_t handle = av_be32(a), ctx = av_be32(a + 4), level = av_be32(a + 8), stride = av_be32(a + 12), layer_stride = av_be32(a + 16);
    struct vr_box box;
    uint32_t addr = av_be32(a + 44), bytes = av_be32(a + 48), unit = av_be32(a + 52);
    uint8_t *mem;
    struct iovec iov;
    box.x = av_be32(a + 20); box.y = av_be32(a + 24); box.z = av_be32(a + 28);
    box.w = av_be32(a + 32); box.h = av_be32(a + 36); box.d = av_be32(a + 40);
    if (!bytes) return ACV_OK;
    if (!(mem = map(mu, addr, bytes))) return ACV_ERR_NOMAP;
    if (!av_need(v, bytes)) return ACV_ERR_RENDERER;
    iov.iov_base = v->tmp;
    iov.iov_len = bytes;
    if (get) {
        if (v->vr.transfer_read_iov(handle, ctx, level, stride, layer_stride, &box, 0, &iov, 1)) return ACV_ERR_RENDERER;
        av_swap_copy(mem, v->tmp, bytes, unit);
        v->st.gets++; v->st.get_bytes += bytes;
        {
            const struct av_res *res = av_res_find(v, handle);
            if (res && (res->bind & (AV_BIND_DISPLAY_TARGET | AV_BIND_SCANOUT))) v->st.frame_get_bytes += bytes;
        }
    } else {
        av_swap_copy(v->tmp, mem, bytes, unit);
        if (v->vr.transfer_write_iov(handle, ctx, (int)level, stride, layer_stride, &box, 0, &iov, 1)) return ACV_ERR_RENDERER;
        v->st.puts++; v->st.put_bytes += bytes;
    }
    return ACV_OK;
}

int acvirgl_run(struct acvirgl *v, uint8_t *(*map)(void *, uint32_t, uint32_t), void *mu, uint32_t addr, uint32_t bytes)
{
    uint8_t *blk;
    uint32_t at = 0, words = bytes / 4;
    if (!v) return OGPU_ERR_BADOP;
    if (!words || !(blk = map(mu, addr, words * 4))) return OGPU_ERR_NOMAP;
    v->st.blocks++;
    if (v->npend) {                         /* frames whose fences have signalled */
        v->vr.poll();
        av_settle_if(v, map, mu, av_m_done, NULL, 0);
    }
    while (at + 2 <= words) {
        uint8_t *c = blk + at * 4, *a = c + 8;
        uint32_t hdr = av_be32(c);
        unsigned cmd = ACV_HDR_CMD(hdr), len = ACV_HDR_WORDS(hdr);
        int n = av_args_of(cmd);
        int32_t r = ACV_OK;
        if (len < 2 || at + len > words || n < 0 || len - 2 < (unsigned)n) {
            av_put_be32(c + 4, (uint32_t)ACV_ERR_BADCMD);
            v->st.errors++;
            return OGPU_OK;                     /* the rest can't be trusted */
        }
        if (v->npend) {
            /* a context's presents are finished before its next command, and a
             * resource's before it goes */
            uint32_t key;
            switch (cmd) {
            case ACV_CTX_DESTROY: case ACV_ATTACH: case ACV_DETACH: case ACV_SUBMIT:
                key = av_be32(a); av_settle_if(v, map, mu, av_m_ctx, &key, 0); break;
            case ACV_PUT: case ACV_GET:
                key = av_be32(a + 4); av_settle_if(v, map, mu, av_m_ctx, &key, 0); break;
            case ACV_RES_UNREF:
                key = av_be32(a); av_settle_if(v, map, mu, av_m_handle, &key, 0); break;
            }
        }
        switch (cmd) {
        case ACV_HELLO:
            av_put_be32(a, ACV_HOST_PRESENT);
            r = ACV_PROTOCOL;
            break;
        case ACV_CAPSET_INFO: {
            uint32_t mv = 0, ms = 0;
            v->vr.get_cap_set(av_be32(a), &mv, &ms);
            av_put_be32(a + 4, mv); av_put_be32(a + 8, ms);
            break;
        }
        case ACV_CAPS: {
            uint32_t set = av_be32(a), ver = av_be32(a + 4), to = av_be32(a + 8), max = av_be32(a + 12), mv = 0, ms = 0, i;
            uint8_t *mem;
            v->vr.get_cap_set(set, &mv, &ms);
            if (ms > max) ms = max;
            ms &= ~3u;
            if (!ms) break;
            if (!(mem = map(mu, to, ms)) || !av_need(v, ms + 4096)) { r = ACV_ERR_NOMAP; break; }
            memset(v->tmp, 0, ms + 4096);
            v->vr.fill_caps(set, ver, v->tmp);
            for (i = 0; i < ms; i += 4) {           /* each word into the Amiga's order */
                uint32_t w;
                memcpy(&w, v->tmp + i, 4);
                av_put_be32(mem + i, w);
            }
            r = (int32_t)ms;
            break;
        }
        case ACV_CTX_CREATE: {
            uint32_t id = av_be32(a), name = av_be32(a + 4), nlen = av_be32(a + 8);
            const uint8_t *s = nlen ? map(mu, name, nlen) : NULL;
            char buf[64];
            if (nlen > sizeof buf - 1) nlen = sizeof buf - 1;
            if (s) memcpy(buf, s, nlen);
            buf[s ? nlen : 0] = 0;
            if (v->vr.context_create(id, (uint32_t)strlen(buf), buf)) r = ACV_ERR_RENDERER;
            /* vrend makes the new context's GL context current to set it up
             * (vrend_renderer_create_sub_ctx) but leaves its record of the
             * current one (current_ctx, current_hw_ctx) as it was. The next
             * command of the context that was current then skips its
             * eglMakeCurrent and runs in the new program's GL context, where
             * its vertex arrays and framebuffers are other names or none:
             * "glBindVertexArray(non-gen name)", DRAW_VBO's GL error 1282
             * (or CLEAR's 1286), and that context is dead ("Illegal command
             * buffer"), one program in a few starting beside others. QEMU
             * forces context 0 before every command; here after the two
             * that change contexts. */
            if (v->vr.force_ctx_0) v->vr.force_ctx_0();
            break;
        }
        case ACV_CTX_DESTROY:
            v->vr.context_destroy(av_be32(a));
            if (v->vr.force_ctx_0) v->vr.force_ctx_0();
            break;
        case ACV_RES_CREATE: {
            struct vr_resource_create_args ra;
            uint32_t ctx = av_be32(a + 44);
            ra.handle = av_be32(a); ra.target = av_be32(a + 4); ra.format = av_be32(a + 8); ra.bind = av_be32(a + 12);
            ra.width = av_be32(a + 16); ra.height = av_be32(a + 20); ra.depth = av_be32(a + 24); ra.array_size = av_be32(a + 28);
            ra.last_level = av_be32(a + 32); ra.nr_samples = av_be32(a + 36); ra.flags = av_be32(a + 40);
            if (v->vr.resource_create(&ra, NULL, 0)) r = ACV_ERR_RENDERER;
            else {
                struct av_res e;
                e.format = ra.format; e.bind = ra.bind; e.width = ra.width; e.height = ra.height;
                av_res_set(v, ra.handle, &e);
                if (ctx) v->vr.ctx_attach_resource((int)ctx, (int)ra.handle);
            }
            break;
        }
        case ACV_RES_UNREF: {
            uint32_t h = av_be32(a);
            v->vr.resource_unref(h);
            av_res_drop(v, h);
            break;
        }
        case ACV_ATTACH: v->vr.ctx_attach_resource((int)av_be32(a), (int)av_be32(a + 4)); break;
        case ACV_DETACH: v->vr.ctx_detach_resource((int)av_be32(a), (int)av_be32(a + 4)); break;
        case ACV_SUBMIT: {
            uint32_t ctx = av_be32(a), cmds = av_be32(a + 4), ndw = av_be32(a + 8);
            const uint8_t *s;
            if (!ndw) break;
            if (ndw > 0x01000000u || !(s = map(mu, cmds, ndw * 4))) { r = ACV_ERR_NOMAP; break; }
            if (!av_need(v, (size_t)ndw * 4)) { r = ACV_ERR_RENDERER; break; }
            memcpy(v->tmp, s, (size_t)ndw * 4);     /* already little-endian, as Mesa's encoder wrote it */
            if (v->vr.submit_cmd(v->tmp, (int)ctx, (int)ndw)) r = ACV_ERR_RENDERER;
            v->st.submits++; v->st.submit_words += ndw;
            break;
        }
        case ACV_PUT: r = av_transfer(v, map, mu, a, 0); break;
        case ACV_GET: r = av_transfer(v, map, mu, a, 1); break;
        case ACV_PRESENT: r = av_present(v, map, mu, a, len - 2); break;
        }
        if (r < 0) v->st.errors++;
        av_put_be32(c + 4, (uint32_t)r);
        at += len;
    }
    return OGPU_OK;
}
