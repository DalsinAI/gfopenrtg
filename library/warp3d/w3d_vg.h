/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * Warp3D.library's virgl route: the batches Warp3D already builds (OGPU
 * stream v1.2, include/gfopengpu/stream3d.h) drawn on the host's graphics
 * chip through GFOpenGPU's OGPU_OP_VIRGL (include/gfopengpu/virgl.h), as
 * minigl.library's virgl route draws GL.
 *
 * Warp3D keeps all its state, its vertex conversion, culling, points and
 * lines as quads, and its textures as they are; only the step that ran a
 * batch changes. Where the CPU route hands a batch to the 3D core
 * (ogpu3d_run), this route reads the same batch and writes:
 *   - its state commands (RENDER3D, TEXENV, TEXTURE, DEPTH, CLIP) as virgl
 *     objects made once and cached by key (blend, depth-stencil-alpha,
 *     rasterizer, samplers, shaders of our own as TGSI text);
 *   - its TRIANGLES (lists, strips, fans) as one indexed triangle list for
 *     each run of draws with the same state, over vertices the host reads
 *     straight from the batch's vertex memory (one PUT a batch), the stream's
 *     own fixed-point layout read by the vertex fetch, so the 68k converts
 *     nothing;
 *   - its FILL and CLEAR3D as virgl clears (or quads, inside a clip).
 * The colour buffer lives on the host. It is loaded from the drawing area
 * at a frame's first draw (unless the frame starts by filling all of it),
 * and written back into the drawing area by ACV_PRESENT when the program
 * syncs (W3D_Flush, W3D_UnLockHardware, W3D_WaitIdle ...).
 *
 * The file needs nothing of AmigaOS: the platform gives it a way to run a
 * request block (w3dvg_os). w3d_vg_os3.c is the Amiga's; the PC test
 * (tests/test_w3d_vg.c) gives its own, through virglrenderer. Words of a
 * request block are the Amiga's order (big-endian); the virgl command
 * stream is little-endian words; data in memory stays as the 68k has it.
 * W3D_HOST builds it for that test: 32-bit, the same memory layout. */
#ifndef W3D_VG_H
#define W3D_VG_H

#include <exec/types.h>

struct w3dvg;

struct w3dvg_os {
    int (*run)(void *user, ULONG *blk, ULONG bytes);    /* one block, done when it returns; 0 on success */
    void *(*alloc)(void *user, ULONG bytes);            /* cleared, memory the host reaches */
    void (*free)(void *user, void *p);
    void (*log)(void *user, const char *msg);           /* or 0 */
    void *user;
};

/* What the host said at the start. */
struct w3dvg_info {
    ULONG host_flags;                   /* virgl.h's ACV_HOST_ */
    ULONG caps_bits;
};

/* HELLO, the context, the caps: 0 when the host has no virgl for us. */
struct w3dvg *w3dvg_open(const struct w3dvg_os *os, struct w3dvg_info *info);
void w3dvg_close(struct w3dvg *v);

/* One batch (`words` stream words, as built: big-endian), its SURFACE and
 * TARGET first. Vertices the batch points into [vbase, vbase + vlen) go to
 * the host in one PUT; others are copied. present: the frame is written
 * into the target surface afterwards (in the same block). Returns 0, or
 * the commands the host refused, or -1 when the block didn't run. */
LONG w3dvg_run(struct w3dvg *v, const UBYTE *stream, long words, ULONG vbase, ULONG vlen, int present);

/* A texture whose first level was at addr is gone (or will change size). */
void w3dvg_forget(struct w3dvg *v, ULONG addr);

/* The stream's state back to the core's start (a new context on the core). */
void w3dvg_reset(struct w3dvg *v);

/* Every texture is gone (W3D_FreeAllTexObj, or the context is reset). */
void w3dvg_forget_all(struct w3dvg *v);

/* The drawing area's pixels changed behind the host's copy: the next draw
 * loads them first. */
void w3dvg_stale(struct w3dvg *v);

/* Counters since the last call (cleared by it). */
struct w3dvg_stats {
    ULONG blocks, draws, merged, triangles, objects, shaders, puts, put_bytes, loads, presents, refused;
    ULONG declined;                     /* commands the route leaves to the CPU core */
};
void w3dvg_stats(struct w3dvg *v, struct w3dvg_stats *s);

/* A virgl shader's text for a key (w3d_vg_shader.c): its length with the
 * closing 0. The keys are w3d_vg.c's VK_ and FK_. */
ULONG w3dvg_vs_text(ULONG key, char *buf, ULONG cap);
ULONG w3dvg_fs_text(ULONG key, char *buf, ULONG cap);

/* Vertex shader keys */
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
