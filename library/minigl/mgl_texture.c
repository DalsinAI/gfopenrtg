/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * minigl.library: texture objects. Every texture is kept as ARGB32 with
 * its mip levels, which is what the 3D core and a GPU read fastest, and
 * with what its texels mean (OGPU_TEXBASE_: alpha only, luminance ...), so
 * the core's environment sums follow OpenGL's tables. Paletted textures
 * (EXT_paletted_texture) keep their indices too, to follow a new palette.
 *
 * A batch reads texels when it runs: changing a texture the waiting batch
 * draws with runs that batch first. */
#include "mgl_internal.h"

/* ---- objects ------------------------------------------------------------------------------- */

static struct mgl_tex *find(GLcontext c, GLuint name)
{
    struct mgl_tex *t;
    for (t = c->tex_hash[name & 63]; t; t = t->next) if (t->name == name) return t;
    return 0;
}

static struct mgl_tex *make(GLcontext c, GLuint name)
{
    struct mgl_tex *t = mgl_alloc(sizeof *t);
    if (!t) { mgl_error(c, GL_OUT_OF_MEMORY); return 0; }
    t->name = name;
    t->min = GL_NEAREST_MIPMAP_LINEAR;
    t->mag = GL_LINEAR;
    t->wrap_s = t->wrap_t = GL_REPEAT;
    t->priority = 1.0f;
    t->next = c->tex_hash[name & 63];
    c->tex_hash[name & 63] = t;
    if (name >= c->next_name) c->next_name = name + 1;
    return t;
}

/* Before texels the waiting batch reads change. */
static void before_change(GLcontext c, struct mgl_tex *t)
{
    if (c->body && t->used_in == c->batch_no) { mgl_pipe_flush(c); mgl_flush(c); }
    if (c->sent_tex == t) c->sent_tex = 0;
    if (c->vg) mgl_vg_tex_before(c, t);
}

static void free_levels(GLcontext c, struct mgl_tex *t, int from)
{
    int i;
    for (i = from; i < MGL_MAX_LEVELS; i++) {
        if (t->level[i]) { c->tex_bytes -= (ULONG)t->lw[i] * t->lh[i] * 4; mgl_free(t->level[i]); t->level[i] = 0; }
        if (t->index[i]) { mgl_free(t->index[i]); t->index[i] = 0; }
        t->lw[i] = t->lh[i] = 0;
    }
}

static void free_tex(GLcontext c, struct mgl_tex *t)
{
    mgl_vg_tex_free(c, t);
    free_levels(c, t, 0);
    mgl_free(t->pal);
    t->pal = 0;
}

void mgl_free_textures(GLcontext c)
{
    int i, u;
    for (i = 0; i < 64; i++) {
        struct mgl_tex *t = c->tex_hash[i], *n;
        for (; t; t = n) { n = t->next; free_tex(c, t); mgl_free(t); }
        c->tex_hash[i] = 0;
    }
    for (u = 0; u < MGL_TEX_UNITS; u++) { free_tex(c, &c->tex0[u]); c->bound[u] = &c->tex0[u]; }
}

void GLGenTextures(GLcontext c, GLsizei n, GLuint *textures)
{
    GLsizei i;
    if (!c || !textures) return;
    if (n < 0) { mgl_error(c, GL_INVALID_VALUE); return; }
    for (i = 0; i < n; i++) {
        while (find(c, c->next_name)) c->next_name++;
        textures[i] = c->next_name++;
        make(c, textures[i]);
    }
}

/* Names at or above mglChooseTextureBufferSize's number (spec 9.2). */
static int name_refused(GLuint name) { return mgl_prefs.tex_names && name >= mgl_prefs.tex_names; }

void GLDeleteTextures(GLcontext c, GLsizei n, const GLuint *textures)
{
    GLsizei i;
    int u;
    if (!c || !textures) return;
    for (i = 0; i < n; i++) {
        struct mgl_tex **pp, *t;
        if (!textures[i] || name_refused(textures[i])) continue;
        for (pp = &c->tex_hash[textures[i] & 63]; (t = *pp) != 0; pp = &t->next)
            if (t->name == textures[i]) break;
        if (!t) continue;
        before_change(c, t);
        for (u = 0; u < MGL_TEX_UNITS; u++) if (c->bound[u] == t) c->bound[u] = &c->tex0[u];
        *pp = t->next;
        free_tex(c, t);
        mgl_free(t);
    }
}

void GLBindTexture(GLcontext c, GLenum target, GLuint texture)
{
    struct mgl_tex *t;
    ULONG a[2];
    a[0] = target; a[1] = texture;
    MGL_REC(c, OP_BIND, 2, a);
    if (target != GL_TEXTURE_2D) { mgl_error(c, GL_INVALID_ENUM); return; }
    if (name_refused(texture)) { mgl_error(c, GL_INVALID_VALUE); return; }
    if (!texture) t = &c->tex0[c->active];
    else if (!(t = find(c, texture)) && !(t = make(c, texture))) return;
    if (c->bound[c->active] != t) {
        mgl_pipe_flush(c);
        c->bound[c->active] = t;
    }
}

GLboolean GLIsTexture(GLcontext c, GLuint texture)
{
    return c && texture && !name_refused(texture) && find(c, texture) ? GL_TRUE : GL_FALSE;
}

GLboolean GLAreTexturesResident(GLcontext c, GLsizei n, const GLuint *textures, GLboolean *residences)
{
    GLsizei i;
    if (!c || !textures) return GL_FALSE;
    for (i = 0; i < n; i++) if (residences) residences[i] = find(c, textures[i]) ? GL_TRUE : GL_FALSE;
    return GL_TRUE;
}

void GLPrioritizeTextures(GLcontext c, GLsizei n, const GLuint *textures, const GLclampf *priorities)
{
    GLsizei i;
    if (!c || !textures || !priorities) return;
    for (i = 0; i < n; i++) {
        struct mgl_tex *t = find(c, textures[i]);
        if (t) t->priority = priorities[i];
    }
}

void MGLTexMemStat(GLcontext c, GLint *cur, GLint *peak)
{
    if (!c) return;
    if (cur) *cur = (GLint)c->tex_bytes;
    if (peak) *peak = (GLint)c->tex_peak;
}

/* The texture a unit draws with, or 0 (disabled, or no texels). */
struct mgl_tex *mgl_tex_unit(GLcontext c, int unit)
{
    struct mgl_tex *t;
    if (!(c->en & (unit ? EN_TEX1 : EN_TEX0))) return 0;
    t = c->bound[unit];
    return t && t->level[0] ? t : 0;
}

/* ---- texels ---------------------------------------------------------------------------------- */

static int base_of(GLenum internal, int *ok)
{
    *ok = 1;
    switch (internal) {
    case 1: case GL_LUMINANCE: case GL_LUMINANCE4: case GL_LUMINANCE8: return OGPU_TEXBASE_LUMINANCE;
    case 2: case GL_LUMINANCE_ALPHA: case GL_LUMINANCE4_ALPHA4: case GL_LUMINANCE8_ALPHA8: return OGPU_TEXBASE_LUM_ALPHA;
    case 3: case GL_RGB: case GL_R3_G3_B2: case GL_RGB4: case GL_RGB5: case GL_RGB8: case GL_BGR: return OGPU_TEXBASE_RGB;
    case 4: case GL_RGBA: case GL_RGBA2: case GL_RGBA4: case GL_RGB5_A1: case GL_RGBA8: case GL_BGRA: return OGPU_TEXBASE_RGBA;
    case GL_ALPHA: case GL_ALPHA4: case GL_ALPHA8: return OGPU_TEXBASE_ALPHA;
    case GL_INTENSITY: case GL_INTENSITY4: case GL_INTENSITY8: return OGPU_TEXBASE_INTENSITY;
    case GL_COLOR_INDEX: case GL_COLOR_INDEX1_EXT: case GL_COLOR_INDEX2_EXT: case GL_COLOR_INDEX4_EXT:
    case GL_COLOR_INDEX8_EXT: case GL_COLOR_INDEX12_EXT: case GL_COLOR_INDEX16_EXT: return OGPU_TEXBASE_RGBA;
    }
    *ok = 0;
    return OGPU_TEXBASE_RGBA;
}

static int is_paletted(GLenum internal)
{
    return internal == GL_COLOR_INDEX || (internal >= GL_COLOR_INDEX1_EXT && internal <= GL_COLOR_INDEX16_EXT);
}

/* What the stored texel is, by the texture's base format. */
static ULONG store_texel(int base, const UBYTE *s)
{
    switch (base) {
    case OGPU_TEXBASE_RGB: return 0xFF000000UL | ((ULONG)s[0] << 16) | ((ULONG)s[1] << 8) | s[2];
    case OGPU_TEXBASE_ALPHA: return ((ULONG)s[3] << 24) | 0x00FFFFFFUL;
    case OGPU_TEXBASE_LUMINANCE: return 0xFF000000UL | ((ULONG)s[0] << 16) | ((ULONG)s[0] << 8) | s[0];
    case OGPU_TEXBASE_LUM_ALPHA: return ((ULONG)s[3] << 24) | ((ULONG)s[0] << 16) | ((ULONG)s[0] << 8) | s[0];
    case OGPU_TEXBASE_INTENSITY: return ((ULONG)s[0] << 24) | ((ULONG)s[0] << 16) | ((ULONG)s[0] << 8) | s[0];
    }
    return ((ULONG)s[3] << 24) | ((ULONG)s[0] << 16) | ((ULONG)s[1] << 8) | s[2];
}

static const ULONG *palette_of(GLcontext c, struct mgl_tex *t)
{
    return t->pal && !(c->en & EN_SHAREDPAL) ? t->pal : c->palette;
}

/* A paletted level's texels again, from its indices. */
static void repaint(GLcontext c, struct mgl_tex *t, int lv)
{
    const ULONG *pal = palette_of(c, t);
    ULONG i, n = (ULONG)t->lw[lv] * t->lh[lv];
    if (!t->index[lv] || !t->level[lv]) return;
    for (i = 0; i < n; i++) PUTW(&t->level[lv][i], pal[t->index[lv][i]]);
}

/* Texels into level `level` of t: a whole new image (sub = 0, w x h) or a
 * rectangle of it at x0, y0 (sub = 1). */
void mgl_tex_store(GLcontext c, struct mgl_tex *t, int level, int w, int h, GLenum internal, GLenum format,
                   GLenum type, const void *pixels, int x0, int y0, int sub)
{
    int pb = mgl_pixel_bytes(format, type), x, y, ok, pal;
    ULONG rowlen, rowbytes;
    const UBYTE *src = pixels;
    if (!pb) { mgl_error(c, GL_INVALID_ENUM); return; }
    before_change(c, t);
    if (!sub) {
        int base = base_of(internal, &ok);
        if (!ok) { mgl_error(c, GL_INVALID_VALUE); return; }
        if (level == 0) {
            if (t->w != w || t->h != h || t->internal != internal) free_levels(c, t, 0);
            t->w = w; t->h = h; t->internal = internal; t->base = base;
        } else if (t->internal != internal && t->level[0]) {
            /* levels of another format than level 0: taken as level 0's */
            internal = t->internal;
        }
        if (t->level[level] && (t->lw[level] != w || t->lh[level] != h)) {
            c->tex_bytes -= (ULONG)t->lw[level] * t->lh[level] * 4;
            mgl_free(t->level[level]); t->level[level] = 0;
            mgl_free(t->index[level]); t->index[level] = 0;
        }
        if (!t->level[level]) {
            t->level[level] = mgl_alloc((ULONG)w * h * 4);
            if (!t->level[level]) { mgl_error(c, GL_OUT_OF_MEMORY); return; }
            t->lw[level] = w; t->lh[level] = h;
            c->tex_bytes += (ULONG)w * h * 4;
            if (c->tex_bytes > c->tex_peak) c->tex_peak = c->tex_bytes;
        }
        x0 = y0 = 0;
    }
    pal = is_paletted(t->internal) || format == GL_COLOR_INDEX;
    if (pal && !t->index[level]) {
        t->index[level] = mgl_alloc((ULONG)t->lw[level] * t->lh[level]);
        if (!t->index[level]) { mgl_error(c, GL_OUT_OF_MEMORY); return; }
    }
    t->generation++;
    if (c->vg) mgl_vg_tex_changed(c, t, level, x0, y0, sub ? w : t->lw[level], sub ? h : t->lh[level]);
    if (!src) return;                           /* storage only */
    rowlen = c->unpack_row ? (ULONG)c->unpack_row : (ULONG)w;
    rowbytes = rowlen * (ULONG)pb;
    rowbytes = (rowbytes + (ULONG)c->unpack_align - 1) & ~((ULONG)c->unpack_align - 1);
    src += (ULONG)c->unpack_skip_rows * rowbytes + (ULONG)c->unpack_skip_px * (ULONG)pb;
    for (y = 0; y < h; y++) {
        const UBYTE *p = src + (ULONG)y * rowbytes;
        int ty = y0 + y;
        ULONG *dst;
        if (ty < 0 || ty >= t->lh[level]) continue;
        dst = t->level[level] + (ULONG)ty * t->lw[level];
        for (x = 0; x < w; x++, p += pb) {
            UBYTE rgba[4], ix = 0;
            int tx = x0 + x;
            if (tx < 0 || tx >= t->lw[level]) continue;
            mgl_read_pixel(p, format, type, rgba, &ix);
            if (pal) {
                t->index[level][(ULONG)ty * t->lw[level] + tx] = ix;
                PUTW(&dst[tx], palette_of(c, t)[ix]);
            } else PUTW(&dst[tx], store_texel(t->base, rgba));
        }
    }
}

static int pow2(int v) { return v > 0 && !(v & (v - 1)); }

void GLTexImage2D(GLcontext c, GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height,
                  GLint border, GLenum format, GLenum type, const GLvoid *pixels)
{
    struct mgl_tex *t;
    if (!c) return;
    if (target != GL_TEXTURE_2D) { mgl_error(c, GL_INVALID_ENUM); return; }
    if (level < 0 || level >= MGL_MAX_LEVELS || border != 0 || width < 1 || height < 1
        || width > 2048 || height > 2048 || !pow2(width) || !pow2(height)) { mgl_error(c, GL_INVALID_VALUE); return; }
    t = c->bound[c->active];
    mgl_tex_store(c, t, level, width, height, (GLenum)internalformat, format, type, pixels, 0, 0, 0);
}

void GLTexSubImage2D(GLcontext c, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width,
                     GLsizei height, GLenum format, GLenum type, const GLvoid *pixels)
{
    struct mgl_tex *t;
    if (!c) return;
    if (target != GL_TEXTURE_2D) { mgl_error(c, GL_INVALID_ENUM); return; }
    t = c->bound[c->active];
    if (level < 0 || level >= MGL_MAX_LEVELS || !t->level[level]) { mgl_error(c, GL_INVALID_OPERATION); return; }
    if (xoffset < 0 || yoffset < 0 || xoffset + width > t->lw[level] || yoffset + height > t->lh[level]) {
        mgl_error(c, GL_INVALID_VALUE); return;
    }
    mgl_tex_store(c, t, level, width, height, t->internal, format, type, pixels, xoffset, yoffset, 1);
}

/* From the drawing area: x, y with OpenGL's origin at the bottom left. */
static void copy_from_area(GLcontext c, struct mgl_tex *t, int level, int dx, int dy, int x, int y, int w, int h)
{
    ULONG *buf, i;
    int row;
    if (w <= 0 || h <= 0) return;
    buf = mgl_alloc((ULONG)w * h * 4);
    if (!buf) { mgl_error(c, GL_OUT_OF_MEMORY); return; }
    mgl_pipe_flush(c);
    mgl_readback(c, x, c->height - y - h, w, h, buf, w);
    /* the read rows are top first; a texture's first row is OpenGL's bottom one */
    for (row = 0; row < h; row++) {
        ULONG *src = buf + (ULONG)(h - 1 - row) * w;
        UBYTE *bytes = (UBYTE *)src;
        for (i = 0; i < (ULONG)w; i++) {
            ULONG v = src[i];
            bytes[i * 4 + 0] = (UBYTE)(v >> 16); bytes[i * 4 + 1] = (UBYTE)(v >> 8);
            bytes[i * 4 + 2] = (UBYTE)v; bytes[i * 4 + 3] = 255;
        }
        {
            GLint keep_row = c->unpack_row, keep_rows = c->unpack_skip_rows, keep_px = c->unpack_skip_px, keep_al = c->unpack_align;
            c->unpack_row = 0; c->unpack_skip_rows = 0; c->unpack_skip_px = 0; c->unpack_align = 1;
            mgl_tex_store(c, t, level, w, 1, t->internal, GL_RGBA, GL_UNSIGNED_BYTE, bytes, dx, dy + row, 1);
            c->unpack_row = keep_row; c->unpack_skip_rows = keep_rows; c->unpack_skip_px = keep_px; c->unpack_align = keep_al;
        }
    }
    mgl_free(buf);
}

void GLCopyTexImage2D(GLcontext c, GLenum target, GLint level, GLenum internalformat, GLint x, GLint y,
                      GLsizei width, GLsizei height, GLint border)
{
    struct mgl_tex *t;
    if (!c) return;
    if (target != GL_TEXTURE_2D) { mgl_error(c, GL_INVALID_ENUM); return; }
    if (level < 0 || level >= MGL_MAX_LEVELS || border || !pow2(width) || !pow2(height)) { mgl_error(c, GL_INVALID_VALUE); return; }
    t = c->bound[c->active];
    mgl_tex_store(c, t, level, width, height, internalformat, GL_RGBA, GL_UNSIGNED_BYTE, 0, 0, 0, 0);
    copy_from_area(c, t, level, 0, 0, x, y, width, height);
}

void GLCopyTexSubImage2D(GLcontext c, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y,
                         GLsizei width, GLsizei height)
{
    struct mgl_tex *t;
    if (!c) return;
    if (target != GL_TEXTURE_2D) { mgl_error(c, GL_INVALID_ENUM); return; }
    t = c->bound[c->active];
    if (level < 0 || level >= MGL_MAX_LEVELS || !t->level[level]) { mgl_error(c, GL_INVALID_OPERATION); return; }
    if (xoffset < 0 || yoffset < 0 || xoffset + width > t->lw[level] || yoffset + height > t->lh[level]) {
        mgl_error(c, GL_INVALID_VALUE); return;
    }
    copy_from_area(c, t, level, xoffset, yoffset, x, y, width, height);
}

void GLTexParameteri(GLcontext c, GLenum target, GLenum pname, GLint param)
{
    struct mgl_tex *t;
    ULONG a[3];
    a[0] = target; a[1] = pname; a[2] = (ULONG)param;
    MGL_REC(c, OP_TEXPARAMI, 3, a);
    if (target != GL_TEXTURE_2D) { mgl_error(c, GL_INVALID_ENUM); return; }
    t = c->bound[c->active];
    switch (pname) {
    case GL_TEXTURE_MIN_FILTER:
        if (param != GL_NEAREST && param != GL_LINEAR && param != GL_NEAREST_MIPMAP_NEAREST && param != GL_LINEAR_MIPMAP_NEAREST
            && param != GL_NEAREST_MIPMAP_LINEAR && param != GL_LINEAR_MIPMAP_LINEAR) { mgl_error(c, GL_INVALID_ENUM); return; }
        t->min = (GLenum)param; break;
    case GL_TEXTURE_MAG_FILTER:
        if (param != GL_NEAREST && param != GL_LINEAR) { mgl_error(c, GL_INVALID_ENUM); return; }
        t->mag = (GLenum)param; break;
    case GL_TEXTURE_WRAP_S: case GL_TEXTURE_WRAP_T:
        if (param != GL_REPEAT && param != GL_CLAMP && param != GL_CLAMP_TO_EDGE) { mgl_error(c, GL_INVALID_ENUM); return; }
        if (pname == GL_TEXTURE_WRAP_S) t->wrap_s = (GLenum)param; else t->wrap_t = (GLenum)param;
        break;
    case GL_TEXTURE_PRIORITY: t->priority = (float)param; return;
    default: mgl_error(c, GL_INVALID_ENUM); return;
    }
    if (c->sent_tex == t) c->sent_tex = 0;
    if (c->vg) mgl_vg_tex_before(c, t);
}

void GLTexEnvi(GLcontext c, GLenum target, GLenum pname, GLint param)
{
    ULONG a[3];
    a[0] = target; a[1] = pname; a[2] = (ULONG)param;
    MGL_REC(c, OP_TEXENVI, 3, a);
    if (target != GL_TEXTURE_ENV || pname != GL_TEXTURE_ENV_MODE) { mgl_error(c, GL_INVALID_ENUM); return; }
    if (param != GL_MODULATE && param != GL_DECAL && param != GL_BLEND && param != GL_REPLACE && param != GL_ADD) {
        mgl_error(c, GL_INVALID_ENUM); return;
    }
    if (c->env_mode[c->active] != (GLenum)param) {
        mgl_pipe_flush(c);
        c->env_mode[c->active] = (GLenum)param;
    }
}

void GLTexEnvfv(GLcontext c, GLenum target, GLenum pname, const GLfloat *params)
{
    ULONG a[6];
    int i;
    if (!params) return;
    a[0] = target; a[1] = pname;
    for (i = 0; i < 4; i++) a[2 + i] = mgl_fu(params[i]);
    MGL_REC(c, OP_TEXENVFV, 6, a);
    if (target != GL_TEXTURE_ENV) { mgl_error(c, GL_INVALID_ENUM); return; }
    if (pname == GL_TEXTURE_ENV_MODE) { GLTexEnvi(c, target, pname, mgl_f2l(params[0])); return; }
    if (pname != GL_TEXTURE_ENV_COLOR) { mgl_error(c, GL_INVALID_ENUM); return; }
    mgl_pipe_flush(c);
    for (i = 0; i < 4; i++) c->env_color[c->active][i] = params[i] < 0 ? 0 : params[i] > 1 ? 1 : params[i];
}

/* ---- texture coordinate generation ---------------------------------------------------------- */

static int coord_index(GLenum coord)
{
    switch (coord) { case GL_S: return 0; case GL_T: return 1; case GL_R: return 2; case GL_Q: return 3; }
    return -1;
}

void GLTexGeni(GLcontext c, GLenum coord, GLenum mode, GLenum map)
{
    ULONG a[3];
    int k;
    a[0] = coord; a[1] = mode; a[2] = map;
    MGL_REC(c, OP_TEXGENI, 3, a);
    k = coord_index(coord);
    if (k < 0 || mode != GL_TEXTURE_GEN_MODE) { mgl_error(c, GL_INVALID_ENUM); return; }
    if (map != GL_OBJECT_LINEAR && map != GL_EYE_LINEAR && map != GL_SPHERE_MAP) { mgl_error(c, GL_INVALID_ENUM); return; }
    if (map == GL_SPHERE_MAP && k > 1) { mgl_error(c, GL_INVALID_ENUM); return; }
    c->gen_mode[c->active][k] = map;
}

void GLTexGenfv(GLcontext c, GLenum coord, GLenum pname, const GLfloat *params)
{
    ULONG a[6];
    int k, i;
    if (!params) return;
    a[0] = coord; a[1] = pname;
    for (i = 0; i < 4; i++) a[2 + i] = mgl_fu(params[i]);
    MGL_REC(c, OP_TEXGENFV, 6, a);
    k = coord_index(coord);
    if (k < 0) { mgl_error(c, GL_INVALID_ENUM); return; }
    switch (pname) {
    case GL_TEXTURE_GEN_MODE: GLTexGeni(c, coord, pname, (GLenum)mgl_f2l(params[0])); return;
    case GL_OBJECT_PLANE: for (i = 0; i < 4; i++) c->gen_obj[c->active][k][i] = params[i]; return;
    case GL_EYE_PLANE: {
        /* the plane times the modelview's inverse, as it is when given */
        float inv[16], *p = c->gen_eye[c->active][k];
        if (!mgl_mat_inverse(inv, c->mv[c->mv_top])) mgl_mat_identity(inv);
        for (i = 0; i < 4; i++)
            p[i] = params[0] * inv[i * 4] + params[1] * inv[i * 4 + 1] + params[2] * inv[i * 4 + 2] + params[3] * inv[i * 4 + 3];
        return;
    }
    }
    mgl_error(c, GL_INVALID_ENUM);
}

/* ---- palettes ------------------------------------------------------------------------------- */

void GLColorTable(GLcontext c, GLenum target, GLenum internalformat, GLint width, GLenum format, GLenum type, GLvoid *data)
{
    ULONG *pal;
    int i, pb, lv;
    const UBYTE *p = data;
    struct mgl_tex *t = 0;
    if (!c || !data) return;
    (void)internalformat;
    if (width < 1 || width > 256) { mgl_error(c, GL_INVALID_VALUE); return; }
    pb = mgl_pixel_bytes(format, type);
    if (!pb) { mgl_error(c, GL_INVALID_ENUM); return; }
    if (target == GL_TEXTURE_2D) {
        t = c->bound[c->active];
        if (!t->pal && !(t->pal = mgl_alloc(256 * 4))) { mgl_error(c, GL_OUT_OF_MEMORY); return; }
        pal = t->pal;
    } else if (target == GL_SHARED_TEXTURE_PALETTE_EXT || target == GL_COLOR_TABLE) pal = c->palette;
    else { mgl_error(c, GL_INVALID_ENUM); return; }
    mgl_pipe_flush(c);
    if (c->body) mgl_flush(c);                  /* paletted textures change */
    for (i = 0; i < width; i++, p += pb) {
        UBYTE rgba[4];
        mgl_read_pixel(p, format, type, rgba, 0);
        pal[i] = ((ULONG)rgba[3] << 24) | ((ULONG)rgba[0] << 16) | ((ULONG)rgba[1] << 8) | rgba[2];
    }
    /* every paletted texture that uses this palette follows it */
    for (i = 0; i < 64 + MGL_TEX_UNITS; i++) {
        struct mgl_tex *x = i < 64 ? c->tex_hash[i] : &c->tex0[i - 64];
        for (; x; x = i < 64 ? x->next : 0) {
            if (!is_paletted(x->internal) && !x->index[0]) continue;
            if (t && x != t) continue;
            if (!t && x->pal && !(c->en & EN_SHAREDPAL)) continue;
            for (lv = 0; lv < MGL_MAX_LEVELS; lv++) repaint(c, x, lv);
            x->generation++;
            if (c->sent_tex == x) c->sent_tex = 0;
        }
    }
}
