/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * Warp3D.library's virgl route: the shaders, our own TGSI text, made from a
 * key when a draw first needs it (w3d_vg.c keeps them for the context).
 *
 * They draw what the 3D core draws (library/opengpu/ogpu_3d.c), in its
 * order: colour (the vertex's, or the current one), texture, chroma test,
 * environment (stream3d.h's modes, with the texture's base as OpenGL's
 * tables have it), specular, fog. The alpha test, stencil, Z, blend, logic
 * op and colour mask are the host's fixed stages (w3d_vg.c's objects).
 *
 * The vertex shader reads the stream's own vertex (stream3d.h's 3D layout)
 * as the vertex fetch converts it: x, y, u, v as 32-bit integers (16.16),
 * the colours as B8G8R8A8 (an ARGB32 word turned round by the host), z as
 * an unsigned 32-bit integer, w as a float. Its outputs: the position in
 * clip space (w = 1 / the vertex's w under perspective, so the host
 * interpolates the texture coordinates as the core does), COLOR, GENERIC[0]
 * the texture coordinates, GENERIC[1] specular RGB and the vertex's fog
 * factor, GENERIC[2] the vertex's w (table fog). */
#include "w3d_vg.h"
#include "../../include/gfopengpu/stream3d.h"

struct sb { char *p; ULONG n, cap; };

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

static void reg(struct sb *b, const char *file, ULONG k, const char *swz)
{
    s(b, file); s(b, "["); sn(b, k); s(b, "]"); s(b, swz);
}

static ULONG done(struct sb *b)
{
    s(b, "END\n");
    b->p[b->n < b->cap ? b->n++ : b->cap - 1] = 0;
    return b->n;
}

/* ---- the vertex shader ----------------------------------------------------------------------- */

ULONG w3dvg_vs_text(ULONG key, char *buf, ULONG cap)
{
    struct sb b;
    ULONG n = 3, i_z = 0, i_w = 0, i_s = 0, o = 3, o_s = 0, o_w = 0, o_c = 1, k;
    int lin = (key & VK_LINEAR) != 0;
    b.p = buf; b.n = 0; b.cap = cap;
    if (key & VK_Z) i_z = n++;
    if (key & VK_W) i_w = n++;
    if (key & VK_SPEC) i_s = n++;
    if (key & VK_SPEC) o_s = o++;
    if (key & VK_W) o_w = o++;
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
    if (key & VK_SPEC) { s(&b, "DCL "); reg(&b, "OUT", o_s, ", GENERIC[1]\n"); }
    if (key & VK_W) { s(&b, "DCL "); reg(&b, "OUT", o_w, ", GENERIC[2]\n"); }
    if (lin) { s(&b, "DCL "); reg(&b, "OUT", o, ", GENERIC[4]\n"); }
    s(&b, "DCL CONST[0..2]\nDCL TEMP[0..1]\n");
    /* x, y: 16.16 pixels to clip space (y down the screen) */
    s(&b, "MAD TEMP[0].xy, IN[0].xyyy, CONST[0].xyyy, CONST[0].zwww\n");
    /* z: 0 .. 2^32 to -1 .. 1 */
    if (key & VK_Z) { s(&b, "MAD TEMP[0].z, "); reg(&b, "IN", i_z, ".xxxx, CONST[1].yyyy, CONST[1].zzzz\n"); }
    else s(&b, "MOV TEMP[0].z, CONST[1].zzzz\n");
    s(&b, "MOV TEMP[0].w, CONST[1].wwww\n");
    if ((key & VK_PERSP) && (key & VK_W)) {
        s(&b, "RCP TEMP[1].x, "); reg(&b, "IN", i_w, ".xxxx\n");
        s(&b, "MUL TEMP[0].xyz, TEMP[0], TEMP[1].xxxx\n");
        s(&b, "MOV TEMP[0].w, TEMP[1].xxxx\n");
    }
    s(&b, "MOV OUT[0], TEMP[0]\n");
    if (lin) { s(&b, "MUL "); reg(&b, "OUT", o_c, ", IN[1], TEMP[0].wwww\n"); }
    else { s(&b, "MOV "); reg(&b, "OUT", o_c, ", IN[1]\n"); }
    s(&b, "MUL OUT[2].xy, IN[2].xyyy, CONST[1].xxxx\n");
    s(&b, "MOV OUT[2].zw, CONST[2].xxxy\n");
    if (key & VK_SPEC) {
        s(&b, lin ? "MUL " : "MOV "); reg(&b, "OUT", o_s, ", "); reg(&b, "IN", i_s, lin ? ", TEMP[0].wwww\n" : "\n");
    }
    if (key & VK_W) {
        s(&b, lin ? "MUL " : "MOV "); reg(&b, "OUT", o_w, ", "); reg(&b, "IN", i_w, lin ? ".xxxx, TEMP[0].wwww\n" : ".xxxx\n");
    }
    if (lin) { s(&b, "MOV "); reg(&b, "OUT", o, ", TEMP[0].wwww\n"); }
    return done(&b);
}

/* ---- the fragment shader --------------------------------------------------------------------- */

/* The texture's environment: TEMP[0] the colour so far, TEMP[1] the texel
 * (ogpu_3d.c's o3_environment, mode by mode). */
static void env(struct sb *b, int mode, int base)
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

ULONG w3dvg_fs_text(ULONG key, char *buf, ULONG cap)
{
    struct sb b;
    ULONG n = 0, i_c = 0, i_t = 0, i_s = 0, i_w = 0;
    int e = (int)FK_ENV(key), fog = (int)FK_FOG(key), chroma = (int)FK_CHROMA(key);
    int spec = (key & FK_SPEC) != 0, vfog = fog == OGPU_FOG_VERTEX, tfog = fog && !vfog;
    int lin = (key & FK_LINEAR) && ((key & FK_GOURAUD) || spec || vfog || tfog);
    ULONG i_k = 0;
    b.p = buf; b.n = 0; b.cap = cap;
    s(&b, "FRAG\nPROPERTY FS_COLOR0_WRITES_ALL_CBUFS 1\n");
    /* FK_LINEAR: the vertex shader's screen-linear values, each times the
     * clip w; TEMP[3].y is 1 / the clip w here, which turns them back. */
    if (key & FK_GOURAUD) { i_c = n++; s(&b, "DCL "); reg(&b, "IN", i_c, lin ? ", GENERIC[3], PERSPECTIVE\n" : ", COLOR, COLOR\n"); }
    if (e) { i_t = n++; s(&b, "DCL "); reg(&b, "IN", i_t, ", GENERIC[0], PERSPECTIVE\n"); }
    if (spec || vfog) { i_s = n++; s(&b, "DCL "); reg(&b, "IN", i_s, ", GENERIC[1], PERSPECTIVE\n"); }
    if (tfog) { i_w = n++; s(&b, "DCL "); reg(&b, "IN", i_w, ", GENERIC[2], PERSPECTIVE\n"); }
    if (lin) { i_k = n++; s(&b, "DCL "); reg(&b, "IN", i_k, ", GENERIC[4], PERSPECTIVE\n"); }
    s(&b, "DCL OUT[0], COLOR\n");
    if (e) s(&b, "DCL SAMP[0]\nDCL SVIEW[0], 2D, FLOAT\n");
    s(&b, "DCL CONST[0..6]\nDCL TEMP[0..6]\n");
    if (lin) { s(&b, "RCP TEMP[4].x, "); reg(&b, "IN", i_k, ".xxxx\n"); }
    if (key & FK_GOURAUD) {
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
        env(&b, e, (int)FK_BASE(key));
    }
    if (spec) s(&b, "ADD_SAT TEMP[0].xyz, TEMP[0], TEMP[5]\n");
    if (fog) {
        if (vfog) s(&b, "MOV TEMP[2].x, TEMP[5].wwww\n");
        else {
            s(&b, "MOV_SAT TEMP[3].x, TEMP[6].xxxx\n");          /* w, 0 .. 1 */
            if (fog == OGPU_FOG_LINEAR) {
                if (key & FK_FOGEQ) s(&b, "SGE TEMP[2].x, TEMP[3].xxxx, CONST[3].xxxx\n");
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
