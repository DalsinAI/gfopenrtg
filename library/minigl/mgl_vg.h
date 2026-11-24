/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * minigl.library's virgl route (IDEA 5, docs/design/MiniGL-Compatible-Plan.md):
 * GL 1.1 drawn on the host's GPU through GFOpenGPU's OGPU_OP_VIRGL, on the
 * OpenGPU shared pipe (library/pipe/): its block, its resources, its arenas,
 * its state objects, its fixed-function shaders and every command of the
 * frame (pipe version 7). The state code, matrices, lists, GLU and GLUT are the
 * library's own; this route replaces the per-vertex work and the output.
 *
 *   mgl_vg_frame.c   frames, clears, reading back
 *   mgl_vg_pipe.c    the context on the shared pipe: resources, sending
 *   mgl_vg_state.c   GL state to the pipe's keys, constants
 *   mgl_vg_draw.c    glBegin/glEnd and arrays into the frame's arena, draws
 *   mgl_vg_tex.c     textures as host resources, renaming
 *   mgl_vg_os3.c     the transport and the present on AmigaOS (a PC test
 *                    gives its own)
 *
 * Numbers of the virgl protocol are virglrenderer's (Mesa's MIT
 * virgl_protocol.h and virgl_hw.h, and Gallium's p_defines.h, as virgl
 * carries them); the host test checks them against those headers. */
#ifndef MGL_VG_H
#define MGL_VG_H

#include "mgl_internal.h"
#include "../../include/gfopengpu/virgl.h"

/* The pipe's numbers and byte order, shared with Warp3D.library; the pipe
 * writes every command. */
#include "../../include/gfopengpu/pipe_vg.h"
#include "../../include/gfopengpu/pipe.h"

/* ---- resources -------------------------------------------------------------------------------- */

/* A host resource. Its handle is this struct's address: virglrenderer has one
 * namespace of resources for all its clients, and an address is unique
 * while the resource lives (as GL.module does). */
struct vg_res {
    ULONG handle;
    OGPUPipeRes *pr;                    /* the pipe's resource */
    ULONG fmt, w, h, levels;
};

/* A texture's host side (mgl_vg_tex.c), hung on struct mgl_tex. */
struct vg_tex {
    struct vg_res *res;                 /* the current version */
    ULONG gen;                          /* the texels' generation it holds */
    ULONG used;                         /* the stream sequence that last drew with it */
    ULONG put;                          /* ... that has a PUT of it waiting */
    ULONG boxgen;                       /* the generation the dirty boxes describe */
    int w, h, levels;
    int dx0[MGL_MAX_LEVELS], dy0[MGL_MAX_LEVELS], dx1[MGL_MAX_LEVELS], dy1[MGL_MAX_LEVELS];   /* dirty, per level */
    int whole;                          /* everything must go up */
};

/* ---- vertex layouts (mgl_vg_state.c) ------------------------------------------------------ */

/* Vertex layouts: one attribute set (VG_A_), with each one's format. */
#define VG_A_POS     0                  /* always */
#define VG_A_COLOR   1
#define VG_A_NORMAL  2
#define VG_A_TC0     3
#define VG_A_TC1     4
#define VG_ATTRS     5

struct vg_layout {
    ULONG key;                          /* the attributes' formats, packed (4 bits each) */
    UBYTE off[VG_ATTRS];                /* byte offsets */
    UBYTE stride;
    OGPUPipeCSO *cso;                   /* the pipe's VERTEX_ELEMENTS for it */
};
#define VG_LAYOUTS 16

/* Formats an attribute has in the arena (4 bits): */
#define VG_AF_NONE  0
#define VG_AF_F1    1
#define VG_AF_F2    2
#define VG_AF_F3    3
#define VG_AF_F4    4
#define VG_AF_UB4   5                   /* four unsigned bytes in a word, as the 68k's memory has them */

/* ---- the per-context state ---------------------------------------------------------------- */

#define VG_VS_CONSTS 96                 /* vec4s of vertex shader constants */
#define VG_FS_CONSTS 8

/* dirty bits */
#define VG_D_FB        (1UL << 0)       /* framebuffer, viewport, scissor: the frame's start */
#define VG_D_VIEWPORT  (1UL << 1)
#define VG_D_SCISSOR   (1UL << 2)
#define VG_D_VSC       (1UL << 5)
#define VG_D_FSC       (1UL << 6)
#define VG_D_ALL       0xFFFFFFFFUL

struct mgl_vg {
    const struct OGPUPipeOps *pipe;     /* the shared pipe: its screen and its context (the block, the resources,
                                           the arenas and the objects are its own) */
    OGPUPipeScreen *screen;
    OGPUPipeCtx *px;
    ULONG ctx_id;
    ULONG host_flags;                   /* HELLO's ACV_HOST_ */
    ULONG prim_mask;                    /* virgl_caps_v1.prim_mask */
    ULONG caps_bits;                    /* virgl_caps_v2.capability_bits */
    int gles;

    ULONG seq;                          /* the pipe's sent(): counts streams sent */
    ULONG pre_errors;                   /* commands the host refused in the last block */
    /* the arenas (vertices as 32-bit words, 16-bit indices), where they go in the host's buffers and the buffers'
     * handles: the pipe's, mirrored here after anything that may have sent (vg_sync) */
    UBYTE *va; ULONG cva;
    UWORD *ia; ULONG cia;
    ULONG *pnva, *pnia;                 /* their use: the pipe's counters */
    ULONG vb_off, ib_base;
    ULONG vbuf_h, ibuf_h;
    struct vg_res *colour, *depth;

    /* what is bound */
    ULONG dirty;
    ULONG stride;                       /* the vertex buffer's for the next draw */
    struct vg_layout lay[VG_LAYOUTS];
    int nlay;

    /* glBegin/glEnd */
    struct vg_layout *im_lay;
    ULONG im_first;                     /* the primitive's first vertex in the arena */
    ULONG im_n;
    GLenum im_mode;                     /* the primitive (a split loop goes on as a strip) */
    int im_close;                       /* a split loop: im_v0 closes it */
    UBYTE im_v0[64];
    float im_col[4];                    /* the colour im_colw was made from */
    ULONG im_colw;
    int im_colok;

    /* glLockArrays: the locked range, once in the arena */
    ULONG lk_base;                      /* its first vertex there */
    ULONG lk_seq;                       /* the stream it is in; it must be copied again after a send */
    struct vg_layout *lk_lay;
    int lk_valid;

    /* statistics (ENV:MiniGL/Trace) */
    ULONG draws, objects, frames, blocks, words, va_bytes, puts, renames;
};

/* The arenas' use */
#define VG_NVA(v) (*(v)->pnva)
#define VG_NIA(v) (*(v)->pnia)
/* The host's vertex and index buffers' handles */
#define VG_VBUF_H(v) ((v)->vbuf_h)
#define VG_IBUF_H(v) ((v)->ibuf_h)

/* ---- the frame's commands: the pipe writes them all (mgl_vg_pipe.c) ---------------------------- */

ULONG vg_fbits(float f);
void vg_clear(GLcontext c, ULONG buffers, const float *rgba, float depth);
/* A draw from the vertex arena at the stride vg_prepare left; indexed: start is in the index arena, bias is added */
void vg_draw(GLcontext c, ULONG prim, ULONG start, ULONG count, int indexed, LONG bias);

/* Block commands (the Amiga's order). */
void vg_blk_cmd(ULONG *p, ULONG cmd, ULONG n, const ULONG *args);
LONG vg_result(const ULONG *blk, ULONG at);             /* the result word of the command at blk[at] */
ULONG vg_word(const ULONG *blk, ULONG at);

/* ---- the frame (mgl_vg_frame.c) --------------------------------------------------------------- */

int mgl_vg_open(GLcontext c);           /* 1: the context draws through virgl */
void mgl_vg_close(GLcontext c);
struct vg_res *vg_res_create(GLcontext c, ULONG target, ULONG fmt, ULONG bind, ULONG w, ULONG h, ULONG levels);
void vg_res_release(GLcontext c, struct vg_res *r);  /* after the next block */
void vg_put(GLcontext c, struct vg_res *r, ULONG level, ULONG x, ULONG y, ULONG w, ULONG h,
            const void *src, ULONG stride, ULONG bytes, ULONG swap);
void vg_send(GLcontext c, int present);  /* the frame so far to the host (present: and shown) */
void vg_need(GLcontext c, ULONG va_bytes, ULONG ia_count);  /* room in the arenas, sent first if not */
void mgl_vg_readback(GLcontext c, int x, int y, int w, int h, ULONG *argb, int stride);
void mgl_vg_clear(GLcontext c, GLbitfield mask);
void mgl_vg_switch(GLcontext c);        /* mglSwitchDisplay */
void mgl_vg_finish(GLcontext c);        /* glFlush, glFinish */
void vg_send_overflow(GLcontext c, ULONG stride);  /* glBegin's arena is full: sent, larger at the frame's end */

/* The same calls, made by the shared pipe (mgl_vg_pipe.c). */
int vgp_open(GLcontext c);              /* 0: no virgl (as mgl_vg_open) */
void vgp_close(GLcontext c);
struct vg_res *vgp_res_create(GLcontext c, ULONG target, ULONG fmt, ULONG bind, ULONG w, ULONG h, ULONG levels);
void vgp_res_release(GLcontext c, struct vg_res *r);
void vgp_put(GLcontext c, struct vg_res *r, ULONG level, ULONG x, ULONG y, ULONG w, ULONG h, const void *src,
             ULONG stride, ULONG swap);
void vgp_send(GLcontext c, int present);
void vgp_need(GLcontext c, ULONG va_bytes, ULONG ia_count);
void vgp_overflow(GLcontext c, ULONG stride);
int vgp_readback(GLcontext c, int x0, int y0, int x1, int y1, ULONG *buf);    /* 0 on success */
void vg_sync(GLcontext c);              /* the arenas, the buffers' handles, what changed: from the pipe */
/* A pipe call that may send (a full stream): the mirrors brought up to date after it. */
#define VGP(c, call) do { ULONG s_ = (c)->vg->seq; call; if ((c)->vg->pipe->sent((c)->vg->px) != s_) vg_sync(c); } while (0)

/* The platform's part (mgl_vg_os3.c, or the PC test's). The transport is the pipe's (library/pipe/). */
/* The present: append ACV_PRESENT (or nothing) to the block at p, which has
 * room for `room` words; returns the words written. After the block has
 * run, vg_os_presented is called with it. */
ULONG vg_os_present(GLcontext c, ULONG *p, ULONG room);
void vg_os_presented(GLcontext c, ULONG *blk, ULONG at);   /* blk 0: nothing was appended */

/* Where the display shows a frame (mgl_display.c): a screen's or the
 * program's bitmap (rp may be 0), or a window's RastPort at x, y. */
struct RastPort;
struct BitMap;
#define MGL_TARGET_SCREEN 0
#define MGL_TARGET_WINDOW 1
#define MGL_TARGET_BITMAP 2
int mgl_disp_target(GLcontext c, struct RastPort **rp, struct BitMap **bm, int *x, int *y);

/* ---- state (mgl_vg_state.c) ----------------------------------------------------------------- */

void vg_state_reset(GLcontext c);       /* nothing is bound: a new context */
/* Everything a draw needs, sent: lay 0 for a clear (the framebuffer only);
 * colswz the colour attribute's byte order (VS_COL_SWZ); flat_first: flat
 * shading takes the first vertex (a polygon's) */
void vg_prepare(GLcontext c, struct vg_layout *lay, int colswz, int flat_first);
int vg_vs_consts(ULONG key);
struct vg_layout *vg_layout_of(GLcontext c, const UBYTE *fmt);   /* fmt[VG_ATTRS]: VG_AF_ */
ULONG vg_mode(GLcontext c, GLenum mode);    /* GL's primitive to Gallium's, quads and polygons as 0xFF */

/* ---- the shaders' keys (the pipe's generator makes the text: mgl_vg_ffkey.h) ----------------------------------------------------------------- */

/* Vertex shader keys */
#define VS_COL_ATTR     (1UL << 0)      /* the colour from the arena (else the current one, a constant) */
#define VS_COL_SWZ(k)   (((k) >> 1) & 3)/* its byte order: 0 R,G,B,A  1 A,R,G,B  2 B,G,R,A */
#define VS_COL_SWZ_SET(s) ((ULONG)(s) << 1)
#define VS_TC0          (1UL << 3)      /* unit 0 has coordinates */
#define VS_TC0_ATTR     (1UL << 4)      /* ... from the arena (else the current ones) */
#define VS_TC0_MAT      (1UL << 5)      /* ... through the texture matrix */
#define VS_TC1          (1UL << 6)
#define VS_TC1_ATTR     (1UL << 7)
#define VS_TC1_MAT      (1UL << 8)
#define VS_FOG          (1UL << 9)      /* the eye distance to the fragment shader */
#define VS_LIGHT        (1UL << 10)     /* lighting */
#define VS_NRM_ATTR     (1UL << 11)     /* the normal from the arena */
#define VS_NLIGHTS(k)   (((k) >> 12) & 15)
#define VS_NLIGHTS_SET(n) ((ULONG)(n) << 12)
#define VS_COLMAT       (1UL << 16)     /* colour material: ambient and diffuse */
#define VS_COLMAT_MODE(k) (((k) >> 17) & 7)
#define VS_COLMAT_MODE_SET(m) ((ULONG)(m) << 17)    /* 0 amb+dif, 1 amb, 2 dif, 3 spec, 4 emission */
#define VS_LOCALV       (1UL << 20)     /* local viewer */
#define VS_GEN0(k)      (((k) >> 21) & 3)   /* texgen on unit 0's S and T: 0 none, 1 object, 2 eye, 3 sphere */
#define VS_GEN0_SET(g)  ((ULONG)(g) << 21)
#define VS_GEN1(k)      (((k) >> 23) & 3)
#define VS_GEN1_SET(g)  ((ULONG)(g) << 23)
#define VS_NORMALIZE    (1UL << 25)
#define VS_LIGHT_POS    (1UL << 26)     /* some light is positional or a spot (needs the eye position) */

/* Fragment shader keys: each unit's function (FS_ENV_) and its texture's
 * base (has colour, has alpha), fog, and the GL's "texture enabled". */
#define FS_ENV_NONE 0
#define FS_ENV_MODULATE 1
#define FS_ENV_REPLACE 2
#define FS_ENV_DECAL 3
#define FS_ENV_BLEND 4
#define FS_ENV_ADD 5
#define FS_ENV0(k)      ((k) & 7)
#define FS_ENV1(k)      (((k) >> 3) & 7)
#define FS_ENV0_SET(e)  ((ULONG)(e))
#define FS_ENV1_SET(e)  ((ULONG)(e) << 3)
#define FS_RGB0         (1UL << 6)
#define FS_A0           (1UL << 7)
#define FS_RGB1         (1UL << 8)
#define FS_A1           (1UL << 9)
#define FS_FOG(k)       (((k) >> 10) & 3)   /* 0 none, 1 linear, 2 exp, 3 exp2 */
#define FS_FOG_SET(f)   ((ULONG)(f) << 10)

/* Constant slots (vec4s). Vertex shader: */
#define VSC_MVP     0                   /* 4 rows */
#define VSC_COLOR   4                   /* the current colour */
#define VSC_MV      5                   /* 3 rows (eye position) */
#define VSC_NM      8                   /* 3 rows: the normal matrix */
#define VSC_TEX0    11                  /* 4 rows */
#define VSC_TEX1    15                  /* 4 rows */
#define VSC_TC0     19                  /* the current coordinates */
#define VSC_TC1     20
#define VSC_NORMAL  21                  /* the current normal */
#define VSC_CONST   22                  /* 0, 1, 0.5, 2 */
#define VSC_SCENE   23                  /* emission + global ambient x material ambient (when not colour material) */
#define VSC_MAT     24                  /* 4: ambient, diffuse, specular, emission (x = shininess in VSC_MISC) */
#define VSC_MISC    28                  /* x shininess, y global ambient's use (colour material), zw - */
#define VSC_GAMB    29                  /* the global ambient */
#define VSC_GEN     30                  /* 4 per unit: S and T planes, object then eye: 30..33, 34..37 */
#define VSC_LIGHT   38                  /* 7 per light: pos, ambient, diffuse, specular, spot dir + cos cutoff,
                                           attenuation (k0 k1 k2, spot exponent), half vector */
#define VSC_LIGHT_N 7
#define VSC_END     (VSC_LIGHT + 8 * VSC_LIGHT_N)
/* Fragment shader: */
#define FSC_ENV0    0
#define FSC_ENV1    1
#define FSC_FOGCOL  2
#define FSC_FOG     3                   /* start, end, density, 1 / (end - start) */
#define FSC_CONST   4                   /* 0, 1, 0.5, log2(e) */
#define FSC_END     5

/* ---- drawing (mgl_vg_draw.c) ---------------------------------------------------------------- */

#define VG_IM_MAX 65532                 /* vertices a quad primitive is drawn in at most (16-bit indices) */
void mgl_vg_begin(GLcontext c);         /* after GLBegin's checks */
void mgl_vg_vertex(GLcontext c, float x, float y, float z, float w);
void mgl_vg_end(GLcontext c);
void mgl_vg_draw_arrays(GLcontext c, GLenum mode, GLint first, GLsizei count);
void mgl_vg_draw_elements(GLcontext c, GLenum mode, GLsizei count, GLenum type, const GLvoid *indices);
void mgl_vg_lock(GLcontext c);          /* glLockArrays, glUnlockArrays: the locked range changed */
void vg_clear_quad(GLcontext c, GLbitfield mask);   /* a clear inside the scissor, through the colour mask */

/* ---- textures (mgl_vg_tex.c) ---------------------------------------------------------------- */

struct vg_res *vg_tex_view(GLcontext c, struct mgl_tex *t);    /* its resource, the texels up to date */
OGPUPipeCSO *vg_tex_sampler(GLcontext c, struct mgl_tex *t);    /* a sampler state for its parameters */
void mgl_vg_tex_changed(GLcontext c, struct mgl_tex *t, int level, int x, int y, int w, int h);
void mgl_vg_tex_free(GLcontext c, struct mgl_tex *t);
void mgl_vg_tex_before(GLcontext c, struct mgl_tex *t);
void mgl_vg_writeback(GLcontext c, const ULONG *argb);
/* A texgen mode's key (VS_GEN): 0 none, 1 object, 2 eye, 3 sphere. */
int vg_gen_of(GLcontext c, int unit);

#endif
