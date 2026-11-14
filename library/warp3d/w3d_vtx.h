/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * Warp3D.library: one vertex into the stream's layout, all but the fog and
 * specular word. convert() (w3d_draw.c) uses it, and tests/test_w3d_hlv.c
 * runs it on the PC against hostlib_w3d_vertices, the same work as host
 * code (library/hostlib/w3d/vertices.c). Needs w3d_internal.h's ovtx and
 * w3d_colour (or the test's copies). */
#ifndef W3D_VTX_H
#define W3D_VTX_H
#include "w3d_conv.h"

/* How u and v become 16.16 texels: times 2^k when the scale is a power of
 * two (a texture's usual size, or normalised coordinates), else times the
 * float scale; UV_NONE without a texture. All in integers but the last. */
#define UV_NONE  -1000
#define UV_FLOAT 1000
struct uvs { int ku, kv; float su, sv; };

W3D_CONV_FN LONG uv(float c, int k, float scale)
{
    if (k == UV_NONE) return 0;
    if (k == UV_FLOAT) return w3d_f2l(c * scale);
    return w3d_fx(c, k);
}

/* state: the context's W3D_GOURAUD bit and the rest; current: its current
 * colour; z: the stream's depth; argb: the colour already packed, or 0. */
W3D_CONV_FN void w3d_vtx_core(ULONG state, const W3D_Color *current, const W3D_Vertex *v, const struct uvs *s, ULONG z,
                              const ULONG *argb, struct ovtx *o)
{
    ULONG w = w3d_fbits(v->w);
    o->x = w3d_fx(v->x, 16);
    o->y = w3d_fx(v->y, 16);
    o->argb = !(state & W3D_GOURAUD) ? w3d_colour(current) : argb ? *argb : w3d_colour(&v->color);
    o->u = uv(v->u, s->ku, s->su);
    o->v = uv(v->v, s->kv, s->sv);
    o->z = z;
    if ((LONG)w <= 0 || w > 0x7f800000UL) w = 0x358637BDUL;    /* w must be positive: 1e-6 */
    o->w = w;
}
#endif
