/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * minigl.library's virgl route: GL's state to the host's. Each draw packs
 * the state it needs into keys of a few longwords, finds the virgl object
 * made for that key (a small table per kind, no hashing of floats: the
 * floats in a key are compared as bits) or makes it, and binds it only when
 * it changed (plan section 4). Objects live as long as the context.
 *
 * The colour resource is drawn top row first, as a window system's
 * surface in Gallium's terms (GLA's frames are too, and ACV_PRESENT takes
 * them so): the viewport turns GL's y round (OpenGL 1.1, 2.10.1), as
 * Mesa's state tracker does for such a surface. */
#include "mgl_vg.h"
#include "mgl_vg_ffkey.h"

void vg_state_reset(GLcontext c)
{
    struct mgl_vg *v = c->vg;
    int i;
    v->dirty = VG_D_ALL;
    v->stride = 0;
    for (i = 0; i < MGL_TEX_UNITS; i++) v->views[i] = v->samps[i] = 0xFFFFFFFFUL;
    v->pipe->state_reset(v->px);
}

/* ---- OpenGL's numbers to Gallium's --------------------------------------------------------- */

static ULONG func_of(GLenum f) { return (ULONG)(f - GL_NEVER) & 7; }   /* NEVER .. ALWAYS: 0 .. 7 in both */

static ULONG factor_of(GLenum f)
{
    switch (f) {
    case GL_ZERO: return VG_BF_ZERO;
    case GL_ONE: return VG_BF_ONE;
    case GL_SRC_COLOR: return VG_BF_SRC_COLOR;
    case GL_ONE_MINUS_SRC_COLOR: return VG_BF_INV_SRC_COLOR;
    case GL_DST_COLOR: return VG_BF_DST_COLOR;
    case GL_ONE_MINUS_DST_COLOR: return VG_BF_INV_DST_COLOR;
    case GL_SRC_ALPHA: return VG_BF_SRC_ALPHA;
    case GL_ONE_MINUS_SRC_ALPHA: return VG_BF_INV_SRC_ALPHA;
    case GL_DST_ALPHA: return VG_BF_DST_ALPHA;
    case GL_ONE_MINUS_DST_ALPHA: return VG_BF_INV_DST_ALPHA;
    case GL_SRC_ALPHA_SATURATE: return VG_BF_SRC_ALPHA_SATURATE;
    case GL_CONSTANT_COLOR: return VG_BF_CONST_COLOR;
    case GL_ONE_MINUS_CONSTANT_COLOR: return VG_BF_INV_CONST_COLOR;
    case GL_CONSTANT_ALPHA: return VG_BF_CONST_ALPHA;
    case GL_ONE_MINUS_CONSTANT_ALPHA: return VG_BF_INV_CONST_ALPHA;
    }
    return VG_BF_ONE;
}

static ULONG equation_of(GLenum e)
{
    switch (e) {
    case GL_FUNC_SUBTRACT: return VG_BLEND_SUBTRACT;
    case GL_FUNC_REVERSE_SUBTRACT: return VG_BLEND_REVERSE_SUBTRACT;
    case GL_MIN: return VG_BLEND_MIN;
    case GL_MAX: return VG_BLEND_MAX;
    }
    return VG_BLEND_ADD;
}

static ULONG fill_of(GLenum m) { return m == GL_LINE ? VG_POLYGON_LINE : m == GL_POINT ? VG_POLYGON_POINT : VG_POLYGON_FILL; }

static void blend(GLcontext c)
{
    struct mgl_vg *v = c->vg;
    struct OGPUBlendKey k;
    ULONG rt;
    rt = (c->en & EN_BLEND) ? 1 : 0;
    if (rt) {
        ULONG src = factor_of(c->blend_src), dst = factor_of(c->blend_dst), eq = equation_of(c->blend_eq);
        rt |= eq << 1 | src << 4 | dst << 9 | eq << 14 | src << 17 | dst << 22;
    }
    rt |= (c->cmask & 15) << 27;
    k.rt0 = rt;                         /* the pipe's object for the key, bound when it changed */
    VGP(c, v->pipe->bind_blend(v->px, v->pipe->blend_create(v->px, &k)));
}

static void dsa(GLcontext c)
{
    struct mgl_vg *v = c->vg;
    struct OGPUDsaKey k;
    ULONG s0 = 0;
    if (c->en & EN_DEPTH) s0 = 1 | (c->depth_mask ? 2 : 0) | func_of(c->depth_func) << 2;
    if (c->en & EN_ALPHA) s0 |= 1UL << 8 | func_of(c->alpha_func) << 9;
    k.s0 = s0;
    k.alpha_ref = (c->en & EN_ALPHA) ? vg_fbits(c->alpha_ref) : 0;
    VGP(c, v->pipe->bind_dsa(v->px, v->pipe->dsa_create(v->px, &k)));
}

static void rasterizer(GLcontext c, int flat_first)
{
    struct mgl_vg *v = c->vg;
    struct OGPURasterKey k;
    ULONG s0;
    float units = 0.0f, scale = 0.0f;
    int offset = 0;
    s0 = 1UL << 1;                                      /* depth clip */
    if (c->shade_model == GL_FLAT) s0 |= 1UL;
    if (flat_first) s0 |= 1UL << 4;
    if (c->en & EN_CULL)
        s0 |= (c->cull_mode == GL_FRONT ? VG_FACE_FRONT : c->cull_mode == GL_BACK ? VG_FACE_BACK : 3UL) << 8;
    s0 |= fill_of(c->poly_front) << 10 | fill_of(c->poly_back) << 12;
    if (c->en & EN_SCISSOR) s0 |= 1UL << 14;
    if (c->front_face == GL_CCW) s0 |= 1UL << 15;
    s0 |= 1UL << 16 | 1UL << 17;                        /* colours clamped (GL 1.1) */
    if (c->en & EN_OFFSET) { units = c->offset_units; scale = c->offset_factor; offset = 1; }
    else if ((c->en & EN_ZOFFSET) && c->zoffset != 0.0f) { units = c->zoffset < 0.0f ? -2.0f : 2.0f; offset = 1; }   /* spec 9.2 */
    if (offset) s0 |= 1UL << 18 | 1UL << 19 | 1UL << 20;
    s0 |= 1UL << 29 | 1UL << 30;                        /* pixel centres at .5; GL's edge rule on a top-down surface */
    k.s0 = s0; k.point_size = vg_fbits(c->point_size); k.line_width = vg_fbits(c->line_width);
    k.offset_units = vg_fbits(units); k.offset_scale = vg_fbits(scale);
    VGP(c, v->pipe->bind_raster(v->px, v->pipe->raster_create(v->px, &k)));
}

/* ---- vertex layouts ---------------------------------------------------------------------------- */

static const UBYTE af_bytes[] = { 0, 4, 8, 12, 16, 4 };
static const UBYTE af_format[] = { 0, VG_FMT_R32_FLOAT, VG_FMT_R32G32_FLOAT, VG_FMT_R32G32B32_FLOAT,
                                   VG_FMT_R32G32B32A32_FLOAT, VG_FMT_R8G8B8A8_UNORM };

struct vg_layout *vg_layout_of(GLcontext c, const UBYTE *fmt)
{
    struct mgl_vg *v = c->vg;
    struct vg_layout *l;
    struct OGPULayout pl;
    ULONG key = 0;
    int i, off = 0;
    for (i = 0; i < VG_ATTRS; i++) key |= (ULONG)fmt[i] << (i * 4);
    for (i = 0; i < v->nlay; i++) if (v->lay[i].key == key) return &v->lay[i];
    if (v->nlay == VG_LAYOUTS) v->nlay = 0;     /* rare: the table starts again (the objects stay) */
    l = &v->lay[v->nlay++];
    l->key = key;
    for (i = 0; i < VG_ATTRS; i++) { l->off[i] = (UBYTE)off; off += af_bytes[fmt[i]]; }
    l->stride = (UBYTE)off;
    pl.key = key; pl.n = 0;             /* the pipe's VERTEX_ELEMENTS for the key */
    for (i = 0; i < VG_ATTRS; i++)
        if (fmt[i]) { pl.e[pl.n].offset = l->off[i]; pl.e[pl.n].format = af_format[fmt[i]]; pl.n++; }
    VGP(c, l->cso = v->pipe->layout_create(v->px, &pl));
    return l;
}

/* ---- constants ------------------------------------------------------------------------------------ */

static void rows(float *d, const float *m)
{
    int r;
    for (r = 0; r < 4; r++) { d[r * 4] = m[r]; d[r * 4 + 1] = m[4 + r]; d[r * 4 + 2] = m[8 + r]; d[r * 4 + 3] = m[12 + r]; }
}

static void copy4(float *d, const float *s) { d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3]; }

static void send_if_changed(GLcontext c, int type, const float *k, int n)
{
    struct mgl_vg *v = c->vg;
    /* the pipe sends them when they changed */
    VGP(c, v->pipe->set_constants(v->px, type == VG_SHADER_VERTEX ? OGPU_STAGE_VERTEX : OGPU_STAGE_FRAGMENT, k, (ULONG)n));
}

/* Vertex shader constants a key needs (vec4s); its DCL says the same. */
int vg_vs_consts(ULONG key)
{
    if (key & VS_LIGHT) return VSC_LIGHT + (int)VS_NLIGHTS(key) * VSC_LIGHT_N;
    if (VS_GEN0(key) || VS_GEN1(key)) return VSC_LIGHT;
    return VSC_CONST + 1;
}

static void clear_slots(float *k, int from, int n)
{
    int i;
    for (i = from * 4; i < (from + n) * 4; i++) k[i] = 0.0f;
}

static void vs_constants(GLcontext c, ULONG key, const int *lights, int nl)
{
    float k[VSC_END * 4];
    int n = vg_vs_consts(key), i, u;
    if (c->mvp_dirty) mgl_update_mvp(c);
    rows(k + VSC_MVP * 4, c->mvp);
    copy4(k + VSC_COLOR * 4, c->color);
    if (c->nm_dirty) mgl_update_nm(c);
    rows(k + VSC_MV * 4, c->mv[c->mv_top]);     /* its fourth row is VSC_NM's place, written next */
    for (i = 0; i < 3; i++) {
        k[(VSC_NM + i) * 4] = c->nm[i * 3]; k[(VSC_NM + i) * 4 + 1] = c->nm[i * 3 + 1];
        k[(VSC_NM + i) * 4 + 2] = c->nm[i * 3 + 2]; k[(VSC_NM + i) * 4 + 3] = 0.0f;
    }
    rows(k + VSC_TEX0 * 4, c->texm[0][c->tex_top[0]]);
    rows(k + VSC_TEX1 * 4, c->texm[1][c->tex_top[1]]);
    copy4(k + VSC_TC0 * 4, c->tc[0]);
    copy4(k + VSC_TC1 * 4, c->tc[1]);
    k[VSC_NORMAL * 4] = c->normal[0]; k[VSC_NORMAL * 4 + 1] = c->normal[1]; k[VSC_NORMAL * 4 + 2] = c->normal[2]; k[VSC_NORMAL * 4 + 3] = 0.0f;
    k[VSC_CONST * 4] = 0.0f; k[VSC_CONST * 4 + 1] = 1.0f; k[VSC_CONST * 4 + 2] = 0.5f; k[VSC_CONST * 4 + 3] = 2.0f;
    /* slots the variant doesn't read stay 0, so that the current values a
     * glBegin/glEnd leaves behind (colour, coordinates, normal) don't make
     * every draw send its constants again */
    {
        int eye = (key & (VS_LIGHT | VS_FOG)) || VS_GEN0(key) >= 2 || VS_GEN1(key) >= 2;
        int nrm = (key & VS_LIGHT) || VS_GEN0(key) == 3 || VS_GEN1(key) == 3;
        if (key & VS_COL_ATTR) clear_slots(k, VSC_COLOR, 1);
        if (!eye) clear_slots(k, VSC_MV, 3);
        if (!nrm) clear_slots(k, VSC_NM, 3);
        if (!(key & VS_TC0_MAT)) clear_slots(k, VSC_TEX0, 4);
        if (!(key & VS_TC1_MAT)) clear_slots(k, VSC_TEX1, 4);
        if (!(key & VS_TC0) || (key & VS_TC0_ATTR)) clear_slots(k, VSC_TC0, 1);
        if (!(key & VS_TC1) || (key & VS_TC1_ATTR)) clear_slots(k, VSC_TC1, 1);
        if (!nrm || (key & VS_NRM_ATTR)) clear_slots(k, VSC_NORMAL, 1);
    }
    if (n > VSC_SCENE) {
        for (i = 0; i < 4; i++) k[VSC_SCENE * 4 + i] = 0.0f;
        copy4(k + VSC_MAT * 4, c->mat[0].ambient);
        copy4(k + (VSC_MAT + 1) * 4, c->mat[0].diffuse);
        copy4(k + (VSC_MAT + 2) * 4, c->mat[0].specular);
        copy4(k + (VSC_MAT + 3) * 4, c->mat[0].emission);
        k[VSC_MISC * 4] = c->mat[0].shininess; k[VSC_MISC * 4 + 1] = k[VSC_MISC * 4 + 2] = k[VSC_MISC * 4 + 3] = 0.0f;
        copy4(k + VSC_GAMB * 4, c->light_ambient);
        for (u = 0; u < 2; u++) {
            copy4(k + (VSC_GEN + u * 4) * 4, c->gen_obj[u][0]);
            copy4(k + (VSC_GEN + u * 4 + 1) * 4, c->gen_obj[u][1]);
            copy4(k + (VSC_GEN + u * 4 + 2) * 4, c->gen_eye[u][0]);
            copy4(k + (VSC_GEN + u * 4 + 3) * 4, c->gen_eye[u][1]);
        }
        for (i = 0; i < nl; i++) {
            const struct mgl_light *l = &c->light[lights[i]];
            float *d = k + (VSC_LIGHT + i * VSC_LIGHT_N) * 4;
            copy4(d, l->position);
            copy4(d + 4, l->ambient);
            copy4(d + 8, l->diffuse);
            copy4(d + 12, l->specular);
            d[16] = l->direction[0]; d[17] = l->direction[1]; d[18] = l->direction[2]; d[19] = l->spot_cos;
            if (l->position[3] != 0.0f) { d[20] = l->att[0]; d[21] = l->att[1]; d[22] = l->att[2]; }
            else { d[20] = 1.0f; d[21] = 0.0f; d[22] = 0.0f; }
            d[23] = l->spot_exp;
            d[24] = d[25] = d[26] = d[27] = 0.0f;
        }
    }
    send_if_changed(c, VG_SHADER_VERTEX, k, n);
}

static void fs_constants(GLcontext c)
{
    float k[FSC_END * 4], range = c->fog_end - c->fog_start;
    copy4(k + FSC_ENV0 * 4, c->env_color[0]);
    copy4(k + FSC_ENV1 * 4, c->env_color[1]);
    copy4(k + FSC_FOGCOL * 4, c->fog_color);
    k[FSC_FOG * 4] = c->fog_start; k[FSC_FOG * 4 + 1] = c->fog_end; k[FSC_FOG * 4 + 2] = c->fog_density;
    k[FSC_FOG * 4 + 3] = range != 0.0f ? 1.0f / range : 0.0f;
    k[FSC_CONST * 4] = 0.0f; k[FSC_CONST * 4 + 1] = 1.0f; k[FSC_CONST * 4 + 2] = 0.5f; k[FSC_CONST * 4 + 3] = 1.44269504f;
    send_if_changed(c, VG_SHADER_FRAGMENT, k, FSC_END);
}

/* ---- framebuffer, viewport, scissor -------------------------------------------------------------- */

static void viewport_scissor(GLcontext c)
{
    struct mgl_vg *v = c->vg;
    float hw = (float)c->vp_w * 0.5f, hh = (float)c->vp_h * 0.5f, vp[6];
    float n = (float)c->depth_near, f = (float)c->depth_far;
    LONG sc[4];
    vp[0] = hw; vp[1] = -hh; vp[2] = (f - n) * 0.5f;
    vp[3] = (float)c->vp_x + hw; vp[4] = (float)(c->height - c->vp_y) - hh; vp[5] = (f + n) * 0.5f;
    VGP(c, v->pipe->set_viewport(v->px, vp));       /* the pipe sends them when they changed */
    if (c->en & EN_SCISSOR) {
        struct OGPUBox b;
        sc[0] = c->sc_x; sc[2] = c->sc_x + c->sc_w;
        sc[1] = c->height - (c->sc_y + c->sc_h); sc[3] = c->height - c->sc_y;
        if (sc[0] < 0) sc[0] = 0;
        if (sc[1] < 0) sc[1] = 0;
        if (sc[2] > c->width) sc[2] = c->width;
        if (sc[3] > c->height) sc[3] = c->height;
        if (sc[2] < sc[0]) sc[2] = sc[0];
        if (sc[3] < sc[1]) sc[3] = sc[1];
        b.x = sc[0]; b.y = sc[1]; b.z = 0; b.w = (ULONG)(sc[2] - sc[0]); b.h = (ULONG)(sc[3] - sc[1]); b.d = 1;
        VGP(c, v->pipe->set_scissor(v->px, &b));
    }
}

/* Unit u's coordinate generation as a key: S and T both on, one mode. */
int vg_gen_of(GLcontext c, int u)
{
    ULONG bits = u ? (EN_GENS1 | EN_GENT1) : (EN_GENS0 | EN_GENT0);
    GLenum m;
    if ((c->en & bits) != bits) return 0;
    m = c->gen_mode[u][0];
    return m == GL_OBJECT_LINEAR ? 1 : m == GL_EYE_LINEAR ? 2 : m == GL_SPHERE_MAP ? 3 : 0;
}

/* ---- a draw's state ------------------------------------------------------------------------------- */

void vg_prepare(GLcontext c, struct vg_layout *lay, int colswz, int flat_first)
{
    struct mgl_vg *v = c->vg;
    ULONG vk = 0, fk = 0, spots = 0;
    int lights[8], nl = 0, i;
    if (v->dirty & VG_D_FB) {
        vg_set_framebuffer(c);
        v->dirty &= ~VG_D_FB;
    }
    viewport_scissor(c);
    if (!lay) return;
    blend(c);
    dsa(c);
    rasterizer(c, flat_first);
    /* the vertex shader's key */
    if ((lay->key >> (VG_A_COLOR * 4)) & 15) vk |= VS_COL_ATTR | VS_COL_SWZ_SET(colswz);
    if (c->en & EN_LIGHTING) {
        for (i = 0; i < 8; i++)
            if (c->en & (1UL << (EN_LIGHT0 + i))) {
                if (c->light[i].spot_cutoff != 180.0f) spots |= 1UL << nl;
                lights[nl++] = i;
            }
        vk |= VS_LIGHT | VS_NLIGHTS_SET(nl);
        if ((lay->key >> (VG_A_NORMAL * 4)) & 15) vk |= VS_NRM_ATTR;
        if (c->en & EN_COLORMAT) {
            int m = c->colormat_mode == GL_AMBIENT ? 1 : c->colormat_mode == GL_DIFFUSE ? 2
                  : c->colormat_mode == GL_SPECULAR ? 3 : c->colormat_mode == GL_EMISSION ? 4 : 0;
            vk |= VS_COLMAT | VS_COLMAT_MODE_SET(m);
        }
        if (c->local_viewer) vk |= VS_LOCALV;
        if (c->en & EN_NORMALIZE) vk |= VS_NORMALIZE;
    }
    if (c->en & EN_FOG) {
        vk |= VS_FOG;
        fk |= FS_FOG_SET(c->fog_mode == GL_EXP ? 2 : c->fog_mode == GL_EXP2 ? 3 : 1);
    }
    /* the texture units (OpenGL 1.1 3.7.5, ARB_multitexture) */
    {
        ULONG views[MGL_TEX_UNITS], samps[MGL_TEX_UNITS];
        int u, vch = 0, sch = 0;
        for (u = 0; u < MGL_TEX_UNITS; u++) {
            struct mgl_tex *t = mgl_tex_unit(c, u);
            views[u] = samps[u] = 0;
            if (t && (views[u] = vg_tex_view(c, t)) != 0) {
                GLenum m = c->env_mode[u];
                int e = m == GL_REPLACE ? FS_ENV_REPLACE : m == GL_DECAL ? FS_ENV_DECAL : m == GL_BLEND ? FS_ENV_BLEND
                      : m == GL_ADD ? FS_ENV_ADD : FS_ENV_MODULATE;
                int rgb = t->base != OGPU_TEXBASE_ALPHA, a = t->base == OGPU_TEXBASE_RGBA || t->base == OGPU_TEXBASE_ALPHA
                        || t->base == OGPU_TEXBASE_LUM_ALPHA || t->base == OGPU_TEXBASE_INTENSITY;
                samps[u] = vg_tex_sampler(c, t);
                fk |= u ? FS_ENV1_SET(e) | (rgb ? FS_RGB1 : 0) | (a ? FS_A1 : 0) : FS_ENV0_SET(e) | (rgb ? FS_RGB0 : 0) | (a ? FS_A0 : 0);
                vk |= u ? VS_TC1 : VS_TC0;
                if ((lay->key >> ((VG_A_TC0 + u) * 4)) & 15) vk |= u ? VS_TC1_ATTR : VS_TC0_ATTR;
                if (!c->tex_identity[u]) vk |= u ? VS_TC1_MAT : VS_TC0_MAT;
                vk |= u ? VS_GEN1_SET(vg_gen_of(c, 1)) : VS_GEN0_SET(vg_gen_of(c, 0));
            }
            if (views[u] != v->views[u]) vch = 1;
            if (samps[u] != v->samps[u]) sch = 1;
        }
        if (VS_GEN0(vk) == 3 || VS_GEN1(vk) == 3) {
            if ((lay->key >> (VG_A_NORMAL * 4)) & 15) vk |= VS_NRM_ATTR;
        }
        if (vch) { vg_sampler_views(c, MGL_TEX_UNITS, views); for (u = 0; u < MGL_TEX_UNITS; u++) v->views[u] = views[u]; }
        if (sch) { vg_sampler_states(c, MGL_TEX_UNITS, samps); for (u = 0; u < MGL_TEX_UNITS; u++) v->samps[u] = samps[u]; }
    }
    {                                   /* the pipe's one generator (stage M5): made once per key, bound when it changed */
        struct OGPUFFKey k;
        k.vs = vg_ff_vs(vk); k.fs = vg_ff_fs(fk); k.spots = spots;
        VGP(c, v->pipe->bind_shader(v->px, v->pipe->ff_shader(v->px, &k)));
    }
    /* the layout and the buffers */
    VGP(c, v->pipe->bind_layout(v->px, lay->cso));
    if (lay->stride != v->stride || (v->dirty & VG_D_VBUF)) { vg_set_vertex_buffer(c, lay->stride); v->dirty &= ~VG_D_VBUF; }
    vs_constants(c, vk, lights, nl);
    fs_constants(c);
}
