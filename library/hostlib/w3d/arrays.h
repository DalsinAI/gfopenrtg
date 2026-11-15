/* SPDX-License-Identifier: MIT */
#ifndef HOSTLIB_W3D_ARRAYS_H
#define HOSTLIB_W3D_ARRAYS_H
/* Warp3D.library's DrawArray and DrawElements for a run of triangles: the
 * vertex arrays (VertexPointer, TexCoordPointer 0, ColorPointer,
 * SecondaryColorPointer) and an optional index array into three OGPU
 * stream vertices per triangle (see vertices.h for the 32-byte layout), in
 * the order gfopenrtg's library/warp3d/w3d_draw.c arrays() takes them, fog
 * off. The caller culls. Pure: no state. The code is in vertices.c.
 * Copyright (c) 2026 AmiCradle Team. MIT. */
#include "vertices.h"

/* The block: HL_WA_LONGS big-endian longs. A pointer of 0 is an array the
 * context does not have; its stride, offsets and mode are then not read. */
enum { HL_WA_VP,        /* the positions */
       HL_WA_VPSTRIDE,  /* 0 .. HL_WA_STRIDE bytes */
       HL_WA_VPMODE,    /* HL_WA_VP_FFF (x, y, z floats) or HL_WA_VP_FFD (x, y floats, z double) */
       HL_WA_TP,        /* texture coordinates 0: u at 0, v at VOFF, w at WOFF (floats); 0: u = v = 0, w = 1 */
       HL_WA_TPSTRIDE,
       HL_WA_TPVOFF,    /* -HL_WA_OFFS .. HL_WA_OFFS */
       HL_WA_TPWOFF,    /* the same; MiniGL keeps w before u */
       HL_WA_CP,        /* the colours; 0: CURRENT */
       HL_WA_CPSTRIDE,
       HL_WA_CPMODE,    /* HL_WA_C_RGBA .. HL_WA_C_BGR, plus HL_WA_C_UBYTE for bytes (else floats) */
       HL_WA_SCP,       /* the secondary colours (r, g, b used); 0: none */
       HL_WA_SCPSTRIDE,
       HL_WA_SCPMODE,
       HL_WA_IX,        /* the indices; 0: element e is vertex BASE + e */
       HL_WA_IXSIZE,    /* 1, 2 or 4 bytes with IX, 0 without */
       HL_WA_BASE,      /* added to every index */
       HL_WA_PRIM,      /* HL_WA_LIST, HL_WA_FAN or HL_WA_STRIP */
       HL_WA_FIRST,     /* the first triangle of the run */
       HL_WA_NTRI,      /* 1 .. HL_WA_MAXTRI */
       HL_WA_OUT,       /* NTRI * 3 * 32 bytes, written in full */
       HL_WA_FLAGS,     /* HL_WV_GOURAUD, HL_WV_SPECULAR */
       HL_WA_CURRENT,   /* ARGB32 */
       HL_WA_KU,        /* as HL_WV_KU */
       HL_WA_KV,
       HL_WA_LONGS };
#define HL_WA_BLOCK (4u * HL_WA_LONGS)
#define HL_WA_VP_FFF 0u
#define HL_WA_VP_FFD 1u
#define HL_WA_C_RGBA 0u
#define HL_WA_C_ARGB 1u
#define HL_WA_C_BGRA 2u
#define HL_WA_C_RGB  3u
#define HL_WA_C_BGR  4u
#define HL_WA_C_UBYTE 8u
#define HL_WA_LIST  0u
#define HL_WA_FAN   1u
#define HL_WA_STRIP 2u
#define HL_WA_MAXTRI 2048u          /* arrays()'s chunk */
#define HL_WA_STRIDE 4096u
#define HL_WA_OFFS 64
#define HL_WA_ELEMENTS 0x01000000u  /* every element number used is under this */

/* Triangle t's vertices are elements (k = 0, 1, 2):
 *   LIST  3t + k
 *   FAN   0, t + 1, t + 2
 *   STRIP t, t + 1, t + 2 for even t; t + 1, t, t + 2 for odd t
 * The result: NTRI * 3 when every vertex was written; 0 when the call does
 * not take these inputs (a count of 0 or over HL_WA_MAXTRI, an unknown
 * primitive, mode, size or flag, a stride or offset out of range, an odd
 * output, an element number of HL_WA_ELEMENTS or more, an index whose
 * offset in an array overflows a LONG, an array span over HL_WV_SPREAD, an
 * output over a source), with nothing written: the caller converts those
 * vertices itself. */

#endif
