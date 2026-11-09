/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * minigl.library's virgl route: drawing. Vertices go into the frame's arena
 * in object space, as the program gave them (floats stay floats, colour
 * bytes stay bytes; the host turns the arena's 32-bit units round), and a
 * draw is DRAW_VBO with the state it needs before it (mgl_vg_state.c). The
 * GPU transforms, lights and clips.
 *
 *  - glBegin/glEnd: each glVertex appends one vertex in the primitive's
 *    layout (position, colour, the normal when lit, the units' coordinates
 *    when textured); glEnd draws it. An arena that fills mid-primitive
 *    draws what is complete and carries on in the next (never truncated).
 *  - glDrawArrays: the range, once, into the arena.
 *  - glDrawElements: the indices into the index arena (16-bit) and, with
 *    glLockArrays, the locked range into the arena once per lock (plan 5:
 *    positions never copied twice); without a lock the range the indices
 *    reach.
 *  - Quads, quad strips and flat-shaded polygons become indexed triangles,
 *    each ending in the vertex GL takes a flat colour from (OpenGL 1.1,
 *    table 2.8); lines, loops, strips and fans go as they are. */
#include "mgl_vg.h"

/* ---- the arena ------------------------------------------------------------------------------- */

static ULONG rgba_word(const float *col)
{
    ULONG argb = mgl_argb(col);
    return (argb << 8) | (argb >> 24);   /* R, G, B, A in the 68k's memory */
}

/* The arena up to a multiple of stride: a vertex's place is its index. */
static ULONG align(struct mgl_vg *v, ULONG stride)
{
    ULONG r = v->nva % stride;
    if (r) v->nva += stride - r;
    return v->nva / stride;
}

/* Forward copy (dst below src may overlap). */
static void move_down(UBYTE *d, const UBYTE *s, ULONG n)
{
    while (n--) *d++ = *s++;
}

/* Indices a primitive of n vertices becomes (0: drawn as it is). */
static ULONG indices_for(GLcontext c, GLenum mode, ULONG n)
{
    switch (mode) {
    case GL_QUADS: return n / 4 * 6;
    case GL_QUAD_STRIP: return n >= 4 ? (n - 2) / 2 * 6 : 0;
    case GL_POLYGON: return c->shade_model == GL_FLAT && n >= 3 ? (n - 2) * 3 : 0;
    }
    return 0;
}

/* Draw n vertices from `first` in the arena (non-indexed), or with ix (n
 * indices relative to `first`). */
static void emit(GLcontext c, GLenum mode, ULONG first, ULONG n, struct vg_layout *lay, int swz, const UWORD *ix)
{
    struct mgl_vg *v = c->vg;
    ULONG prim, need, k, start;
    UWORD *o;
    switch (mode) {
    case GL_POINTS: prim = VG_PRIM_POINTS; break;
    case GL_LINES: n &= ~1UL; prim = VG_PRIM_LINES; break;
    case GL_LINE_STRIP: prim = VG_PRIM_LINE_STRIP; if (n < 2) return; break;
    case GL_LINE_LOOP: prim = VG_PRIM_LINE_LOOP; if (n < 2) return; break;
    case GL_TRIANGLES: n -= n % 3; prim = VG_PRIM_TRIANGLES; break;
    case GL_TRIANGLE_STRIP: prim = VG_PRIM_TRIANGLE_STRIP; if (n < 3) return; break;
    case GL_TRIANGLE_FAN: prim = VG_PRIM_TRIANGLE_FAN; if (n < 3) return; break;
    case GL_POLYGON: prim = c->shade_model == GL_FLAT ? VG_PRIM_TRIANGLES : VG_PRIM_TRIANGLE_FAN; if (n < 3) return; break;
    case GL_QUADS: n &= ~3UL; prim = VG_PRIM_TRIANGLES; break;
    case GL_QUAD_STRIP: n &= ~1UL; prim = VG_PRIM_TRIANGLES; if (n < 4) return; break;
    default: return;
    }
    if (!n) return;
    need = indices_for(c, mode, n);
    if (!need && ix) need = n;
    if (need && v->nia + need > v->cia) {
        /* no room for the indices: what there is goes, and this draw's
         * vertices move to the start of the arena */
        ULONG stride = lay->stride, bytes = ix ? 0 : n * stride;
        if (!ix) {
            vg_send(c, 0);
            move_down(v->va, v->va + first * stride, bytes);
            first = 0;
            v->nva = bytes;
        } else return;                  /* callers reserve room for index lists (vg_need) */
    }
    vg_prepare(c, lay, swz, 0);         /* GL's provoking vertex is the last: a flat polygon's triangles end in its first */
    if (!need) { vg_draw(c, prim, first, n, 0, 0); return; }
    start = v->nia;
    o = v->ia + v->nia;
#define I(k) (ix ? (ULONG)ix[k] : (ULONG)(k))
    switch (mode) {
    case GL_QUADS:
        for (k = 0; k + 3 < n; k += 4) {
            VG_A16(o, I(k + 1)); o++; VG_A16(o, I(k + 2)); o++; VG_A16(o, I(k + 3)); o++;
            VG_A16(o, I(k)); o++; VG_A16(o, I(k + 1)); o++; VG_A16(o, I(k + 3)); o++;
        }
        break;
    case GL_QUAD_STRIP:
        for (k = 0; k + 3 < n; k += 2) {
            VG_A16(o, I(k)); o++; VG_A16(o, I(k + 1)); o++; VG_A16(o, I(k + 3)); o++;
            VG_A16(o, I(k + 2)); o++; VG_A16(o, I(k)); o++; VG_A16(o, I(k + 3)); o++;
        }
        break;
    case GL_POLYGON:
        if (c->shade_model == GL_FLAT) {        /* fan triangles ending in vertex 0, the polygon's colour */
            for (k = 1; k + 1 < n; k++) { VG_A16(o, I(k)); o++; VG_A16(o, I(k + 1)); o++; VG_A16(o, I(0)); o++; }
            break;
        }
        /* fall through - a fan, as listed */
    default:
        for (k = 0; k < n; k++) { VG_A16(o, I(k)); o++; }
        break;
    }
#undef I
    v->nia = (ULONG)(o - v->ia);
    vg_draw(c, prim, start, v->nia - start, 1, (LONG)first);
}

/* ---- glBegin / glEnd ------------------------------------------------------------------------------- */

/* A sphere map needs the normal even unlit. */
static int needs_normal(GLcontext c)
{
    return (mgl_tex_unit(c, 0) && vg_gen_of(c, 0) == 3) || (mgl_tex_unit(c, 1) && vg_gen_of(c, 1) == 3);
}

static struct vg_layout *im_layout(GLcontext c)
{
    UBYTE f[VG_ATTRS];
    f[VG_A_POS] = VG_AF_F4;
    f[VG_A_COLOR] = VG_AF_UB4;
    f[VG_A_NORMAL] = (c->en & EN_LIGHTING) || needs_normal(c) ? VG_AF_F3 : VG_AF_NONE;
    f[VG_A_TC0] = mgl_tex_unit(c, 0) ? VG_AF_F4 : VG_AF_NONE;
    f[VG_A_TC1] = mgl_tex_unit(c, 1) ? VG_AF_F4 : VG_AF_NONE;
    return vg_layout_of(c, f);
}

void mgl_vg_begin(GLcontext c)
{
    struct mgl_vg *v = c->vg;
    vg_need(c, 64 * 64, 0);
    v->im_lay = im_layout(c);
    v->im_first = align(v, v->im_lay->stride);
    v->im_n = 0;
    v->im_mode = c->prim;
    v->im_close = 0;
}

/* The arena is full mid-primitive: what is complete is drawn, the rest
 * carries on at the start of the next arena (as mgl_vertex.c's part()). */
static void im_overflow(GLcontext c)
{
    struct mgl_vg *v = c->vg;
    ULONG stride = v->im_lay->stride, n = v->im_n, draw = n, keep_from = n, k, nkeep = 0;
    UBYTE keep[4 * 64];
    GLenum mode = v->im_mode;
    UBYTE *first = v->va + v->im_first * stride;
    switch (mode) {
    case GL_POINTS: break;
    case GL_LINES: draw = n & ~1UL; keep_from = draw; break;
    case GL_TRIANGLES: draw = n - n % 3; keep_from = draw; break;
    case GL_QUADS: draw = n & ~3UL; keep_from = draw; break;
    case GL_LINE_STRIP: keep_from = n ? n - 1 : 0; break;
    case GL_LINE_LOOP:                  /* from now on a strip, closed at glEnd with its first vertex */
        if (n) move_down(v->im_v0, first, stride);
        v->im_close = n != 0;
        mode = GL_LINE_STRIP;
        keep_from = n ? n - 1 : 0;
        break;
    case GL_QUAD_STRIP: case GL_TRIANGLE_STRIP:     /* an even count drawn: the next triangle stays even */
        draw = n & ~1UL; keep_from = draw >= 2 ? draw - 2 : 0; break;
    case GL_TRIANGLE_FAN: case GL_POLYGON:          /* the first vertex and the last */
        if (n) { move_down(keep, first, stride); nkeep = 1; }
        keep_from = n > 1 ? n - 1 : n;
        break;
    }
    for (k = keep_from; k < n && nkeep < 4; k++, nkeep++) move_down(keep + nkeep * stride, first + k * stride, stride);
    emit(c, mode, v->im_first, draw, v->im_lay, 0, 0);
    if (v->nva + stride > v->cva) v->grow_va = v->cva * 2;
    vg_send(c, 0);
    v->im_first = align(v, stride);
    move_down(v->va + v->nva, keep, nkeep * stride);
    v->nva += nkeep * stride;
    v->im_n = nkeep;
    v->im_mode = mode;
}

void mgl_vg_vertex(GLcontext c, float x, float y, float z, float w)
{
    struct mgl_vg *v = c->vg;
    struct vg_layout *l = v->im_lay;
    UBYTE *p;
    /* a primitive goes in parts past the arena's end, and at 65532 vertices
     * (its 16-bit relative indices, when it becomes triangles) */
    if (v->nva + l->stride > v->cva || v->im_n >= VG_IM_MAX) im_overflow(c);
    p = v->va + v->nva;
    VG_A32(p, vg_fbits(x)); VG_A32(p + 4, vg_fbits(y)); VG_A32(p + 8, vg_fbits(z)); VG_A32(p + 12, vg_fbits(w));
    if (!v->im_colok || c->color[0] != v->im_col[0] || c->color[1] != v->im_col[1] || c->color[2] != v->im_col[2] || c->color[3] != v->im_col[3]) {
        v->im_colok = 1;
        v->im_col[0] = c->color[0]; v->im_col[1] = c->color[1]; v->im_col[2] = c->color[2]; v->im_col[3] = c->color[3];
        v->im_colw = rgba_word(c->color);
    }
    VG_A32(p + l->off[VG_A_COLOR], v->im_colw);
    if ((l->key >> (VG_A_NORMAL * 4)) & 15) {
        UBYTE *q = p + l->off[VG_A_NORMAL];
        VG_A32(q, vg_fbits(c->normal[0])); VG_A32(q + 4, vg_fbits(c->normal[1])); VG_A32(q + 8, vg_fbits(c->normal[2]));
    }
    {
        int u;
        for (u = 0; u < MGL_TEX_UNITS; u++)
            if ((l->key >> ((VG_A_TC0 + u) * 4)) & 15) {
                UBYTE *q = p + l->off[VG_A_TC0 + u];
                VG_A32(q, vg_fbits(c->tc[u][0])); VG_A32(q + 4, vg_fbits(c->tc[u][1]));
                VG_A32(q + 8, vg_fbits(c->tc[u][2])); VG_A32(q + 12, vg_fbits(c->tc[u][3]));
            }
    }
    v->nva += l->stride;
    v->im_n++;
}

void mgl_vg_end(GLcontext c)
{
    struct mgl_vg *v = c->vg;
    struct vg_layout *l = v->im_lay;
    if (v->im_close) {                  /* a split loop: its first vertex closes it */
        if (v->nva + l->stride > v->cva) im_overflow(c);
        move_down(v->va + v->nva, v->im_v0, l->stride);
        v->nva += l->stride;
        v->im_n++;
    }
    emit(c, v->im_mode, v->im_first, v->im_n, l, 0, 0);
    v->im_n = 0;
}

/* ---- arrays ------------------------------------------------------------------------------------- */

static int type_bytes(GLenum t)
{
    switch (t) {
    case GL_BYTE: case GL_UNSIGNED_BYTE: case MGL_UBYTE_ARGB: case MGL_UBYTE_BGRA: return 1;
    case GL_SHORT: case GL_UNSIGNED_SHORT: return 2;
    case GL_DOUBLE: return 8;
    }
    return 4;
}

static float comp(const UBYTE *p, GLenum type, int k, int normalise)
{
    switch (type) {
    case GL_FLOAT: { union { ULONG u; float f; } x; x.u = ((const ULONG *)p)[k]; return x.f; }
    case GL_DOUBLE: return (float)((const double *)p)[k];
    case GL_INT: return normalise ? (float)((const LONG *)p)[k] * (1.0f / 2147483647.0f) : (float)((const LONG *)p)[k];
    case GL_UNSIGNED_INT: return normalise ? (float)((const ULONG *)p)[k] * (1.0f / 4294967295.0f) : (float)((const ULONG *)p)[k];
    case GL_SHORT: return normalise ? (float)((const WORD *)p)[k] * (1.0f / 32767.0f) : (float)((const WORD *)p)[k];
    case GL_UNSIGNED_SHORT: return normalise ? (float)((const UWORD *)p)[k] * (1.0f / 65535.0f) : (float)((const UWORD *)p)[k];
    case GL_BYTE: return normalise ? (float)((const BYTE *)p)[k] * (1.0f / 127.0f) : (float)((const BYTE *)p)[k];
    }
    return normalise ? (float)p[k] * (1.0f / 255.0f) : (float)p[k];
}

static ULONG stride_of(const struct mgl_array *a)
{
    return a->stride ? (ULONG)a->stride : (ULONG)(a->size * type_bytes(a->type));
}

/* The layout an array draw has: the enabled arrays a draw uses (the rest
 * are constants); *swz the colour bytes' order. */
static struct vg_layout *array_layout(GLcontext c, int *swz)
{
    UBYTE f[VG_ATTRS];
    *swz = 0;
    f[VG_A_POS] = (UBYTE)(VG_AF_F1 + (c->va.size - 1));
    f[VG_A_COLOR] = (c->client & CL_COLOR) ? VG_AF_UB4 : VG_AF_NONE;
    if (c->client & CL_COLOR) *swz = c->ca.type == MGL_UBYTE_ARGB ? 1 : c->ca.type == MGL_UBYTE_BGRA ? 2 : 0;
    f[VG_A_NORMAL] = ((c->en & EN_LIGHTING) || needs_normal(c)) && (c->client & CL_NORMAL) ? VG_AF_F3 : VG_AF_NONE;
    f[VG_A_TC0] = mgl_tex_unit(c, 0) && (c->client & CL_TEX0) ? (UBYTE)(VG_AF_F1 + (c->ta[0].size - 1)) : VG_AF_NONE;
    f[VG_A_TC1] = mgl_tex_unit(c, 1) && (c->client & CL_TEX1) ? (UBYTE)(VG_AF_F1 + (c->ta[1].size - 1)) : VG_AF_NONE;
    return vg_layout_of(c, f);
}

/* Vertices first .. first + n - 1 of the arrays into the arena at dst. */
static void copy_range(GLcontext c, struct vg_layout *l, ULONG first, ULONG n, UBYTE *dst)
{
    ULONG vs = stride_of(&c->va), cs = stride_of(&c->ca), ns = stride_of(&c->na), i;
    const UBYTE *vp = c->va.ptr + first * vs, *cp = c->ca.ptr + first * cs, *np = c->na.ptr + first * ns;
    int psize = c->va.size, k, col = (int)((l->key >> (VG_A_COLOR * 4)) & 15), nrm = (int)((l->key >> (VG_A_NORMAL * 4)) & 15);
    int u, tcn[MGL_TEX_UNITS];
    ULONG ts[MGL_TEX_UNITS];
    int ctype = c->ca.type, raw = col && (ctype == GL_UNSIGNED_BYTE || ctype == MGL_UBYTE_ARGB || ctype == MGL_UBYTE_BGRA) && c->ca.size == 4;
    for (u = 0; u < MGL_TEX_UNITS; u++) {
        tcn[u] = (int)((l->key >> ((VG_A_TC0 + u) * 4)) & 15);
        ts[u] = stride_of(&c->ta[u]);
    }
    for (i = 0; i < n; i++, dst += l->stride, vp += vs) {
        if (c->va.type == GL_FLOAT) for (k = 0; k < psize; k++) VG_A32(dst + k * 4, ((const ULONG *)vp)[k]);
        else for (k = 0; k < psize; k++) VG_A32(dst + k * 4, vg_fbits(comp(vp, c->va.type, k, 0)));
        if (col) {
            UBYTE *q = dst + l->off[VG_A_COLOR];
            if (raw) { q[0] = cp[0]; q[1] = cp[1]; q[2] = cp[2]; q[3] = cp[3]; }
            else {
                float f[4];
                f[3] = 1.0f;
                for (k = 0; k < c->ca.size && k < 4; k++) f[k] = comp(cp, ctype, k, 1);
                VG_A32(q, rgba_word(f));
            }
            cp += cs;
        }
        if (nrm) {
            UBYTE *q = dst + l->off[VG_A_NORMAL];
            for (k = 0; k < 3; k++) VG_A32(q + k * 4, vg_fbits(comp(np, c->na.type, k, 1)));
            np += ns;
        }
        for (u = 0; u < MGL_TEX_UNITS; u++)
            if (tcn[u]) {
                const UBYTE *tp = c->ta[u].ptr + (first + i) * ts[u];
                UBYTE *q = dst + l->off[VG_A_TC0 + u];
                if (c->ta[u].type == GL_FLOAT) for (k = 0; k < tcn[u]; k++) VG_A32(q + k * 4, ((const ULONG *)tp)[k]);
                else for (k = 0; k < tcn[u]; k++) VG_A32(q + k * 4, vg_fbits(comp(tp, c->ta[u].type, k, 0)));
            }
    }
}

void mgl_vg_draw_arrays(GLcontext c, GLenum mode, GLint first, GLsizei count)
{
    struct mgl_vg *v = c->vg;
    struct vg_layout *l;
    int swz;
    ULONG base, n = (ULONG)count;
    if (n > VG_IM_MAX && (mode == GL_QUADS || mode == GL_QUAD_STRIP)) {
        /* in parts whose triangles' 16-bit indices fit; a strip's parts share two */
        ULONG step = VG_IM_MAX, back = mode == GL_QUAD_STRIP ? 2 : 0;
        while (n > step) {
            mgl_vg_draw_arrays(c, mode, first, (GLsizei)step);
            first += (GLint)(step - back);
            n -= step - back;
        }
    }
    l = array_layout(c, &swz);
    vg_need(c, (n + 1) * l->stride, indices_for(c, mode, n));
    l = array_layout(c, &swz);          /* the same, if the send made it again */
    base = align(v, l->stride);
    copy_range(c, l, (ULONG)first, n, v->va + v->nva);
    v->nva += n * l->stride;
    emit(c, mode, base, n, l, swz, 0);
}

void mgl_vg_draw_elements(GLcontext c, GLenum mode, GLsizei count, GLenum type, const GLvoid *indices)
{
    struct mgl_vg *v = c->vg;
    struct vg_layout *l;
    ULONG n = (ULONG)count, lo = 0xFFFFFFFFUL, hi = 0, k, sub, from, range, need, base, ni;
    LONG bias;
    int swz, locked;
    UWORD *ix;
    for (k = 0; k < n; k++) {
        ULONG i = type == GL_UNSIGNED_BYTE ? ((const UBYTE *)indices)[k]
                : type == GL_UNSIGNED_SHORT ? ((const UWORD *)indices)[k] : ((const ULONG *)indices)[k];
        if (i < lo) lo = i;
        if (i > hi) hi = i;
    }
    l = array_layout(c, &swz);
    locked = c->lock_count > 0 && lo >= (ULONG)c->lock_first && hi < (ULONG)c->lock_first + (ULONG)c->lock_count;
    from = locked ? (ULONG)c->lock_first : lo;
    range = locked ? (ULONG)c->lock_count : hi - lo + 1;
    ni = indices_for(c, mode, n);
    if (!ni) ni = n;
    need = range * l->stride;           /* room for the range even when locked: a send makes it go again */
    vg_need(c, need + l->stride, ni + n);
    l = array_layout(c, &swz);
    if (locked) {
        if (!v->lk_valid || v->lk_lay != l || v->lk_seq != v->seq) {
            v->lk_base = align(v, l->stride);
            copy_range(c, l, from, range, v->va + v->nva);
            v->nva += range * l->stride;
            v->lk_valid = 1; v->lk_lay = l; v->lk_seq = v->seq;
        }
        base = v->lk_base;
    } else {
        base = align(v, l->stride);
        copy_range(c, l, from, range, v->va + v->nva);
        v->nva += range * l->stride;
    }
    /* the indices, less `sub`, as 16 bits: from the range's first, so they
     * fit; the vertex of index i is at base + (i - from) */
    sub = hi - from < 65536 ? from : lo;
    bias = (LONG)base + (LONG)sub - (LONG)from;
    /* through a scratch list in the index arena's spare end: emit expands it */
    ix = v->ia + v->cia - n;
    for (k = 0; k < n; k++) {
        ULONG i = type == GL_UNSIGNED_BYTE ? ((const UBYTE *)indices)[k]
                : type == GL_UNSIGNED_SHORT ? ((const UWORD *)indices)[k] : ((const ULONG *)indices)[k];
        ix[k] = (UWORD)(i - sub);
    }
    emit(c, mode, (ULONG)bias, n, l, swz, ix);
}

void mgl_vg_lock(GLcontext c)
{
    c->vg->lk_valid = 0;
}

/* ---- clears GL limits (the scissor box, the colour mask) ----------------------------------------- */

/* A quad over the window at the clear depth, drawn with the clear colour
 * and nothing else of GL's state but the scissor and the colour mask:
 * virgl's CLEAR is the whole surface (OpenGL 1.1, 4.2.3). */
void vg_clear_quad(GLcontext c, GLbitfield mask)
{
    ULONG en = c->en, cmask = c->cmask, mvpd = c->mvp_dirty;
    GLenum df = c->depth_func, sm = c->shade_model, pf = c->poly_front, pb = c->poly_back;
    float col[4], mvp[16], z = (float)(c->clear_depth * 2.0 - 1.0);
    GLint vx = c->vp_x, vy = c->vp_y;
    GLsizei vw = c->vp_w, vh = c->vp_h;
    double dn = c->depth_near, dfar = c->depth_far;
    int i;
    for (i = 0; i < 4; i++) col[i] = c->color[i];
    for (i = 0; i < 16; i++) mvp[i] = c->mvp[i];
    c->en &= EN_SCISSOR;
    if ((mask & GL_DEPTH_BUFFER_BIT) && c->depth_mask) { c->en |= EN_DEPTH; c->depth_func = GL_ALWAYS; }
    if (!(mask & GL_COLOR_BUFFER_BIT)) c->cmask = 0;
    for (i = 0; i < 4; i++) c->color[i] = c->clear_color[i];
    c->shade_model = GL_SMOOTH;
    c->poly_front = c->poly_back = GL_FILL;
    mgl_mat_identity(c->mvp);
    c->mvp_dirty = 0;
    c->vp_x = 0; c->vp_y = 0; c->vp_w = c->width; c->vp_h = c->height;
    c->depth_near = 0.0; c->depth_far = 1.0;
    c->prim = GL_QUADS;
    mgl_vg_begin(c);
    mgl_vg_vertex(c, -1.0f, -1.0f, z, 1.0f);
    mgl_vg_vertex(c, 1.0f, -1.0f, z, 1.0f);
    mgl_vg_vertex(c, 1.0f, 1.0f, z, 1.0f);
    mgl_vg_vertex(c, -1.0f, 1.0f, z, 1.0f);
    mgl_vg_end(c);
    c->prim = PRIM_NONE;
    c->en = en; c->cmask = cmask; c->depth_func = df; c->shade_model = sm; c->poly_front = pf; c->poly_back = pb;
    for (i = 0; i < 4; i++) c->color[i] = col[i];
    for (i = 0; i < 16; i++) c->mvp[i] = mvp[i];
    c->mvp_dirty = (int)mvpd;
    c->vp_x = vx; c->vp_y = vy; c->vp_w = vw; c->vp_h = vh;
    c->depth_near = dn; c->depth_far = dfar;
}
