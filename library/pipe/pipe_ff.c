/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * The shared pipe's one fixed-function generator (stage M5 of docs/design/OpenGPU-Shared-Pipe-Plan.md, section 2.4):
 * TGSI text virglrenderer reads (CREATE_OBJECT SHADER), made from one key (struct OGPUFFKey, pipe.h) when a draw
 * first needs it, kept per context, and bound only when it changed.
 *
 * Taken from minigl.library's mgl_vg_shader.c (object-space vertices: transform, OpenGL 1.1's lighting, texture
 * coordinate generation, two units' environments, fog) and Warp3D.library's w3d_vg_shader.c (screen-space vertices:
 * 16.16 positions, perspective by 1/w, chroma, specular, vertex and table fog, colours across the screen), into one
 * key, one cache and one entry point. Each front end's text is the same, word for word, as its own generator made;
 * tests/pipe/test_ff.c holds that for every key it enumerates.
 *
 * Object-space vertex shader: inputs in the layout's order (position, colour, normal, unit 0's coordinates, unit
 * 1's); outputs position, colour, GENERIC[0] and [1] (the units' coordinates), GENERIC[2] (the eye distance, fog).
 * Screen-space: stream3d.h's vertex as the vertex fetch converts it; outputs the clip position, COLOR (or GENERIC[3]
 * across the screen), GENERIC[0] the texture coordinates, GENERIC[1] specular and fog, GENERIC[2] w, GENERIC[4] the
 * clip w (across the screen). */
#include "pipe_int.h"
#include "../../include/gfopengpu/stream3d.h"

/* ---- text ----------------------------------------------------------------------------------------------------- */

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

/* "CONST[k]", "IN[k]", "FILE[k]swz" */
static void cst(struct sb *b, ULONG k) { s(b, "CONST["); sn(b, k); s(b, "]"); }
static void in(struct sb *b, ULONG k) { s(b, "IN["); sn(b, k); s(b, "]"); }
static void reg(struct sb *b, const char *file, ULONG k, const char *swz) { s(b, file); s(b, "["); sn(b, k); s(b, "]"); s(b, swz); }

static ULONG done(struct sb *b)
{
    s(b, "END\n");
    b->p[b->n < b->cap ? b->n++ : b->cap - 1] = 0;
    return b->n;
}

/* Vertex constants an object-space key's shader reads (vec4s); its DCL says the same. */
static ULONG obj_vs_consts(ULONG key)
{
    if (key & OGPU_FF_LIGHT) return OGPU_VSC_LIGHT + OGPU_FF_NLIGHTS(key) * OGPU_VSC_LIGHT_N;
    if (OGPU_FF_GEN0(key) || OGPU_FF_GEN1(key)) return OGPU_VSC_LIGHT;
    return OGPU_VSC_CONST + 1;
}

ULONG pipe_m_ff_vs_consts(const struct OGPUFFKey *k)
{
    if (!k) return 0;
    return OGPU_FF_POS(k->vs) ? 3 : obj_vs_consts(k->vs);
}

/* ==== object-space vertices (from mgl_vg_shader.c) =============================================================== */

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
        s(b, "MAX TEMP[4].x, TEMP[4].xxxx, "); cst(b, OGPU_VSC_CONST); s(b, ".xxxx\n");
        s(b, "POW TEMP[4].x, TEMP[4].xxxx, "); cst(b, AT); s(b, ".wwww\n");
        s(b, "MUL TEMP[4].x, TEMP[4].xxxx, TEMP[4].yyyy\n");
        s(b, "MUL TEMP[3].w, TEMP[3].wwww, TEMP[4].xxxx\n");
    }
    /* the half vector: L + (0, 0, 1), or L + the vector to the eye */
    if (key & OGPU_FF_LOCALV) s(b, "ADD TEMP[4].xyz, TEMP[2], TEMP[5]\n");
    else { s(b, "ADD TEMP[4].xyz, TEMP[2], "); cst(b, OGPU_VSC_CONST); s(b, ".xxyx\n"); }
    s(b, "DP3 TEMP[4].w, TEMP[4], TEMP[4]\n");
    s(b, "RSQ TEMP[4].w, TEMP[4].wwww\n");
    s(b, "MUL TEMP[4].xyz, TEMP[4], TEMP[4].wwww\n");
    /* LIT: (1, max(n.L, 0), n.L > 0 ? max(n.h, 0)^shininess : 0, 1) */
    s(b, "DP3 TEMP[6].x, TEMP[1], TEMP[2]\n");
    s(b, "DP3 TEMP[6].y, TEMP[1], TEMP[4]\n");
    s(b, "MOV TEMP[6].w, "); cst(b, OGPU_VSC_MISC); s(b, ".xxxx\n");
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
    ULONG planes = OGPU_VSC_GEN + (ULONG)u * 4;
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
        s(b, "ADD TEMP[12].xyz, TEMP[11], "); cst(b, OGPU_VSC_CONST); s(b, ".xxyx\n");
        s(b, "DP3 TEMP[12].w, TEMP[12], TEMP[12]\n");
        s(b, "RSQ TEMP[12].w, TEMP[12].wwww\n");
        s(b, "MUL TEMP[12].w, TEMP[12].wwww, "); cst(b, OGPU_VSC_CONST); s(b, ".zzzz\n");   /* 1 / m */
        s(b, "MAD TEMP[10].xy, TEMP[11], TEMP[12].wwww, "); cst(b, OGPU_VSC_CONST); s(b, ".zzzz\n");
    }
}

static ULONG obj_vs_text(ULONG key, int fog, ULONG spots, char *buf, ULONG cap)
{
    struct sb b;
    ULONG nin = 1, i_col = 0, i_nrm = 0, i_tc0 = 0, i_tc1 = 0, nl = OGPU_FF_NLIGHTS(key), k, nout = 2, o_tc[2], o_fog = 0;
    int lit = (key & OGPU_FF_LIGHT) != 0, eye, nrm;
    char ma[24], md[24], ms[24], me[24], vcol[24];
    o_tc[0] = o_tc[1] = 0;
    b.p = buf; b.n = 0; b.cap = cap; b.temps = 0;
    if (key & OGPU_FF_COL_ATTR) i_col = nin++;
    if (key & OGPU_FF_NRM_ATTR) i_nrm = nin++;
    if (key & OGPU_FF_TC0_ATTR) i_tc0 = nin++;
    if (key & OGPU_FF_TC1_ATTR) i_tc1 = nin++;
    eye = lit || (fog) || OGPU_FF_GEN0(key) >= 2 || OGPU_FF_GEN1(key) >= 2;
    nrm = lit || OGPU_FF_GEN0(key) == 3 || OGPU_FF_GEN1(key) == 3;
    s(&b, "VERT\n");
    for (k = 0; k < nin; k++) { s(&b, "DCL IN["); sn(&b, k); s(&b, "]\n"); }
    s(&b, "DCL OUT[0], POSITION\nDCL OUT[1], COLOR\n");
    /* outputs numbered without gaps */
    if (key & OGPU_FF_TC0) { o_tc[0] = nout++; s(&b, "DCL OUT["); sn(&b, o_tc[0]); s(&b, "], GENERIC[0]\n"); }
    if (key & OGPU_FF_TC1) { o_tc[1] = nout++; s(&b, "DCL OUT["); sn(&b, o_tc[1]); s(&b, "], GENERIC[1]\n"); }
    if (fog) { o_fog = nout++; s(&b, "DCL OUT["); sn(&b, o_fog); s(&b, "], GENERIC[2]\n"); }
    s(&b, "DCL CONST[0.."); sn(&b, (ULONG)obj_vs_consts(key) - 1); s(&b, "]\n");
    s(&b, "DCL TEMP[0..13]\n");
    for (k = 0; k < 4; k++) { s(&b, "DP4 OUT[0]."); s(&b, k == 0 ? "x" : k == 1 ? "y" : k == 2 ? "z" : "w"); s(&b, ", IN[0], "); cst(&b, OGPU_VSC_MVP + k); s(&b, "\n"); }
    if (eye) {
        for (k = 0; k < 3; k++) { s(&b, "DP4 TEMP[0]."); s(&b, k == 0 ? "x" : k == 1 ? "y" : "z"); s(&b, ", IN[0], "); cst(&b, OGPU_VSC_MV + k); s(&b, "\n"); }
        s(&b, "MOV TEMP[0].w, "); cst(&b, OGPU_VSC_CONST); s(&b, ".yyyy\n");
    }
    if (nrm) {
        char src[24];
        struct sb tb;
        tb.p = src; tb.n = 0; tb.cap = sizeof src;
        if (key & OGPU_FF_NRM_ATTR) in(&tb, i_nrm); else cst(&tb, OGPU_VSC_NORMAL);
        src[tb.n] = 0;
        for (k = 0; k < 3; k++) { s(&b, "DP3 TEMP[1]."); s(&b, k == 0 ? "x" : k == 1 ? "y" : "z"); s(&b, ", "); s(&b, src); s(&b, ", "); cst(&b, OGPU_VSC_NM + k); s(&b, "\n"); }
        if (key & OGPU_FF_NORMALIZE) {
            s(&b, "DP3 TEMP[1].w, TEMP[1], TEMP[1]\nRSQ TEMP[1].w, TEMP[1].wwww\nMUL TEMP[1].xyz, TEMP[1], TEMP[1].wwww\n");
        }
    }
    /* the vertex's colour: the arena's (in its byte order) or the current one */
    {
        struct sb tb;
        tb.p = vcol; tb.n = 0; tb.cap = sizeof vcol;
        if (key & OGPU_FF_COL_ATTR) { in(&tb, i_col); s(&tb, col_swz[OGPU_FF_COL_SWZ(key)]); }
        else cst(&tb, OGPU_VSC_COLOR);
        vcol[tb.n] = 0;
    }
    if (!lit) {
        s(&b, "MOV_SAT OUT[1], "); s(&b, vcol); s(&b, "\n");
    } else {
        int cm = (key & OGPU_FF_COLMAT) ? (int)OGPU_FF_COLMAT_MODE(key) : -1;
        struct sb tb;
#define MAT(dst, slot, use) do { tb.p = dst; tb.n = 0; tb.cap = sizeof dst; if (use) s(&tb, "TEMP[13]"); else cst(&tb, slot); dst[tb.n] = 0; } while (0)
        MAT(ma, OGPU_VSC_MAT, cm == 0 || cm == 1);
        MAT(md, OGPU_VSC_MAT + 1, cm == 0 || cm == 2);
        MAT(ms, OGPU_VSC_MAT + 2, cm == 3);
        MAT(me, OGPU_VSC_MAT + 3, cm == 4);
#undef MAT
        if (cm >= 0) { s(&b, "MOV TEMP[13], "); s(&b, vcol); s(&b, "\n"); }
        if (key & OGPU_FF_LOCALV) s(&b, "DP3 TEMP[5].w, TEMP[0], TEMP[0]\nRSQ TEMP[5].w, TEMP[5].wwww\nMUL TEMP[5].xyz, -TEMP[0], TEMP[5].wwww\n");
        /* emission + the global ambient times the material's ambient */
        s(&b, "MAD TEMP[9], "); cst(&b, OGPU_VSC_GAMB); s(&b, ", "); s(&b, ma); s(&b, ", "); s(&b, me); s(&b, "\n");
        for (k = 0; k < nl; k++) light(&b, key, (int)((spots >> k) & 1), OGPU_VSC_LIGHT + k * OGPU_VSC_LIGHT_N, ma, md, ms);
        /* clamped to [0, 1] (2.13.6): the rasterizer's clamp bit isn't kept by core-profile hosts */
        s(&b, "MOV_SAT OUT[1].xyz, TEMP[9]\n");
        s(&b, "MOV_SAT OUT[1].w, "); s(&b, md); s(&b, ".wwww\n");
    }
    /* texture coordinates: the arena's or the current ones, generated, through the texture matrix */
    for (k = 0; k < 2; k++) {
        ULONG has = k ? OGPU_FF_TC1 : OGPU_FF_TC0, attr = k ? OGPU_FF_TC1_ATTR : OGPU_FF_TC0_ATTR, mat = k ? OGPU_FF_TC1_MAT : OGPU_FF_TC0_MAT;
        int gen = k ? (int)OGPU_FF_GEN1(key) : (int)OGPU_FF_GEN0(key);
        char src[24];
        struct sb tb;
        if (!(key & has)) continue;
        tb.p = src; tb.n = 0; tb.cap = sizeof src;
        if (key & attr) in(&tb, k ? i_tc1 : i_tc0); else cst(&tb, k ? OGPU_VSC_TC1 : OGPU_VSC_TC0);
        src[tb.n] = 0;
        if (gen) { texgen(&b, gen, (int)k, src); tb.n = 0; s(&tb, "TEMP[10]"); src[tb.n] = 0; }
        if (key & mat) {
            ULONG r, base = k ? OGPU_VSC_TEX1 : OGPU_VSC_TEX0;
            for (r = 0; r < 4; r++) {
                s(&b, "DP4 OUT["); sn(&b, o_tc[k]); s(&b, "]."); s(&b, r == 0 ? "x" : r == 1 ? "y" : r == 2 ? "z" : "w");
                s(&b, ", "); s(&b, src); s(&b, ", "); cst(&b, base + r); s(&b, "\n");
            }
        } else { s(&b, "MOV OUT["); sn(&b, o_tc[k]); s(&b, "], "); s(&b, src); s(&b, "\n"); }
    }
    if (fog) { s(&b, "MOV OUT["); sn(&b, o_fog); s(&b, "], |TEMP[0].zzzz|\n"); }
    return done(&b);
}

/* ---- the fragment shader --------------------------------------------------------------------- */

/* Unit u's function on TEMP[0] (the colour so far) with TEMP[1] (its texel). */
static void obj_env(struct sb *b, int e, int rgb, int a, ULONG cslot)
{
    switch (e) {
    case OGPU_FF_ENV_MODULATE:
        /* the texel's missing parts are 1 (the 68k's copy has them so) */
        s(b, "MUL TEMP[0], TEMP[0], TEMP[1]\n");
        break;
    case OGPU_FF_ENV_REPLACE:
        if (rgb) s(b, "MOV TEMP[0].xyz, TEMP[1]\n");
        if (a) s(b, "MOV TEMP[0].w, TEMP[1].wwww\n");
        break;
    case OGPU_FF_ENV_DECAL:
        s(b, "LRP TEMP[0].xyz, TEMP[1].wwww, TEMP[1], TEMP[0]\n");
        break;
    case OGPU_FF_ENV_BLEND:
        if (rgb) { s(b, "LRP TEMP[0].xyz, TEMP[1], "); cst(b, cslot); s(b, ", TEMP[0]\n"); }
        s(b, "MUL TEMP[0].w, TEMP[0].wwww, TEMP[1].wwww\n");
        break;
    case OGPU_FF_ENV_ADD:
        if (rgb) s(b, "ADD_SAT TEMP[0].xyz, TEMP[0], TEMP[1]\n");
        s(b, "MUL TEMP[0].w, TEMP[0].wwww, TEMP[1].wwww\n");
        break;
    }
}

static ULONG obj_fs_text(ULONG key, char *buf, ULONG cap)
{
    struct sb b;
    ULONG n = 1, i_t0 = 0, i_t1 = 0, i_f = 0;
    int e0 = (int)OGPU_FF_ENV0(key), e1 = (int)OGPU_FF_ENV1(key), fog = (int)OGPU_FF_FOG(key);
    b.p = buf; b.n = 0; b.cap = cap; b.temps = 0;
    s(&b, "FRAG\nPROPERTY FS_COLOR0_WRITES_ALL_CBUFS 1\nDCL IN[0], COLOR, COLOR\n");
    if (e0) { i_t0 = n++; s(&b, "DCL IN["); sn(&b, i_t0); s(&b, "], GENERIC[0], PERSPECTIVE\n"); }
    if (e1) { i_t1 = n++; s(&b, "DCL IN["); sn(&b, i_t1); s(&b, "], GENERIC[1], PERSPECTIVE\n"); }
    if (fog) { i_f = n++; s(&b, "DCL IN["); sn(&b, i_f); s(&b, "], GENERIC[2], PERSPECTIVE\n"); }
    s(&b, "DCL OUT[0], COLOR\n");
    if (e0) s(&b, "DCL SAMP[0]\nDCL SVIEW[0], 2D, FLOAT\n");
    if (e1) s(&b, "DCL SAMP[1]\nDCL SVIEW[1], 2D, FLOAT\n");
    s(&b, "DCL CONST[0.."); sn(&b, OGPU_FSC_END - 1); s(&b, "]\n");
    s(&b, "DCL TEMP[0..2]\n");
    s(&b, "MOV TEMP[0], IN[0]\n");
    if (e0) {
        s(&b, "TXP TEMP[1], "); in(&b, i_t0); s(&b, ", SAMP[0], 2D\n");
        obj_env(&b, e0, (key & OGPU_FF_RGB0) != 0, (key & OGPU_FF_A0) != 0, OGPU_FSC_ENV0);
    }
    if (e1) {
        s(&b, "TXP TEMP[1], "); in(&b, i_t1); s(&b, ", SAMP[1], 2D\n");
        obj_env(&b, e1, (key & OGPU_FF_RGB1) != 0, (key & OGPU_FF_A1) != 0, OGPU_FSC_ENV1);
    }
    if (fog) {
        /* f = (end - z) / (end - start), e^(-density z), e^(-(density z)^2), clamped (3.9) */
        if (fog == 1) {
            s(&b, "ADD TEMP[2].x, "); cst(&b, OGPU_FSC_FOG); s(&b, ".yyyy, -"); in(&b, i_f); s(&b, ".xxxx\n");
            s(&b, "MUL_SAT TEMP[2].x, TEMP[2].xxxx, "); cst(&b, OGPU_FSC_FOG); s(&b, ".wwww\n");
        } else {
            s(&b, "MUL TEMP[2].x, "); in(&b, i_f); s(&b, ".xxxx, "); cst(&b, OGPU_FSC_FOG); s(&b, ".zzzz\n");
            if (fog == 3) s(&b, "MUL TEMP[2].x, TEMP[2].xxxx, TEMP[2].xxxx\n");
            s(&b, "MUL TEMP[2].x, TEMP[2].xxxx, -"); cst(&b, OGPU_FSC_CONST); s(&b, ".wwww\n");
            s(&b, "EX2_SAT TEMP[2].x, TEMP[2].xxxx\n");
        }
        s(&b, "LRP TEMP[0].xyz, TEMP[2].xxxx, TEMP[0], "); cst(&b, OGPU_FSC_FOGCOL); s(&b, "\n");
    }
    s(&b, "MOV OUT[0], TEMP[0]\n");
    return done(&b);
}

/* ==== screen-space vertices (from w3d_vg_shader.c) =============================================================== */

/* ---- the vertex shader ----------------------------------------------------------------------- */

static ULONG scr_vs_text(ULONG key, char *buf, ULONG cap)
{
    struct sb b;
    ULONG n = 3, i_z = 0, i_w = 0, i_s = 0, o = 3, o_s = 0, o_w = 0, o_c = 1, k;
    int lin = (key & OGPU_FF_LINEAR) != 0;
    b.p = buf; b.n = 0; b.cap = cap;
    if (key & OGPU_FF_SCR_Z) i_z = n++;
    if (key & OGPU_FF_SCR_W) i_w = n++;
    if (key & OGPU_FF_SPEC) i_s = n++;
    if (key & OGPU_FF_SPEC) o_s = o++;
    if (key & OGPU_FF_SCR_W) o_w = o++;
    s(&b, "VERT\n");
    for (k = 0; k < n; k++) { s(&b, "DCL "); reg(&b, "IN", k, "\n"); }
    s(&b, "DCL OUT[0], POSITION\n");
    /* Screen-linear values (the core's way) as perspective ones: each times
     * the clip w, and the clip w itself, which the fragment shader divides
     * by. Hosts without noperspective (GLES) take it, and it holds where a
     * triangle is clipped. The colour then goes as a GENERIC (a COLOR output
     * is clamped). */
    if (lin) { s(&b, "DCL OUT[1], GENERIC[3]\n"); }
    else s(&b, "DCL OUT[1], COLOR\n");
    s(&b, "DCL OUT[2], GENERIC[0]\n");
    if (key & OGPU_FF_SPEC) { s(&b, "DCL "); reg(&b, "OUT", o_s, ", GENERIC[1]\n"); }
    if (key & OGPU_FF_SCR_W) { s(&b, "DCL "); reg(&b, "OUT", o_w, ", GENERIC[2]\n"); }
    if (lin) { s(&b, "DCL "); reg(&b, "OUT", o, ", GENERIC[4]\n"); }
    s(&b, "DCL CONST[0..2]\nDCL TEMP[0..1]\n");
    /* x, y: 16.16 pixels to clip space (y down the screen) */
    s(&b, "MAD TEMP[0].xy, IN[0].xyyy, CONST[0].xyyy, CONST[0].zwww\n");
    /* z: 0 .. 2^32 to -1 .. 1 */
    if (key & OGPU_FF_SCR_Z) { s(&b, "MAD TEMP[0].z, "); reg(&b, "IN", i_z, ".xxxx, CONST[1].yyyy, CONST[1].zzzz\n"); }
    else s(&b, "MOV TEMP[0].z, CONST[1].zzzz\n");
    s(&b, "MOV TEMP[0].w, CONST[1].wwww\n");
    if ((key & OGPU_FF_PERSP) && (key & OGPU_FF_SCR_W)) {
        s(&b, "RCP TEMP[1].x, "); reg(&b, "IN", i_w, ".xxxx\n");
        s(&b, "MUL TEMP[0].xyz, TEMP[0], TEMP[1].xxxx\n");
        s(&b, "MOV TEMP[0].w, TEMP[1].xxxx\n");
    }
    s(&b, "MOV OUT[0], TEMP[0]\n");
    if (lin) { s(&b, "MUL "); reg(&b, "OUT", o_c, ", IN[1], TEMP[0].wwww\n"); }
    else { s(&b, "MOV "); reg(&b, "OUT", o_c, ", IN[1]\n"); }
    s(&b, "MUL OUT[2].xy, IN[2].xyyy, CONST[1].xxxx\n");
    s(&b, "MOV OUT[2].zw, CONST[2].xxxy\n");
    if (key & OGPU_FF_SPEC) {
        s(&b, lin ? "MUL " : "MOV "); reg(&b, "OUT", o_s, ", "); reg(&b, "IN", i_s, lin ? ", TEMP[0].wwww\n" : "\n");
    }
    if (key & OGPU_FF_SCR_W) {
        s(&b, lin ? "MUL " : "MOV "); reg(&b, "OUT", o_w, ", "); reg(&b, "IN", i_w, lin ? ".xxxx, TEMP[0].wwww\n" : ".xxxx\n");
    }
    if (lin) { s(&b, "MOV "); reg(&b, "OUT", o, ", TEMP[0].wwww\n"); }
    return done(&b);
}

/* ---- the fragment shader --------------------------------------------------------------------- */

/* The texture's environment: TEMP[0] the colour so far, TEMP[1] the texel
 * (ogpu_3d.c's o3_environment, mode by mode). */
static void scr_env(struct sb *b, int mode, int base)
{
    int has_c = 1, has_a = 1;
    switch (base) {
    case OGPU_TEXBASE_RGB: has_a = 0; break;
    case OGPU_TEXBASE_ALPHA: has_c = 0; break;
    case OGPU_TEXBASE_LUMINANCE: s(b, "MOV TEMP[1].yz, TEMP[1].xxxx\n"); has_a = 0; break;
    case OGPU_TEXBASE_LUM_ALPHA: s(b, "MOV TEMP[1].yz, TEMP[1].xxxx\n"); break;
    case OGPU_TEXBASE_INTENSITY: s(b, "MOV TEMP[1].yzw, TEMP[1].xxxx\n"); break;
    }
    switch (mode) {
    case OGPU_ENV_REPLACE:
        if (has_c) s(b, "MOV TEMP[0].xyz, TEMP[1]\n");
        if (has_a) s(b, "MOV TEMP[0].w, TEMP[1].wwww\n");
        break;
    case OGPU_ENV_DECAL:
        if (base == OGPU_TEXBASE_RGB) s(b, "MOV TEMP[0].xyz, TEMP[1]\n");
        else if (base == OGPU_TEXBASE_RGBA) s(b, "LRP TEMP[0].xyz, TEMP[1].wwww, TEMP[1], TEMP[0]\n");
        break;
    case OGPU_ENV_MODULATE:
        if (has_c) s(b, "MUL TEMP[0].xyz, TEMP[0], TEMP[1]\n");
        if (has_a) s(b, "MUL TEMP[0].w, TEMP[0].wwww, TEMP[1].wwww\n");
        break;
    case OGPU_ENV_BLEND:
        if (has_c) s(b, "LRP TEMP[0].xyz, TEMP[1], CONST[1], TEMP[0]\n");
        if (base == OGPU_TEXBASE_INTENSITY) s(b, "LRP TEMP[0].w, TEMP[1].wwww, CONST[1].wwww, TEMP[0].wwww\n");
        else if (has_a) s(b, "MUL TEMP[0].w, TEMP[0].wwww, TEMP[1].wwww\n");
        break;
    case OGPU_ENV_ADD:
        if (has_c) s(b, "ADD_SAT TEMP[0].xyz, TEMP[0], TEMP[1]\n");
        if (has_a) s(b, "MUL TEMP[0].w, TEMP[0].wwww, TEMP[1].wwww\n");
        break;
    case OGPU_ENV_SUB:
        if (has_c) s(b, "ADD_SAT TEMP[0].xyz, TEMP[0], -TEMP[1]\n");
        if (has_a) s(b, "MUL TEMP[0].w, TEMP[0].wwww, TEMP[1].wwww\n");
        break;
    }
}

static ULONG scr_fs_text(ULONG vkey, ULONG key, char *buf, ULONG cap)
{
    struct sb b;
    ULONG n = 0, i_c = 0, i_t = 0, i_s = 0, i_w = 0;
    int e = (int)OGPU_FF_ENV0(key), fog = (int)OGPU_FF_FOG(key), chroma = (int)OGPU_FF_CHROMA(key);
    int spec = (key & OGPU_FF_SPEC_ADD) != 0, vfog = fog == OGPU_FOG_VERTEX, tfog = fog && !vfog;
    int lin = (vkey & OGPU_FF_LINEAR) && ((vkey & OGPU_FF_COL_ATTR) || spec || vfog || tfog);
    ULONG i_k = 0;
    b.p = buf; b.n = 0; b.cap = cap;
    s(&b, "FRAG\nPROPERTY FS_COLOR0_WRITES_ALL_CBUFS 1\n");
    /* OGPU_FF_LINEAR: the vertex shader's screen-linear values, each times the
     * clip w; TEMP[3].y is 1 / the clip w here, which turns them back. */
    if (vkey & OGPU_FF_COL_ATTR) { i_c = n++; s(&b, "DCL "); reg(&b, "IN", i_c, lin ? ", GENERIC[3], PERSPECTIVE\n" : ", COLOR, COLOR\n"); }
    if (e) { i_t = n++; s(&b, "DCL "); reg(&b, "IN", i_t, ", GENERIC[0], PERSPECTIVE\n"); }
    if (spec || vfog) { i_s = n++; s(&b, "DCL "); reg(&b, "IN", i_s, ", GENERIC[1], PERSPECTIVE\n"); }
    if (tfog) { i_w = n++; s(&b, "DCL "); reg(&b, "IN", i_w, ", GENERIC[2], PERSPECTIVE\n"); }
    if (lin) { i_k = n++; s(&b, "DCL "); reg(&b, "IN", i_k, ", GENERIC[4], PERSPECTIVE\n"); }
    s(&b, "DCL OUT[0], COLOR\n");
    if (e) s(&b, "DCL SAMP[0]\nDCL SVIEW[0], 2D, FLOAT\n");
    s(&b, "DCL CONST[0..6]\nDCL TEMP[0..6]\n");
    if (lin) { s(&b, "RCP TEMP[4].x, "); reg(&b, "IN", i_k, ".xxxx\n"); }
    if (vkey & OGPU_FF_COL_ATTR) {
        if (lin) { s(&b, "MUL_SAT TEMP[0], "); reg(&b, "IN", i_c, ", TEMP[4].xxxx\n"); }
        else { s(&b, "MOV TEMP[0], "); reg(&b, "IN", i_c, "\n"); }
    }
    else s(&b, "MOV TEMP[0], CONST[0]\n");
    /* specular and fog in TEMP[5], w in TEMP[6].x */
    if (spec || vfog) {
        if (lin) { s(&b, "MUL_SAT TEMP[5], "); reg(&b, "IN", i_s, ", TEMP[4].xxxx\n"); }
        else { s(&b, "MOV TEMP[5], "); reg(&b, "IN", i_s, "\n"); }
    }
    if (tfog) {
        if (lin) { s(&b, "MUL TEMP[6].x, "); reg(&b, "IN", i_w, ".xxxx, TEMP[4].xxxx\n"); }
        else { s(&b, "MOV TEMP[6].x, "); reg(&b, "IN", i_w, ".xxxx\n"); }
    }
    if (e) {
        s(&b, "TEX TEMP[1], "); reg(&b, "IN", i_t, ", SAMP[0], 2D\n");
        if (chroma >= 2) {
            /* inside the range, channel by channel (the texel as sampled, as the core tests it) */
            s(&b, "SGE TEMP[2].xyz, TEMP[1], CONST[4]\n");
            s(&b, "SGE TEMP[3].xyz, CONST[5], TEMP[1]\n");
            s(&b, "MUL TEMP[2].xyz, TEMP[2], TEMP[3]\n");
            s(&b, "MUL TEMP[2].x, TEMP[2].xxxx, TEMP[2].yyyy\n");
            s(&b, "MUL TEMP[2].x, TEMP[2].xxxx, TEMP[2].zzzz\n");
            if (chroma == 2) s(&b, "ADD TEMP[2].x, TEMP[2].xxxx, -CONST[6].zzzz\n");     /* keeps the range */
            else s(&b, "ADD TEMP[2].x, CONST[6].zzzz, -TEMP[2].xxxx\n");                 /* drops it */
            s(&b, "KILL_IF TEMP[2].xxxx\n");
        }
        scr_env(&b, e, (int)OGPU_FF_BASE(key));
    }
    if (spec) s(&b, "ADD_SAT TEMP[0].xyz, TEMP[0], TEMP[5]\n");
    if (fog) {
        if (vfog) s(&b, "MOV TEMP[2].x, TEMP[5].wwww\n");
        else {
            s(&b, "MOV_SAT TEMP[3].x, TEMP[6].xxxx\n");          /* w, 0 .. 1 */
            if (fog == OGPU_FOG_LINEAR) {
                if (key & OGPU_FF_FOG_STEP) s(&b, "SGE TEMP[2].x, TEMP[3].xxxx, CONST[3].xxxx\n");
                else {
                    /* f = (w - end) / (start - end) */
                    s(&b, "ADD TEMP[2].x, TEMP[3].xxxx, -CONST[3].yyyy\n");
                    s(&b, "MUL_SAT TEMP[2].x, TEMP[2].xxxx, CONST[3].wwww\n");
                }
            } else {
                /* d = 1 - w; f = e^-(density d) or e^-(density d)^2, as 2^(-x log2 e) */
                s(&b, "ADD TEMP[3].x, CONST[6].yyyy, -TEMP[3].xxxx\n");
                s(&b, "MUL TEMP[2].x, TEMP[3].xxxx, CONST[3].zzzz\n");
                if (fog == OGPU_FOG_EXP2) s(&b, "MUL TEMP[2].x, TEMP[2].xxxx, TEMP[2].xxxx\n");
                s(&b, "MUL TEMP[2].x, TEMP[2].xxxx, CONST[6].wwww\n");
                s(&b, "EX2_SAT TEMP[2].x, -TEMP[2].xxxx\n");
            }
        }
        s(&b, "LRP TEMP[0].xyz, TEMP[2].xxxx, TEMP[0], CONST[2]\n");
    }
    s(&b, "MOV OUT[0], TEMP[0]\n");
    return done(&b);
}

/* ==== the generator's entry: made once per key, kept, bound when it changed ======================================= */

#define PIPE_TEXT_BYTES 16384

static ULONG make_shader(OGPUPipeCtx *x, int type, ULONG len)
{
    ULONG h = pipe_m_obj_new(x), *p = pipe_cmd(x, VGC_SHADER_WORDS(len));
    if (p) vgc_shader(p, h, type, x->text, len);
    return h;
}

/* The caches' keys: the vertex shader's word, the spots and, for object space, whether the fragment side fogs (the
 * eye distance goes to it); the fragment shader's word and, for screen space, what of the vertex it reads. */
OGPUPipeShader *pipe_m_ff_shader(OGPUPipeCtx *x, const struct OGPUFFKey *k)
{
    ULONG vk0, vk1, fk0, fk1 = 0, len;
    int i, scr;
    if (!x || !k) return 0;
    scr = OGPU_FF_POS(k->vs) != 0;
    vk0 = k->vs; vk1 = k->spots; fk0 = k->fs;
    if (!scr && OGPU_FF_FOG(k->fs)) vk1 |= 1UL << 16;
    if (scr) fk1 = (k->vs & (OGPU_FF_COL_ATTR | OGPU_FF_LINEAR)) | 1UL;
    if (!x->text && !(x->text = ogpu_pipe_xport_alloc(PIPE_TEXT_BYTES))) return 0;
    for (i = 0; i < x->nvs; i++) if (x->vs_key[i][0] == vk0 && x->vs_key[i][1] == vk1) break;
    if (i < x->nvs) x->cur.vs = x->vs_h[i];
    else {
        len = scr ? scr_vs_text(k->vs, x->text, PIPE_TEXT_BYTES)
                  : obj_vs_text(k->vs, OGPU_FF_FOG(k->fs) != 0, k->spots, x->text, PIPE_TEXT_BYTES);
        x->cur.vs = make_shader(x, VG_SHADER_VERTEX, len);
        if (x->nvs == PIPE_SHADERS) x->nvs = 0;     /* the table starts again; the old ones stay on the host */
        x->vs_key[x->nvs][0] = vk0; x->vs_key[x->nvs][1] = vk1; x->vs_h[x->nvs] = x->cur.vs;
        x->nvs++;
    }
    for (i = 0; i < x->nfs; i++) if (x->fs_key[i][0] == fk0 && x->fs_key[i][1] == fk1) break;
    if (i < x->nfs) x->cur.fs = x->fs_h[i];
    else {
        len = scr ? scr_fs_text(k->vs, k->fs, x->text, PIPE_TEXT_BYTES) : obj_fs_text(k->fs, x->text, PIPE_TEXT_BYTES);
        x->cur.fs = make_shader(x, VG_SHADER_FRAGMENT, len);
        if (x->nfs == PIPE_SHADERS) x->nfs = 0;
        x->fs_key[x->nfs][0] = fk0; x->fs_key[x->nfs][1] = fk1; x->fs_h[x->nfs] = x->cur.fs;
        x->nfs++;
    }
    return &x->cur;
}

void pipe_m_bind_shader(OGPUPipeCtx *x, OGPUPipeShader *sh)
{
    ULONG *p;
    if (!x || !sh) return;
    if (sh->vs != x->b_vs) {
        if ((p = pipe_cmd(x, VGC_BIND_SHADER_WORDS)) != 0) vgc_bind_shader(p, sh->vs, VG_SHADER_VERTEX);
        x->b_vs = sh->vs;
    }
    if (sh->fs != x->b_fs) {
        if ((p = pipe_cmd(x, VGC_BIND_SHADER_WORDS)) != 0) vgc_bind_shader(p, sh->fs, VG_SHADER_FRAGMENT);
        x->b_fs = sh->fs;
    }
}

/* For the differential test (tests/pipe/test_ff.c): a key's text, as ff_shader makes it. */
ULONG pipe_ff_text(const struct OGPUFFKey *k, int fragment, char *buf, ULONG cap)
{
    if (OGPU_FF_POS(k->vs))
        return fragment ? scr_fs_text(k->vs, k->fs, buf, cap) : scr_vs_text(k->vs, buf, cap);
    return fragment ? obj_fs_text(k->fs, buf, cap) : obj_vs_text(k->vs, OGPU_FF_FOG(k->fs) != 0, k->spots, buf, cap);
}
