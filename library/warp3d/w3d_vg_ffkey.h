/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * Warp3D.library's shader keys (w3d_vg.h's VK_ and FK_) as the shared pipe's one fixed-function key (pipe.h's
 * OGPU_FF_, stage M5): screen-space vertices. FK_GOURAUD and FK_LINEAR become the vertex word's colour and
 * across-the-screen bits, which the fragment side reads too. Used from stage M6, and by tests/pipe/test_ff.c. */
#ifndef W3D_VG_FFKEY_H
#define W3D_VG_FFKEY_H

static __inline__ void w3dvg_ff_key(ULONG vk, ULONG fk, struct OGPUFFKey *u)
{
    u->vs = OGPU_FF_POS_SET(1) | OGPU_FF_FOG_W;
    if (vk & VK_Z) u->vs |= OGPU_FF_SCR_Z;
    if (vk & VK_W) u->vs |= OGPU_FF_SCR_W;
    if (vk & VK_SPEC) u->vs |= OGPU_FF_SPEC;
    if (vk & VK_PERSP) u->vs |= OGPU_FF_PERSP;
    if ((vk & VK_LINEAR) || (fk & FK_LINEAR)) u->vs |= OGPU_FF_LINEAR;
    if (fk & FK_GOURAUD) u->vs |= OGPU_FF_COL_ATTR;
    u->fs = OGPU_FF_ENV0_SET(FK_ENV(fk)) | OGPU_FF_BASE_SET(FK_BASE(fk)) | OGPU_FF_FOG_SET(FK_FOG(fk))
          | OGPU_FF_CHROMA_SET(FK_CHROMA(fk));
    if (fk & FK_SPEC) u->fs |= OGPU_FF_SPEC_ADD;
    if (fk & FK_FOGEQ) u->fs |= OGPU_FF_FOG_STEP;
    u->spots = 0;
}

#endif
