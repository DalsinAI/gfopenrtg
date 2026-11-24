/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * minigl.library as a face of GL.module: the GL calls that can't be forwarded as they come, because MiniGL's
 * differ from OpenGL's (docs/design/MiniGL-Compatible-Spec.md, sections 4 and 9):
 *
 *   - GLboolean is four bytes in MiniGL and one in the module: arguments (any non-zero long is true), answers
 *     and the elements of arrays;
 *   - MiniGL's own capabilities, hint targets and texel types (MGL_ tokens), which the module doesn't know;
 *   - paletted textures (EXT_paletted_texture, the shared palette), which the module doesn't have: textures are
 *     unpacked here into R, G, B, A bytes (../mgl_unpack.c, as MiniGL's own renderer reads them), indices through
 *     the palette of the moment;
 *   - glRotatefEXT and glRotatefEXTs, which are MiniGL's alone;
 *   - glGetString, whose answers are MiniGL's (GL_VERSION is "1.1" exactly, spec 9.1).
 *
 * The names are the library's own for its entries (GLColorMask for glColorMask); the generated table
 * (mglw_table.c) calls them and leaves a NULL context out.
 *
 * Not carried over from MiniGL's own renderer: a palette changed after its textures were loaded doesn't reach
 * them (the indices aren't kept), and MGL_UBYTE_ARGB colour arrays are refused (GL_INVALID_ENUM). */
#include <exec/types.h>
#include <proto/dos.h>

#include "mglw_session.h"

#define CAP_SHAREDPAL   1UL
#define CAP_PERSPECTIVE 2UL
#define CAP_ZOFFSET     4UL
#define CAP_ARRAYTRANS  8UL

static ULONG FL(float f)
{
    union { float f; ULONG u; } v;
    v.f = f;
    return v.u;
}

static void current(GLcontext c)
{
    if (c->s->current != (struct mglw_context *)c) mglw_switch((struct mglw_context *)c);
}

#define GL(c, name) MGLW_F(c, name)
#define CALL(c, name, n, ...) (current(c), mglw_invoke((c)->s, GL(c, name), (n), __VA_ARGS__))
#define CALL0(c, name) (current(c), mglw_invoke((c)->s, GL(c, name), 0))

/* ---- booleans ------------------------------------------------------------------------------ */

void GLColorMask(GLcontext c, GLboolean r, GLboolean g, GLboolean b, GLboolean a)
{
    CALL(c, glColorMask, 4, (ULONG)(r != 0), (ULONG)(g != 0), (ULONG)(b != 0), (ULONG)(a != 0));
}

void GLDepthMask(GLcontext c, GLboolean flag) { CALL(c, glDepthMask, 1, (ULONG)(flag != 0)); }
void GLEdgeFlag(GLcontext c, GLboolean flag) { CALL(c, glEdgeFlag, 1, (ULONG)(flag != 0)); }
void GLEdgeFlagv(GLcontext c, const GLboolean *flag) { if (flag) GLEdgeFlag(c, *flag); }

/* The array's elements are longs here and bytes in the module: on the 68k a long's low byte is its last, so the
 * module reads every fourth byte from there. (A flag is 0 or 1; a long with only higher bits set would read as
 * false, and no program writes one.) */
void GLEdgeFlagPointer(GLcontext c, GLsizei stride, const GLvoid *ptr)
{
    CALL(c, glEdgeFlagPointer, 2, (ULONG)(stride ? stride : (GLsizei)sizeof(GLboolean)),
         (ULONG)((const UBYTE *)ptr + sizeof(GLboolean) - 1));
}

/* The module writes one byte a value and doesn't say how many: the bytes start as a value no answer has, and
 * those it changed are the answers. */
void GLGetBooleanv(GLcontext c, GLenum pname, GLboolean *params)
{
    UBYTE b[16];
    int i;
    if (!params) return;
    for (i = 0; i < 16; i++) b[i] = 0xAA;
    CALL(c, glGetBooleanv, 2, (ULONG)pname, (ULONG)b);
    for (i = 0; i < 16 && b[i] != 0xAA; i++) params[i] = b[i] ? GL_TRUE : GL_FALSE;
}

GLboolean GLAreTexturesResident(GLcontext c, GLsizei n, const GLuint *textures, GLboolean *residences)
{
    GLboolean all = GL_TRUE;
    GLsizei i;
    if (n <= 0 || !textures) return GL_FALSE;
    for (i = 0; i < n; i++) {
        UBYTE one = 1;
        ULONG r = CALL(c, glAreTexturesResident, 3, 1, (ULONG)&textures[i], (ULONG)&one) & 0xFF;
        if (!r) all = GL_FALSE;
        if (residences) residences[i] = (r || one) ? GL_TRUE : GL_FALSE;
    }
    return all;
}

/* ---- MiniGL's own capabilities and hints ---------------------------------------------------- */

/* The bit for a capability that is MiniGL's alone, or 0. */
static ULONG own_cap(GLenum cap)
{
    switch (cap) {
    case GL_SHARED_TEXTURE_PALETTE_EXT: return CAP_SHAREDPAL;
    case MGL_PERSPECTIVE_MAPPING: return CAP_PERSPECTIVE;
    case MGL_Z_OFFSET: return CAP_ZOFFSET;
    case MGL_ARRAY_TRANSFORMATIONS: return CAP_ARRAYTRANS;
    }
    return 0;
}

static GLenum gl_cap(GLenum cap) { return cap == GL_POLYGON_OFFSET ? GL_POLYGON_OFFSET_FILL : cap; }

/* mglSetZOffset's offset, on while MGL_Z_OFFSET is: as the module's polygon offset (in its depth units). */
static void zoffset(GLcontext c)
{
    if (c->w_caps & CAP_ZOFFSET) {
        CALL(c, glPolygonOffset, 2, FL(0.0f), FL(c->zoffset));
        CALL(c, glEnable, 1, GL_POLYGON_OFFSET_FILL);
    } else {
        CALL(c, glDisable, 1, GL_POLYGON_OFFSET_FILL);
    }
}

void GLEnable(GLcontext c, GLenum cap)
{
    ULONG own = own_cap(cap);
    if (!own) { CALL(c, glEnable, 1, (ULONG)gl_cap(cap)); return; }
    c->w_caps |= own;
    if (own == CAP_ZOFFSET) zoffset(c);
}

void GLDisable(GLcontext c, GLenum cap)
{
    ULONG own = own_cap(cap);
    if (!own) { CALL(c, glDisable, 1, (ULONG)gl_cap(cap)); return; }
    c->w_caps &= ~own;
    if (own == CAP_ZOFFSET) zoffset(c);
}

GLboolean GLIsEnabled(GLcontext c, GLenum cap)
{
    ULONG own = own_cap(cap);
    if (own) return (c->w_caps & own) ? GL_TRUE : GL_FALSE;
    return (CALL(c, glIsEnabled, 1, (ULONG)gl_cap(cap)) & 0xFF) ? GL_TRUE : GL_FALSE;
}

void GLHint(GLcontext c, GLenum target, GLenum mode)
{
    if (target == MGL_W_ONE_HINT || target == MGL_FIXPOINTTRANS_HINT) return;  /* nothing here depends on them */
    CALL(c, glHint, 2, (ULONG)target, (ULONG)mode);
}

/* MGL_UBYTE_BGRA: four bytes B, G, R, A, which is the module's size GL_BGRA with unsigned bytes. */
void GLColorPointer(GLcontext c, GLint size, GLenum type, GLsizei stride, const GLvoid *ptr)
{
    if (type == MGL_UBYTE_BGRA) { size = GL_BGRA; type = GL_UNSIGNED_BYTE; }
    else if (type == MGL_UBYTE_ARGB) { mgl_error(c, GL_INVALID_ENUM); return; }
    CALL(c, glColorPointer, 4, (ULONG)size, (ULONG)type, (ULONG)stride, (ULONG)ptr);
}

GLenum GLGetError(GLcontext c)
{
    GLenum e = c->error;
    if (e) { c->error = 0; return e; }
    return (GLenum)CALL0(c, glGetError);
}

/* ---- strings -------------------------------------------------------------------------------- */

const GLubyte *GLGetString(GLcontext c, GLenum name)
{
    switch (name) {
    case GL_VENDOR: return (const GLubyte *)"AmiCradle Team (GFOpenRTG)";
    case GL_RENDERER:
        return (const GLubyte *)(c->route == ROUTE_VIRGL
                                 ? "AmiCradle GL 1.1 for minigl.library programs, on the host's GPU (virgl)"
                                 : "AmiCradle GL 1.1 for minigl.library programs, on the 68k");
    case GL_VERSION: return (const GLubyte *)"1.1";     /* exactly (spec 9.1) */
    case GL_EXTENSIONS:
        return (const GLubyte *)"GL_MGL_ARB_multitexture GL_EXT_compiled_vertex_array GL_EXT_paletted_texture "
                                "GL_EXT_shared_texture_palette GL_EXT_color_table GL_EXT_texture_env_add "
                                "GL_SGIS_texture_edge_clamp GL_EXT_bgra";
    }
    mgl_error(c, GL_INVALID_ENUM);
    return 0;
}

/* ---- arrays and matrices -------------------------------------------------------------------- */

void GLLockArrays(GLcontext c, GLuint first, GLsizei count) { CALL(c, glLockArraysEXT, 2, (ULONG)first, (ULONG)count); }
void GLUnlockArrays(GLcontext c) { CALL0(c, glUnlockArraysEXT); }

/* The axis: 0 x, 1 y, 2 z (as MiniGL's own renderer reads it). */
void GLRotatefEXT(GLcontext c, GLfloat angle, const GLint axis)
{
    CALL(c, glRotatef, 4, FL(angle), FL(axis == 1 || axis == 2 ? 0.0f : 1.0f), FL(axis == 1 ? 1.0f : 0.0f),
         FL(axis == 2 ? 1.0f : 0.0f));
}

/* The rotation from its sine and cosine: the matrix itself (column-major, as OpenGL's). */
void GLRotatefEXTs(GLcontext c, GLfloat s, GLfloat k, const GLint axis)
{
    float m[16];
    int i;
    for (i = 0; i < 16; i++) m[i] = (i % 5) ? 0.0f : 1.0f;
    if (axis == 1) { m[0] = k; m[2] = -s; m[8] = s; m[10] = k; }           /* about y */
    else if (axis == 2) { m[0] = k; m[1] = s; m[4] = -s; m[5] = k; }       /* about z */
    else { m[5] = k; m[6] = s; m[9] = -s; m[10] = k; }                     /* about x */
    CALL(c, glMultMatrixf, 1, (ULONG)m);
}

/* ---- pixels and textures -------------------------------------------------------------------- */

/* The unpack settings stay here, where textures are unpacked: the module always gets tight rows of R, G, B, A
 * bytes, which its own defaults read. The rest (the pack settings, for glReadPixels) are the module's. */
void GLPixelStorei(GLcontext c, GLenum pname, GLint param)
{
    switch (pname) {
    case GL_UNPACK_ALIGNMENT:
        if (param != 1 && param != 2 && param != 4 && param != 8) { mgl_error(c, GL_INVALID_VALUE); return; }
        c->unpack_align = param;
        return;
    case GL_UNPACK_ROW_LENGTH: if (param < 0) { mgl_error(c, GL_INVALID_VALUE); return; } c->unpack_row = param; return;
    case GL_UNPACK_SKIP_ROWS: if (param < 0) { mgl_error(c, GL_INVALID_VALUE); return; } c->unpack_skip_rows = param; return;
    case GL_UNPACK_SKIP_PIXELS: if (param < 0) { mgl_error(c, GL_INVALID_VALUE); return; } c->unpack_skip_px = param; return;
    }
    CALL(c, glPixelStorei, 2, (ULONG)pname, (ULONG)param);
}

static int is_paletted(GLenum internal)
{
    return internal == GL_COLOR_INDEX || (internal >= GL_COLOR_INDEX1_EXT && internal <= GL_COLOR_INDEX16_EXT);
}

static const ULONG *palette_now(GLcontext c)
{
    return c->w_texpal_set && !(c->w_caps & CAP_SHAREDPAL) ? c->w_texpal : c->palette;
}

/* w x h of the program's pixels as tight R, G, B, A bytes (mgl_free when done), or 0: *err says why. */
static UBYTE *unpack(GLcontext c, int w, int h, GLenum format, GLenum type, const void *pixels, GLenum *err)
{
    UBYTE *out;
    *err = GL_OUT_OF_MEMORY;
    if (format == GL_COLOR_INDEX) {
        /* a byte an index, through the palette of the moment */
        const ULONG *pal = palette_now(c);
        ULONG rowlen = c->unpack_row ? (ULONG)c->unpack_row : (ULONG)w;
        ULONG rowbytes = (rowlen + (ULONG)c->unpack_align - 1) & ~((ULONG)c->unpack_align - 1);
        const UBYTE *src = (const UBYTE *)pixels + (ULONG)c->unpack_skip_rows * rowbytes + (ULONG)c->unpack_skip_px;
        UBYTE *o;
        int x, y;
        if (type != GL_UNSIGNED_BYTE) { *err = GL_INVALID_ENUM; return 0; }
        if (!(out = mgl_alloc((ULONG)w * (ULONG)h * 4))) return 0;
        for (o = out, y = 0; y < h; y++) {
            const UBYTE *p = src + (ULONG)y * rowbytes;
            for (x = 0; x < w; x++, o += 4) {
                ULONG v = pal[p[x]];
                o[0] = (UBYTE)(v >> 16); o[1] = (UBYTE)(v >> 8); o[2] = (UBYTE)v; o[3] = (UBYTE)(v >> 24);
            }
        }
        return out;
    }
    if (!mgl_pixel_bytes(format, type)) { *err = GL_INVALID_ENUM; return 0; }
    return mgl_unpack_rgba(c, w, h, format, type, pixels);
}

void GLTexImage2D(GLcontext c, GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height,
                  GLint border, GLenum format, GLenum type, const GLvoid *pixels)
{
    UBYTE *rgba = 0;
    GLenum err;
    if (is_paletted((GLenum)internalformat)) internalformat = GL_RGBA;
    if (pixels) {
        if (width < 1 || height < 1) { mgl_error(c, GL_INVALID_VALUE); return; }
        if (!(rgba = unpack(c, width, height, format, type, pixels, &err))) { mgl_error(c, err); return; }
    }
    current(c);
    mglw_invoke(c->s, GL(c, glTexImage2D), 9, (ULONG)target, (ULONG)level, (ULONG)internalformat, (ULONG)width,
                (ULONG)height, (ULONG)border, GL_RGBA, GL_UNSIGNED_BYTE, (ULONG)rgba);
    mgl_free(rgba);
}

void GLTexSubImage2D(GLcontext c, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width,
                     GLsizei height, GLenum format, GLenum type, const GLvoid *pixels)
{
    UBYTE *rgba;
    GLenum err;
    if (!pixels) return;
    if (width < 1 || height < 1) { mgl_error(c, GL_INVALID_VALUE); return; }
    if (!(rgba = unpack(c, width, height, format, type, pixels, &err))) { mgl_error(c, err); return; }
    current(c);
    mglw_invoke(c->s, GL(c, glTexSubImage2D), 9, (ULONG)target, (ULONG)level, (ULONG)xoffset, (ULONG)yoffset,
                (ULONG)width, (ULONG)height, GL_RGBA, GL_UNSIGNED_BYTE, (ULONG)rgba);
    mgl_free(rgba);
}

/* A palette: the shared one (GL_SHARED_TEXTURE_PALETTE_EXT, GL_COLOR_TABLE) or a texture's (GL_TEXTURE_2D), for
 * the textures loaded after it. */
void GLColorTable(GLcontext c, GLenum target, GLenum internalformat, GLint width, GLenum format, GLenum type,
                  GLvoid *data)
{
    ULONG *pal;
    const UBYTE *p = data;
    int i, pb;
    (void)internalformat;
    if (!data) return;
    if (width < 1 || width > 256) { mgl_error(c, GL_INVALID_VALUE); return; }
    if (!(pb = mgl_pixel_bytes(format, type))) { mgl_error(c, GL_INVALID_ENUM); return; }
    if (target == GL_TEXTURE_2D) { pal = c->w_texpal; c->w_texpal_set = 1; }
    else if (target == GL_SHARED_TEXTURE_PALETTE_EXT || target == GL_COLOR_TABLE) pal = c->palette;
    else { mgl_error(c, GL_INVALID_ENUM); return; }
    for (i = 0; i < width; i++, p += pb) {
        UBYTE rgba[4];
        mgl_read_pixel(p, format, type, rgba, 0);
        pal[i] = ((ULONG)rgba[3] << 24) | ((ULONG)rgba[0] << 16) | ((ULONG)rgba[1] << 8) | rgba[2];
    }
}

/* ---- GLU's two matrices, and the mgl* calls that were the renderer's ------------------------- */

/* gluPerspective and gluLookAt (current context: they take none): the matrix made here and multiplied in, so no
 * double crosses to the module. */
void GLUPerspective(GLfloat fovy, GLfloat aspect, GLfloat znear, GLfloat zfar)
{
    GLcontext c = mgl_current;
    float t, m[16];
    int i;
    if (!c) return;
    t = mgl_tan(fovy * (MGL_PI / 360.0f));
    if (t == 0.0f || aspect == 0.0f || znear == zfar) return;
    for (i = 0; i < 16; i++) m[i] = 0.0f;
    m[0] = 1.0f / (t * aspect);
    m[5] = 1.0f / t;
    m[10] = (zfar + znear) / (znear - zfar);
    m[11] = -1.0f;
    m[14] = 2.0f * zfar * znear / (znear - zfar);
    CALL(c, glMultMatrixf, 1, (ULONG)m);
}

void GLULookAt(GLfloat ex, GLfloat ey, GLfloat ez, GLfloat cx, GLfloat cy, GLfloat cz, GLfloat ux, GLfloat uy, GLfloat uz)
{
    GLcontext c = mgl_current;
    float f[3], s[3], u[3], m[16], l;
    if (!c) return;
    f[0] = cx - ex; f[1] = cy - ey; f[2] = cz - ez;
    l = mgl_rsqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    f[0] *= l; f[1] *= l; f[2] *= l;
    s[0] = f[1] * uz - f[2] * uy; s[1] = f[2] * ux - f[0] * uz; s[2] = f[0] * uy - f[1] * ux;
    l = mgl_rsqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
    s[0] *= l; s[1] *= l; s[2] *= l;
    u[0] = s[1] * f[2] - s[2] * f[1]; u[1] = s[2] * f[0] - s[0] * f[2]; u[2] = s[0] * f[1] - s[1] * f[0];
    m[0] = s[0]; m[4] = s[1]; m[8] = s[2];
    m[1] = u[0]; m[5] = u[1]; m[9] = u[2];
    m[2] = -f[0]; m[6] = -f[1]; m[10] = -f[2];
    m[3] = m[7] = m[11] = 0.0f;
    m[12] = -(s[0] * ex + s[1] * ey + s[2] * ez);
    m[13] = -(u[0] * ex + u[1] * ey + u[2] * ez);
    m[14] = f[0] * ex + f[1] * ey + f[2] * ez;
    m[15] = 1.0f;
    CALL(c, glMultMatrixf, 1, (ULONG)m);
}

void MGLSetState(GLcontext c, GLenum cap, GLboolean state)
{
    if (state) GLEnable(c, cap);
    else GLDisable(c, cap);
}

void MGLSetZOffset(GLcontext c, GLfloat offset)
{
    c->zoffset = offset;
    if (c->w_caps & CAP_ZOFFSET) zoffset(c);
}

/* The module has its own rule for small triangles: kept, and not used. */
void MGLMinTriArea(GLcontext c, GLfloat area) { c->min_tri_area = area; }

/* The module doesn't say what its textures take. */
void MGLTexMemStat(GLcontext c, GLint *cur, GLint *peak)
{
    (void)c;
    if (cur) *cur = 0;
    if (peak) *peak = 0;
}

/* The matrix on top of a stack, in thousandths. The ones beneath it are the module's own: mglPrintMatrixStack
 * prints the top one too. */
void MGLPrintMatrix(GLcontext c, int mode)
{
    float m[16];
    int r, k;
    for (k = 0; k < 16; k++) m[k] = 0.0f;
    CALL(c, glGetFloatv, 2, (ULONG)(mode == GL_PROJECTION ? 0x0BA7 : mode == GL_TEXTURE ? 0x0BA8 : 0x0BA6), (ULONG)m);
    for (r = 0; r < 4; r++) {
        LONG v[4];
        for (k = 0; k < 4; k++) v[k] = mgl_f2l(m[k * 4 + r] * 1000.0f);
        Printf((STRPTR)"  %8ld %8ld %8ld %8ld  (x1000)\n", v[0], v[1], v[2], v[3]);
    }
}

void MGLPrintMatrixStack(GLcontext c, int mode) { MGLPrintMatrix(c, mode); }
