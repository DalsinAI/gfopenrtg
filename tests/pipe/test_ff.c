/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * Stage M5's differential test (docs/design/OpenGPU-Shared-Pipe-Plan.md): the shared pipe's one fixed-function
 * generator (library/pipe/pipe_ff.c) against the two it replaces, minigl.library's mgl_vg_shader.c and
 * Warp3D.library's w3d_vg_shader.c. For every key enumerated, the text must be the same, byte for byte: then the
 * host draws the same, and no draw is needed to show it.
 *   MiniGL: every fragment key; vertex keys exhaustively over the features that change the text's shape (colour
 *   order, units, matrices, texgen modes, lights 0 to 8 with every spot mask for up to 3, colour material modes,
 *   local viewer, normalise, fog), and N random keys more (argument 1, default 200000).
 *   Warp3D: every vertex key (5 bits) and every fragment key (16 bits).
 * The MiniGL and Warp3D keys go through their front ends' own conversions (mgl_vg_ffkey.h, w3d_vg_ffkey.h). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../library/minigl/mgl_vg.h"
#include "../../library/minigl/mgl_vg_ffkey.h"

/* Warp3D's keys and generator */
#include "test_ff_w3d.h"

ULONG pipe_ff_text(const struct OGPUFFKey *k, int fragment, char *buf, ULONG cap);

/* mgl_vg_state.c's, which mgl_vg_shader.c calls (that file brings the whole state code with it) */
int vg_vs_consts(ULONG key)
{
    if (key & VS_LIGHT) return VSC_LIGHT + (int)VS_NLIGHTS(key) * VSC_LIGHT_N;
    if (VS_GEN0(key) || VS_GEN1(key)) return VSC_LIGHT;
    return VSC_CONST + 1;
}

static char a[65536], b[65536];
static unsigned long compared, failures;

static int same(const char *what, ULONG k0, ULONG k1, ULONG la, ULONG lb)
{
    compared++;
    if (la == lb && !memcmp(a, b, la)) return 1;
    if (++failures <= 5) {
        ULONG i = 0;
        while (i < la && i < lb && a[i] == b[i]) i++;
        printf("FAIL %s key %08lx %08lx: lengths %lu, %lu; first difference at %lu:\n--- old\n%.200s\n--- new\n%.200s\n",
               what, (unsigned long)k0, (unsigned long)k1, (unsigned long)la, (unsigned long)lb, (unsigned long)i,
               a + (i > 40 ? i - 40 : 0), b + (i > 40 ? i - 40 : 0));
    }
    return 0;
}

static void mgl_vs(ULONG vk, ULONG spots, int fog)
{
    struct OGPUFFKey k;
    ULONG la, lb;
    if (fog) vk |= VS_FOG;
    la = vg_vs_text(vk, spots, a, sizeof a);
    k.vs = vg_ff_vs(vk); k.fs = fog ? FS_FOG_SET(1) : 0; k.spots = spots;
    lb = pipe_ff_text(&k, 0, b, sizeof b);
    same("MiniGL vertex", vk, spots, la, lb);
}

static ULONG rnd_state = 0x12345678;
static ULONG rnd(void) { rnd_state = rnd_state * 1103515245UL + 12345UL; return rnd_state >> 8; }

int main(int argc, char **argv)
{
    unsigned long nrand = argc > 1 ? strtoul(argv[1], 0, 10) : 200000;
    ULONG k, i;
    struct OGPUFFKey u;
    unsigned long n0;

    /* ---- MiniGL, fragment: every key (env 0-5 x 2 units, rgb/a x 2, fog 0-3) ---- */
    n0 = compared;
    for (k = 0; k < (1UL << 12); k++) {
        if (FS_ENV0(k) > FS_ENV_ADD || FS_ENV1(k) > FS_ENV_ADD) continue;
        u.vs = 0; u.fs = vg_ff_fs(k); u.spots = 0;
        same("MiniGL fragment", k, 0, vg_fs_text(k, a, sizeof a), pipe_ff_text(&u, 1, b, sizeof b));
    }
    printf("test_ff: MiniGL fragment keys: %lu\n", compared - n0);

    /* ---- MiniGL, vertex: the features that shape the text, exhaustively ---- */
    n0 = compared;
    {
        ULONG col, tc, gen0, gen1, light, nl, spots, cm, lv, nz, fog;
        for (col = 0; col < 4; col++)                       /* none, or the colour in each byte order */
        for (tc = 0; tc < 64; tc++)                         /* unit 0's and 1's has, from the vertex, through the matrix */
        for (gen0 = 0; gen0 < 4; gen0++)
        for (gen1 = 0; gen1 < 4; gen1++)
        for (fog = 0; fog < 2; fog++) {
            ULONG base = (col ? VS_COL_ATTR | VS_COL_SWZ_SET(col - 1) : 0)
                       | ((tc & 1) ? VS_TC0 : 0) | ((tc & 2) ? VS_TC0_ATTR : 0) | ((tc & 4) ? VS_TC0_MAT : 0)
                       | ((tc & 8) ? VS_TC1 : 0) | ((tc & 16) ? VS_TC1_ATTR : 0) | ((tc & 32) ? VS_TC1_MAT : 0)
                       | VS_GEN0_SET(gen0) | VS_GEN1_SET(gen1);
            if ((!(tc & 1) && (tc & 6)) || (!(tc & 8) && (tc & 48))) continue;   /* no coordinates: no source, no matrix */
            mgl_vs(base, 0, (int)fog);
            if (gen0 > 1 || gen1 > 1 || col > 1 || (tc & 9) != 9) continue;      /* lighting over a smaller base */
            for (light = 0; light < 2; light++)
            for (nl = 0; nl <= (light ? 8UL : 0UL); nl++)
            for (cm = 0; cm < (light ? 6UL : 1UL); cm++)          /* off, or modes 0-4 */
            for (lv = 0; lv < 4; lv++)                             /* local viewer, normalise */
            for (nz = 0; nz < 2; nz++) {                           /* normal from the vertex */
                ULONG vk = base | (light ? VS_LIGHT | VS_NLIGHTS_SET(nl) | VS_LIGHT_POS : 0)
                         | (cm ? VS_COLMAT | VS_COLMAT_MODE_SET(cm - 1) : 0)
                         | ((lv & 1) ? VS_LOCALV : 0) | ((lv & 2) ? VS_NORMALIZE : 0) | (nz ? VS_NRM_ATTR : 0);
                ULONG top = nl <= 3 ? (1UL << nl) : 2;     /* every spot mask up to 3 lights; none and one beyond */
                for (spots = 0; spots < top; spots++) mgl_vs(vk, nl > 3 && spots ? (1UL << (nl - 1)) | 1UL : spots, (int)fog);
            }
        }
    }
    printf("test_ff: MiniGL vertex keys, structured: %lu\n", compared - n0);
    n0 = compared;
    for (i = 0; i < nrand; i++) {
        ULONG vk = rnd() & ((1UL << 27) - 1), nl;
        if (VS_COL_SWZ(vk) == 3) vk &= ~VS_COL_SWZ_SET(3);
        if (VS_COLMAT_MODE(vk) > 4) vk &= ~VS_COLMAT_MODE_SET(7);
        nl = VS_NLIGHTS(vk);
        if (nl > 8) { vk &= ~VS_NLIGHTS_SET(15); vk |= VS_NLIGHTS_SET(nl & 7); nl &= 7; }
        mgl_vs(vk & ~VS_FOG, rnd() & ((1UL << nl) - 1), (vk & VS_FOG) != 0);
    }
    printf("test_ff: MiniGL vertex keys, random: %lu\n", compared - n0);

    /* ---- Warp3D: every vertex key, every fragment key ---- */
    n0 = compared;
    for (k = 0; k < 32; k++) {
        w3dvg_ff_key(k, (k & VK_LINEAR) ? FK_LINEAR : 0, &u);
        same("Warp3D vertex", k, 0, w3dvg_vs_text(k, a, sizeof a), pipe_ff_text(&u, 0, b, sizeof b));
    }
    for (k = 0; k < (1UL << 16); k++) {
        if (k & (1UL << 14)) continue;                      /* unused bit */
        w3dvg_ff_key((k & FK_LINEAR) ? VK_LINEAR : 0, k, &u);
        same("Warp3D fragment", k, 0, w3dvg_fs_text(k, a, sizeof a), pipe_ff_text(&u, 1, b, sizeof b));
    }
    printf("test_ff: Warp3D keys: %lu\n", compared - n0);

    printf("test_ff: %lu keys compared, %lu different\n", compared, failures);
    return failures ? 1 : 0;
}
