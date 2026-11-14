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
/* DrawArray's and DrawElements' arrays as the context holds them
 * (VertexPointer, TexCoordPointer 0, ColorPointer, SecondaryColorPointer):
 * arrays() (w3d_draw.c) takes them once a call. */
struct w3d_arrs {
    const UBYTE *vp; LONG vps; ULONG vpmode;
    const UBYTE *tp; LONG tps, tpv, tpw;
    const UBYTE *cp; LONG cps; ULONG cpmode;
    const UBYTE *scp; LONG scps; ULONG scpmode;
};

W3D_CONV_FN float w3d_getf(const UBYTE *p, int dbl) { return dbl ? (float)*(const double *)p : *(const float *)p; }

W3D_CONV_FN void w3d_colour_at(const UBYTE *p, ULONG mode, W3D_Color *o)
{
    float ch[4];
    int i, n = (mode & (W3D_CMODE_RGB | W3D_CMODE_BGR)) ? 3 : 4;
    for (i = 0; i < n; i++) ch[i] = (mode & W3D_COLOR_UBYTE) ? p[i] / 255.0f : ((const float *)p)[i];
    o->a = 1.0f;
    if (mode & W3D_CMODE_RGB) { o->r = ch[0]; o->g = ch[1]; o->b = ch[2]; }
    else if (mode & W3D_CMODE_BGR) { o->b = ch[0]; o->g = ch[1]; o->r = ch[2]; }
    else if (mode & W3D_CMODE_ARGB) { o->a = ch[0]; o->r = ch[1]; o->g = ch[2]; o->b = ch[3]; }
    else if (mode & W3D_CMODE_BGRA) { o->b = ch[0]; o->g = ch[1]; o->r = ch[2]; o->a = ch[3]; }
    else { o->r = ch[0]; o->g = ch[1]; o->b = ch[2]; o->a = ch[3]; }
}

/* A colour of bytes packed as ARGB32 (what w3d_colour_at and w3d_colour
 * give for it, without the floats). */
W3D_CONV_FN ULONG w3d_colour_bytes(const UBYTE *p, ULONG mode)
{
    if (mode & W3D_CMODE_RGB) return 0xFF000000UL | (ULONG)p[0] << 16 | (ULONG)p[1] << 8 | p[2];
    if (mode & W3D_CMODE_BGR) return 0xFF000000UL | (ULONG)p[2] << 16 | (ULONG)p[1] << 8 | p[0];
    if (mode & W3D_CMODE_ARGB) return (ULONG)p[0] << 24 | (ULONG)p[1] << 16 | (ULONG)p[2] << 8 | p[3];
    if (mode & W3D_CMODE_BGRA) return (ULONG)p[3] << 24 | (ULONG)p[2] << 16 | (ULONG)p[1] << 8 | p[0];
    return (ULONG)p[3] << 24 | (ULONG)p[0] << 16 | (ULONG)p[1] << 8 | p[2];
}

/* Vertex i of the arrays: its fields in *v (z as a double only with fog,
 * its one user), the stream depth as the result, and with byte colours the
 * colour already packed in *argb and *packed set. */
W3D_CONV_FN ULONG w3d_array_read(const struct w3d_arrs *a, ULONG i, int fog, const W3D_Color *current,
                                 W3D_Vertex *v, ULONG *argb, int *packed)
{
    const UBYTE *p = a->vp + (LONG)i * a->vps;
    ULONG z;
    switch (a->vpmode) {
    case W3D_VERTEX_F_F_F:
        v->x = w3d_getf(p, 0); v->y = w3d_getf(p + 4, 0);
        z = w3d_zf(*(const float *)(p + 8));
        v->z = fog ? w3d_getf(p + 8, 0) : 0.0;     /* only fog needs it as a double: a library call */
        break;
    case W3D_VERTEX_F_F_D: v->x = w3d_getf(p, 0); v->y = w3d_getf(p + 4, 0); v->z = *(const double *)(p + 8); z = w3d_zd(&v->z); break;
    default: v->x = w3d_getf(p, 1); v->y = w3d_getf(p + 8, 1); v->z = *(const double *)(p + 16); z = w3d_zd(&v->z); break;
    }
    v->u = v->v = 0.0f; v->w = 1.0f; v->tex3d = 0.0f; v->l = 0.0f;
    if (a->tp) {
        const UBYTE *q = a->tp + (LONG)i * a->tps;
        v->u = *(const float *)q;
        v->v = *(const float *)(q + a->tpv);
        v->w = *(const float *)(q + a->tpw);       /* the offset may be negative: w before u, as MiniGL keeps it */
    }
    *packed = 0;
    if (a->cp && (a->cpmode & W3D_COLOR_UBYTE)) {
        *argb = w3d_colour_bytes(a->cp + (LONG)i * a->cps, a->cpmode);
        *packed = 1;
    } else if (a->cp) w3d_colour_at(a->cp + (LONG)i * a->cps, a->cpmode, &v->color);
    else v->color = *current;
    v->spec.r = v->spec.g = v->spec.b = 0.0f;
    if (a->scp) {
        W3D_Color s;
        w3d_colour_at(a->scp + (LONG)i * a->scps, a->scpmode, &s);
        v->spec.r = s.r; v->spec.g = s.g; v->spec.b = s.b;
    }
    return z;
}

/* Triangle t's vertex k (0, 1, 2) as an element number: lists 3t + k;
 * fans 0, t + 1, t + 2; strips t, t + 1, t + 2 with the first two swapped
 * for odd t. The kinds are hostlib arrays.h's HL_WA_LIST, _FAN, _STRIP. */
#define W3D_TRI_LIST  0
#define W3D_TRI_FAN   1
#define W3D_TRI_STRIP 2
W3D_CONV_FN ULONG w3d_tri_elem(int kind, ULONG t, ULONG k)
{
    if (kind == W3D_TRI_LIST) return t * 3 + k;
    if (kind == W3D_TRI_FAN) return k ? t + k : 0;
    if (t & 1) return k == 0 ? t + 1 : k == 1 ? t : t + 2;
    return t + k;
}

/* A colour mode as arrays.h's HL_WA_C_* (RGBA 0, ARGB 1, BGRA 2, RGB 3,
 * BGR 4, plus 8 for bytes), in w3d_colour_at's order of tests. */
/* DrawArray and DrawElements use hostlib_w3d_arrays only where the runtime
 * runs it as host code: hostlib_probe's answer for it (rc, the ABI, and its
 * answer byte, HL_PROBE_HOST is bit 0). Elsewhere they keep their own loop,
 * so a machine without the host function draws exactly as before. */
#define W3D_HL_ARRAYS_VERSION 1u
W3D_CONV_FN int w3d_hl_hosted(ULONG rc, UBYTE answer)
{
    return rc != 0 && (answer & 1u) != 0;
}

W3D_CONV_FN ULONG w3d_hl_cmode(ULONG m)
{
    ULONG r = (m & W3D_CMODE_RGB) ? 3 : (m & W3D_CMODE_BGR) ? 4 : (m & W3D_CMODE_ARGB) ? 1 : (m & W3D_CMODE_BGRA) ? 2 : 0;
    return r | ((m & W3D_COLOR_UBYTE) ? 8 : 0);
}
#endif
