/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * minigl.library: a program's pixels read into R, G, B, A bytes, for every format and type MiniGL takes
 * (OpenGL 1.1's, the packed types, MiniGL's own MGL_ types, colour indices). Out of mgl_texture.c so that the
 * library's own renderer and the wrapper over GL.module (wrap/) read pixels the same way. */
#include "mgl_internal.h"

/* Bytes per pixel of format and type, or 0 when they don't go together. */
int mgl_pixel_bytes(GLenum format, GLenum type)
{
    int comps;
    switch (type) {
    case GL_UNSIGNED_SHORT_5_6_5: case MGL_UNSIGNED_SHORT_5_6_5: case GL_UNSIGNED_SHORT_4_4_4_4:
    case MGL_UNSIGNED_SHORT_4_4_4_4: case GL_UNSIGNED_SHORT_5_5_5_1: case GL_UNSIGNED_SHORT_4_4_4_4_REV:
    case GL_UNSIGNED_SHORT_1_5_5_5_REV:
        return 2;
    case GL_UNSIGNED_INT_8_8_8_8: case GL_UNSIGNED_INT_8_8_8_8_REV: return 4;
    case GL_UNSIGNED_BYTE_3_3_2: return 1;
    }
    switch (format) {
    case GL_RGB: case GL_BGR: comps = 3; break;
    case GL_RGBA: case GL_BGRA: case MGL_UBYTE_ARGB: case MGL_UBYTE_BGRA: comps = 4; break;
    case GL_LUMINANCE_ALPHA: comps = 2; break;
    case GL_LUMINANCE: case GL_ALPHA: case GL_RED: case GL_GREEN: case GL_BLUE: case GL_COLOR_INDEX:
    case GL_INTENSITY: comps = 1; break;
    default: return 0;
    }
    switch (type) {
    case GL_UNSIGNED_BYTE: case GL_BYTE: return comps;
    case GL_UNSIGNED_SHORT: case GL_SHORT: return comps * 2;
    case GL_UNSIGNED_INT: case GL_INT: case GL_FLOAT: return comps * 4;
    }
    return 0;
}

static UBYTE comp8(const UBYTE *p, GLenum type, int i)
{
    switch (type) {
    case GL_BYTE: { int v = ((const BYTE *)p)[i]; return (UBYTE)(v < 0 ? 0 : v * 2 + (v >> 6)); }
    case GL_UNSIGNED_SHORT: return (UBYTE)(((const UWORD *)p)[i] >> 8);
    case GL_SHORT: { int v = ((const WORD *)p)[i]; return (UBYTE)(v < 0 ? 0 : v >> 7); }
    case GL_UNSIGNED_INT: return (UBYTE)(((const ULONG *)p)[i] >> 24);
    case GL_INT: { LONG v = ((const LONG *)p)[i]; return (UBYTE)(v < 0 ? 0 : v >> 23); }
    case GL_FLOAT: {
        union { ULONG u; float f; } x;
        LONG v;
        x.u = ((const ULONG *)p)[i];
        v = mgl_f2l(x.f * 255.0f);
        return (UBYTE)(v < 0 ? 0 : v > 255 ? 255 : v);
    }
    }
    return p[i];
}

/* One source pixel to R, G, B, A (0..255); `index` for paletted sources. */
void mgl_read_pixel(const UBYTE *p, GLenum format, GLenum type, UBYTE *rgba, UBYTE *index)
{
    ULONG w;
    UBYTE r = 0, g = 0, b = 0, a = 255;
    switch (type) {
    case GL_UNSIGNED_SHORT_5_6_5: case MGL_UNSIGNED_SHORT_5_6_5:
        w = *(const UWORD *)p;
        r = (UBYTE)(((w >> 11) & 31) * 255 / 31); g = (UBYTE)(((w >> 5) & 63) * 255 / 63); b = (UBYTE)((w & 31) * 255 / 31);
        if (format == GL_BGR) { UBYTE t = r; r = b; b = t; }
        goto out;
    case GL_UNSIGNED_SHORT_4_4_4_4: case MGL_UNSIGNED_SHORT_4_4_4_4:
        w = *(const UWORD *)p;
        r = (UBYTE)(((w >> 12) & 15) * 17); g = (UBYTE)(((w >> 8) & 15) * 17); b = (UBYTE)(((w >> 4) & 15) * 17); a = (UBYTE)((w & 15) * 17);
        if (format == GL_BGRA) { UBYTE t = r; r = b; b = t; }
        goto out;
    case GL_UNSIGNED_SHORT_4_4_4_4_REV:
        w = *(const UWORD *)p;
        a = (UBYTE)(((w >> 12) & 15) * 17); b = (UBYTE)(((w >> 8) & 15) * 17); g = (UBYTE)(((w >> 4) & 15) * 17); r = (UBYTE)((w & 15) * 17);
        if (format == GL_BGRA) { UBYTE t = r; r = b; b = t; }
        goto out;
    case GL_UNSIGNED_SHORT_5_5_5_1:
        w = *(const UWORD *)p;
        r = (UBYTE)(((w >> 11) & 31) * 255 / 31); g = (UBYTE)(((w >> 6) & 31) * 255 / 31); b = (UBYTE)(((w >> 1) & 31) * 255 / 31);
        a = (w & 1) ? 255 : 0;
        if (format == GL_BGRA) { UBYTE t = r; r = b; b = t; }
        goto out;
    case GL_UNSIGNED_SHORT_1_5_5_5_REV:
        w = *(const UWORD *)p;
        a = (w & 0x8000) ? 255 : 0; b = (UBYTE)(((w >> 10) & 31) * 255 / 31); g = (UBYTE)(((w >> 5) & 31) * 255 / 31); r = (UBYTE)((w & 31) * 255 / 31);
        if (format == GL_BGRA) { UBYTE t = r; r = b; b = t; }
        goto out;
    case GL_UNSIGNED_INT_8_8_8_8:
        w = *(const ULONG *)p;
        r = (UBYTE)(w >> 24); g = (UBYTE)(w >> 16); b = (UBYTE)(w >> 8); a = (UBYTE)w;
        if (format == GL_BGRA) { UBYTE t = r; r = b; b = t; }
        goto out;
    case GL_UNSIGNED_INT_8_8_8_8_REV:
        w = *(const ULONG *)p;
        a = (UBYTE)(w >> 24); b = (UBYTE)(w >> 16); g = (UBYTE)(w >> 8); r = (UBYTE)w;
        if (format == GL_BGRA) { UBYTE t = r; r = b; b = t; }
        goto out;
    case GL_UNSIGNED_BYTE_3_3_2:
        w = *p;
        r = (UBYTE)(((w >> 5) & 7) * 255 / 7); g = (UBYTE)(((w >> 2) & 7) * 255 / 7); b = (UBYTE)((w & 3) * 85);
        goto out;
    }
    switch (format) {
    case GL_RGB: r = comp8(p, type, 0); g = comp8(p, type, 1); b = comp8(p, type, 2); break;
    case GL_BGR: b = comp8(p, type, 0); g = comp8(p, type, 1); r = comp8(p, type, 2); break;
    case GL_RGBA: r = comp8(p, type, 0); g = comp8(p, type, 1); b = comp8(p, type, 2); a = comp8(p, type, 3); break;
    case GL_BGRA: case MGL_UBYTE_BGRA: b = comp8(p, type, 0); g = comp8(p, type, 1); r = comp8(p, type, 2); a = comp8(p, type, 3); break;
    case MGL_UBYTE_ARGB: a = comp8(p, type, 0); r = comp8(p, type, 1); g = comp8(p, type, 2); b = comp8(p, type, 3); break;
    case GL_LUMINANCE: r = g = b = comp8(p, type, 0); break;
    case GL_LUMINANCE_ALPHA: r = g = b = comp8(p, type, 0); a = comp8(p, type, 1); break;
    case GL_ALPHA: r = g = b = 255; a = comp8(p, type, 0); break;
    case GL_INTENSITY: r = g = b = a = comp8(p, type, 0); break;
    case GL_RED: r = comp8(p, type, 0); break;
    case GL_GREEN: g = comp8(p, type, 0); break;
    case GL_BLUE: b = comp8(p, type, 0); break;
    case GL_COLOR_INDEX: if (index) *index = comp8(p, GL_UNSIGNED_BYTE, 0); break;
    }
out:
    rgba[0] = r; rgba[1] = g; rgba[2] = b; rgba[3] = a;
}

/* w x h pixels as R, G, B, A bytes, as the unpack settings lay them out
 * (for gluBuild2DMipmaps). */
UBYTE *mgl_unpack_rgba(GLcontext c, int w, int h, GLenum format, GLenum type, const void *pixels)
{
    int pb = mgl_pixel_bytes(format, type), x, y;
    ULONG rowlen, rowbytes;
    const UBYTE *src = pixels;
    UBYTE *out, *o;
    if (!pb) return 0;
    out = mgl_alloc((ULONG)w * h * 4);
    if (!out) return 0;
    rowlen = c->unpack_row ? (ULONG)c->unpack_row : (ULONG)w;
    rowbytes = (rowlen * (ULONG)pb + (ULONG)c->unpack_align - 1) & ~((ULONG)c->unpack_align - 1);
    src += (ULONG)c->unpack_skip_rows * rowbytes + (ULONG)c->unpack_skip_px * (ULONG)pb;
    for (o = out, y = 0; y < h; y++) {
        const UBYTE *p = src + (ULONG)y * rowbytes;
        for (x = 0; x < w; x++, p += pb, o += 4) {
            UBYTE ix = 0;
            mgl_read_pixel(p, format, type, o, &ix);
            if (format == GL_COLOR_INDEX) {
                ULONG v = c->palette[ix];
                o[0] = (UBYTE)(v >> 16); o[1] = (UBYTE)(v >> 8); o[2] = (UBYTE)v; o[3] = (UBYTE)(v >> 24);
            }
        }
    }
    return out;
}
