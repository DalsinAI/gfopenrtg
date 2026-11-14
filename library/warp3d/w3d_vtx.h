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

/* Whether triangle a b c faces back, by the winding SetFrontFace gave (cw:
 * W3D_CW; y runs down the screen): exact, in integers. */
W3D_CONV_FN int w3d_back(int cw, const struct ovtx *a, const struct ovtx *b, const struct ovtx *c)
{
    long long l = (long long)(LONG)(b->x - a->x) * (LONG)(c->y - a->y);
    long long r = (long long)(LONG)(c->x - a->x) * (LONG)(b->y - a->y);
    return cw ? l < r : l > r;
}

/* The front-facing triangles of a fan (fan 1) or strip (0) whose n converted
 * vertices are at t, as a list into o; returns how many vertices that is. o
 * may hold t as its last n of (n - 2) * 3 slots, as draw() lays them out:
 * triangle i writes no further than slot 3i - 4, below where vertex i + 1
 * is (2n - 5 + i), and each vertex is copied before its own slot can be
 * written. */
W3D_CONV_FN ULONG w3d_front(int fan, int cw, const struct ovtx *t, ULONG n, struct ovtx *o)
{
    struct ovtx a = t[0], b = t[1], c;
    ULONG i, k = 0;
    for (i = 2; i < n; i++) {
        const struct ovtx *p0, *p1;
        c = t[i];
        if (!fan && (i & 1)) { p0 = &b; p1 = &a; }  /* odd strip triangles turn the other way */
        else { p0 = &a; p1 = &b; }
        if (!w3d_back(cw, p0, p1, &c)) { o[k] = *p0; o[k + 1] = *p1; o[k + 2] = c; k += 3; }
        if (fan) b = c;
        else { a = b; b = c; }
    }
    return k;
}
#endif
