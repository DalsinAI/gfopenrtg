/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * virgl commands as the 68k writes them: the shared pipe's own (only
 * library/pipe includes this file; tests/ownership_check.sh's virgl-cmd).
 * A writer puts one command at p, where the caller has made room for
 * VGC_<NAME>_WORDS words, and returns the word after it.
 *
 * The numbers a front end hands the pipe, the byte order and the request
 * block's framing are include/gfopengpu/pipe_vg.h's. */
#ifndef OPENGPU_VIRGL_CMD_H
#define OPENGPU_VIRGL_CMD_H

#include "../../include/gfopengpu/pipe_vg.h"

/* ---- the protocol's command numbers --------------------------------------------------------- */

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

/* One stream word, little-endian. */
#define VG_W(p, v) (*(p)++ = VG_LE(v))

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
