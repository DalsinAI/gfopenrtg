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
#if defined(__linux__)
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#define AV_HAVE_DMABUF 1
/* memfd's names, which a file including this one after <sys/mman.h>
 * without _GNU_SOURCE (acrtg.c) doesn't see: the kernel's numbers */
#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC 1u
#endif
#ifndef MFD_ALLOW_SEALING
#define MFD_ALLOW_SEALING 2u
#endif
#ifndef F_ADD_SEALS
#define F_ADD_SEALS 1033
#endif
#ifndef F_SEAL_SHRINK
#define F_SEAL_SHRINK 2
#endif
#endif
#if defined(__aarch64__) && defined(__ARM_NEON)
#include <arm_neon.h>
#endif

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
    /* optional (presents one frame behind): a resource's GL texture */
    int (*resource_get_info)(int res_handle, void *info);
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

/* virgl_renderer_resource_info as virglrenderer.h gives it (1.0 and later);
 * room to spare should a later one be longer. */
struct vr_resource_info {
    uint32_t handle, virgl_format, width, height, depth, flags, tex_id, stride;
    int drm_fourcc, fd;
    uint32_t spare[16];
};
#define VR_RESOURCE_Y_0_TOP 1       /* its GL rows run bottom up from the resource's top: read back reversed */

/* ---- the GL calls presents one frame behind make, in the frame's own GL
 * context (virglrenderer's, made current for it), found with EGL ---------------------- */

typedef unsigned int av_glenum;
#define AV_GL_UNSIGNED_BYTE         0x1401
#define AV_GL_RGBA                  0x1908
#define AV_GL_BGRA                  0x80E1
#define AV_GL_TEXTURE_2D            0x0DE1
#define AV_GL_PACK_ROW_LENGTH       0x0D02
#define AV_GL_PACK_SKIP_ROWS        0x0D03
#define AV_GL_PACK_SKIP_PIXELS      0x0D04
#define AV_GL_PACK_ALIGNMENT        0x0D05
#define AV_GL_PIXEL_PACK_BUFFER     0x88EB
#define AV_GL_PIXEL_PACK_BUFFER_BINDING 0x88ED
#define AV_GL_STREAM_READ           0x88E1
#define AV_GL_READ_FRAMEBUFFER      0x8CA8
#define AV_GL_READ_FRAMEBUFFER_BINDING 0x8CAA
#define AV_GL_COLOR_ATTACHMENT0     0x8CE0
#define AV_GL_FRAMEBUFFER_COMPLETE  0x8CD5
#define AV_GL_MAP_READ_BIT          0x0001
#define AV_GL_DRAW_FRAMEBUFFER      0x8CA9
#define AV_GL_DRAW_FRAMEBUFFER_BINDING 0x8CA6
#define AV_GL_TEXTURE_BINDING_2D    0x8069
#define AV_GL_SCISSOR_TEST          0x0C11
#define AV_GL_COLOR_BUFFER_BIT      0x4000
#define AV_GL_NEAREST               0x2600
#define AV_GL_DEPTH_ATTACHMENT      0x8D00
#define AV_GL_STENCIL_ATTACHMENT    0x8D20
#define AV_GL_SYNC_GPU_COMMANDS_COMPLETE 0x9117
#define AV_GL_TIMEOUT_EXPIRED       0x911B
#define AV_GL_WAIT_FAILED           0x911D

/* Staging into memory the CPU caches (gfopenrtg #2xx, 9 Nov 2026): the
 * GPU's own memory is mapped write-combined on a Pi (V3D's buffers), where
 * every CPU read goes to DRAM uncached, so reading a frame back cost 14 ms
 * (desktop GL) to 47 ms (GLES) at 1920x1080 on the Pi 5, and its
 * conversion from a mapped pixel-pack buffer 26 to 136 ms. A stage is a
 * dmabuf of ordinary pages (udmabuf over a memfd, else a DMA heap) imported
 * into EGL as a linear B, G, R, A image: the GPU blits the frame into it,
 * the CPU reads it cached after DMA_BUF_IOCTL_SYNC. */
#define AV_STAGES 4
struct av_stage {
    int fd;                     /* the dmabuf; -1: not made */
    uint8_t *mem;               /* its pages, mapped for the CPU */
    size_t bytes;               /* mapped */
    uint32_t w, h, stride;      /* the image */
    uint32_t fourcc;            /* DRM_FORMAT_ARGB8888, or XRGB8888 for a frame with no alpha */
    void *img;                  /* EGLImage */
    unsigned tex;               /* its GL texture (shared by virglrenderer's contexts) */
    int busy;                   /* a present's, until written */
};

struct av_gl {
    void (*GetIntegerv)(av_glenum, int *);
    av_glenum (*GetError)(void);
    void (*GenFramebuffers)(int, unsigned *);
    void (*DeleteFramebuffers)(int, const unsigned *);
    void (*BindFramebuffer)(av_glenum, unsigned);
    void (*FramebufferTexture2D)(av_glenum, av_glenum, av_glenum, unsigned, int);
    av_glenum (*CheckFramebufferStatus)(av_glenum);
    void (*GenBuffers)(int, unsigned *);
    void (*DeleteBuffers)(int, const unsigned *);
    void (*BindBuffer)(av_glenum, unsigned);
    void (*BufferData)(av_glenum, intptr_t, const void *, av_glenum);
    void (*PixelStorei)(av_glenum, int);
    void (*ReadPixels)(int, int, int, int, av_glenum, av_glenum, void *);
    void *(*MapBufferRange)(av_glenum, intptr_t, intptr_t, unsigned);
    unsigned char (*UnmapBuffer)(av_glenum);
    void (*Finish)(void);
    /* stages and depth invalidation: optional (stage_state, inval_ok) */
    void (*GenTextures)(int, unsigned *);
    void (*DeleteTextures)(int, const unsigned *);
    void (*BindTexture)(av_glenum, unsigned);
    void (*BlitFramebuffer)(int, int, int, int, int, int, int, int, unsigned, av_glenum);
    unsigned char (*IsEnabled)(av_glenum);
    void (*Enable)(av_glenum);
    void (*Disable)(av_glenum);
    void (*Flush)(void);
    void *(*FenceSync)(av_glenum, unsigned);
    av_glenum (*ClientWaitSync)(void *, unsigned, uint64_t);
    void (*DeleteSync)(void *);
    void (*EGLImageTargetTexture2DOES)(av_glenum, void *);
    void (*InvalidateFramebuffer)(av_glenum, int, const av_glenum *);
    void *(*GetCurrentDisplay)(void);                                     /* EGL's */
    void *(*CreateImageKHR)(void *, void *, unsigned, void *, const int *);
    unsigned (*DestroyImageKHR)(void *, void *);
};

/* An ACV_PRESENT checked and placed, its pixels not yet read back and
 * written: what av_present_finish needs. */
struct av_pres {
    uint32_t ctx, handle, layer, addr, bpr, fmt, span;
    int sx, sy, rh, flip, nr;
    int rect[ACV_PRESENT_MAX][4];
    struct vr_box box;
    uint64_t fence;             /* deferred: the context's fence after the frame */
    int done;                   /* deferred: that fence has signalled */
    /* behind: the staging buffer the GPU copies the frame into (0: deferred,
     * read back from the resource), its bytes, where R, G, B and A are in its
     * pixels, and whether its rows run bottom up */
    unsigned pbo;
    uint32_t pbo_bytes;
    int lr, lg, lb, la, rev;
    /* or a stage the GPU blits the frame into (1 + its index; 0: none),
     * and the GL fence after the blit */
    int stage;
    void *sync;
    uint32_t stage_at;          /* the box's first row there, in bytes */
};
#define AV_PEND 16                          /* deferred presents at once */

/* Depth and stencil the GPU need not store (9 Nov 2026). Mesa's V3D (a
 * tiler) writes every buffer of a frame's job back to memory when the job
 * ends: colour, and depth/stencil, 8 MB of each at 1920x1080. A game clears
 * depth at the start of each frame, so the stored depth is never read: at
 * ACV_PRESENT the framebuffer's depth/stencil is invalidated (as EGL does
 * at a swap) when the context cleared both since its last present, and has
 * at every present before. A context that once presents with a depth
 * buffer it didn't clear is never invalidated again (its first such frame
 * may lose that depth: PiStorm3D's trade). Read from the command stream as
 * it goes by (av_zs_scan). AC_VIRGL_ZDISCARD=0 turns it off. */
#define AV_ZS_CTX 16                        /* contexts followed */
#define AV_ZS_SURF 1024                     /* surfaces of each (a hash; full: given up) */
struct av_zs {
    uint32_t ctx;                           /* 0: a free slot */
    uint32_t sub;                           /* its sub-context (another: given up) */
    int off;                                /* given up for good */
    uint32_t zsurf;                         /* the framebuffer's depth surface now */
    int dirty;                              /* commands since the last present */
    uint32_t cleared_surf, cleared;         /* the one cleared since the last present, and which (1 depth, 2 stencil) */
    uint32_t surf[AV_ZS_SURF][2];           /* surface handle, its resource */
};

struct acvirgl {
    int no_batch;                       /* AC_VIRGL_BATCH=0: HELLO asks GL.module for a block a command (ACV_HOST_NO_BATCH) */
    int ctx0_every;                     /* context 0 before every command (QEMU's way); AC_VIRGL_CTX0=create: only around ACV_CTX_* */
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
    /* presents one frame behind (acvirgl_behind_presents) */
    int (*behind_ok)(void *map_user, uint32_t addr, uint32_t bytes);
    struct av_gl gl;
    void *(*egl_current)(void); /* eglGetCurrentContext */
    int gl_state;               /* 0 not looked for, 1 found, -1 not there */
    struct { unsigned id; uint32_t bytes; } pool[AV_PEND];     /* staging buffers not in use */
    int npool;
    int parts;                  /* AC_VIRGL_PARTS=1: the GPU's wait timed apart from the read back */
    uint64_t stg_ns[5];         /* parts of asking for the staging copy: switch, framebuffer, read, put back, fence */
    /* stages (struct av_stage): 0 not tried, 1 in use, -1 not here or off
     * (AC_VIRGL_STAGE: 1 on, 0 off; unset: on for V3D, the Pi's GPU) */
    int stage_state;
    uint32_t stage_min;         /* pixels a present needs to go through one (smaller: read back as before) */
    struct av_stage stages[AV_STAGES];
    int stage_heap;             /* the DMA heap's fd, or -1 (udmabuf: the device's, udmabuf_fd) */
    int udmabuf_fd;
    int stage_said;             /* the log told which way stages went */
    /* depth/stencil invalidation (struct av_zs) */
    int zs_on;
    struct av_zs *zs;           /* AV_ZS_CTX of them, made when first needed */
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
    /* a resource's GL texture, for presents one frame behind; AC_VIRGL_PARTS=1 times a present's parts */
    *(void **)&v->vr.resource_get_info = dlsym(v->dl, "virgl_renderer_resource_get_info");
    v->parts = getenv("AC_VIRGL_PARTS") && !strcmp(getenv("AC_VIRGL_PARTS"), "1");
    /* GL.module queues commands into one block until it needs their results
     * (gla_virgl.c, 9 Nov 2026); AC_VIRGL_BATCH=0 asks it for a block a command, as before */
    v->no_batch = getenv("AC_VIRGL_BATCH") && !strcmp(getenv("AC_VIRGL_BATCH"), "0");
    /* per-context fences, for presents left to finish (virglrenderer 1.0 and later) */
    *(void **)&v->vr.context_create_fence = dlsym(v->dl, "virgl_renderer_context_create_fence");
    *(void **)&v->vr.poll = dlsym(v->dl, "virgl_renderer_poll");
    *(void **)&v->vr.force_ctx_0 = dlsym(v->dl, "virgl_renderer_force_ctx_0");
    /* Context 0 before every command, as QEMU does. Around context creation
     * only (#198) cured the contexts lost with "glBindVertexArray(non-gen
     * name)", but some still died with GL_INVALID_FRAMEBUFFER_OPERATION
     * (1286) at a draw: in t4-gl1286, 4 rounds of three ogles3 demos
     * started staggered a run, 6 contexts in 6 runs (2 of the runs with the
     * ring on the C core) against 0 in 6 runs with this. Some other vrend
     * path leaves its record of the current context and the GL context
     * really current apart; QEMU never meets it because it forces context 0
     * before everything. It costs two eglMakeCurrent calls a command, and
     * three demos at VSYNC kept 49 fps. AC_VIRGL_CTX0=create goes back to
     * #198's way, for comparing. */
    { const char *c0 = getenv("AC_VIRGL_CTX0"); v->ctx0_every = !(c0 && !strcmp(c0, "create")) && v->vr.force_ctx_0; }
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
    {
        const char *st = getenv("AC_VIRGL_STAGE"), *zd = getenv("AC_VIRGL_ZDISCARD");
        int i;
        v->stage_heap = v->udmabuf_fd = -1;
        for (i = 0; i < AV_STAGES; i++) v->stages[i].fd = -1;
        /* on by default where the GPU's memory is write-combined for the CPU (V3D) */
        v->stage_state = st && *st ? (strcmp(st, "0") ? 0 : -1) : (!strncmp(v->renderer, "V3D", 3) ? 0 : -1);
        /* a small box costs little either way; a stage per size would be made and remade */
        v->stage_min = st && !strcmp(st, "1") ? 0 : 16384;
        v->zs_on = !(zd && !strcmp(zd, "0"));
    }
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
#ifdef AV_HAVE_DMABUF
    {
        int i;
        for (i = 0; i < AV_STAGES; i++)     /* the GL objects went with virglrenderer's contexts */
            if (v->stages[i].fd >= 0) { munmap(v->stages[i].mem, v->stages[i].bytes); close(v->stages[i].fd); }
        if (v->stage_heap >= 0) close(v->stage_heap);
        if (v->udmabuf_fd >= 0) close(v->udmabuf_fd);
    }
#endif
    free(v->zs);
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
    p->fence = 0; p->done = 0; p->pbo = 0; p->pbo_bytes = 0; p->rev = 0; p->stage = 0; p->sync = NULL;
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
        x = 0;
#if defined(__aarch64__) && defined(__ARM_NEON)
        {
            /* four pixels at a time (a Pi: 1920x1080 in about a third of the time) */
            const uint8x16_t keep = vreinterpretq_u8_u32(vdupq_n_u32(~clear)), add = vreinterpretq_u8_u32(vdupq_n_u32(set));
            for (; x + 4 <= n; x += 4, sp += 16, dp += 16)
                vst1q_u8(dp, vorrq_u8(vandq_u8(vrev32q_u8(vld1q_u8(sp)), keep), add));
        }
#endif
        for (; x < n; x++, sp += 4, dp += 4) {
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

/* The present's rectangles written into dst from src, the box read back:
 * rows of box.w pixels sstride bytes apart with R, G, B and A at lr, lg,
 * lb and la (la 4: none), bottom up when rev. */
static uint64_t av_present_write(const struct av_pres *p, uint8_t *dst, const uint8_t *src, size_t sstride, int lr, int lg, int lb, int la, int rev)
{
    const struct vr_box *box = &p->box;
    uint64_t pixels = 0;
    int i;
    for (i = 0; i < p->nr; i++) {
        int x0 = p->rect[i][0], y0 = p->rect[i][1], x1 = p->rect[i][2], y1 = p->rect[i][3], x, y;
        for (y = y0; y < y1; y++) {
            int ry = (p->flip ? p->rh - 1 - (y - p->sy) : y - p->sy) - (int)box->y;
            const uint8_t *sp;
            if (rev) ry = (int)box->h - 1 - ry;
            sp = src + (size_t)ry * sstride + (size_t)(x0 - p->sx - (int)box->x) * 4;
            if (p->fmt == OGPU_FMT_RGB565) {
                uint8_t *dp = dst + (size_t)y * p->bpr + (size_t)x0 * 2;
                for (x = x0; x < x1; x++, sp += 4, dp += 2) {
                    dp[0] = (uint8_t)((sp[lr] & 0xF8) | (sp[lg] >> 5));
                    dp[1] = (uint8_t)(((sp[lg] & 0x1C) << 3) | (sp[lb] >> 3));
                }
            } else
                av_row32(dst + (size_t)y * p->bpr + (size_t)x0 * 4, sp, x1 - x0, lr, lg, lb, la, p->fmt == OGPU_FMT_XRGB32);
        }
        pixels += (uint64_t)(x1 - x0) * (uint64_t)(y1 - y0);
    }
    return pixels;
}

/* The context's GL context made current: virglrenderer does it for a
 * command buffer, even one of no words. */
static int av_current(struct acvirgl *v, uint32_t ctx)
{
    static uint32_t none[1];
    return !v->vr.submit_cmd(none, (int)ctx, 0);
}

/* The GL entry points, from EGL (virglrenderer's libepoxy has loaded it). */
static int av_gl_load(struct acvirgl *v)
{
    void *egl;
    void *(*gpa)(const char *);
    if (v->gl_state) return v->gl_state > 0;
    v->gl_state = -1;
    if (!(egl = dlopen("libEGL.so.1", RTLD_NOW | RTLD_LOCAL))) return 0;
    if (!(*(void **)&gpa = dlsym(egl, "eglGetProcAddress"))
        || !(*(void **)&v->egl_current = dlsym(egl, "eglGetCurrentContext"))) return 0;
#define GLF(f) if (!(*(void **)&v->gl.f = gpa("gl" #f))) return 0;
    GLF(GetIntegerv) GLF(GetError) GLF(GenFramebuffers) GLF(DeleteFramebuffers) GLF(BindFramebuffer)
    GLF(FramebufferTexture2D) GLF(CheckFramebufferStatus) GLF(GenBuffers) GLF(DeleteBuffers) GLF(BindBuffer)
    GLF(BufferData) GLF(PixelStorei) GLF(ReadPixels) GLF(MapBufferRange) GLF(UnmapBuffer) GLF(Finish)
#undef GLF
    /* for stages and depth invalidation: optional, NULL when not there */
#define GLO(f) *(void **)&v->gl.f = gpa("gl" #f);
    GLO(GenTextures) GLO(DeleteTextures) GLO(BindTexture) GLO(BlitFramebuffer) GLO(IsEnabled) GLO(Enable) GLO(Disable)
    GLO(Flush) GLO(FenceSync) GLO(ClientWaitSync) GLO(DeleteSync) GLO(EGLImageTargetTexture2DOES) GLO(InvalidateFramebuffer)
#undef GLO
    *(void **)&v->gl.GetCurrentDisplay = dlsym(egl, "eglGetCurrentDisplay");
    *(void **)&v->gl.CreateImageKHR = gpa("eglCreateImageKHR");
    *(void **)&v->gl.DestroyImageKHR = gpa("eglDestroyImageKHR");
    v->gl_state = 1;                        /* libEGL stays open with the GL it gave */
    return 1;
}

/* Staging buffers: one not in use, at least `bytes` if there is one (else
 * it grows), or a new one (0: none). Back when done; past AV_PEND, gone. */
static unsigned av_pool_get(struct acvirgl *v, uint32_t bytes, uint32_t *have)
{
    int i, best = -1;
    unsigned id = 0;
    for (i = 0; i < v->npool; i++)
        if (best < 0 || (v->pool[i].bytes >= bytes && (v->pool[best].bytes < bytes || v->pool[i].bytes < v->pool[best].bytes))) best = i;
    if (best >= 0) {
        id = v->pool[best].id; *have = v->pool[best].bytes;
        v->pool[best] = v->pool[--v->npool];
        return id;
    }
    v->gl.GenBuffers(1, &id);
    *have = 0;
    return id;
}

static void av_pool_put(struct acvirgl *v, unsigned id, uint32_t bytes, int gl)
{
    if (!id) return;
    if (v->npool < AV_PEND) { v->pool[v->npool].id = id; v->pool[v->npool].bytes = bytes; v->npool++; }
    else if (gl) v->gl.DeleteBuffers(1, &id);
}

/* ---- stages: the frame blitted by the GPU into memory the CPU caches ------------------- */

#ifdef AV_HAVE_DMABUF
/* The kernel's interfaces, as linux/udmabuf.h, linux/dma-heap.h and
 * linux/dma-buf.h give them (declared here: older headers lack some). */
struct av_udmabuf_create { uint32_t memfd, flags; uint64_t offset, size; };
#define AV_UDMABUF_CREATE   _IOW('u', 0x42, struct av_udmabuf_create)
#define AV_UDMABUF_CLOEXEC  1
struct av_heap_alloc { uint64_t len; uint32_t fd, fd_flags; uint64_t heap_flags; };
#define AV_DMA_HEAP_ALLOC   _IOWR('H', 0x0, struct av_heap_alloc)
struct av_dmabuf_sync { uint64_t flags; };
#define AV_DMA_BUF_SYNC     _IOW('b', 0, struct av_dmabuf_sync)
#define AV_SYNC_READ        1
#define AV_SYNC_END         4

static void av_dmabuf_sync(int fd, uint64_t flags)
{
    struct av_dmabuf_sync s;
    s.flags = flags;
    while (ioctl(fd, AV_DMA_BUF_SYNC, &s) && (errno == EINTR || errno == EAGAIN)) ;
}

/* A dmabuf of `bytes` of ordinary pages, mapped (cached) at *mem: udmabuf
 * over a memfd (any user the device allows: its pages are the process's
 * own), else the system DMA heap, else the CMA heap. Its fd, or -1. */
static int av_dmabuf_alloc(struct acvirgl *v, size_t bytes, uint8_t **mem)
{
    int fd = -1;
    void *p;
    if (v->udmabuf_fd == -1 && (v->udmabuf_fd = open("/dev/udmabuf", O_RDWR | O_CLOEXEC)) < 0) v->udmabuf_fd = -2;
    if (v->udmabuf_fd >= 0) {
        int mfd = (int)syscall(SYS_memfd_create, "acvirgl-stage", MFD_CLOEXEC | MFD_ALLOW_SEALING);
        if (mfd >= 0 && !ftruncate(mfd, (off_t)bytes) && !fcntl(mfd, F_ADD_SEALS, F_SEAL_SHRINK)) {
            struct av_udmabuf_create c;
            memset(&c, 0, sizeof c);
            c.memfd = (uint32_t)mfd; c.flags = AV_UDMABUF_CLOEXEC; c.size = bytes;
            fd = ioctl(v->udmabuf_fd, AV_UDMABUF_CREATE, &c);
        }
        if (mfd >= 0) close(mfd);
    }
    if (fd < 0) {
        if (v->stage_heap == -1 && (v->stage_heap = open("/dev/dma_heap/system", O_RDWR | O_CLOEXEC)) < 0
            && (v->stage_heap = open("/dev/dma_heap/linux,cma", O_RDWR | O_CLOEXEC)) < 0)
            v->stage_heap = -2;
        if (v->stage_heap >= 0) {
            struct av_heap_alloc a;
            memset(&a, 0, sizeof a);
            a.len = bytes; a.fd_flags = O_RDWR | O_CLOEXEC;
            if (!ioctl(v->stage_heap, AV_DMA_HEAP_ALLOC, &a)) fd = (int)a.fd;
        }
    }
    if (fd < 0) return -1;
    if ((p = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0)) == MAP_FAILED) { close(fd); return -1; }
    *mem = p;
    if (!v->stage_said) {
        v->stage_said = 1;
        fprintf(stderr, "ACVIRGL: frames read back through a stage in cached memory (%s)\n",
                v->udmabuf_fd >= 0 ? "udmabuf" : "a DMA heap");
    }
    return fd;
}

static void av_stage_free(struct acvirgl *v, struct av_stage *s)
{
    if (s->fd < 0) return;
    if (s->tex) v->gl.DeleteTextures(1, &s->tex);
    if (s->img) v->gl.DestroyImageKHR(v->gl.GetCurrentDisplay(), s->img);
    munmap(s->mem, s->bytes);
    close(s->fd);
    memset(s, 0, sizeof *s);
    s->fd = -1;
}

/* A stage not in use of w x h in that format, made (or made
 * again) as needed, in the GL context that is current; its index, or -1. */
static int av_stage_get(struct acvirgl *v, uint32_t w, uint32_t h, uint32_t fourcc)
{
    int i, pick = -1, tb = 0;
    struct av_stage *s;
    for (i = 0; i < AV_STAGES; i++) {
        s = &v->stages[i];
        if (s->busy) continue;
        if (s->fd >= 0 && s->w == w && s->h == h && s->fourcc == fourcc) return i;
        if (pick < 0 || s->fd < 0) pick = i;
    }
    if (pick < 0) return -1;
    s = &v->stages[pick];
    av_stage_free(v, s);
    {
        /* rows 256 bytes apart (a linear image's alignment on most GPUs);
         * exactly the frame's size, which V3D's fast blit wants */
        uint32_t sw = w, sh = h, stride = (w * 4 + 255) & ~255u;
        size_t bytes = ((size_t)stride * sh + 4095) & ~(size_t)4095;
        int attrs[] = { 0x3057 /* EGL_WIDTH */, (int)sw, 0x3056 /* EGL_HEIGHT */, (int)sh,
                        0x3271 /* EGL_LINUX_DRM_FOURCC_EXT */, (int)fourcc,
                        0x3272 /* EGL_DMA_BUF_PLANE0_FD_EXT */, 0, 0x3273 /* EGL_DMA_BUF_PLANE0_OFFSET_EXT */, 0,
                        0x3274 /* EGL_DMA_BUF_PLANE0_PITCH_EXT */, (int)stride, 0x3038 /* EGL_NONE */ };
        void *dpy = v->gl.GetCurrentDisplay();
        if (!dpy || (s->fd = av_dmabuf_alloc(v, bytes, &s->mem)) < 0) {
            s->fd = -1;
            for (i = 0; i < AV_STAGES && v->stages[i].fd < 0; i++) ;
            if (!dpy || i == AV_STAGES) {   /* none to be had at all: not again */
                v->stage_state = -1;
                fprintf(stderr, "ACVIRGL: no stage for presents (%s): frames read back as before\n",
                        !dpy ? "no EGL display" : "no /dev/udmabuf or DMA heap this user may open");
            }
            return -1;
        }
        s->bytes = bytes; s->w = sw; s->h = sh; s->stride = stride; s->fourcc = fourcc;
        attrs[7] = s->fd;
        if (!(s->img = v->gl.CreateImageKHR(dpy, NULL, 0x3270 /* EGL_LINUX_DMA_BUF_EXT */, NULL, attrs))) {
            av_stage_free(v, s);
            v->stage_state = -1;            /* the driver won't take it: the old way from now on */
            fprintf(stderr, "ACVIRGL: no stage for presents (EGL won't import the dmabuf): frames read back as before\n");
            return -1;
        }
        for (i = 0; i < 8 && v->gl.GetError(); i++) ;
        v->gl.GetIntegerv(AV_GL_TEXTURE_BINDING_2D, &tb);
        v->gl.GenTextures(1, &s->tex);
        v->gl.BindTexture(AV_GL_TEXTURE_2D, s->tex);
        v->gl.EGLImageTargetTexture2DOES(AV_GL_TEXTURE_2D, s->img);
        v->gl.BindTexture(AV_GL_TEXTURE_2D, (unsigned)tb);
        if (v->gl.GetError()) {
            av_stage_free(v, s);
            v->stage_state = -1;
            return -1;
        }
    }
    return pick;
}

/* The present's box of the frame blitted by the GPU into a stage, in the
 * frame's GL context after its drawing, with a GL fence after it; its rows
 * as glReadPixels would give them (bottom up when p->rev). virglrenderer's
 * GL state is put back as it was. 1 when asked (p->stage), 0 when it
 * can't (the present then goes another way). */
static int av_stage_start(struct acvirgl *v, struct av_pres *p)
{
    struct vr_resource_info info;
    const struct av_res *res = av_res_find(v, p->handle);
    int dl, dg, db, da, rfb = 0, dfb = 0, sc, i, si, ok;
    unsigned fb[2] = { 0, 0 };
    struct av_stage *s;
    if (v->stage_state < 0 || (uint64_t)p->box.w * p->box.h < v->stage_min || !res || !av_layout(res->format, &dl, &dg, &db, &da)
        || p->layer || !v->vr.resource_get_info)
        return 0;
    if (!v->stage_state) {                  /* the first time: all it needs there? */
        if (!av_gl_load(v) || !v->gl.BlitFramebuffer || !v->gl.GenTextures || !v->gl.DeleteTextures || !v->gl.BindTexture
            || !v->gl.IsEnabled || !v->gl.Enable || !v->gl.Disable || !v->gl.Flush || !v->gl.FenceSync || !v->gl.ClientWaitSync
            || !v->gl.DeleteSync || !v->gl.EGLImageTargetTexture2DOES || !v->gl.GetCurrentDisplay || !v->gl.CreateImageKHR
            || !v->gl.DestroyImageKHR) {
            v->stage_state = -1;
            return 0;
        }
        v->stage_state = 1;
    }
    memset(&info, 0, sizeof info);
    if (v->vr.resource_get_info((int)p->handle, &info) || !info.tex_id || !av_current(v, p->ctx)) return 0;
    /* The stage is the frame's size and the blit keeps the box where it is,
     * in the frame's own format (X for a frame with none): Mesa's V3D then
     * makes the blit a second store of the frame's own job (its fast TLB
     * blit): no load, no draw. */
    if ((si = av_stage_get(v, info.width, info.height, da == 4 ? 0x34325258 /* XRGB8888 */ : 0x34325241 /* ARGB8888 */)) < 0)
        return 0;
    s = &v->stages[si];
    for (i = 0; i < 8 && v->gl.GetError(); i++) ;     /* errors that were virglrenderer's, not ours */
    v->gl.GetIntegerv(AV_GL_READ_FRAMEBUFFER_BINDING, &rfb);
    v->gl.GetIntegerv(AV_GL_DRAW_FRAMEBUFFER_BINDING, &dfb);
    sc = v->gl.IsEnabled(AV_GL_SCISSOR_TEST);
    v->gl.GenFramebuffers(2, fb);
    v->gl.BindFramebuffer(AV_GL_READ_FRAMEBUFFER, fb[0]);
    v->gl.FramebufferTexture2D(AV_GL_READ_FRAMEBUFFER, AV_GL_COLOR_ATTACHMENT0, AV_GL_TEXTURE_2D, info.tex_id, 0);
    v->gl.BindFramebuffer(AV_GL_DRAW_FRAMEBUFFER, fb[1]);
    v->gl.FramebufferTexture2D(AV_GL_DRAW_FRAMEBUFFER, AV_GL_COLOR_ATTACHMENT0, AV_GL_TEXTURE_2D, s->tex, 0);
    ok = v->gl.CheckFramebufferStatus(AV_GL_READ_FRAMEBUFFER) == AV_GL_FRAMEBUFFER_COMPLETE
         && v->gl.CheckFramebufferStatus(AV_GL_DRAW_FRAMEBUFFER) == AV_GL_FRAMEBUFFER_COMPLETE;
    p->sync = NULL;
    if (ok) {
        int y1 = info.flags & VR_RESOURCE_Y_0_TOP ? (int)info.height - (int)p->box.y - (int)p->box.h : (int)p->box.y;
        if (sc) v->gl.Disable(AV_GL_SCISSOR_TEST);
        v->gl.BlitFramebuffer((int)p->box.x, y1, (int)(p->box.x + p->box.w), y1 + (int)p->box.h,
                              (int)p->box.x, y1, (int)(p->box.x + p->box.w), y1 + (int)p->box.h, AV_GL_COLOR_BUFFER_BIT, AV_GL_NEAREST);
        p->stage_at = (uint32_t)y1 * s->stride + p->box.x * 4;
        if (sc) v->gl.Enable(AV_GL_SCISSOR_TEST);
        ok = v->gl.GetError() == 0 && (p->sync = v->gl.FenceSync(AV_GL_SYNC_GPU_COMMANDS_COMPLETE, 0)) != NULL;
        v->gl.Flush();                      /* the frame and the blit to the GPU now */
    }
    v->gl.BindFramebuffer(AV_GL_READ_FRAMEBUFFER, (unsigned)rfb);
    v->gl.BindFramebuffer(AV_GL_DRAW_FRAMEBUFFER, (unsigned)dfb);
    v->gl.DeleteFramebuffers(2, fb);
    if (!ok) {
        if (p->sync) v->gl.DeleteSync(p->sync);
        p->sync = NULL;
        return 0;
    }
    s->busy = 1;
    p->stage = si + 1;
    p->lr = 2; p->lg = 1; p->lb = 0; p->la = da == 4 ? 4 : 3;
    p->rev = (info.flags & VR_RESOURCE_Y_0_TOP) != 0;
    return 1;
}

/* A stage's present no longer wanted (dropped): its stage free again. */
static void av_stage_drop(struct acvirgl *v, struct av_pres *p)
{
    if (!p->stage) return;
    v->stages[p->stage - 1].busy = 0;
    if (p->sync && v->egl_current && v->egl_current()) v->gl.DeleteSync(p->sync);
    p->stage = 0; p->sync = NULL;
}

/* Waits for the stage's blit (its fence, unless already signalled) and
 * makes its pages the CPU's; 0 when that failed. */
static int av_stage_wait(struct acvirgl *v, struct av_pres *p)
{
    int ok = 1;
    if (p->sync && (v->egl_current() || av_current(v, p->ctx))) {
        if (!p->done && v->gl.ClientWaitSync(p->sync, 0, 4000000000ull) == AV_GL_WAIT_FAILED) ok = 0;
        v->gl.DeleteSync(p->sync);
    }
    p->sync = NULL;
    av_dmabuf_sync(v->stages[p->stage - 1].fd, AV_SYNC_READ);  /* waits for its writer too, and invalidates the CPU's caches */
    return ok;
}
#else
static int av_stage_start(struct acvirgl *v, struct av_pres *p) { (void)v; (void)p; return 0; }
static void av_stage_drop(struct acvirgl *v, struct av_pres *p) { (void)v; (void)p; }
#endif

/* ---- depth/stencil invalidation (struct av_zs) -------------------------------------- */

#define AV_CCMD_CREATE_OBJECT 1             /* virgl_protocol.h's numbers */
#define AV_CCMD_SET_FRAMEBUFFER_STATE 5
#define AV_CCMD_CLEAR 7
#define AV_CCMD_SET_SUB_CTX 28
#define AV_OBJECT_SURFACE 8

static struct av_zs *av_zs_of(struct acvirgl *v, uint32_t ctx, int make)
{
    int i, fr = -1;
    if (!ctx) return NULL;
    if (!v->zs && (!make || !(v->zs = calloc(AV_ZS_CTX, sizeof *v->zs)))) return NULL;
    for (i = 0; i < AV_ZS_CTX; i++) {
        if (v->zs[i].ctx == ctx) return &v->zs[i];
        if (!v->zs[i].ctx && fr < 0) fr = i;
    }
    if (!make || fr < 0) return NULL;
    memset(&v->zs[fr], 0, sizeof v->zs[fr]);
    v->zs[fr].ctx = ctx;
    return &v->zs[fr];
}

static void av_zs_forget(struct acvirgl *v, uint32_t ctx)
{
    struct av_zs *z = v->zs ? av_zs_of(v, ctx, 0) : NULL;
    if (z) z->ctx = 0;
}

static uint32_t *av_zs_slot(struct av_zs *z, uint32_t surf, int make)
{
    uint32_t i = (surf * 0x9E3779B1u) >> 22 & (AV_ZS_SURF - 1), n;
    for (n = 0; n < AV_ZS_SURF; n++, i = (i + 1) & (AV_ZS_SURF - 1)) {
        if (z->surf[i][0] == surf) return z->surf[i];
        if (!z->surf[i][0]) {
            if (!make) return NULL;
            z->surf[i][0] = surf;
            return z->surf[i];
        }
    }
    return NULL;
}

static uint32_t av_le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

/* A command stream of ndw words (little-endian) the context submits: its
 * surfaces, its framebuffer's depth surface, and its clears. */
static void av_zs_scan(struct acvirgl *v, uint32_t ctx, const uint8_t *s, uint32_t ndw)
{
    struct av_zs *z;
    uint32_t i = 0;
    if (!v->zs_on || !(z = av_zs_of(v, ctx, 1)) || z->off) return;
    z->dirty = 1;
    while (i < ndw) {
        uint32_t h = av_le32(s + i * 4), len = h >> 16, cmd = h & 0xFF;
        const uint8_t *a = s + (i + 1) * 4;
        if (len > ndw - i - 1) break;
        switch (cmd) {
        case AV_CCMD_CREATE_OBJECT:
            if ((h >> 8 & 0xFF) == AV_OBJECT_SURFACE && len >= 2) {
                uint32_t *e = av_zs_slot(z, av_le32(a), 1);
                if (!e) { z->off = 1; return; }     /* more than it follows */
                e[1] = av_le32(a + 4);
            }
            break;
        case AV_CCMD_SET_FRAMEBUFFER_STATE:
            if (len >= 2) z->zsurf = av_le32(a + 4);
            break;
        case AV_CCMD_CLEAR:
            if (len >= 1 && z->zsurf && (av_le32(a) & 3)) {
                if (z->cleared_surf != z->zsurf) { z->cleared_surf = z->zsurf; z->cleared = 0; }
                z->cleared |= av_le32(a) & 3;      /* PIPE_CLEAR_DEPTH, PIPE_CLEAR_STENCIL */
            }
            break;
        case AV_CCMD_SET_SUB_CTX:
            if (len >= 1) {
                if (!z->sub) z->sub = av_le32(a);
                else if (z->sub != av_le32(a)) { z->off = 1; return; }   /* several: not followed */
            }
            break;
        }
        i += 1 + len;
    }
}

/* At a present: the framebuffer's depth/stencil invalidated in the
 * context's GL context, before anything flushes its job, when the context
 * has cleared it since its last present (and always has). */
static void av_zs_discard(struct acvirgl *v, const struct av_pres *p)
{
    struct av_zs *z = v->zs_on && v->zs ? av_zs_of(v, p->ctx, 0) : NULL;
    const uint32_t *e;
    const struct av_res *zr;
    struct vr_resource_info zi, ci;
    int stencil, rfb = 0, i;
    unsigned fb = 0;
    if (!z || z->off || !z->dirty) return;  /* nothing drawn since the last present: as it was */
    z->dirty = 0;
    if (!z->zsurf) { z->cleared_surf = 0; return; }     /* no depth buffer */
    e = av_zs_slot(z, z->zsurf, 0);
    zr = e ? av_res_find(v, e[1]) : NULL;
    if (!zr) { z->off = 1; return; }
    stencil = !(zr->format == 16 || zr->format == 17 || zr->format == 18 || zr->format == 21 || zr->format == 22);
    if (z->cleared_surf != z->zsurf || (z->cleared & (stencil ? 3u : 1u)) != (stencil ? 3u : 1u)) {
        z->off = 1;                         /* kept from frame to frame: never again for this context */
        return;
    }
    z->cleared_surf = 0; z->cleared = 0;
    memset(&zi, 0, sizeof zi); memset(&ci, 0, sizeof ci);
    if (!v->vr.resource_get_info || !av_gl_load(v) || !v->gl.InvalidateFramebuffer) return;
    /* a depth format has no DRM fourcc: virglrenderer says EINVAL for that,
     * after filling in the rest (the texture) */
    v->vr.resource_get_info((int)zr->handle, &zi);
    if (!zi.tex_id || v->vr.resource_get_info((int)p->handle, &ci) || !ci.tex_id || !av_current(v, p->ctx)) return;
    for (i = 0; i < 8 && v->gl.GetError(); i++) ;
    v->gl.GetIntegerv(AV_GL_READ_FRAMEBUFFER_BINDING, &rfb);
    v->gl.GenFramebuffers(1, &fb);
    v->gl.BindFramebuffer(AV_GL_READ_FRAMEBUFFER, fb);
    /* the colour too, so the framebuffer is complete everywhere; only depth/stencil invalidated */
    v->gl.FramebufferTexture2D(AV_GL_READ_FRAMEBUFFER, AV_GL_COLOR_ATTACHMENT0, AV_GL_TEXTURE_2D, ci.tex_id, 0);
    v->gl.FramebufferTexture2D(AV_GL_READ_FRAMEBUFFER, AV_GL_DEPTH_ATTACHMENT, AV_GL_TEXTURE_2D, zi.tex_id, 0);
    if (stencil) v->gl.FramebufferTexture2D(AV_GL_READ_FRAMEBUFFER, AV_GL_STENCIL_ATTACHMENT, AV_GL_TEXTURE_2D, zi.tex_id, 0);
    if (v->gl.CheckFramebufferStatus(AV_GL_READ_FRAMEBUFFER) == AV_GL_FRAMEBUFFER_COMPLETE) {
        static const av_glenum zs[2] = { AV_GL_DEPTH_ATTACHMENT, AV_GL_STENCIL_ATTACHMENT };
        v->gl.InvalidateFramebuffer(AV_GL_READ_FRAMEBUFFER, stencil ? 2 : 1, zs);
        if (!v->gl.GetError()) v->st.zs_discards++;
    }
    v->gl.BindFramebuffer(AV_GL_READ_FRAMEBUFFER, (unsigned)rfb);
    v->gl.DeleteFramebuffers(1, &fb);
    for (i = 0; i < 8 && v->gl.GetError(); i++) ;     /* ours: not left for virglrenderer to find */
}

static void av_parts_say(struct acvirgl *v)
{
    const struct acvirgl_stats *s = &v->st;
    double n = s->presents ? (double)s->presents : 1;
    if (!v->parts || s->presents % 500) return;
    fprintf(stderr, "ACVIRGL: %llu presents (%llu behind): a present %.0f us: GPU wait %.0f, read back %.0f (of it waited %.0f), "
            "conversion %.0f, staging asked %.0f\n", (unsigned long long)s->presents, (unsigned long long)s->behind,
            s->present_ns / n / 1e3, s->present_gpu_ns / n / 1e3, s->present_read_ns / n / 1e3, s->present_wait_ns / n / 1e3,
            s->present_conv_ns / n / 1e3, s->present_issue_ns / n / 1e3);
    if (s->behind)
        fprintf(stderr, "ACVIRGL: staging a present: switch %.0f us, framebuffer %.0f, glReadPixels %.0f, put back %.0f, fence %.0f\n",
                v->stg_ns[0] / (double)s->behind / 1e3, v->stg_ns[1] / (double)s->behind / 1e3, v->stg_ns[2] / (double)s->behind / 1e3,
                v->stg_ns[3] / (double)s->behind / 1e3, v->stg_ns[4] / (double)s->behind / 1e3);
}

static int32_t av_present_finish(struct acvirgl *v, uint8_t *(*map)(void *, uint32_t, uint32_t), void *mu, struct av_pres *p)
{
    int lr, lg, lb, la;
    const struct av_res *res;
    struct vr_box box = p->box;
    struct iovec iov;
    uint8_t *dst;
    uint64_t pixels, ts = av_ns(), t0 = ts, t1;
#ifdef AV_HAVE_DMABUF
    /* read back now (at once, or deferred): through a stage when there is one */
    if (!p->pbo && !p->stage && av_stage_start(v, p)) {
        t1 = av_ns();
        v->st.present_issue_ns += t1 - t0;
        t0 = t1;
    }
    if (p->stage) {
        /* a stage: the GPU's blit waited for, then read cached */
        struct av_stage *s = &v->stages[p->stage - 1];
        int ok = av_stage_wait(v, p), was_done = p->done;
        dst = map(mu, p->addr, p->span);
        t1 = av_ns();
        v->st.present_read_ns += t1 - t0;
        if (!was_done) v->st.present_wait_ns += t1 - t0;
        pixels = dst && ok ? av_present_write(p, dst, s->mem + p->stage_at, s->stride, p->lr, p->lg, p->lb, p->la, p->rev) : 0;
        av_dmabuf_sync(s->fd, AV_SYNC_READ | AV_SYNC_END);
        s->busy = 0;
        p->stage = 0;
        v->st.present_conv_ns += av_ns() - t1;
        if (!dst) return ACV_ERR_NOMAP;
        if (!ok) return ACV_ERR_RENDERER;
        v->st.staged++;
    } else
#endif
    if (p->pbo) {
        /* behind: the GPU's copy of the frame, mapped (a wait only if it isn't done) */
        const uint8_t *src = NULL;
        int pb = 0, ok;
        unsigned id = p->pbo;
        p->pbo = 0;
        /* any of virglrenderer's GL contexts maps it (they share their
         * objects): the one current stays so, rather than switching to the
         * frame's and back for the next command (a quarter of a millisecond
         * a present with three programs) */
        ok = (dst = map(mu, p->addr, p->span)) != NULL && (v->egl_current() || av_current(v, p->ctx));
        if (ok) {
            v->gl.GetIntegerv(AV_GL_PIXEL_PACK_BUFFER_BINDING, &pb);
            v->gl.BindBuffer(AV_GL_PIXEL_PACK_BUFFER, id);
            src = v->gl.MapBufferRange(AV_GL_PIXEL_PACK_BUFFER, 0, (intptr_t)p->pbo_bytes, AV_GL_MAP_READ_BIT);
        }
        t1 = av_ns();
        v->st.present_read_ns += t1 - t0;
        if (!p->done) v->st.present_wait_ns += t1 - t0;
        pixels = src ? av_present_write(p, dst, src, (size_t)p->box.w * 4, p->lr, p->lg, p->lb, p->la, p->rev) : 0;
        if (src) v->gl.UnmapBuffer(AV_GL_PIXEL_PACK_BUFFER);
        if (ok) v->gl.BindBuffer(AV_GL_PIXEL_PACK_BUFFER, (unsigned)pb);
        av_pool_put(v, id, p->pbo_bytes, ok);
        v->st.present_conv_ns += av_ns() - t1;
        if (!dst) return ACV_ERR_NOMAP;
        if (!src) return ACV_ERR_RENDERER;
    } else {
        res = av_res_find(v, p->handle);
        if (!res || !av_layout(res->format, &lr, &lg, &lb, &la) || !av_need(v, (size_t)box.w * box.h * 4)) return ACV_ERR_RENDERER;
        if (!(dst = map(mu, p->addr, p->span))) return ACV_ERR_NOMAP;
        if (v->parts && av_gl_load(v) && av_current(v, p->ctx)) {
            v->gl.Finish();                 /* the GPU's wait on its own, for the measurement */
            t1 = av_ns();
            v->st.present_gpu_ns += t1 - t0;
            t0 = t1;
        }
        iov.iov_base = v->tmp;
        iov.iov_len = (size_t)box.w * box.h * 4;
        if (v->vr.transfer_read_iov(p->handle, p->ctx, 0, box.w * 4, 0, &box, 0, &iov, 1)) return ACV_ERR_RENDERER;
        t1 = av_ns();
        v->st.present_read_ns += t1 - t0;
        pixels = av_present_write(p, dst, v->tmp, (size_t)box.w * 4, lr, lg, lb, la, 0);
        v->st.present_conv_ns += av_ns() - t1;
    }
    v->st.presents++; v->st.present_rects += (uint64_t)p->nr; v->st.present_pixels += pixels;
    v->st.present_ns += av_ns() - ts;
    av_parts_say(v);
    return pixels > 0x7FFFFFFF ? 0x7FFFFFFF : (int32_t)pixels;
}

/* Behind: the GPU asked to copy the present's box of the frame into a
 * staging buffer (glReadPixels into a pixel-pack buffer), in the frame's GL
 * context, after its drawing. virglrenderer's GL state is put back as it
 * was: the read framebuffer, the pack buffer and the pack settings. 0 when
 * it can't (no texture, an array layer, a framebuffer GL won't read). */
static int av_behind_start(struct acvirgl *v, struct av_pres *p)
{
    struct vr_resource_info info;
    int rfb = 0, pb = 0, al = 4, rl = 0, sr = 0, sp = 0, ok, i, la_none;
    int dl, dg, db, da;
    unsigned fb = 0, id;
    uint32_t need = p->box.w * p->box.h * 4, have = 0;
    const struct av_res *res = av_res_find(v, p->handle);
    uint64_t t0 = av_ns(), t1, t2, t3;
    memset(&info, 0, sizeof info);
    if (!res || !av_layout(res->format, &dl, &dg, &db, &da) || p->layer || v->vr.resource_get_info((int)p->handle, &info)
        || !info.tex_id || !av_current(v, p->ctx))
        return 0;
    t1 = av_ns();
    v->stg_ns[0] += t1 - t0;
    la_none = da == 4;
    if (!(id = av_pool_get(v, need, &have))) return 0;
    for (i = 0; i < 8 && v->gl.GetError(); i++) ;     /* errors that were virglrenderer's, not ours */
    v->gl.GetIntegerv(AV_GL_READ_FRAMEBUFFER_BINDING, &rfb);
    v->gl.GetIntegerv(AV_GL_PIXEL_PACK_BUFFER_BINDING, &pb);
    v->gl.GetIntegerv(AV_GL_PACK_ALIGNMENT, &al);
    v->gl.GetIntegerv(AV_GL_PACK_ROW_LENGTH, &rl);
    v->gl.GetIntegerv(AV_GL_PACK_SKIP_ROWS, &sr);
    v->gl.GetIntegerv(AV_GL_PACK_SKIP_PIXELS, &sp);
    v->gl.GenFramebuffers(1, &fb);
    v->gl.BindFramebuffer(AV_GL_READ_FRAMEBUFFER, fb);
    v->gl.FramebufferTexture2D(AV_GL_READ_FRAMEBUFFER, AV_GL_COLOR_ATTACHMENT0, AV_GL_TEXTURE_2D, info.tex_id, 0);
    ok = v->gl.CheckFramebufferStatus(AV_GL_READ_FRAMEBUFFER) == AV_GL_FRAMEBUFFER_COMPLETE;
    t2 = t3 = av_ns();
    v->stg_ns[1] += t2 - t1;
    if (ok) {
        int y1 = info.flags & VR_RESOURCE_Y_0_TOP ? (int)info.height - (int)p->box.y - (int)p->box.h : (int)p->box.y;
        v->gl.BindBuffer(AV_GL_PIXEL_PACK_BUFFER, id);
        if (have < need) { v->gl.BufferData(AV_GL_PIXEL_PACK_BUFFER, (intptr_t)need, NULL, AV_GL_STREAM_READ); have = need; }
        v->gl.PixelStorei(AV_GL_PACK_ALIGNMENT, 4);
        v->gl.PixelStorei(AV_GL_PACK_ROW_LENGTH, 0);
        v->gl.PixelStorei(AV_GL_PACK_SKIP_ROWS, 0);
        v->gl.PixelStorei(AV_GL_PACK_SKIP_PIXELS, 0);
        /* desktop GL reads B, G, R, A (a byte swap a pixel to the Amiga's
         * A, R, G, B); GLES reads R, G, B, A, the one order it must give */
        v->gl.ReadPixels((int)p->box.x, y1, (int)p->box.w, (int)p->box.h, strcmp(v->api, "GLES") ? AV_GL_BGRA : AV_GL_RGBA,
                         AV_GL_UNSIGNED_BYTE, NULL);
        ok = v->gl.GetError() == 0;
        t3 = av_ns();
        v->stg_ns[2] += t3 - t2;
        v->gl.PixelStorei(AV_GL_PACK_ALIGNMENT, al);
        v->gl.PixelStorei(AV_GL_PACK_ROW_LENGTH, rl);
        v->gl.PixelStorei(AV_GL_PACK_SKIP_ROWS, sr);
        v->gl.PixelStorei(AV_GL_PACK_SKIP_PIXELS, sp);
        v->gl.BindBuffer(AV_GL_PIXEL_PACK_BUFFER, (unsigned)pb);
    }
    v->gl.BindFramebuffer(AV_GL_READ_FRAMEBUFFER, (unsigned)rfb);
    v->gl.DeleteFramebuffers(1, &fb);
    v->stg_ns[3] += av_ns() - t3;
    if (!ok) { av_pool_put(v, id, have, 1); return 0; }
    p->pbo = id; p->pbo_bytes = have;
    if (strcmp(v->api, "GLES")) { p->lr = 2; p->lg = 1; p->lb = 0; }
    else { p->lr = 0; p->lg = 1; p->lb = 2; }
    p->la = la_none ? 4 : 3;
    p->rev = (info.flags & VR_RESOURCE_Y_0_TOP) != 0;
    return 1;
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
        } else if (p->stage)
            av_stage_drop(v, p);
        else if (p->pbo)
            av_pool_put(v, p->pbo, p->pbo_bytes, 0);     /* its copy forgotten; the buffer kept for the next */
    }
    v->npend = j;
    return n;
}

static int av_m_all(const struct av_pres *p, const void *arg) { (void)p; (void)arg; return 1; }
static int av_m_done(const struct av_pres *p, const void *arg) { (void)arg; return p->done; }
static int av_m_ctx(const struct av_pres *p, const void *arg) { return p->ctx == *(const uint32_t *)arg; }
/* those still to read back from the resource: deferred, not behind (whose copy is made) */
static int av_m_ctx_read(const struct av_pres *p, const void *arg) { return !p->pbo && !p->stage && p->ctx == *(const uint32_t *)arg; }
static int av_m_handle_read(const struct av_pres *p, const void *arg) { return !p->pbo && !p->stage && p->handle == *(const uint32_t *)arg; }
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
    if (!v) return;
    v->defer_ok = v->vr.context_create_fence && v->vr.poll ? ok : NULL;
    if (ok) v->behind_ok = NULL;
}

void acvirgl_behind_presents(struct acvirgl *v, int (*ok)(void *map_user, uint32_t addr, uint32_t bytes))
{
    if (!v) return;
    v->behind_ok = ok && v->vr.context_create_fence && v->vr.poll && v->vr.resource_get_info && av_gl_load(v) ? ok : NULL;
    if (ok) v->defer_ok = NULL;
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
    av_zs_discard(v, &p);                   /* before anything flushes the frame's job */
    if (v->behind_ok && v->npend < AV_PEND && v->behind_ok(mu, p.addr, p.span)) {
        uint64_t t0 = av_ns(), t1;
        if (av_stage_start(v, &p) || av_behind_start(v, &p)) {
            p.fence = ++v->fence_seq;
            t1 = av_ns();
            if (!v->vr.context_create_fence(p.ctx, 0, 0, p.fence)) {
                v->stg_ns[4] += av_ns() - t1;
                v->pend[v->npend++] = p;
                v->st.deferred++;
                v->st.behind++;
                t1 = av_ns();
                v->st.present_issue_ns += t1 - t0;
                v->st.present_ns += t1 - t0;
                return n;
            }
            v->st.present_issue_ns += av_ns() - t0;
            return av_present_finish(v, map, mu, &p);   /* no fence: mapped now, which waits */
        }
    }
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

/* [a, a + n) meets [lo, lo + len) */
static int av_meets(uint32_t a, uint64_t n, uint32_t lo, uint32_t len)
{
    return len && n && (uint64_t)a < (uint64_t)lo + len && (uint64_t)lo < (uint64_t)a + n;
}

int acvirgl_block_scan(uint8_t *(*map)(void *, uint32_t, uint32_t), void *mu, uint32_t addr, uint32_t bytes,
                       uint32_t avoid, uint32_t avoid_len, void (*present)(void *, uint32_t, uint32_t), void *user)
{
    const uint8_t *blk;
    uint32_t at = 0, words = bytes / 4;
    if (!words || !(blk = map(mu, addr, words * 4))) return 1;
    if (av_meets(addr, (uint64_t)words * 4, avoid, avoid_len)) return 1;    /* the block itself in video RAM */
    while (at + 2 <= words) {
        const uint8_t *c = blk + at * 4, *a = c + 8;
        uint32_t hdr = av_be32(c);
        unsigned cmd = ACV_HDR_CMD(hdr), len = ACV_HDR_WORDS(hdr);
        int n = av_args_of(cmd);
        if (len < 2 || at + len > words || n < 0 || len - 2 < (unsigned)n) return 0;   /* acvirgl_run stops there: nothing more */
        switch (cmd) {
        case ACV_HELLO: case ACV_CAPSET_INFO: case ACV_CTX_DESTROY: case ACV_RES_CREATE: case ACV_RES_UNREF:
        case ACV_ATTACH: case ACV_DETACH:
            break;                                      /* their words only */
        case ACV_CTX_CREATE:                            /* reads its name */
            if (av_meets(av_be32(a + 4), av_be32(a + 8), avoid, avoid_len)) return 1;
            break;
        case ACV_SUBMIT:                                /* reads its commands */
            if (av_meets(av_be32(a + 4), (uint64_t)av_be32(a + 8) * 4, avoid, avoid_len)) return 1;
            break;
        case ACV_PUT:                                   /* reads its pixels */
            if (av_meets(av_be32(a + 44), av_be32(a + 48), avoid, avoid_len)) return 1;
            break;
        case ACV_PRESENT: {                             /* writes its target, as av_present_prep places it */
            uint32_t to = av_be32(a + 16), bpr = av_be32(a + 20), fmt = av_be32(a + 24), wh = av_be32(a + 28);
            uint32_t tw = wh >> 16, th = wh & 0xFFFFu, bpp = fmt == OGPU_FMT_XRGB32 || fmt == OGPU_FMT_ARGB32 ? 4 : fmt == OGPU_FMT_RGB565 ? 2 : 0;
            uint64_t span;
            if (!bpp || !tw || !th) break;              /* refused, or nothing to write */
            span = (uint64_t)bpr * (uint64_t)(th - 1) + (uint64_t)tw * bpp;
            if (span > 0xFFFFFFFFu) break;              /* refused (ACV_ERR_NOMAP) */
            if (present) present(user, to, (uint32_t)span);
            break;
        }
        default:                                        /* ACV_GET, ACV_CAPS: the guest's memory */
            return 1;
        }
        at += len;
    }
    return 0;
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
            case ACV_CTX_DESTROY:
                key = av_be32(a); av_settle_if(v, map, mu, av_m_ctx, &key, 0); break;
            case ACV_ATTACH: case ACV_DETACH: case ACV_SUBMIT:
                key = av_be32(a); av_settle_if(v, map, mu, av_m_ctx_read, &key, 0); break;
            case ACV_PUT: case ACV_GET:
                key = av_be32(a + 4); av_settle_if(v, map, mu, av_m_ctx_read, &key, 0); break;
            case ACV_RES_UNREF:
                key = av_be32(a); av_settle_if(v, map, mu, av_m_handle_read, &key, 0); break;
            }
        }
        if (v->ctx0_every) v->vr.force_ctx_0();
        switch (cmd) {
        case ACV_HELLO:
            av_put_be32(a, ACV_HOST_PRESENT | (v->no_batch ? ACV_HOST_NO_BATCH : 0));
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
            /* vrend makes the new context's GL context current to set it up
             * (vrend_renderer_create_sub_ctx) but leaves its record of the
             * current context (current_ctx, current_hw_ctx) as it was. The
             * next command of the context that was current then skipped its
             * eglMakeCurrent and ran in the new program's GL context, where
             * its vertex arrays and framebuffers are other names or none:
             * "glBindVertexArray(non-gen name)", DRAW_VBO's GL error 1282
             * (or CLEAR's 1286), and that context was dead ("Illegal command
             * buffer"), one program in a few starting beside others. QEMU
             * forces context 0 before every command; here around the two
             * that change contexts, so vrend's record and the GL context
             * really current agree again: context 0 before the new context
             * is made (it shares with context 0), and after. Forced before
             * only, the record said context 0 while the new context's GL
             * context was current, so work vrend does "in context 0"
             * without a switch would run there (one lab run of that hung
             * in a GPU wait). */
            if (v->vr.force_ctx_0) v->vr.force_ctx_0();
            if (v->vr.context_create(id, (uint32_t)strlen(buf), buf)) r = ACV_ERR_RENDERER;
            if (v->vr.force_ctx_0) v->vr.force_ctx_0();
            break;
        }
        case ACV_CTX_DESTROY:
            av_zs_forget(v, av_be32(a));
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
            if (v->zs_on) av_zs_scan(v, ctx, v->tmp, ndw);
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
