/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * minigl.library's virgl route: textures as host resources (plan 3.3).
 *
 * The library keeps every texture's levels in Fast RAM as ARGB words
 * (mgl_texture.c, the Amiga's order); a level goes to the host as
 * B8G8R8A8 with ACV_PUT, the host turning each 32-bit unit round. Only
 * what changed goes up (a box per level, mgl_vg_tex_changed).
 *
 * Changes between draws of one frame (lightmaps): GL says the earlier
 * draws see the old texels. A texture already drawn with in the stream not
 * yet sent is renamed: a new resource takes all its levels and the later
 * draws, and the old one is let go of after the frame's block. So the host
 * never writes into a texture a pending draw reads, and no frame is split
 * for it. A PUT reads the library's copy when the block runs, so a texture
 * changed again before then sends what there is first. */
#include "mgl_vg.h"

/* Levels of a complete chain from level 0 (as mgl_out.c counts them). */
static int levels_of(const struct mgl_tex *t)
{
    int n = 1, w = t->w, h = t->h;
    while (n < MGL_MAX_LEVELS) {
        w = w > 1 ? w >> 1 : 1;
        h = h > 1 ? h >> 1 : 1;
        if (!t->level[n] || t->lw[n] != w || t->lh[n] != h) break;
        n++;
        if (w == 1 && h == 1) break;
    }
    return n;
}

static struct vg_tex *tv_of(struct mgl_tex *t)
{
    if (!t->vg && (t->vg = mgl_alloc(sizeof *t->vg)) != 0) t->vg->whole = 1;
    return t->vg;
}

static void destroy_view(GLcontext c, ULONG h)
{
    ULONG *p;
    int u;
    if (!h) return;
    for (u = 0; u < MGL_TEX_UNITS; u++) if (c->vg->views[u] == h) c->vg->views[u] = 0xFFFFFFFFUL;
    p = vg_cmd(c, 2);
    if (p) { VG_W(p, VG_CMD0(VG_CCMD_DESTROY_OBJECT, VG_OBJ_SAMPLER_VIEW, 1)); VG_W(p, h); }
}

/* A new resource version: all levels go up, the old one goes after the block. */
static int new_version(GLcontext c, struct mgl_tex *t, struct vg_tex *tv, int levels)
{
    struct vg_res *r = vg_res_create(c, VG_TARGET_2D, VG_FMT_B8G8R8A8_UNORM, VG_BIND_SAMPLER_VIEW,
                                     (ULONG)t->w, (ULONG)t->h, (ULONG)levels);
    ULONG *p, fmt;
    if (!r) return 0;
    if (tv->res) { destroy_view(c, tv->view); vg_res_release(c, tv->res); }
    tv->res = r;
    tv->w = t->w; tv->h = t->h; tv->levels = levels;
    tv->whole = 1;
    tv->view = vg_new_obj(c);
    fmt = VG_FMT_B8G8R8A8_UNORM;
    if (c->vg->caps_bits & (1UL << 1)) fmt |= (ULONG)VG_TARGET_2D << 24;   /* VIRGL_CAP_TEXTURE_VIEW */
    p = vg_cmd(c, 7);
    if (p) {
        VG_W(p, VG_CMD0(VG_CCMD_CREATE_OBJECT, VG_OBJ_SAMPLER_VIEW, 6));
        VG_W(p, tv->view); VG_W(p, r->handle); VG_W(p, fmt);
        VG_W(p, 0);                                     /* layers 0 .. 0 */
        VG_W(p, (ULONG)(levels - 1) << 8);              /* levels 0 .. levels - 1 */
        VG_W(p, 0 | 1 << 3 | 2 << 6 | 3 << 9);          /* R, G, B, A as they are */
    }
    return 1;
}

/* What changed since the host's copy, up. */
static void upload(GLcontext c, struct mgl_tex *t, struct vg_tex *tv)
{
    int l;
    for (l = 0; l < tv->levels; l++) {
        int x0 = 0, y0 = 0, x1 = t->lw[l], y1 = t->lh[l];
        if (!tv->whole) {
            if (tv->dx1[l] <= tv->dx0[l] || tv->dy1[l] <= tv->dy0[l]) continue;
            x0 = tv->dx0[l]; y0 = tv->dy0[l]; x1 = tv->dx1[l]; y1 = tv->dy1[l];
        }
        vg_put(c, tv->res, (ULONG)l, (ULONG)x0, (ULONG)y0, (ULONG)(x1 - x0), (ULONG)(y1 - y0),
               t->level[l] + (ULONG)y0 * t->lw[l] + x0, (ULONG)t->lw[l] * 4,
               (ULONG)(y1 - y0 - 1) * t->lw[l] * 4 + (ULONG)(x1 - x0) * 4, VG_SW32);
        tv->dx0[l] = tv->dy0[l] = 0x7FFF; tv->dx1[l] = tv->dy1[l] = 0;
    }
    tv->whole = 0;
    tv->gen = t->generation;
    tv->put = c->vg->seq;
}

ULONG vg_tex_view(GLcontext c, struct mgl_tex *t)
{
    struct mgl_vg *v = c->vg;
    struct vg_tex *tv = tv_of(t);
    int levels;
    if (!tv) return 0;
    levels = levels_of(t);
    if (!tv->res || tv->w != t->w || tv->h != t->h || tv->levels != levels) {
        if (!new_version(c, t, tv, levels)) return 0;
    } else if (tv->gen != t->generation && tv->used == v->seq) {
        /* drawn with in the stream not yet sent: renamed */
        if (!new_version(c, t, tv, levels)) return 0;
        v->renames++;
    }
    if (tv->gen != t->generation && tv->boxgen != t->generation) tv->whole = 1;   /* changed without a box (a palette) */
    if (tv->gen != t->generation || tv->whole) upload(c, t, tv);
    tv->used = v->seq;
    return tv->view;
}

/* The sampler state for t's filters and wraps, cached by key. */
ULONG vg_tex_sampler(GLcontext c, struct mgl_tex *t)
{
    struct mgl_vg *v = c->vg;
    ULONG key[2], s0, h, *p;
    int i, slot = -1, mip = t->vg && t->vg->levels > 1 && !c->no_mip && !mgl_prefs.no_mip;
    ULONG min = t->min == GL_NEAREST || t->min == GL_NEAREST_MIPMAP_NEAREST || t->min == GL_NEAREST_MIPMAP_LINEAR ? VG_FILTER_NEAREST : VG_FILTER_LINEAR;
    ULONG mipf = !mip || t->min == GL_NEAREST || t->min == GL_LINEAR ? VG_MIP_NONE
               : t->min == GL_NEAREST_MIPMAP_NEAREST || t->min == GL_LINEAR_MIPMAP_NEAREST ? VG_MIP_NEAREST : VG_MIP_LINEAR;
    ULONG mag = t->mag == GL_NEAREST ? VG_FILTER_NEAREST : VG_FILTER_LINEAR;
    /* GL_CLAMP is clamp to edge here, as GLES hosts have no border (plan 8) */
    s0 = (t->wrap_s == GL_REPEAT ? VG_WRAP_REPEAT : VG_WRAP_CLAMP_TO_EDGE)
       | (t->wrap_t == GL_REPEAT ? VG_WRAP_REPEAT : VG_WRAP_CLAMP_TO_EDGE) << 3
       | (ULONG)VG_WRAP_CLAMP_TO_EDGE << 6 | min << 9 | mipf << 11 | mag << 13;
    key[0] = s0;
    key[1] = (ULONG)(t->vg ? t->vg->levels : 1);
    for (i = 0; i < v->nsamp; i++) if (v->samp_key[i][0] == key[0] && v->samp_key[i][1] == key[1]) return v->samp_h[i];
    if (v->nsamp < (int)(sizeof v->samp_h / sizeof v->samp_h[0])) slot = v->nsamp++;
    else slot = 0;
    h = vg_new_obj(c);
    p = vg_cmd(c, 10);
    if (p) {
        VG_W(p, VG_CMD0(VG_CCMD_CREATE_OBJECT, VG_OBJ_SAMPLER_STATE, 9));
        VG_W(p, h); VG_W(p, s0);
        VG_W(p, vg_fbits(0.0f)); VG_W(p, vg_fbits(0.0f)); VG_W(p, vg_fbits((float)(key[1] - 1)));
        VG_W(p, 0); VG_W(p, 0); VG_W(p, 0); VG_W(p, 0);
    }
    v->samp_key[slot][0] = key[0]; v->samp_key[slot][1] = key[1]; v->samp_h[slot] = h;
    return h;
}

/* Texels of level `level` changed in the box (mgl_tex_store, after). */
void mgl_vg_tex_changed(GLcontext c, struct mgl_tex *t, int level, int x, int y, int w, int h)
{
    struct vg_tex *tv = tv_of(t);
    (void)c;
    if (!tv || level < 0 || level >= MGL_MAX_LEVELS) return;
    tv->boxgen = t->generation;
    if (level >= tv->levels) { tv->whole = 1; return; }
    if (tv->dx1[level] <= tv->dx0[level]) { tv->dx0[level] = tv->dy0[level] = 0x7FFF; tv->dx1[level] = tv->dy1[level] = 0; }
    if (x < tv->dx0[level]) tv->dx0[level] = x;
    if (y < tv->dy0[level]) tv->dy0[level] = y;
    if (x + w > tv->dx1[level]) tv->dx1[level] = x + w;
    if (y + h > tv->dy1[level]) tv->dy1[level] = y + h;
}

/* Before texels change: a PUT still waiting in the block reads them when it
 * runs, so the block goes now. */
void mgl_vg_tex_before(GLcontext c, struct mgl_tex *t)
{
    if (t->vg && t->vg->put == c->vg->seq && t->vg->gen == t->generation) vg_send(c, 0);
}

void mgl_vg_tex_free(GLcontext c, struct mgl_tex *t)
{
    struct vg_tex *tv = t->vg;
    if (!tv) return;
    if (c->vg) {
        if (tv->put == c->vg->seq) vg_send(c, 0);
        if (tv->res) { destroy_view(c, tv->view); vg_res_release(c, tv->res); }
    } else mgl_free(tv->res);
    mgl_free(tv);
    t->vg = 0;
}

/* mglUnlockDisplay after mglLockBack: the program's pixels (ARGB words) back
 * into the colour buffer. */
void mgl_vg_writeback(GLcontext c, const ULONG *argb)
{
    vg_put(c, c->vg->colour, 0, 0, 0, (ULONG)c->width, (ULONG)c->height, argb, (ULONG)c->width * 4,
           (ULONG)c->width * c->height * 4, VG_SW32);
    vg_send(c, 0);                      /* argb is freed when this returns */
}
