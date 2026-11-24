/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * The shared pipe's state objects (stage M4 of docs/design/OpenGPU-Shared-Pipe-Plan.md): from minigl.library's
 * mgl_vg_state.c (the object caches, binding only what changed, constants sent only when they change, the viewport and
 * the scissor) and mgl_vg_tex.c (the sampler states).
 *
 * A front end turns its API's state into keys (pipe.h: Gallium's numbers, floats as bits); the pipe finds the virgl
 * object made for a key, or makes it, in a small table per kind, and binds it only when it changed. Objects live as
 * long as the context; a full blend, DSA or rasterizer table lets its oldest probe go (destroyed on the host).
 * The objects' handles are the context's one namespace: a front end's own objects (surfaces, sampler views, shaders
 * until M5) take theirs from obj_new. */
#include "pipe_int.h"

/* A long from memory, through a data register. Comparing two memory longs makes GCC emit CMPM.L, which AC090's JIT
 * does not translate, so the compare would run in the interpreter (MiniGL's GLQuake ranking, 10 Nov: 24% of the
 * emulation thread in this loop). A data register makes it CMP.L. */
static ULONG meml(const ULONG *p)
{
#if defined(__mc68000__) && !defined(VG_HOST)
    ULONG v;
    __asm__ __volatile__("move.l %1,%0" : "=d"(v) : "m"(*p));
    return v;
#else
    return *p;
#endif
}

ULONG pipe_m_obj_new(OGPUPipeCtx *x)
{
    x->objects++;
    return ++x->next_obj;
}

ULONG pipe_m_cso_handle(const OGPUPipeCSO *o) { return o ? o->handle : 0; }

void pipe_m_state_reset(OGPUPipeCtx *x)
{
    int i;
    if (!x) return;
    x->b_blend = x->b_dsa = x->b_rs = x->b_ve = 0;
    x->b_vs = x->b_fs = 0;
    x->vp_known = x->sc_known = 0;
    x->nconst[0] = x->nconst[1] = 0;
    for (i = 0; i < PIPE_UNITS; i++) { x->u_res[i] = 0; x->u_samp[i] = 0; }   /* version 7's units and frame state */
    pipe_m_begin_frame(x);
}

/* ---- the caches of blend, DSA and rasterizer objects ----------------------------------------------------------- */

/* The object made for key (n words), or 0 with *slot where to keep a new one (-1 - slot: that one goes). */
static OGPUPipeCSO *cached(struct pipe_cache *k, const ULONG *key, int n, int *slot)
{
    ULONG h = key[0] ^ (key[0] >> 11) ^ (n > 1 ? key[1] * 7 : 0) ^ (n > 2 ? key[2] * 13 : 0);
    int i, s, j;
    for (i = 0; i < 4; i++) {
        s = (int)((h + (ULONG)i) & (PIPE_CACHE - 1));
        if (!k->cso[s].handle) { *slot = s; return 0; }
        for (j = 0; j < n && meml(&k->key[s][j]) == meml(&key[j]); j++) ;
        if (j == n) return &k->cso[s];
    }
    *slot = -1 - (int)(h & (PIPE_CACHE - 1));    /* full: that one goes */
    return 0;
}

/* A new object for key in the cache: its create command's room (words after the header), the handle written. */
static ULONG *make(OGPUPipeCtx *x, struct pipe_cache *k, const ULONG *key, int n, int slot, ULONG obj, ULONG words,
                   OGPUPipeCSO **out)
{
    ULONG *p, h;
    int i;
    if (slot < 0) {                                 /* the old object at -1 - slot is destroyed */
        slot = -1 - slot;
        if ((p = pipe_cmd(x, VGC_DESTROY_WORDS)) != 0) vgc_destroy(p, obj, k->cso[slot].handle);
    }
    h = pipe_m_obj_new(x);
    k->cso[slot].handle = h;
    for (i = 0; i < n; i++) k->key[slot][i] = key[i];
    *out = &k->cso[slot];
    p = pipe_cmd(x, words + 1);
    if (p) { VG_W(p, VG_CMD0(VG_CCMD_CREATE_OBJECT, obj, words)); VG_W(p, h); }
    return p;
}

/* Version 5: Warp3D's logic op. MiniGL's keys are these with logic 0 (the same objects, the same hash). */
OGPUPipeCSO *pipe_m_blend_create2(OGPUPipeCtx *x, const struct OGPUBlendKey2 *b)
{
    OGPUPipeCSO *o;
    ULONG key[2], *p;
    int slot, i;
    key[0] = b->rt0; key[1] = b->logic;
    if ((o = cached(&x->c_blend, key, 2, &slot)) != 0) return o;
    p = make(x, &x->c_blend, key, 2, slot, VG_OBJ_BLEND, 11, &o);
    if (p) {
        VG_W(p, b->logic ? 2 : 0);                  /* S0: the logic op on (no independent targets, dither off) */
        VG_W(p, b->logic & 15);                     /* S1: which */
        VG_W(p, b->rt0);
        for (i = 1; i < 8; i++) VG_W(p, 0);
    }
    return o;
}

OGPUPipeCSO *pipe_m_blend_create(OGPUPipeCtx *x, const struct OGPUBlendKey *b)
{
    struct OGPUBlendKey2 k;
    k.rt0 = b->rt0; k.logic = 0;
    return pipe_m_blend_create2(x, &k);
}

/* Version 5: Warp3D's stencil, the same word for both faces. */
OGPUPipeCSO *pipe_m_dsa_create2(OGPUPipeCtx *x, const struct OGPUDsaKey2 *d)
{
    OGPUPipeCSO *o;
    ULONG key[3], *p;
    int slot;
    key[0] = d->s0; key[1] = d->alpha_ref; key[2] = d->stencil;
    if ((o = cached(&x->c_dsa, key, 3, &slot)) != 0) return o;
    p = make(x, &x->c_dsa, key, 3, slot, VG_OBJ_DSA, 5, &o);
    if (p) { VG_W(p, d->s0); VG_W(p, d->stencil); VG_W(p, d->stencil); VG_W(p, d->alpha_ref); }
    return o;
}

OGPUPipeCSO *pipe_m_dsa_create(OGPUPipeCtx *x, const struct OGPUDsaKey *d)
{
    struct OGPUDsaKey2 k;
    k.s0 = d->s0; k.alpha_ref = d->alpha_ref; k.stencil = 0;
    return pipe_m_dsa_create2(x, &k);
}

OGPUPipeCSO *pipe_m_raster_create(OGPUPipeCtx *x, const struct OGPURasterKey *r)
{
    OGPUPipeCSO *o;
    ULONG key[5], *p;
    int slot;
    key[0] = r->s0; key[1] = r->point_size; key[2] = r->line_width; key[3] = r->offset_units; key[4] = r->offset_scale;
    if ((o = cached(&x->c_rs, key, 5, &slot)) != 0) return o;
    p = make(x, &x->c_rs, key, 5, slot, VG_OBJ_RASTERIZER, 9, &o);
    if (p) {
        VG_W(p, r->s0); VG_W(p, r->point_size); VG_W(p, 0); VG_W(p, 0); VG_W(p, r->line_width);
        VG_W(p, r->offset_units); VG_W(p, r->offset_scale); VG_W(p, 0);
    }
    return o;
}

/* ---- samplers and layouts: small tables, the objects stay -------------------------------------------------------- */

/* Version 5: with Warp3D's border colour (MiniGL's: 0). */
OGPUPipeCSO *pipe_m_sampler_create2(OGPUPipeCtx *x, const struct OGPUSamplerKey2 *s)
{
    ULONG h, *p;
    int i, j, slot;
    for (i = 0; i < x->nsamp; i++) {
        if (x->samp_key[i][0] != s->s0 || x->samp_key[i][1] != s->levels) continue;
        for (j = 0; j < 4 && x->samp_key[i][2 + j] == s->border[j]; j++) ;
        if (j == 4) return &x->samp[i];
    }
    if (x->nsamp < PIPE_SAMPLERS) slot = x->nsamp++;
    else slot = 0;                                  /* full: the first slot is used again (its object stays) */
    h = pipe_m_obj_new(x);
    p = pipe_cmd(x, 10);
    if (p) {
        VG_W(p, VG_CMD0(VG_CCMD_CREATE_OBJECT, VG_OBJ_SAMPLER_STATE, 9));
        VG_W(p, h); VG_W(p, s->s0);
        VG_W(p, vgc_fbits(0.0f)); VG_W(p, vgc_fbits(0.0f)); VG_W(p, vgc_fbits((float)(s->levels - 1)));
        for (j = 0; j < 4; j++) VG_W(p, s->border[j]);
    }
    x->samp_key[slot][0] = s->s0; x->samp_key[slot][1] = s->levels;
    for (j = 0; j < 4; j++) x->samp_key[slot][2 + j] = s->border[j];
    x->samp[slot].handle = h;
    return &x->samp[slot];
}

OGPUPipeCSO *pipe_m_sampler_create(OGPUPipeCtx *x, const struct OGPUSamplerKey *s)
{
    struct OGPUSamplerKey2 k;
    k.s0 = s->s0; k.levels = s->levels; k.border[0] = k.border[1] = k.border[2] = k.border[3] = 0;
    return pipe_m_sampler_create2(x, &k);
}

OGPUPipeCSO *pipe_m_layout_create(OGPUPipeCtx *x, const struct OGPULayout *l)
{
    ULONG h, *p, n = l->n > OGPU_LAYOUT_MAX ? OGPU_LAYOUT_MAX : l->n, i;
    int slot;
    for (i = 0; i < (ULONG)x->nlay; i++) if (x->lay_key[i] == l->key) return &x->lay[i];
    if (x->nlay == PIPE_LAYOUTS) x->nlay = 0;     /* rare: the table starts again (the objects stay) */
    slot = x->nlay++;
    h = pipe_m_obj_new(x);
    x->lay_key[slot] = l->key;
    x->lay[slot].handle = h;
    p = pipe_cmd(x, n * 4 + 2);
    if (p) {
        VG_W(p, VG_CMD0(VG_CCMD_CREATE_OBJECT, VG_OBJ_VERTEX_ELEMENTS, n * 4 + 1));
        VG_W(p, h);
        for (i = 0; i < n; i++) { VG_W(p, l->e[i].offset); VG_W(p, 0); VG_W(p, 0); VG_W(p, l->e[i].format); }
    }
    return &x->lay[slot];
}

void pipe_m_cso_release(OGPUPipeCtx *x, OGPUPipeCSO *o) { (void)x; (void)o; }   /* objects live with the context */

/* ---- binding: only what changed ---------------------------------------------------------------------------------- */

static void bind(OGPUPipeCtx *x, ULONG obj, ULONG *bound, const OGPUPipeCSO *o)
{
    ULONG *p;
    if (!o || o->handle == *bound) return;
    if ((p = pipe_cmd(x, VGC_BIND_WORDS)) != 0) vgc_bind(p, obj, o->handle);
    *bound = o->handle;
}

void pipe_m_bind_blend(OGPUPipeCtx *x, OGPUPipeCSO *o) { bind(x, VG_OBJ_BLEND, &x->b_blend, o); }
void pipe_m_bind_dsa(OGPUPipeCtx *x, OGPUPipeCSO *o) { bind(x, VG_OBJ_DSA, &x->b_dsa, o); }
void pipe_m_bind_raster(OGPUPipeCtx *x, OGPUPipeCSO *o) { bind(x, VG_OBJ_RASTERIZER, &x->b_rs, o); }
void pipe_m_bind_layout(OGPUPipeCtx *x, OGPUPipeCSO *o) { bind(x, VG_OBJ_VERTEX_ELEMENTS, &x->b_ve, o); }

/* Compared as bits, as the other keys are: no float arithmetic in the pipe (a soft-float build has no compare to call). */
void pipe_m_set_viewport(OGPUPipeCtx *x, const float v6[6])
{
    const ULONG *b = (const ULONG *)v6;
    ULONG *p;
    int i, same = x->vp_known;
    for (i = 0; i < 6; i++) if (meml(b + i) != x->vp[i]) same = 0;
    if (same) return;
    if ((p = pipe_cmd(x, VGC_VIEWPORT_WORDS)) != 0) vgc_viewport(p, v6);
    for (i = 0; i < 6; i++) x->vp[i] = b[i];
    x->vp_known = 1;
}

/* b: the box (top-left origin); 0 sends nothing: the rasterizer's scissor bit is what turns it off. */
void pipe_m_set_scissor(OGPUPipeCtx *x, const struct OGPUBox *b)
{
    ULONG *p;
    LONG sc[4];
    int i;
    if (!b) return;
    sc[0] = b->x; sc[1] = b->y; sc[2] = b->x + (LONG)b->w; sc[3] = b->y + (LONG)b->h;
    if (x->sc_known && sc[0] == x->sc[0] && sc[1] == x->sc[1] && sc[2] == x->sc[2] && sc[3] == x->sc[3]) return;
    if ((p = pipe_cmd(x, VGC_SCISSOR_WORDS)) != 0) vgc_scissor(p, (int)sc[0], (int)sc[1], (int)sc[2], (int)sc[3]);
    for (i = 0; i < 4; i++) x->sc[i] = sc[i];
    x->sc_known = 1;
}

void pipe_m_set_constants(OGPUPipeCtx *x, ULONG stage, const float *v, ULONG vec4s)
{
    const ULONG *a = (const ULONG *)v;
    ULONG *b, *p, i;
    if (stage > OGPU_STAGE_FRAGMENT || vec4s > OGPU_CONST_MAX) return;
    b = x->consts[stage];
    if (x->nconst[stage] == vec4s) {
        for (i = 0; i < vec4s * 4; i++) if (meml(a + i) != meml(b + i)) break;
        if (i == vec4s * 4) return;
    }
    for (i = 0; i < vec4s * 4; i++) b[i] = a[i];
    x->nconst[stage] = vec4s;
    if ((p = pipe_cmd(x, VGC_CONSTANTS_WORDS(vec4s))) != 0) vgc_constants(p, (int)stage, a, (int)vec4s);
}
