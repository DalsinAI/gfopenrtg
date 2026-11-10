/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * minigl.library's virgl route (IDEA 5, docs/design/MiniGL-Compatible-Plan.md):
 * GL 1.1 drawn on the host's GPU through GFOpenGPU's OGPU_OP_VIRGL, with our
 * own encoder, our own fixed-function shaders (TGSI text) and one request
 * block a frame. The state code, matrices, lists, GLU and GLUT are the
 * library's own; this route replaces the per-vertex work and the output.
 *
 *   mgl_vg_enc.c     the virgl command stream (little-endian words) and the
 *                    ACVirgl request block (the Amiga's order)
 *   mgl_vg_frame.c   the context on the host, resources, the frame's block,
 *                    presenting, reading back
 *   mgl_vg_state.c   GL state to keys to cached virgl objects, constants
 *   mgl_vg_shader.c  the fixed-function shaders, as TGSI text from keys
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

/* The protocol's numbers, byte order and command writers are shared with
 * Warp3D.library's virgl route. */
#include "../../include/gfopengpu/virgl_cmd.h"

/* ---- resources -------------------------------------------------------------------------------- */

/* A host resource. Its handle is this struct's address: virglrenderer has one
 * namespace of resources for all its clients, and an address is unique
 * while the resource lives (as GL.module does). */
struct vg_res {
    ULONG handle;
    ULONG fmt, w, h, levels;
    struct vg_res *next_dead;           /* retired: released after the frame's block */
};

/* A texture's host side (mgl_vg_tex.c), hung on struct mgl_tex. */
struct vg_tex {
    struct vg_res *res;                 /* the current version */
    ULONG gen;                          /* the texels' generation it holds */
    ULONG used;                         /* the stream sequence that last drew with it */
    ULONG put;                          /* ... that has a PUT of it waiting */
    ULONG boxgen;                       /* the generation the dirty boxes describe */
    ULONG view;                         /* its sampler view (object handle), 0 none */
    int w, h, levels;
    int dx0[MGL_MAX_LEVELS], dy0[MGL_MAX_LEVELS], dx1[MGL_MAX_LEVELS], dy1[MGL_MAX_LEVELS];   /* dirty, per level */
    int whole;                          /* everything must go up */
};

/* ---- the state caches (mgl_vg_state.c) ---------------------------------------------------- */

#define VG_CACHE 64                     /* objects of one kind a context keeps */

struct vg_cache {
    ULONG key[VG_CACHE][5];
    UWORD handle[VG_CACHE];
    UWORD n;
};

/* Shader variants a context keeps. */
#define VG_SHADERS 64

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
    UWORD ve;                           /* its VERTEX_ELEMENTS object */
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
#define VG_D_VBUF      (1UL << 3)
#define VG_D_IBUF      (1UL << 4)
#define VG_D_VSC       (1UL << 5)
#define VG_D_FSC       (1UL << 6)
#define VG_D_ALL       0xFFFFFFFFUL

#define VG_RING        4                /* the vertex and index buffers: arenas long */

struct mgl_vg {
    ULONG ctx_id;
    ULONG host_flags;                   /* HELLO's ACV_HOST_ */
    ULONG prim_mask;                    /* virgl_caps_v1.prim_mask */
    ULONG caps_bits;                    /* virgl_caps_v2.capability_bits */
    int gles;

    /* the frame's block: commands before the stream (creates, puts), after it (unrefs) */
    ULONG *pre; ULONG npre, cpre;
    ULONG *post; ULONG npost, cpost;
    ULONG *blk; ULONG cblk;             /* the block as sent */
    /* the command stream, little-endian words */
    ULONG *cmd; ULONG ncmd, ccmd;
    ULONG seq;                          /* counts streams sent */
    ULONG pre_errors;                   /* commands the host refused in the last block */
    /* the arenas: vertices (32-bit words) and 16-bit indices */
    UBYTE *va; ULONG nva, cva;
    UWORD *ia; ULONG nia, cia;
    struct vg_res *colour, *depth, *vbuf, *ibuf;
    /* Where the arenas go in vbuf and ibuf (bytes): each block's after the
     * last one's, never over them. virglrenderer writes a buffer's PUT
     * unsynchronized (GL_MAP_UNSYNCHRONIZED_BIT), so a PUT over vertices an
     * earlier block's draws still read changed those draws on the GPU. The
     * buffers are VG_RING arenas long; past the end, new ones (vg_ring). */
    ULONG vb_off, vb_size, ib_base, ib_size;
    struct vg_res *dead;                /* resources released after the next block */
    ULONG grow_va, grow_ia, grow_cmd;   /* asked for at the next frame's start */

    ULONG next_obj;                     /* object handles: ours, per context */
    ULONG surf_c, surf_z;               /* the surfaces of colour and depth */

    /* what is bound */
    ULONG dirty;
    ULONG b_blend, b_dsa, b_rs, b_vs, b_fs, b_ve;
    ULONG stride;                       /* the vertex buffer's, as set */
    ULONG ib_off;                       /* the index buffer's offset, as set */
    struct vg_cache blend, dsa, rs, samp;
    ULONG vs_key[VG_SHADERS * 2], fs_key[VG_SHADERS * 2];   /* key, spot lights */
    char *text;                         /* a shader's text, being made */
    UWORD vs_h[VG_SHADERS], fs_h[VG_SHADERS];
    int nvs, nfs;
    struct vg_layout lay[VG_LAYOUTS];
    int nlay;
    ULONG views[MGL_TEX_UNITS], samps[MGL_TEX_UNITS];   /* bound */
    float vp[6];
    LONG sc[4];
    ULONG blend_color[4];

    /* constants, as last sent */
    float vsc[VG_VS_CONSTS * 4];
    int nvsc;
    float fsc[VG_FS_CONSTS * 4];
    int nfsc;

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

    /* sampler states, by key */
    ULONG samp_key[32][2], samp_h[32];
    int nsamp;
    /* statistics (ENV:MiniGL/Trace) */
    ULONG draws, objects, frames, blocks, words, va_bytes, puts, renames;
};

/* ---- the encoder (mgl_vg_enc.c) --------------------------------------------------------------- */

/* Room for n words in the stream; the frame is sent first when there is none. */
ULONG *vg_cmd(GLcontext c, ULONG n);
ULONG vg_fbits(float f);
void vg_bind(GLcontext c, ULONG obj, ULONG handle);
void vg_set_viewport(GLcontext c, const float *v6);
void vg_set_scissor(GLcontext c, int x0, int y0, int x1, int y1);
void vg_set_framebuffer(GLcontext c);
void vg_clear(GLcontext c, ULONG buffers, const float *rgba, float depth);
void vg_set_vertex_buffer(GLcontext c, ULONG stride);
void vg_set_index_buffer(GLcontext c, ULONG offset);
void vg_draw(GLcontext c, ULONG prim, ULONG start, ULONG count, int indexed, LONG bias);
void vg_constants(GLcontext c, int shader, const float *v, int vec4s);
void vg_shader(GLcontext c, ULONG handle, int type, const char *text, ULONG len);
void vg_bind_shader(GLcontext c, ULONG handle, int type);
void vg_sampler_views(GLcontext c, int n, const ULONG *h);
void vg_sampler_states(GLcontext c, int n, const ULONG *h);
ULONG vg_new_obj(GLcontext c);

/* Block commands (the Amiga's order). */
ULONG *vg_pre(GLcontext c, ULONG n);   /* room in the commands before the stream */
void vg_blk_cmd(ULONG *p, ULONG cmd, ULONG n, const ULONG *args);
#define VG_BLK_WORDS(n) ((n) + 2)
int vg_run(GLcontext c, ULONG *blk, ULONG words);       /* one block now; 0 on success */
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

/* The platform's part (mgl_vg_os3.c, or the PC test's). */
int vg_os_open(GLcontext c);            /* the transport is there (else no virgl) */
int vg_os_run(GLcontext c, ULONG *blk, ULONG bytes);
/* The present: append ACV_PRESENT (or nothing) to the block at p, which has
 * room for `room` words; returns the words written. After the block has
 * run, vg_os_presented is called with it. */
ULONG vg_os_present(GLcontext c, ULONG *p, ULONG room);
void vg_os_presented(GLcontext c, ULONG *blk, ULONG at);   /* blk 0: nothing was appended */
void vg_os_close(GLcontext c);

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

/* ---- shaders (mgl_vg_shader.c) ----------------------------------------------------------------- */

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

/* A shader's text for a key: into buf (cap bytes), its length with the
 * closing 0 returned. */
ULONG vg_vs_text(ULONG key, ULONG spots, char *buf, ULONG cap);   /* spots: which lights are spots */
ULONG vg_fs_text(ULONG key, char *buf, ULONG cap);

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

ULONG vg_tex_view(GLcontext c, struct mgl_tex *t);      /* its sampler view, the texels up to date */
ULONG vg_tex_sampler(GLcontext c, struct mgl_tex *t);   /* a sampler state for its parameters */
void mgl_vg_tex_changed(GLcontext c, struct mgl_tex *t, int level, int x, int y, int w, int h);
void mgl_vg_tex_free(GLcontext c, struct mgl_tex *t);
void mgl_vg_tex_before(GLcontext c, struct mgl_tex *t);
void mgl_vg_writeback(GLcontext c, const ULONG *argb);
/* A texgen mode's key (VS_GEN): 0 none, 1 object, 2 eye, 3 sphere. */
int vg_gen_of(GLcontext c, int unit);

#endif
