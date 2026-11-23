/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * The OpenGPU shared pipe's interface (docs/design/OpenGPU-Shared-Pipe-Plan.md, sections 2.1 to 2.3).
 *
 * INTERNAL: for our own libraries only (minigl.library, Warp3D.library, later TinyGL and EGL), never for programs,
 * which have Warp3D, MiniGL, GL and GLES. It is not installed with the SDK.
 *
 * The libraries reach the pipe only through the table ogpu_pipe_ops() returns, never through its files' own
 * functions, so the pipe can become LIBS:GFOpenGPU/Pipe.module later (stage M10) without touching them.
 *
 * Version 2 (stage M3, 23 November 2026): the members M3 makes real (the context's block, resources, arenas; end_frame
 * and flush early, as the block's send) and, at the end, the bridge a front end that still writes its own stream needs
 * until its stages land (M4 to M7): its stream's room, renames, what a block changed, the arenas' state.
 *
 * Stage M2 (23 November 2026) fixes this table: version 1. From here on a change is a new member at the end and a
 * version bump, never a changed member. In M2 only the screen and the transport are real; the other members are 0
 * until their stage lands (M3 resources and arenas, M4 the context and state objects, M5 the fixed-function
 * generator, M7 the frame calls), and a front end asks for a member only once its stage has landed.
 *
 * Who owns what (DESIGN.md section 13): the pipe owns the screen (HELLO, the host's flags and caps, until stage MQ
 * moves them into gfopengpu.library's OGPU_Query), the transport (one OGPU_OP_VIRGL batch a block), the blocks,
 * resources, arenas, state objects and fixed-function shaders. The front ends own their API's state and calls. */
#ifndef GFOPENGPU_PIPE_H
#define GFOPENGPU_PIPE_H

#include <exec/types.h>

#define OGPU_PIPE_VERSION 2

typedef struct ogpu_pipe_screen OGPUPipeScreen;   /* one per open (M2: per API context, as before) */
typedef struct ogpu_pipe_ctx    OGPUPipeCtx;      /* one per API context: every call names it */
typedef struct ogpu_pipe_res    OGPUPipeRes;      /* a GPU-visible resource; its handle never changes */
typedef struct ogpu_pipe_cso    OGPUPipeCSO;      /* a state object: blend, DSA, rasterizer, sampler, layout */
typedef struct ogpu_pipe_shader OGPUPipeShader;   /* a fixed-function shader pair */

/* The keys are defined by their stages (M4: state objects, M5: OGPUFFKey, section 2.4). */
struct OGPUBlendKey;
struct OGPUDsaKey;
struct OGPURasterKey;
struct OGPUSamplerKey;
struct OGPULayout;
struct OGPUFFKey;
struct OGPUPipeStats;

enum { OGPU_EMIT_VIRGL = 1, OGPU_EMIT_S3D = 2 };

/* What a screen is opened with. M2: the open's first block carries the caller's first context with it (HELLO,
 * ACV_CTX_CREATE and ACV_CAPSET_INFO in one block, as the libraries sent it before), so nothing the host sees
 * changes. */
struct OGPUPipeOpen {
    ULONG size;                   /* sizeof (struct OGPUPipeOpen): the struct only grows at the end */
    ULONG emit_wanted;            /* OGPU_EMIT_VIRGL (the only one until M7) */
    ULONG ctx_id;                 /* the first context's ACVirgl id */
    const char *name;             /* the library's name, in memory the host reaches */
    ULONG name_len;
    ULONG *blk;                   /* 16 words for the open's blocks, in memory the host reaches */
    ULONG *caps;                  /* room for the caps, in memory the host reaches */
    ULONG caps_bytes;             /* its size: at most this much is asked for */
};

/* What the screen found. */
enum {
    OGPU_PIPE_OPEN_OK = 0,
    OGPU_PIPE_OPEN_NO_TRANSPORT,  /* no gfopengpu.library, or no driver of it carries virgl */
    OGPU_PIPE_OPEN_NO_RUN,        /* the first block didn't run */
    OGPU_PIPE_OPEN_PROTOCOL,      /* the host speaks another ACVirgl: detail is what its HELLO said */
    OGPU_PIPE_OPEN_NO_CONTEXT,    /* the host refused the context: detail is its result */
    OGPU_PIPE_OPEN_NO_CAPS        /* open, but the caps didn't come: caps_bits 0, prim_mask 0x7F */
};

struct OGPUPipeInfo {
    ULONG emit;                   /* OGPU_EMIT_ */
    ULONG host_flags;             /* ACV_HOST_ */
    ULONG caps_bits, prim_mask;   /* the caps' words 98 and 72 (prim_mask 0x7F when the caps are shorter) */
    ULONG max_tex_size, max_tex_units;   /* host caps (from MQ); 0 until then */
    ULONG status;                 /* OGPU_PIPE_OPEN_ */
    LONG  detail;
    ULONG blocks;                 /* the blocks the open ran, for the library's own counts */
};

struct OGPUBox { LONG x, y, z; ULONG w, h, d; };

struct OGPUPipeResDesc {
    ULONG target, format, bind;   /* virgl's numbers (virgl_cmd.h) */
    ULONG width, height, levels;
    ULONG flags;                  /* OGPU_RES_STREAM: rewritten every frame (rings, not renames) */
};
#define OGPU_RES_STREAM 1

/* What a block changed, for a front end that writes its own bindings (changed(), M3's bridge) */
#define OGPU_PIPE_CH_VBUF  1UL            /* the vertex buffer or this block's place in it: VERTEX_BUFFER again */
#define OGPU_PIPE_CH_IBUF  2UL            /* the index buffer or its base: INDEX_BUFFER again */
#define OGPU_PIPE_CH_SENT  4UL            /* a block went: anything the front end keeps per stream starts again */

/* res_pending's bits */
#define OGPU_PIPE_RES_DRAWN 1UL           /* a stream not yet sent reads it: a write renames it */
#define OGPU_PIPE_RES_PUT   2UL           /* a PUT of it waits in the block: it reads the source memory when it runs */

/* The arenas, as they are now (arenas(), M3's bridge): a front end that writes vertices itself adds to *nva and
 * *nia, after need() made the room. */
struct OGPUPipeArenas {
    UBYTE *va; ULONG *nva; ULONG cva;     /* the vertex arena, its use (bytes), its size */
    UWORD *ia; ULONG *nia; ULONG cia;     /* the index arena (16-bit), its use, its size */
    ULONG vbuf, vb_off;                   /* the host's vertex buffer (handle) and this block's offset in it */
    ULONG ibuf, ib_base;                  /* the index buffer and this block's base in it (bytes) */
};

struct OGPUDrawInfo {
    ULONG prim, first, count;
    ULONG indexed;                /* 0, or 16 for 16-bit indices from the index arena */
    LONG  bias;
};

/* How the frame's present rides in the frame's own block (section 2.7): OpenRTG's, never the pipe's. */
struct OGPUPresentHook {
    ULONG (*prepare)(void *user, ULONG ctx, ULONG handle, ULONG *p, ULONG room);   /* words written, 0: none */
    void  (*finish)(void *user, const ULONG *blk, ULONG at);                       /* after the block ran */
    void  *user;
};

struct OGPUPipeOps {
    ULONG version;                /* first, always */

    /* screen: HELLO, caps, emitter (M2). 0 with info->status saying why; with OGPU_PIPE_OPEN_NO_CAPS it is open. */
    OGPUPipeScreen *(*screen_open)(const struct OGPUPipeOpen *open, struct OGPUPipeInfo *info);
    void  (*screen_close)(OGPUPipeScreen *);

    /* the transport (M2): one request block, done when it returns; 0 on success. Front ends send their own blocks
     * through it until their blocks move into the pipe (M3 to M7); from then on only the pipe calls it. */
    LONG  (*run)(OGPUPipeScreen *, ULONG *blk, ULONG bytes);

    /* context: explicit on every call (no "current" context: libraries are shared by tasks) (M4) */
    OGPUPipeCtx *(*ctx_create)(OGPUPipeScreen *, ULONG flags);
    void  (*ctx_destroy)(OGPUPipeCtx *);

    /* resources: the only GPU-visible allocation there is (M3) */
    OGPUPipeRes *(*res_create)(OGPUPipeCtx *, const struct OGPUPipeResDesc *);
    void  (*res_update)(OGPUPipeCtx *, OGPUPipeRes *, ULONG level, const struct OGPUBox *, const void *src, ULONG stride, ULONG swap);
          /* renames inside when an earlier block still reads it: the handle stays the same */
    void  (*res_dirty)(OGPUPipeCtx *, OGPUPipeRes *, ULONG level, const struct OGPUBox *);  /* texels changed in Amiga memory */
    LONG  (*res_read)(OGPUPipeCtx *, OGPUPipeRes *, ULONG level, const struct OGPUBox *, void *dst, ULONG stride);
    void  (*res_release)(OGPUPipeCtx *, OGPUPipeRes *);   /* refcounted; freed after the last block that used it */

    /* state objects: made or found by key, refcounted; front ends never free what is under them (M4) */
    OGPUPipeCSO *(*blend_create)(OGPUPipeCtx *, const struct OGPUBlendKey *);
    OGPUPipeCSO *(*dsa_create)(OGPUPipeCtx *, const struct OGPUDsaKey *);       /* depth, stencil, alpha test */
    OGPUPipeCSO *(*raster_create)(OGPUPipeCtx *, const struct OGPURasterKey *);
    OGPUPipeCSO *(*sampler_create)(OGPUPipeCtx *, const struct OGPUSamplerKey *);
    OGPUPipeCSO *(*layout_create)(OGPUPipeCtx *, const struct OGPULayout *);
    void  (*cso_release)(OGPUPipeCtx *, OGPUPipeCSO *);

    /* the one fixed-function generator (section 2.4): made, prebuilt or cached by key (M5) */
    OGPUPipeShader *(*ff_shader)(OGPUPipeCtx *, const struct OGPUFFKey *);

    /* binding: the pipe context owns what is bound (M4) */
    void  (*bind_blend)(OGPUPipeCtx *, OGPUPipeCSO *);
    void  (*bind_dsa)(OGPUPipeCtx *, OGPUPipeCSO *);
    void  (*bind_raster)(OGPUPipeCtx *, OGPUPipeCSO *);
    void  (*bind_layout)(OGPUPipeCtx *, OGPUPipeCSO *);
    void  (*bind_shader)(OGPUPipeCtx *, OGPUPipeShader *);
    void  (*bind_framebuffer)(OGPUPipeCtx *, OGPUPipeRes *colour, OGPUPipeRes *depth);
    void  (*bind_texture)(OGPUPipeCtx *, ULONG unit, OGPUPipeRes *, OGPUPipeCSO *sampler);
    void  (*set_constants)(OGPUPipeCtx *, ULONG stage, const float *v, ULONG vec4s);
    void  (*set_viewport)(OGPUPipeCtx *, const float v6[6]);
    void  (*set_scissor)(OGPUPipeCtx *, const struct OGPUBox *);   /* 0: off */
    void  (*set_blend_colour)(OGPUPipeCtx *, const float rgba[4]);
    void  (*set_stencil_ref)(OGPUPipeCtx *, ULONG front, ULONG back);

    /* geometry: the arenas (M3) */
    void *(*vtx_room)(OGPUPipeCtx *, ULONG bytes, ULONG *first);
    UWORD *(*idx_room)(OGPUPipeCtx *, ULONG count, ULONG *first);
    ULONG (*vtx_put_range)(OGPUPipeCtx *, const void *mem, ULONG bytes);  /* Warp3D: a batch's vertex memory in one PUT */

    /* frame (M7) */
    void  (*begin_frame)(OGPUPipeCtx *);
    void  (*draw)(OGPUPipeCtx *, const struct OGPUDrawInfo *);
    void  (*clear)(OGPUPipeCtx *, ULONG buffers, const float rgba[4], double depth, ULONG stencil);
    LONG  (*end_frame)(OGPUPipeCtx *, const struct OGPUPresentHook *hook, ULONG *fence);
          /* sends the frame; the hook (OpenRTG's) puts the present in the same block; fence for OGPU_Wait */
    LONG  (*flush)(OGPUPipeCtx *, ULONG *fence);   /* send what there is, no present (glFlush, a full batch) */
    void  (*stats)(OGPUPipeCtx *, struct OGPUPipeStats *);

    /* ---- version 2 (stage M3): resources, and the bridge for front ends that still write their own stream ---- */

    LONG  (*res_write)(OGPUPipeCtx *, OGPUPipeRes *);     /* before writing it: 1 when renamed (send it all again) */
    void  (*res_ref)(OGPUPipeRes *);                      /* one more holder: res_release once more */
    ULONG (*res_handle)(const OGPUPipeRes *);             /* its current version's handle (changes on a rename) */
    void  (*res_used)(OGPUPipeCtx *, OGPUPipeRes *);      /* the stream being built reads it */
    ULONG (*res_pending)(OGPUPipeCtx *, const OGPUPipeRes *);   /* OGPU_PIPE_RES_ */
    ULONG *(*cmd_room)(OGPUPipeCtx *, ULONG words);       /* room in the stream (little-endian words); 0: too big */
    ULONG (*sent)(OGPUPipeCtx *);                         /* blocks sent so far: the stream being built's number */
    ULONG (*errors)(OGPUPipeCtx *);                       /* commands the host refused in the last block */
    ULONG (*changed)(OGPUPipeCtx *);                      /* OGPU_PIPE_CH_ since the last call */
    void  (*counts)(OGPUPipeCtx *, ULONG *v5);            /* blocks, words, puts, renames, vertex bytes; reset */
    LONG  (*need)(OGPUPipeCtx *, ULONG va_bytes, ULONG ia_count, ULONG cmd_words);  /* 1: sent first, 0: room, -1 */
    void  (*arenas)(OGPUPipeCtx *, struct OGPUPipeArenas *);

    /* new calls only ever added here, at the end */
};

/* The table, when the pipe is at least `version`; 0 when it is older. */
const struct OGPUPipeOps *ogpu_pipe_ops(ULONG version);

/* ---- the transport's two builds (library/pipe/pipe_xport_os3.c, pipe_xport_host.c) -------------- */
/* The pipe's own: front ends call the table's run, never these. */
int  ogpu_pipe_xport_ready(void);                        /* a transport that carries virgl is there */
LONG ogpu_pipe_xport_run(ULONG *blk, ULONG bytes);       /* one block, done when it returns; 0 on success */
void *ogpu_pipe_xport_alloc(ULONG bytes);                /* cleared, memory the host reaches (AllocVec MEMF_ANY) */
void ogpu_pipe_xport_free(void *p);                      /* 0 is fine */

#endif
