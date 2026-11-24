/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * The shared pipe's numbers, for its front ends (minigl.library's
 * library/minigl/mgl_vg*.c and Warp3D.library's library/warp3d/w3d_vg*.c):
 * the formats, bind flags, targets, clear buffers, primitives, blend and
 * sampler numbers a front end hands the pipe (pipe.h), the byte order of
 * arenas and request blocks, and the request block's framing (virgl.h).
 * No virgl command is here: only the pipe writes those
 * (library/pipe/virgl_cmd.h, which includes this file).
 *
 * Numbers of the virgl protocol are virglrenderer's (Mesa's MIT
 * virgl_protocol.h and virgl_hw.h, and Gallium's p_defines.h, as virgl
 * carries them); minigl.library's host test checks them against those
 * headers.
 *
 * Build for a PC test with VG_HOST (MGL_HOST and W3D_HOST set it): 32-bit,
 * the arenas and blocks in the Amiga's order, as the host reads them. */
#ifndef OPENGPU_PIPE_VG_H
#define OPENGPU_PIPE_VG_H

#include <exec/types.h>
#include "virgl.h"

#if defined(MGL_HOST) || defined(W3D_HOST)
#ifndef VG_HOST
#define VG_HOST 1
#endif
#endif

/* ---- the numbers ---------------------------------------------------------------------------- */

#define VG_SHADER_VERTEX   0
#define VG_SHADER_FRAGMENT 1

/* virgl_formats */
#define VG_FMT_B8G8R8A8_UNORM    1
#define VG_FMT_B5G6R5_UNORM      7
#define VG_FMT_Z16_UNORM         16
#define VG_FMT_Z32_FLOAT         18
/* depth and stencil packed 24/8 (depth << 8 | stencil in a word): the one
 * virglrenderer takes for a depth-stencil buffer (it refuses 19; the
 * Warp3D host test, tests/run_w3d_vg.sh, makes one) */
#define VG_FMT_D24S8             20
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

#endif
