/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * The shared pipe's frame (stage M7 of docs/design/OpenGPU-Shared-Pipe-Plan.md, pipe version 7): the commands
 * Warp3D.library (w3d_vg.c) and minigl.library's own renderer (mgl_vg_enc.c, mgl_vg_pipe.c, mgl_vg_tex.c) wrote by
 * hand until now, word for word as they wrote them (tests/pipe/test_draw.sh):
 *   surfaces and sampler views: the context's objects for a resource, made when it is first bound (and again for a
 *     new version of it), destroyed with it;
 *   the framebuffer, sent at every bind_framebuffer (the front ends call it when their target changed);
 *   the texture units: bind_texture records them; the next draw sends the views and the sampler states of units 0
 *     to the highest one ever named, each command only when it changed;
 *   the vertex and index buffers, sent at a draw when they changed; the draw;
 *   the stencil reference, the blend colour and the polygon stipple, sent only when they changed;
 *   CLEAR, and BLIT (copy).
 * Compared as bits, never as floats: no float arithmetic in the pipe. */
#include "pipe_int.h"

static ULONG handle_of(const OGPUPipeRes *r) { return r && r->ver ? r->ver->handle : 0; }

/* ---- the context's objects for a resource ------------------------------------------------------------------------ */

static void destroy(OGPUPipeCtx *x, ULONG obj, ULONG h)
{
    ULONG *p;
    if (h && (p = pipe_cmd(x, VGC_DESTROY_WORDS)) != 0) vgc_destroy(p, obj, h);
}

/* Its surface (0 when there is none to be had): made for its current version. */
static ULONG surface(OGPUPipeCtx *x, OGPUPipeRes *r)
{
    ULONG h = handle_of(r), *p;
    if (!h) return 0;
    if (r->surf && r->obj_ctx == x && r->surf_of == h) return r->surf;
    if (r->surf && r->obj_ctx == x) destroy(x, VG_OBJ_SURFACE, r->surf);
    r->obj_ctx = x;
    r->surf = pipe_m_obj_new(x);
    r->surf_of = h;
    if ((p = pipe_cmd(x, 6)) != 0) {
        VG_W(p, VG_CMD0(VG_CCMD_CREATE_OBJECT, VG_OBJ_SURFACE, 5));
        VG_W(p, r->surf); VG_W(p, h); VG_W(p, r->desc.format);
        VG_W(p, 0);                                     /* level 0 */
        VG_W(p, 0);                                     /* layers 0 .. 0 */
    }
    return r->surf;
}

/* Its sampler view, all its levels, R, G, B, A as they are. */
static ULONG view(OGPUPipeCtx *x, OGPUPipeRes *r)
{
    ULONG h = handle_of(r), *p, fmt, levels;
    if (!h) return 0;
    if (r->view && r->obj_ctx == x && r->view_of == h) return r->view;
    if (r->view && r->obj_ctx == x) destroy(x, VG_OBJ_SAMPLER_VIEW, r->view);
    r->obj_ctx = x;
    r->view = pipe_m_obj_new(x);
    r->view_of = h;
    fmt = r->desc.format;
    if (x->caps_bits & (1UL << 1)) fmt |= r->desc.target << 24;     /* VIRGL_CAP_TEXTURE_VIEW */
    levels = r->desc.levels ? r->desc.levels : 1;
    if ((p = pipe_cmd(x, 7)) != 0) {
        VG_W(p, VG_CMD0(VG_CCMD_CREATE_OBJECT, VG_OBJ_SAMPLER_VIEW, 6));
        VG_W(p, r->view); VG_W(p, h); VG_W(p, fmt);
        VG_W(p, 0);                                     /* layers 0 .. 0 */
        VG_W(p, (levels - 1) << 8);                     /* levels 0 .. levels - 1 */
        VG_W(p, 0 | 1 << 3 | 2 << 6 | 3 << 9);
    }
    return r->view;
}

/* r's last release (pipe_res.c), in x: its objects go in x's stream, and no unit of x holds it any more. Objects
 * another context made (one namespace a context) are that context's to end with it: never reached through r, which
 * may outlive it. */
void pipe_frame_res_gone(OGPUPipeCtx *x, OGPUPipeRes *r)
{
    ULONG u;
    if (!x || !r || r->obj_ctx != x) return;
    destroy(x, VG_OBJ_SURFACE, r->surf);
    destroy(x, VG_OBJ_SAMPLER_VIEW, r->view);
    for (u = 0; u < PIPE_UNITS; u++) if (x->u_res[u] == r) x->u_res[u] = 0;
    r->surf = r->view = 0;
    r->obj_ctx = 0;
}

/* ---- binding ----------------------------------------------------------------------------------------------------- */

void pipe_m_begin_frame(OGPUPipeCtx *x)
{
    if (!x) return;
    x->s_views = x->s_samps = 0;
    x->s_vb_known = x->s_ib_known = 0;
    x->s_sref_known = x->s_bcol_known = x->s_stip_known = 0;
}

void pipe_m_bind_framebuffer(OGPUPipeCtx *x, OGPUPipeRes *colour, OGPUPipeRes *depth)
{
    ULONG c, z, *p;
    if (!x) return;
    c = surface(x, colour);
    z = surface(x, depth);
    if ((p = pipe_cmd(x, VGC_FRAMEBUFFER_WORDS)) != 0) vgc_framebuffer(p, z, c);
}

void pipe_m_bind_texture(OGPUPipeCtx *x, ULONG unit, OGPUPipeRes *r, OGPUPipeCSO *sampler)
{
    if (!x || unit >= PIPE_UNITS) return;
    x->u_res[unit] = r;
    x->u_samp[unit] = r ? pipe_m_cso_handle(sampler) : 0;
    if (unit >= x->nunits) x->nunits = unit + 1;
}

void pipe_m_set_stencil_ref(OGPUPipeCtx *x, ULONG front, ULONG back)
{
    ULONG w = (front & 255) | (back & 255) << 8, *p;
    if (!x || (x->s_sref_known && x->s_sref == w)) return;
    if ((p = pipe_cmd(x, 2)) != 0) { VG_W(p, VG_CMD0(VG_CCMD_SET_STENCIL_REF, 0, 1)); VG_W(p, w); }
    x->s_sref = w;
    x->s_sref_known = 1;
}

void pipe_m_set_blend_colour(OGPUPipeCtx *x, const float rgba[4])
{
    const ULONG *b = (const ULONG *)rgba;
    ULONG *p;
    int i;
    if (!x || !rgba) return;
    if (x->s_bcol_known && b[0] == x->s_bcol[0] && b[1] == x->s_bcol[1] && b[2] == x->s_bcol[2] && b[3] == x->s_bcol[3])
        return;
    if ((p = pipe_cmd(x, 5)) != 0) {
        VG_W(p, VG_CMD0(VG_CCMD_SET_BLEND_COLOR, 0, 4));
        for (i = 0; i < 4; i++) VG_W(p, b[i]);
    }
    for (i = 0; i < 4; i++) x->s_bcol[i] = b[i];
    x->s_bcol_known = 1;
}

void pipe_m_set_stipple(OGPUPipeCtx *x, const ULONG rows[32])
{
    ULONG *p;
    int i;
    if (!x || !rows) return;
    if (x->s_stip_known) {
        for (i = 0; i < 32 && rows[i] == x->s_stip[i]; i++) ;
        if (i == 32) return;
    }
    if ((p = pipe_cmd(x, 33)) != 0) {
        VG_W(p, VG_CMD0(VG_CCMD_SET_POLYGON_STIPPLE, 0, 32));
        for (i = 0; i < 32; i++) VG_W(p, rows[i]);
    }
    for (i = 0; i < 32; i++) x->s_stip[i] = rows[i];
    x->s_stip_known = 1;
}

/* ---- draws ------------------------------------------------------------------------------------------------------- */

/* The units' views and sampler states, each command only when it changed. */
static void units(OGPUPipeCtx *x)
{
    ULONG v[PIPE_UNITS], s[PIPE_UNITS], n = x->nunits, u, *p;
    int vch = !x->s_views, sch = !x->s_samps;
    if (!n) return;
    for (u = 0; u < n; u++) {
        v[u] = x->u_res[u] ? view(x, x->u_res[u]) : 0;
        s[u] = x->u_res[u] ? x->u_samp[u] : 0;
        if (v[u] != x->s_view[u]) vch = 1;
        if (s[u] != x->s_samp[u]) sch = 1;
    }
    if (vch && (p = pipe_cmd(x, VGC_SAMPLERS_WORDS(n))) != 0) {
        vgc_sampler_views(p, (int)n, v);
        for (u = 0; u < n; u++) x->s_view[u] = v[u];
        x->s_views = 1;
    }
    if (sch && (p = pipe_cmd(x, VGC_SAMPLERS_WORDS(n))) != 0) {
        vgc_sampler_states(p, (int)n, s);
        for (u = 0; u < n; u++) x->s_samp[u] = s[u];
        x->s_samps = 1;
    }
}

void pipe_m_draw_buf(OGPUPipeCtx *x, const struct OGPUDrawBuf *d)
{
    ULONG vh, vo, ih = 0, io = 0, start, *p;
    if (!x || !d || d->size < sizeof *d || !d->count) return;
    if (d->vbuf) { vh = handle_of(d->vbuf); vo = d->vb_offset; }
    else { vh = handle_of(x->vbuf); vo = x->vb_off + d->vb_offset; }
    if (!vh) return;
    if (d->indexed) {
        if (d->ibuf) { ih = handle_of(d->ibuf); io = d->ib_offset + d->first * 2; }
        else { ih = handle_of(x->ibuf); io = x->ib_base + d->ib_offset + d->first * 2; }
        if (!ih) return;
    }
    units(x);
    if (!x->s_vb_known || x->s_vb[0] != vh || x->s_vb[1] != vo || x->s_vb[2] != d->stride) {
        if ((p = pipe_cmd(x, VGC_VERTEX_BUFFER_WORDS)) != 0) vgc_vertex_buffer(p, d->stride, vo, vh);
        x->s_vb[0] = vh; x->s_vb[1] = vo; x->s_vb[2] = d->stride;
        x->s_vb_known = 1;
    }
    start = d->first;
    if (d->indexed) {
        /* virglrenderer draws indexed from the index buffer's offset: its DRAW_VBO start is not read */
        if (!x->s_ib_known || x->s_ib[0] != ih || x->s_ib[1] != io) {
            if ((p = pipe_cmd(x, VGC_INDEX_BUFFER_WORDS)) != 0) vgc_index_buffer(p, ih, 2, io);
            x->s_ib[0] = ih; x->s_ib[1] = io;
            x->s_ib_known = 1;
        }
        start = 0;
    }
    if ((p = pipe_cmd(x, VGC_DRAW_WORDS)) != 0) vgc_draw(p, d->prim, start, d->count, d->indexed != 0, d->bias);
    x->d_stride = d->stride;
}

void pipe_m_draw(OGPUPipeCtx *x, const struct OGPUDrawInfo *i)
{
    struct OGPUDrawBuf d;
    if (!x || !i) return;
    d.size = sizeof d;
    d.prim = i->prim; d.first = i->first; d.count = i->count; d.indexed = i->indexed; d.bias = i->bias;
    d.stride = x->d_stride;
    d.vbuf = d.ibuf = 0;
    d.vb_offset = d.ib_offset = 0;
    pipe_m_draw_buf(x, &d);
}

void pipe_m_clear(OGPUPipeCtx *x, ULONG buffers, const float rgba[4], double depth, ULONG stencil)
{
    static const float none[4] = { 0, 0, 0, 0 };
    ULONG *p;
    if (!x) return;
    if ((p = pipe_cmd(x, VGC_CLEAR_WORDS)) != 0) vgc_clear(p, buffers, rgba ? rgba : none, depth, stencil);
}

/* One side of a BLIT: the resource, its level and format, the box. */
static ULONG *blit_side(ULONG *p, const OGPUPipeRes *r, ULONG level, const struct OGPUBox *b)
{
    VG_W(p, handle_of(r)); VG_W(p, level); VG_W(p, r->desc.format);
    VG_W(p, (ULONG)b->x); VG_W(p, (ULONG)b->y); VG_W(p, (ULONG)b->z);
    VG_W(p, b->w); VG_W(p, b->h); VG_W(p, b->d ? b->d : 1);
    return p;
}

void pipe_m_copy(OGPUPipeCtx *x, const struct OGPUCopy *c)
{
    ULONG *p;
    if (!x || !c || c->size < sizeof *c || !handle_of(c->dst) || !handle_of(c->src)) return;
    if (!(p = pipe_cmd(x, 22))) return;
    VG_W(p, VG_CMD0(VG_CCMD_BLIT, 0, 21));
    VG_W(p, (c->mask & 0xFF) | (c->filter & 1) << 8);  /* no scissor, no render condition, no alpha blend */
    VG_W(p, 0); VG_W(p, 0);                             /* the scissor's corners */
    p = blit_side(p, c->dst, c->dst_level, &c->dst_box);
    (void)blit_side(p, c->src, c->src_level, &c->src_box);
}
