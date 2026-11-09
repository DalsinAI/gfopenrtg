/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * minigl.library's virgl route: GL's state to the host's. Each draw packs
 * the state it needs into keys of a few longwords, finds the virgl object
 * made for that key (a small direct-mapped table per kind, no hashing of
 * floats) or makes it, and binds it only when it changed (plan section 4).
 *
 * The colour resource is drawn top row first, as a window system's
 * surface in Gallium's terms (GLA's frames are too, and ACV_PRESENT takes
 * them so): the viewport turns GL's y round (OpenGL 1.1, 2.10.1). */
#include "mgl_vg.h"

void vg_state_reset(GLcontext c)
{
    struct mgl_vg *v = c->vg;
    int i;
    v->dirty = VG_D_ALL;
    v->b_blend = v->b_dsa = v->b_rs = v->b_vs = v->b_fs = v->b_ve = 0;
    v->stride = 0;
    v->nvsc = v->nfsc = 0;
    for (i = 0; i < MGL_TEX_UNITS; i++) v->views[i] = v->samps[i] = 0xFFFFFFFFUL;
}

/* The viewport (window coordinates, top row 0) and the scissor box. */
static void viewport_scissor(GLcontext c)
{
    struct mgl_vg *v = c->vg;
    float hw = (float)c->vp_w * 0.5f, hh = (float)c->vp_h * 0.5f, vp[6];
    float n = (float)c->depth_near, f = (float)c->depth_far;
    LONG sc[4];
    int i, same = 1;
    vp[0] = hw; vp[1] = -hh; vp[2] = (f - n) * 0.5f;
    vp[3] = (float)c->vp_x + hw; vp[4] = (float)(c->height - c->vp_y) - hh; vp[5] = (f + n) * 0.5f;
    for (i = 0; i < 6; i++) if (vp[i] != v->vp[i]) same = 0;
    if (!same || (v->dirty & VG_D_VIEWPORT)) {
        vg_set_viewport(c, vp);
        for (i = 0; i < 6; i++) v->vp[i] = vp[i];
        v->dirty &= ~VG_D_VIEWPORT;
    }
    if (c->en & EN_SCISSOR) {
        sc[0] = c->sc_x; sc[2] = c->sc_x + c->sc_w;
        sc[1] = c->height - (c->sc_y + c->sc_h); sc[3] = c->height - c->sc_y;
        if (sc[0] < 0) sc[0] = 0;
        if (sc[1] < 0) sc[1] = 0;
        if (sc[2] > c->width) sc[2] = c->width;
        if (sc[3] > c->height) sc[3] = c->height;
        if (sc[2] < sc[0]) sc[2] = sc[0];
        if (sc[3] < sc[1]) sc[3] = sc[1];
        if ((v->dirty & VG_D_SCISSOR) || sc[0] != v->sc[0] || sc[1] != v->sc[1] || sc[2] != v->sc[2] || sc[3] != v->sc[3]) {
            vg_set_scissor(c, (int)sc[0], (int)sc[1], (int)sc[2], (int)sc[3]);
            for (i = 0; i < 4; i++) v->sc[i] = sc[i];
            v->dirty &= ~VG_D_SCISSOR;
        }
    }
}

void vg_prepare(GLcontext c, struct vg_layout *lay, int lines)
{
    struct mgl_vg *v = c->vg;
    (void)lines;
    if (v->dirty & VG_D_FB) {
        vg_set_framebuffer(c);
        v->dirty &= ~VG_D_FB;
    }
    viewport_scissor(c);
    if (!lay) return;
}
