/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * minigl.library's shader keys (mgl_vg.h's VS_ and FS_) as the shared pipe's one fixed-function key (pipe.h's
 * OGPU_FF_, stage M5). The fragment word is the same bits; the vertex word moves into the unified layout. VS_FOG has
 * no bit of its own there: the pipe reads fog from the fragment word (MiniGL sets both together). */
#ifndef MGL_VG_FFKEY_H
#define MGL_VG_FFKEY_H

static __inline__ ULONG vg_ff_vs(ULONG k)
{
    ULONG u = OGPU_FF_POS_SET(0) | OGPU_FF_COL_SWZ_SET(VS_COL_SWZ(k))
            | OGPU_FF_GEN0_SET(VS_GEN0(k)) | OGPU_FF_GEN1_SET(VS_GEN1(k))
            | OGPU_FF_NLIGHTS_SET(VS_NLIGHTS(k)) | OGPU_FF_COLMAT_MODE_SET(VS_COLMAT_MODE(k));
    if (k & VS_COL_ATTR) u |= OGPU_FF_COL_ATTR;
    if (k & VS_TC0) u |= OGPU_FF_TC0;
    if (k & VS_TC0_ATTR) u |= OGPU_FF_TC0_ATTR;
    if (k & VS_TC0_MAT) u |= OGPU_FF_TC0_MAT;
    if (k & VS_TC1) u |= OGPU_FF_TC1;
    if (k & VS_TC1_ATTR) u |= OGPU_FF_TC1_ATTR;
    if (k & VS_TC1_MAT) u |= OGPU_FF_TC1_MAT;
    if (k & VS_LIGHT) u |= OGPU_FF_LIGHT;
    if (k & VS_NRM_ATTR) u |= OGPU_FF_NRM_ATTR;
    if (k & VS_COLMAT) u |= OGPU_FF_COLMAT;
    if (k & VS_LOCALV) u |= OGPU_FF_LOCALV;
    if (k & VS_NORMALIZE) u |= OGPU_FF_NORMALIZE;
    if (k & VS_LIGHT_POS) u |= OGPU_FF_LIGHT_POS;
    return u;
}

static __inline__ ULONG vg_ff_fs(ULONG k) { return k; }    /* FS_ is OGPU_FF_'s fragment word, bit for bit */

#endif
