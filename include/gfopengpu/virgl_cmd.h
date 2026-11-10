/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * virgl commands as the 68k writes them, shared by minigl.library's virgl
 * route (library/minigl/mgl_vg*.c) and Warp3D.library's (library/warp3d/
 * w3d_vg*.c): the protocol's numbers, the byte order, and writers for the
 * commands both send. A writer puts one command at p, where the caller has
 * made room for VGC_<NAME>_WORDS words, and returns the word after it; the
 * room and the frame (arenas, blocks, presents) are each library's own.
 *
 * Numbers of the virgl protocol are virglrenderer's (Mesa's MIT
 * virgl_protocol.h and virgl_hw.h, and Gallium's p_defines.h, as virgl
 * carries them); minigl.library's host test checks them against those
 * headers. The ACVirgl request block around them is virgl.h's.
 *
 * Build for a PC test with VG_HOST (MGL_HOST and W3D_HOST set it): 32-bit,
 * the arenas and blocks in the Amiga's order, as the host reads them. */
#ifndef OPENGPU_VIRGL_CMD_H
#define OPENGPU_VIRGL_CMD_H

#include <exec/types.h>
#include "virgl.h"

#if defined(MGL_HOST) || defined(W3D_HOST)
#ifndef VG_HOST
#define VG_HOST 1
#endif
#endif

/* ---- the protocol's numbers ----------------------------------------------------------------- */

#define VG_CMD0(cmd, obj, len) ((ULONG)(cmd) | ((ULONG)(obj) << 8) | ((ULONG)(len) << 16))

enum {                                  /* virgl_context_cmd */
    VG_CCMD_CREATE_OBJECT = 1, VG_CCMD_BIND_OBJECT = 2, VG_CCMD_DESTROY_OBJECT = 3,
    VG_CCMD_SET_VIEWPORT_STATE = 4, VG_CCMD_SET_FRAMEBUFFER_STATE = 5, VG_CCMD_SET_VERTEX_BUFFERS = 6,
    VG_CCMD_CLEAR = 7, VG_CCMD_DRAW_VBO = 8, VG_CCMD_SET_SAMPLER_VIEWS = 10, VG_CCMD_SET_INDEX_BUFFER = 11,
    VG_CCMD_SET_CONSTANT_BUFFER = 12, VG_CCMD_SET_STENCIL_REF = 13, VG_CCMD_SET_BLEND_COLOR = 14, VG_CCMD_SET_SCISSOR_STATE = 15,
    VG_CCMD_BLIT = 16, VG_CCMD_RESOURCE_COPY_REGION = 17, VG_CCMD_BIND_SAMPLER_STATES = 18,
    VG_CCMD_SET_POLYGON_STIPPLE = 22, VG_CCMD_BIND_SHADER = 31
};

enum {                                  /* virgl_object_type */
    VG_OBJ_BLEND = 1, VG_OBJ_RASTERIZER = 2, VG_OBJ_DSA = 3, VG_OBJ_SHADER = 4, VG_OBJ_VERTEX_ELEMENTS = 5,
    VG_OBJ_SAMPLER_VIEW = 6, VG_OBJ_SAMPLER_STATE = 7, VG_OBJ_SURFACE = 8
};

#define VG_SHADER_VERTEX   0
#define VG_SHADER_FRAGMENT 1

/* virgl_formats */
#define VG_FMT_B8G8R8A8_UNORM    1
#define VG_FMT_B5G6R5_UNORM      7
#define VG_FMT_Z16_UNORM         16
#define VG_FMT_Z32_FLOAT         18
#define VG_FMT_Z24_UNORM_S8_UINT 19
#define VG_FMT_Z24X8_UNORM       21
#define VG_FMT_R32_FLOAT         28
#define VG_FMT_R32G32_FLOAT      29
#define VG_FMT_R32G32B32_FLOAT   30
#define VG_FMT_R32G32B32A32_FLOAT 31
#define VG_FMT_R32_USCALED       36
#define VG_FMT_R32G32_SSCALED    45
#define VG_FMT_R8_UNORM          64
#define VG_FMT_R8G8B8A8_UNORM    67
#define VG_FMT_R16_UINT          185

/* virgl_hw.h's bind flags */
#define VG_BIND_DEPTH_STENCIL  (1UL << 0)
#define VG_BIND_RENDER_TARGET  (1UL << 1)
#define VG_BIND_SAMPLER_VIEW   (1UL << 3)
#define VG_BIND_VERTEX_BUFFER  (1UL << 4)
#define VG_BIND_INDEX_BUFFER   (1UL << 5)

/* Gallium's numbers, as virgl carries them (p_defines.h) */
#define VG_TARGET_BUFFER 0
#define VG_TARGET_2D 2
#define VG_CLEAR_DEPTH  (1UL << 0)
#define VG_CLEAR_STENCIL (1UL << 1)
#define VG_CLEAR_COLOR0 (1UL << 2)
#define VG_PRIM_POINTS 0
#define VG_PRIM_LINES 1
#define VG_PRIM_LINE_LOOP 2
#define VG_PRIM_LINE_STRIP 3
#define VG_PRIM_TRIANGLES 4
#define VG_PRIM_TRIANGLE_STRIP 5
#define VG_PRIM_TRIANGLE_FAN 6
#define VG_BLEND_ADD 0
#define VG_BLEND_SUBTRACT 1
#define VG_BLEND_REVERSE_SUBTRACT 2
#define VG_BLEND_MIN 3
#define VG_BLEND_MAX 4
#define VG_BF_ONE 0x01
#define VG_BF_SRC_COLOR 0x02
#define VG_BF_SRC_ALPHA 0x03
#define VG_BF_DST_ALPHA 0x04
#define VG_BF_DST_COLOR 0x05
#define VG_BF_SRC_ALPHA_SATURATE 0x06
#define VG_BF_CONST_COLOR 0x07
#define VG_BF_CONST_ALPHA 0x08
#define VG_BF_ZERO 0x11
#define VG_BF_INV_SRC_COLOR 0x12
#define VG_BF_INV_SRC_ALPHA 0x13
#define VG_BF_INV_DST_ALPHA 0x14
#define VG_BF_INV_DST_COLOR 0x15
#define VG_BF_INV_CONST_COLOR 0x17
#define VG_BF_INV_CONST_ALPHA 0x18
#define VG_FACE_FRONT 1
#define VG_FACE_BACK 2
#define VG_POLYGON_FILL 0
#define VG_POLYGON_LINE 1
#define VG_POLYGON_POINT 2
#define VG_WRAP_REPEAT 0
#define VG_WRAP_CLAMP 1
#define VG_WRAP_CLAMP_TO_EDGE 2
#define VG_FILTER_NEAREST 0
#define VG_FILTER_LINEAR 1
#define VG_MIP_NEAREST 0
#define VG_MIP_LINEAR 1
#define VG_MIP_NONE 2

/* virgl_caps_v1's primitive mask bits we look at, and its words we read */
#define VG_CAPS_WORDS 256               /* room for virgl_caps_v2 */

/* ---- byte order ----------------------------------------------------------------------------- */

/* The command stream is little-endian words; the block, the Amiga's (big-
 * endian) words. Vertex, index and texel data stay in the 68k's order and
 * the host turns them round in ACV_PUT (VG_SW32, VG_SW16). A PC test keeps
 * the arenas in the Amiga's order too (VG_A32, VG_A16 write them so), so
 * that it reads every byte as the host does for the 68k. */
#define VG_SW32 ACV_SWAP_32
#define VG_SW16 ACV_SWAP_16
#ifdef VG_HOST
#define VG_LE(v) ((ULONG)(v))
#define VG_A32(p, v) (*(ULONG *)(p) = __builtin_bswap32((ULONG)(v)))
#define VG_A16(p, v) (*(UWORD *)(p) = __builtin_bswap16((UWORD)(v)))
#else
#define VG_A32(p, v) (*(ULONG *)(p) = (ULONG)(v))
#define VG_A16(p, v) (*(UWORD *)(p) = (UWORD)(v))
static inline ULONG vg_swap32(ULONG v)
{
    __asm__("rol.w #8,%0\n\tswap %0\n\trol.w #8,%0" : "+d"(v));
    return v;
}
#define VG_LE(v) vg_swap32((ULONG)(v))
#endif


/* A request block's words (the Amiga's order). */
#ifdef VG_HOST
#define VG_PUTW(p, v) (*(ULONG *)(p) = __builtin_bswap32((ULONG)(v)))
#define VG_GETW(p) __builtin_bswap32(*(const ULONG *)(p))
#else
#define VG_PUTW(p, v) (*(ULONG *)(p) = (ULONG)(v))
#define VG_GETW(p) (*(const ULONG *)(p))
#endif

/* One stream word, little-endian. */
#define VG_W(p, v) (*(p)++ = VG_LE(v))

static inline ULONG vgc_fbits(float f)
{
    union { float f; ULONG u; } x;
    x.f = f;
    return x.u;
}

/* ---- request blocks (virgl.h) ------------------------------------------------------------------ */

#define VG_BLK_WORDS(n) ((n) + 2)
/* Header, result word (0), the n arguments. */
static inline void vgc_blk_cmd(ULONG *p, ULONG cmd, ULONG n, const ULONG *args)
{
    ULONG i;
    VG_PUTW(p, ACV_HDR(cmd, n + 2));
    VG_PUTW(p + 1, 0);
    for (i = 0; i < n; i++) VG_PUTW(p + 2 + i, args[i]);
}
static inline LONG vgc_result(const ULONG *blk, ULONG at) { return (LONG)VG_GETW(blk + at + 1); }
static inline ULONG vgc_word(const ULONG *blk, ULONG at) { return VG_GETW(blk + at); }

/* ---- stream commands ------------------------------------------------------------------------- */

#define VGC_BIND_WORDS 2
static inline ULONG *vgc_bind(ULONG *p, ULONG obj, ULONG handle)
{
    VG_W(p, VG_CMD0(VG_CCMD_BIND_OBJECT, obj, 1)); VG_W(p, handle);
    return p;
}

#define VGC_DESTROY_WORDS 2
static inline ULONG *vgc_destroy(ULONG *p, ULONG obj, ULONG handle)
{
    VG_W(p, VG_CMD0(VG_CCMD_DESTROY_OBJECT, obj, 1)); VG_W(p, handle);
    return p;
}

/* Scale x, y, z, then translate x, y, z. */
#define VGC_VIEWPORT_WORDS 8
static inline ULONG *vgc_viewport(ULONG *p, const float *v6)
{
    int i;
    VG_W(p, VG_CMD0(VG_CCMD_SET_VIEWPORT_STATE, 0, 7));
    VG_W(p, 0);
    for (i = 0; i < 6; i++) VG_W(p, vgc_fbits(v6[i]));
    return p;
}

#define VGC_SCISSOR_WORDS 4
static inline ULONG *vgc_scissor(ULONG *p, int x0, int y0, int x1, int y1)
{
    VG_W(p, VG_CMD0(VG_CCMD_SET_SCISSOR_STATE, 0, 3));
    VG_W(p, 0);
    VG_W(p, (ULONG)x0 | ((ULONG)y0 << 16));
    VG_W(p, (ULONG)x1 | ((ULONG)y1 << 16));
    return p;
}

/* One colour surface and a depth surface (0 none). */
#define VGC_FRAMEBUFFER_WORDS 4
static inline ULONG *vgc_framebuffer(ULONG *p, ULONG zsurf, ULONG csurf)
{
    VG_W(p, VG_CMD0(VG_CCMD_SET_FRAMEBUFFER_STATE, 0, 3));
    VG_W(p, 1);
    VG_W(p, zsurf);
    VG_W(p, csurf);
    return p;
}

#define VGC_CLEAR_WORDS 9
static inline ULONG *vgc_clear(ULONG *p, ULONG buffers, const float *rgba, double depth, ULONG stencil)
{
    union { double d; ULONG w[2]; } z;
    int i;
    z.d = depth;
    VG_W(p, VG_CMD0(VG_CCMD_CLEAR, 0, 8));
    VG_W(p, buffers);
    for (i = 0; i < 4; i++) VG_W(p, vgc_fbits(rgba[i]));
    /* the depth is a double, its low word first (little-endian qword) */
#ifdef VG_HOST
    VG_W(p, z.w[0]); VG_W(p, z.w[1]);
#else
    VG_W(p, z.w[1]); VG_W(p, z.w[0]);
#endif
    VG_W(p, stencil);
    return p;
}

#define VGC_VERTEX_BUFFER_WORDS 4
static inline ULONG *vgc_vertex_buffer(ULONG *p, ULONG stride, ULONG offset, ULONG handle)
{
    VG_W(p, VG_CMD0(VG_CCMD_SET_VERTEX_BUFFERS, 0, 3));
    VG_W(p, stride);
    VG_W(p, offset);
    VG_W(p, handle);
    return p;
}

/* virglrenderer draws indexed from the index buffer's offset (DRAW_VBO's
 * start is not read for indexed draws, as Mesa's virgl folds it in here). */
#define VGC_INDEX_BUFFER_WORDS 4
static inline ULONG *vgc_index_buffer(ULONG *p, ULONG handle, ULONG index_size, ULONG offset)
{
    VG_W(p, VG_CMD0(VG_CCMD_SET_INDEX_BUFFER, 0, 3));
    VG_W(p, handle);
    VG_W(p, index_size);
    VG_W(p, offset);
    return p;
}

#define VGC_DRAW_WORDS 13
static inline ULONG *vgc_draw(ULONG *p, ULONG prim, ULONG start, ULONG count, int indexed, LONG bias)
{
    VG_W(p, VG_CMD0(VG_CCMD_DRAW_VBO, 0, 12));
    VG_W(p, start);
    VG_W(p, count);
    VG_W(p, prim);
    VG_W(p, indexed ? 1 : 0);
    VG_W(p, 1);                         /* instances */
    VG_W(p, indexed ? (ULONG)bias : 0);
    VG_W(p, 0);                         /* start instance */
    VG_W(p, 0);                         /* primitive restart */
    VG_W(p, 0);
    VG_W(p, 0);                         /* min index */
    VG_W(p, 0xFFFFFFFFUL);              /* max index */
    VG_W(p, 0);
    return p;
}

/* vec4s constant vectors (n * 4 words, as bits) for shader stage `shader`. */
#define VGC_CONSTANTS_WORDS(vec4s) ((ULONG)(vec4s) * 4 + 3)
static inline ULONG *vgc_constants(ULONG *p, int shader, const ULONG *bits, int vec4s)
{
    ULONG n = (ULONG)vec4s * 4, i;
    VG_W(p, VG_CMD0(VG_CCMD_SET_CONSTANT_BUFFER, 0, n + 2));
    VG_W(p, (ULONG)shader);
    VG_W(p, 0);
    for (i = 0; i < n; i++) VG_W(p, bits[i]);
    return p;
}

/* A shader from TGSI text, as bytes after the header, in one command. */
#define VGC_SHADER_WORDS(len) (((ULONG)(len) + 3) / 4 + 6)
static inline ULONG *vgc_shader(ULONG *p, ULONG handle, int type, const char *text, ULONG len)
{
    ULONG words = (len + 3) / 4, i;
    UBYTE *b;
    VG_W(p, VG_CMD0(VG_CCMD_CREATE_OBJECT, VG_OBJ_SHADER, words + 5));
    VG_W(p, handle);
    VG_W(p, (ULONG)type);
    VG_W(p, len);                       /* the whole length, in this one command */
    VG_W(p, len + 300);                 /* tokens to make room for: fewer than its bytes */
    VG_W(p, 0);                         /* no stream output */
    b = (UBYTE *)p;
    for (i = 0; i < len; i++) b[i] = (UBYTE)text[i];
    for (; i < words * 4; i++) b[i] = 0;
    return p + words;
}

#define VGC_BIND_SHADER_WORDS 3
static inline ULONG *vgc_bind_shader(ULONG *p, ULONG handle, int type)
{
    VG_W(p, VG_CMD0(VG_CCMD_BIND_SHADER, 0, 2));
    VG_W(p, handle);
    VG_W(p, (ULONG)type);
    return p;
}

/* The fragment stage's sampler views or sampler states, from slot 0. */
#define VGC_SAMPLERS_WORDS(n) ((ULONG)(n) + 3)
static inline ULONG *vgc_sampler_views(ULONG *p, int n, const ULONG *h)
{
    int i;
    VG_W(p, VG_CMD0(VG_CCMD_SET_SAMPLER_VIEWS, 0, n + 2));
    VG_W(p, VG_SHADER_FRAGMENT);
    VG_W(p, 0);
    for (i = 0; i < n; i++) VG_W(p, h[i]);
    return p;
}
static inline ULONG *vgc_sampler_states(ULONG *p, int n, const ULONG *h)
{
    int i;
    VG_W(p, VG_CMD0(VG_CCMD_BIND_SAMPLER_STATES, 0, n + 2));
    VG_W(p, VG_SHADER_FRAGMENT);
    VG_W(p, 0);
    for (i = 0; i < n; i++) VG_W(p, h[i]);
    return p;
}

#endif
