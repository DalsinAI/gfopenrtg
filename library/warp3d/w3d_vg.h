/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * Warp3D.library's virgl route: what Warp3D draws (the numbers of OGPU
 * stream v1.2, include/gfopengpu/stream3d.h) drawn on the host's graphics
 * chip through GFOpenGPU's OGPU_OP_VIRGL (include/gfopengpu/virgl.h), on
 * the shared pipe (library/pipe), as minigl.library's virgl route draws GL.
 *
 * Warp3D keeps all its state, its vertex conversion, culling, points and
 * lines as quads, and its textures as they are. Where the CPU route records
 * a batch for the 3D core (ogpu3d_run), on this route the draw calls call
 * this file (w3dvg_clip, w3dvg_triangles ...), which makes:
 *   - the state (RENDER3D, TEXENV, TEXTURE, DEPTH, CLIP) as the pipe's
 *     objects, made once and kept by key (blend, depth-stencil-alpha,
 *     rasterizer, samplers, the pipe's fixed-function shaders);
 *   - its TRIANGLES (lists, strips, fans) as one indexed triangle list for
 *     each run of draws with the same state, over vertices the host reads
 *     straight from the batch's vertex memory (a PUT a run), the stream's
 *     own fixed-point layout read by the vertex fetch, so the 68k converts
 *     nothing;
 *   - its FILL and CLEAR3D as virgl clears (or quads, inside a clip).
 * The colour buffer lives on the host. It is loaded from the drawing area
 * at a frame's first draw (unless the frame starts by filling all of it),
 * and written back into the drawing area by ACV_PRESENT when the program
 * syncs (W3D_Flush, W3D_UnLockHardware, W3D_WaitIdle ...).
 *
 * The file needs nothing of AmigaOS: the platform gives it memory and the
 * drawing area's lock (w3dvg_os). w3d_vg_os3.c is the Amiga's; the PC test
 * (tests/test_w3d_vg.c) gives its own, through virglrenderer. Words of a
 * request block are the Amiga's order (big-endian); the virgl command
 * stream is little-endian words; data in memory stays as the 68k has it.
 * W3D_HOST builds it for that test: 32-bit, the same memory layout. */
#ifndef W3D_VG_H
#define W3D_VG_H

#include <exec/types.h>

struct w3dvg;

/* The platform's part. The transport is the shared pipe's (library/pipe/, stage M2 of
 * docs/design/OpenGPU-Shared-Pipe-Plan.md): pipe_xport_os3.c on the Amiga, pipe_xport_host.c in the PC test. */
struct w3dvg_os {
    void *(*alloc)(void *user, ULONG bytes);            /* cleared, memory the host reaches */
    void (*free)(void *user, void *p);
    void (*log)(void *user, const char *msg);           /* or 0 */
    void *user;
    /* The drawing area's memory (both calls are needed). area_lock is called just before a send that reads the area
     * (the frame's first pixels) or writes it (the present), area_unlock once that send has run, before the call that caused it returns: the lock is never held
     * between two calls. area_lock answers 0 when the area isn't to be had (not visible): nothing is loaded, or the
     * frame stays on the host. */
    int (*area_lock)(void *user, ULONG *address, ULONG *bpr);
    void (*area_unlock)(void *user);
};

/* What the host said at the start. */
struct w3dvg_info {
    ULONG host_flags;                   /* virgl.h's ACV_HOST_ */
    ULONG caps_bits;
};

/* HELLO, the context, the caps (the shared pipe's screen does them): 0 when the host has no virgl for us. */
struct w3dvg *w3dvg_open(const struct w3dvg_os *os, struct w3dvg_info *info);
void w3dvg_close(struct w3dvg *v);

/* A batch, as calls made as Warp3D's draw calls come: nothing is recorded. w3dvg_begin starts one: vertices the draws name inside [vbase, vbase + vcap) are read by the host from there (each
 * run of draws PUTs the part it reads; they must stay as they are until w3dvg_end), others are copied. The numbers
 * are the stream's (stream3d.h), as the builders (build3d.h) take them. w3dvg_end sends what there is; with
 * present the frame is written into the drawing area afterwards (in the same block). It answers the commands the host refused or the route left out, or -1 when a block didn't run at all
 * (the host is gone: what the batch drew is lost, and the caller stops using the route); *presented (or 0) is 1 when the frame
 * is in the drawing area afterwards, 0 when it is still the host's (no present asked for, or the area not to be
 * had). */
void w3dvg_begin(struct w3dvg *v, ULONG vbase, ULONG vcap);
/* The drawing area. The address only says which area it is (its memory comes from w3dvg_os.area_lock), and bpr
 * isn't read. */
void w3dvg_area(struct w3dvg *v, ULONG address, ULONG bpr, LONG w, LONG h, ULONG format);
void w3dvg_depth(struct w3dvg *v, ULONG kind, ULONG address, ULONG bpr, LONG w, LONG h, ULONG format);
void w3dvg_clip(struct w3dvg *v, LONG x, LONG y, LONG w, LONG h);
void w3dvg_render3d(struct w3dvg *v, const ULONG *args);        /* OGPU_R3D_ARGS of them */
void w3dvg_texenv(struct w3dvg *v, ULONG mode, ULONG colour);
void w3dvg_texture(struct w3dvg *v, ULONG address, ULONG bpr, LONG w, LONG h, ULONG format, ULONG generation,
                   ULONG min, ULONG mag, ULONG wrap_s, ULONG wrap_t, ULONG border, ULONG levels, const ULONG *level_addrs);
void w3dvg_triangles(struct w3dvg *v, ULONG vaddr, ULONG vcount, ULONG stride, ULONG tex, ULONG layout);
void w3dvg_fill(struct w3dvg *v, LONG x, LONG y, LONG w, LONG h, ULONG raw);
void w3dvg_clear3d(struct w3dvg *v, ULONG kind, LONG x, LONG y, LONG w, LONG h, ULONG value);
LONG w3dvg_end(struct w3dvg *v, int present, int *presented);

/* A texture whose first level was at addr is gone (or will change size). */
void w3dvg_forget(struct w3dvg *v, ULONG addr);

/* The stream's state back to the core's start (a new context on the core). */
void w3dvg_reset(struct w3dvg *v);

/* Every texture is gone (W3D_FreeAllTexObj, or the context is reset). */
void w3dvg_forget_all(struct w3dvg *v);

/* The drawing area's pixels changed behind the host's copy: the next draw
 * loads them first. */
void w3dvg_stale(struct w3dvg *v);

/* Memory's Z or stencil changed behind the host (W3D_WriteZSpan ...): the
 * next depth op loads them first. */
void w3dvg_stale_z(struct w3dvg *v);

/* The host's depth and stencil written into memory (the buffers the last
 * batch named), for a program that reads them. */
void w3dvg_read_z(struct w3dvg *v);

/* The Z and stencil buffers are gone or moved (W3D_FreeZBuffer ...): the
 * route forgets them until a batch names them again. */
void w3dvg_forget_z(struct w3dvg *v);

/* Counters since the last call (cleared by it). */
struct w3dvg_stats {
    ULONG blocks, draws, merged, triangles, objects, shaders, puts, put_bytes, loads, presents, refused;
    ULONG declined;                     /* commands the route leaves to the CPU core */
};
void w3dvg_stats(struct w3dvg *v, struct w3dvg_stats *s);

/* Warp3D's shader keys, as w3d_vg.c makes them from the state; w3d_vg_ffkey.h turns them into the pipe's.
 * Vertex shader keys */
#define VK_Z        (1UL << 0)          /* the vertex has z (u32) */
#define VK_W        (1UL << 1)          /* ... w (float) */
#define VK_SPEC     (1UL << 2)          /* ... specular and fog (ARGB32) */
#define VK_PERSP    (1UL << 3)          /* clip w = 1 / w: perspective-correct texture coordinates */
#define VK_LINEAR   (1UL << 4)          /* colours, specular and fog across the screen (the core's way);
                                           off on GLES hosts, which have no noperspective */

/* Fragment shader keys */
#define FK_ENV(k)       ((k) & 7)       /* stream3d.h's OGPU_ENV_, 0 untextured */
#define FK_ENV_SET(e)   ((ULONG)(e))
#define FK_BASE(k)      (((k) >> 3) & 7)/* OGPU_TEXBASE_ */
#define FK_BASE_SET(b)  ((ULONG)(b) << 3)
#define FK_GOURAUD      (1UL << 6)      /* the vertex's colour (else the current one) */
#define FK_SPEC         (1UL << 7)      /* specular added */
#define FK_FOG(k)       (((k) >> 8) & 7)/* OGPU_FOG_, 0 none */
#define FK_FOG_SET(f)   ((ULONG)(f) << 8)
#define FK_CHROMA(k)    (((k) >> 11) & 3)   /* 2 keep the range, 3 drop it */
#define FK_CHROMA_SET(c) ((ULONG)(c) << 11)
#define FK_LINEAR       (1UL << 13)     /* as VK_LINEAR */
#define FK_FOGEQ        (1UL << 15)     /* linear fog with start = end: a step */

/* Shader constants (vec4s). Vertex: */
#define VC_POS      0                   /* x scale, y scale, x offset, y offset (16.16 to clip space) */
#define VC_MISC     1                   /* 1/65536, 2^-31 (z to -1..1), -1, 1 */
#define VC_ZERO     2                   /* 0, 1, 0, 0 */
#define VC_N        3
/* Fragment: */
#define FC_CUR      0                   /* the current colour */
#define FC_ENV      1                   /* the environment colour */
#define FC_FOGCOL   2
#define FC_FOG      3                   /* start, end, density, 1 / (start - end) */
#define FC_CLO      4                   /* chroma lower bound, - half a step */
#define FC_CHI      5                   /* chroma upper bound, + half a step */
#define FC_CONST    6                   /* 0, 1, 0.5, log2(e) */
#define FC_N        7

#endif
