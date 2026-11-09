/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * minigl.library's virgl route: the fixed-function pipeline as shaders of
 * our own, TGSI text virglrenderer reads (CREATE_OBJECT SHADER). Written
 * from the OpenGL 1.1 specification's equations: transform (2.10), colours
 * and lighting (2.13), texture coordinate generation (2.10.4), texture
 * environments (3.7.5, with ARB_multitexture's second unit and
 * EXT_texture_env_add), fog (3.9). A variant is made from its key when a
 * draw first needs it and kept for the context (mgl_vg_state.c).
 *
 * The constants' slots are VSC_ and FSC_ (mgl_vg.h). The vertex shader's
 * inputs follow the layout's order: position, colour, normal, unit 0's
 * coordinates, unit 1's (those the layout has). Its outputs: position,
 * colour, GENERIC[0] and [1] (the units' coordinates), GENERIC[2] (the
 * eye distance, for fog). The alpha test is the DSA object's. */
#include "mgl_vg.h"

/* ---- text ---------------------------------------------------------------------------------- */

struct sb { char *p; ULONG n, cap; int temps; };

static void s(struct sb *b, const char *t)
{
    while (*t) { if (b->n + 1 < b->cap) b->p[b->n++] = *t; t++; }
}

static void sn(struct sb *b, ULONG v)
{
    char d[12];
    int i = 0;
    do { d[i++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (i) { char t[2]; t[0] = d[--i]; t[1] = 0; s(b, t); }
}

/* "CONST[k]" */
static void cst(struct sb *b, ULONG k) { s(b, "CONST["); sn(b, k); s(b, "]"); }
static void in(struct sb *b, ULONG k) { s(b, "IN["); sn(b, k); s(b, "]"); }

static ULONG done(struct sb *b)
{
    s(b, "END\n");
    b->p[b->n++] = 0;
    return b->n;
}

/* ---- the vertex shader ----------------------------------------------------------------------- */

static const char *const col_swz[] = { ".wzyx", ".zyxw", ".yzwx", ".wzyx" };

/* One light's contribution added into TEMP[9] (2.13.1). L in TEMP[2], its
 * attenuation and spot factor in TEMP[3].w, the half vector in TEMP[4];
 * TEMP[1] the eye normal, TEMP[0] the eye position, TEMP[5] the vector to
 * the eye (local viewer). ma, md, ms: the material's colours (constants
 * or the vertex's colour, colour material). */
static void light(struct sb *b, ULONG key, int spot, ULONG base, const char *ma, const char *md, const char *ms)
{
    ULONG P = base, A = base + 1, D = base + 2, S = base + 3, SP = base + 4, AT = base + 5;
    /* L = P.xyz - eye * P.w (a directional light's w is 0) */
    s(b, "MUL TEMP[2], TEMP[0], "); cst(b, P); s(b, ".wwww\n");
    s(b, "ADD TEMP[2], "); cst(b, P); s(b, ", -TEMP[2]\n");
    s(b, "DP3 TEMP[3].x, TEMP[2], TEMP[2]\n");
    s(b, "RSQ TEMP[3].y, TEMP[3].xxxx\n");
    s(b, "MUL TEMP[2].xyz, TEMP[2], TEMP[3].yyyy\n");
    s(b, "MUL TEMP[3].z, TEMP[3].xxxx, TEMP[3].yyyy\n");
    /* 1 / (k0 + k1 d + k2 d^2): the 68k makes a directional light's 1, 0, 0 */
    s(b, "MAD TEMP[3].w, "); cst(b, AT); s(b, ".yyyy, TEMP[3].zzzz, "); cst(b, AT); s(b, ".xxxx\n");
    s(b, "MAD TEMP[3].w, "); cst(b, AT); s(b, ".zzzz, TEMP[3].xxxx, TEMP[3].wwww\n");
    s(b, "RCP TEMP[3].w, TEMP[3].wwww\n");
    if (spot) {
        /* (max(-L . dir, 0))^exp inside the cone, else 0 */
        s(b, "DP3 TEMP[4].x, -TEMP[2], "); cst(b, SP); s(b, "\n");
        s(b, "SGE TEMP[4].y, TEMP[4].xxxx, "); cst(b, SP); s(b, ".wwww\n");
        s(b, "MAX TEMP[4].x, TEMP[4].xxxx, "); cst(b, VSC_CONST); s(b, ".xxxx\n");
        s(b, "POW TEMP[4].x, TEMP[4].xxxx, "); cst(b, AT); s(b, ".wwww\n");
        s(b, "MUL TEMP[4].x, TEMP[4].xxxx, TEMP[4].yyyy\n");
        s(b, "MUL TEMP[3].w, TEMP[3].wwww, TEMP[4].xxxx\n");
    }
    /* the half vector: L + (0, 0, 1), or L + the vector to the eye */
    if (key & VS_LOCALV) s(b, "ADD TEMP[4].xyz, TEMP[2], TEMP[5]\n");
    else { s(b, "ADD TEMP[4].xyz, TEMP[2], "); cst(b, VSC_CONST); s(b, ".xxyx\n"); }
    s(b, "DP3 TEMP[4].w, TEMP[4], TEMP[4]\n");
    s(b, "RSQ TEMP[4].w, TEMP[4].wwww\n");
    s(b, "MUL TEMP[4].xyz, TEMP[4], TEMP[4].wwww\n");
    /* LIT: (1, max(n.L, 0), n.L > 0 ? max(n.h, 0)^shininess : 0, 1) */
    s(b, "DP3 TEMP[6].x, TEMP[1], TEMP[2]\n");
    s(b, "DP3 TEMP[6].y, TEMP[1], TEMP[4]\n");
    s(b, "MOV TEMP[6].w, "); cst(b, VSC_MISC); s(b, ".xxxx\n");
    s(b, "LIT TEMP[6], TEMP[6]\n");
    s(b, "MUL TEMP[7], "); cst(b, A); s(b, ", "); s(b, ma); s(b, "\n");
    s(b, "MUL TEMP[8], "); cst(b, D); s(b, ", "); s(b, md); s(b, "\n");
    s(b, "MAD TEMP[7], TEMP[8], TEMP[6].yyyy, TEMP[7]\n");
    s(b, "MUL TEMP[8], "); cst(b, S); s(b, ", "); s(b, ms); s(b, "\n");
    s(b, "MAD TEMP[7], TEMP[8], TEMP[6].zzzz, TEMP[7]\n");
    s(b, "MAD TEMP[9].xyz, TEMP[7], TEMP[3].wwww, TEMP[9]\n");
}

/* Unit u's S and T generated (2.10.4) into TEMP[dst].xy; R and Q from src. */
static void texgen(struct sb *b, int mode, int u, const char *src)
{
    ULONG planes = VSC_GEN + (ULONG)u * 4;
    s(b, "MOV TEMP[10], "); s(b, src); s(b, "\n");
    if (mode == 1) {                    /* object linear */
        s(b, "DP4 TEMP[10].x, IN[0], "); cst(b, planes); s(b, "\n");
        s(b, "DP4 TEMP[10].y, IN[0], "); cst(b, planes + 1); s(b, "\n");
    } else if (mode == 2) {             /* eye linear: the planes as the 68k turned them at glTexGen */
        s(b, "DP4 TEMP[10].x, TEMP[0], "); cst(b, planes + 2); s(b, "\n");
        s(b, "DP4 TEMP[10].y, TEMP[0], "); cst(b, planes + 3); s(b, "\n");
    } else {                            /* sphere map: r = u - 2 n (n . u), m = 2 |r + (0, 0, 1)| */
        s(b, "DP3 TEMP[11].w, TEMP[0], TEMP[0]\n");
        s(b, "RSQ TEMP[11].w, TEMP[11].wwww\n");
        s(b, "MUL TEMP[11].xyz, TEMP[0], TEMP[11].wwww\n");     /* u */
        s(b, "DP3 TEMP[12].x, TEMP[1], TEMP[11]\n");
        s(b, "ADD TEMP[12].x, TEMP[12].xxxx, TEMP[12].xxxx\n");
        s(b, "MAD TEMP[11].xyz, -TEMP[1], TEMP[12].xxxx, TEMP[11]\n");   /* r */
        s(b, "ADD TEMP[12].xyz, TEMP[11], "); cst(b, VSC_CONST); s(b, ".xxyx\n");
        s(b, "DP3 TEMP[12].w, TEMP[12], TEMP[12]\n");
        s(b, "RSQ TEMP[12].w, TEMP[12].wwww\n");
        s(b, "MUL TEMP[12].w, TEMP[12].wwww, "); cst(b, VSC_CONST); s(b, ".zzzz\n");   /* 1 / m */
        s(b, "MAD TEMP[10].xy, TEMP[11], TEMP[12].wwww, "); cst(b, VSC_CONST); s(b, ".zzzz\n");
    }
}

ULONG vg_vs_text(ULONG key, ULONG spots, char *buf, ULONG cap)
{
    struct sb b;
    ULONG nin = 1, i_col = 0, i_nrm = 0, i_tc0 = 0, i_tc1 = 0, nl = VS_NLIGHTS(key), k, nout = 2, o_tc[2], o_fog = 0;
    int lit = (key & VS_LIGHT) != 0, eye, nrm;
    char ma[24], md[24], ms[24], me[24], vcol[24];
    o_tc[0] = o_tc[1] = 0;
    b.p = buf; b.n = 0; b.cap = cap; b.temps = 0;
    if (key & VS_COL_ATTR) i_col = nin++;
    if (key & VS_NRM_ATTR) i_nrm = nin++;
    if (key & VS_TC0_ATTR) i_tc0 = nin++;
    if (key & VS_TC1_ATTR) i_tc1 = nin++;
    eye = lit || (key & VS_FOG) || VS_GEN0(key) >= 2 || VS_GEN1(key) >= 2;
    nrm = lit || VS_GEN0(key) == 3 || VS_GEN1(key) == 3;
    s(&b, "VERT\n");
    for (k = 0; k < nin; k++) { s(&b, "DCL IN["); sn(&b, k); s(&b, "]\n"); }
    s(&b, "DCL OUT[0], POSITION\nDCL OUT[1], COLOR\n");
    /* outputs numbered without gaps */
    if (key & VS_TC0) { o_tc[0] = nout++; s(&b, "DCL OUT["); sn(&b, o_tc[0]); s(&b, "], GENERIC[0]\n"); }
    if (key & VS_TC1) { o_tc[1] = nout++; s(&b, "DCL OUT["); sn(&b, o_tc[1]); s(&b, "], GENERIC[1]\n"); }
    if (key & VS_FOG) { o_fog = nout++; s(&b, "DCL OUT["); sn(&b, o_fog); s(&b, "], GENERIC[2]\n"); }
    s(&b, "DCL CONST[0.."); sn(&b, (ULONG)vg_vs_consts(key) - 1); s(&b, "]\n");
    s(&b, "DCL TEMP[0..13]\n");
    for (k = 0; k < 4; k++) { s(&b, "DP4 OUT[0]."); s(&b, k == 0 ? "x" : k == 1 ? "y" : k == 2 ? "z" : "w"); s(&b, ", IN[0], "); cst(&b, VSC_MVP + k); s(&b, "\n"); }
    if (eye) {
        for (k = 0; k < 3; k++) { s(&b, "DP4 TEMP[0]."); s(&b, k == 0 ? "x" : k == 1 ? "y" : "z"); s(&b, ", IN[0], "); cst(&b, VSC_MV + k); s(&b, "\n"); }
        s(&b, "MOV TEMP[0].w, "); cst(&b, VSC_CONST); s(&b, ".yyyy\n");
    }
    if (nrm) {
        char src[24];
        struct sb tb;
        tb.p = src; tb.n = 0; tb.cap = sizeof src;
        if (key & VS_NRM_ATTR) in(&tb, i_nrm); else cst(&tb, VSC_NORMAL);
        src[tb.n] = 0;
        for (k = 0; k < 3; k++) { s(&b, "DP3 TEMP[1]."); s(&b, k == 0 ? "x" : k == 1 ? "y" : "z"); s(&b, ", "); s(&b, src); s(&b, ", "); cst(&b, VSC_NM + k); s(&b, "\n"); }
        if (key & VS_NORMALIZE) {
            s(&b, "DP3 TEMP[1].w, TEMP[1], TEMP[1]\nRSQ TEMP[1].w, TEMP[1].wwww\nMUL TEMP[1].xyz, TEMP[1], TEMP[1].wwww\n");
        }
    }
    /* the vertex's colour: the arena's (in its byte order) or the current one */
    {
        struct sb tb;
        tb.p = vcol; tb.n = 0; tb.cap = sizeof vcol;
        if (key & VS_COL_ATTR) { in(&tb, i_col); s(&tb, col_swz[VS_COL_SWZ(key)]); }
        else cst(&tb, VSC_COLOR);
        vcol[tb.n] = 0;
    }
    if (!lit) {
        s(&b, "MOV_SAT OUT[1], "); s(&b, vcol); s(&b, "\n");
    } else {
        int cm = (key & VS_COLMAT) ? (int)VS_COLMAT_MODE(key) : -1;
        struct sb tb;
#define MAT(dst, slot, use) do { tb.p = dst; tb.n = 0; tb.cap = sizeof dst; if (use) s(&tb, "TEMP[13]"); else cst(&tb, slot); dst[tb.n] = 0; } while (0)
        MAT(ma, VSC_MAT, cm == 0 || cm == 1);
        MAT(md, VSC_MAT + 1, cm == 0 || cm == 2);
        MAT(ms, VSC_MAT + 2, cm == 3);
        MAT(me, VSC_MAT + 3, cm == 4);
#undef MAT
        if (cm >= 0) { s(&b, "MOV TEMP[13], "); s(&b, vcol); s(&b, "\n"); }
        if (key & VS_LOCALV) s(&b, "DP3 TEMP[5].w, TEMP[0], TEMP[0]\nRSQ TEMP[5].w, TEMP[5].wwww\nMUL TEMP[5].xyz, -TEMP[0], TEMP[5].wwww\n");
        /* emission + the global ambient times the material's ambient */
        s(&b, "MAD TEMP[9], "); cst(&b, VSC_GAMB); s(&b, ", "); s(&b, ma); s(&b, ", "); s(&b, me); s(&b, "\n");
        for (k = 0; k < nl; k++) light(&b, key, (int)((spots >> k) & 1), VSC_LIGHT + k * VSC_LIGHT_N, ma, md, ms);
        /* clamped to [0, 1] (2.13.6): the rasterizer's clamp bit isn't kept by core-profile hosts */
        s(&b, "MOV_SAT OUT[1].xyz, TEMP[9]\n");
        s(&b, "MOV_SAT OUT[1].w, "); s(&b, md); s(&b, ".wwww\n");
    }
    /* texture coordinates: the arena's or the current ones, generated, through the texture matrix */
    for (k = 0; k < 2; k++) {
        ULONG has = k ? VS_TC1 : VS_TC0, attr = k ? VS_TC1_ATTR : VS_TC0_ATTR, mat = k ? VS_TC1_MAT : VS_TC0_MAT;
        int gen = k ? (int)VS_GEN1(key) : (int)VS_GEN0(key);
        char src[24];
        struct sb tb;
        if (!(key & has)) continue;
        tb.p = src; tb.n = 0; tb.cap = sizeof src;
        if (key & attr) in(&tb, k ? i_tc1 : i_tc0); else cst(&tb, k ? VSC_TC1 : VSC_TC0);
        src[tb.n] = 0;
        if (gen) { texgen(&b, gen, (int)k, src); tb.n = 0; s(&tb, "TEMP[10]"); src[tb.n] = 0; }
        if (key & mat) {
            ULONG r, base = k ? VSC_TEX1 : VSC_TEX0;
            for (r = 0; r < 4; r++) {
                s(&b, "DP4 OUT["); sn(&b, o_tc[k]); s(&b, "]."); s(&b, r == 0 ? "x" : r == 1 ? "y" : r == 2 ? "z" : "w");
                s(&b, ", "); s(&b, src); s(&b, ", "); cst(&b, base + r); s(&b, "\n");
            }
        } else { s(&b, "MOV OUT["); sn(&b, o_tc[k]); s(&b, "], "); s(&b, src); s(&b, "\n"); }
    }
    if (key & VS_FOG) { s(&b, "MOV OUT["); sn(&b, o_fog); s(&b, "], |TEMP[0].zzzz|\n"); }
    return done(&b);
}

/* ---- the fragment shader --------------------------------------------------------------------- */

/* Unit u's function on TEMP[0] (the colour so far) with TEMP[1] (its texel). */
static void env(struct sb *b, int e, int rgb, int a, ULONG cslot)
{
    switch (e) {
    case FS_ENV_MODULATE:
        /* the texel's missing parts are 1 (the 68k's copy has them so) */
        s(b, "MUL TEMP[0], TEMP[0], TEMP[1]\n");
        break;
    case FS_ENV_REPLACE:
        if (rgb) s(b, "MOV TEMP[0].xyz, TEMP[1]\n");
        if (a) s(b, "MOV TEMP[0].w, TEMP[1].wwww\n");
        break;
    case FS_ENV_DECAL:
        s(b, "LRP TEMP[0].xyz, TEMP[1].wwww, TEMP[1], TEMP[0]\n");
        break;
    case FS_ENV_BLEND:
        if (rgb) { s(b, "LRP TEMP[0].xyz, TEMP[1], "); cst(b, cslot); s(b, ", TEMP[0]\n"); }
        s(b, "MUL TEMP[0].w, TEMP[0].wwww, TEMP[1].wwww\n");
        break;
    case FS_ENV_ADD:
        if (rgb) s(b, "ADD_SAT TEMP[0].xyz, TEMP[0], TEMP[1]\n");
        s(b, "MUL TEMP[0].w, TEMP[0].wwww, TEMP[1].wwww\n");
        break;
    }
}

ULONG vg_fs_text(ULONG key, char *buf, ULONG cap)
{
    struct sb b;
    ULONG n = 1, i_t0 = 0, i_t1 = 0, i_f = 0;
    int e0 = (int)FS_ENV0(key), e1 = (int)FS_ENV1(key), fog = (int)FS_FOG(key);
    b.p = buf; b.n = 0; b.cap = cap; b.temps = 0;
    s(&b, "FRAG\nPROPERTY FS_COLOR0_WRITES_ALL_CBUFS 1\nDCL IN[0], COLOR, COLOR\n");
    if (e0) { i_t0 = n++; s(&b, "DCL IN["); sn(&b, i_t0); s(&b, "], GENERIC[0], PERSPECTIVE\n"); }
    if (e1) { i_t1 = n++; s(&b, "DCL IN["); sn(&b, i_t1); s(&b, "], GENERIC[1], PERSPECTIVE\n"); }
    if (fog) { i_f = n++; s(&b, "DCL IN["); sn(&b, i_f); s(&b, "], GENERIC[2], PERSPECTIVE\n"); }
    s(&b, "DCL OUT[0], COLOR\n");
    if (e0) s(&b, "DCL SAMP[0]\nDCL SVIEW[0], 2D, FLOAT\n");
    if (e1) s(&b, "DCL SAMP[1]\nDCL SVIEW[1], 2D, FLOAT\n");
    s(&b, "DCL CONST[0.."); sn(&b, FSC_END - 1); s(&b, "]\n");
    s(&b, "DCL TEMP[0..2]\n");
    s(&b, "MOV TEMP[0], IN[0]\n");
    if (e0) {
        s(&b, "TXP TEMP[1], "); in(&b, i_t0); s(&b, ", SAMP[0], 2D\n");
        env(&b, e0, (key & FS_RGB0) != 0, (key & FS_A0) != 0, FSC_ENV0);
    }
    if (e1) {
        s(&b, "TXP TEMP[1], "); in(&b, i_t1); s(&b, ", SAMP[1], 2D\n");
        env(&b, e1, (key & FS_RGB1) != 0, (key & FS_A1) != 0, FSC_ENV1);
    }
    if (fog) {
        /* f = (end - z) / (end - start), e^(-density z), e^(-(density z)^2), clamped (3.9) */
        if (fog == 1) {
            s(&b, "ADD TEMP[2].x, "); cst(&b, FSC_FOG); s(&b, ".yyyy, -"); in(&b, i_f); s(&b, ".xxxx\n");
            s(&b, "MUL_SAT TEMP[2].x, TEMP[2].xxxx, "); cst(&b, FSC_FOG); s(&b, ".wwww\n");
        } else {
            s(&b, "MUL TEMP[2].x, "); in(&b, i_f); s(&b, ".xxxx, "); cst(&b, FSC_FOG); s(&b, ".zzzz\n");
            if (fog == 3) s(&b, "MUL TEMP[2].x, TEMP[2].xxxx, TEMP[2].xxxx\n");
            s(&b, "MUL TEMP[2].x, TEMP[2].xxxx, -"); cst(&b, FSC_CONST); s(&b, ".wwww\n");
            s(&b, "EX2_SAT TEMP[2].x, TEMP[2].xxxx\n");
        }
        s(&b, "LRP TEMP[0].xyz, TEMP[2].xxxx, TEMP[0], "); cst(&b, FSC_FOGCOL); s(&b, "\n");
    }
    s(&b, "MOV OUT[0], TEMP[0]\n");
    return done(&b);
}
