/* SPDX-License-Identifier: MIT */
#ifndef HOSTLIB_W3D_VERTICES_H
#define HOSTLIB_W3D_VERTICES_H
/* Warp3D.library's per-vertex conversion for a whole primitive: count
 * W3D_Vertex (64 bytes each, the 68k layout) into the OGPU stream's 32-byte
 * vertex (x, y 16.16; ARGB32; u, v 16.16; z 32 bits; w IEEE single; fog
 * factor and specular), as gfopenrtg's library/warp3d/w3d_draw.c convert()
 * does with fog off. Pure: no state.
 * Copyright (c) 2026 AmiCradle Team. MIT. */
#include "../hl.h"

/* The block: HL_WV_LONGS big-endian longs. */
enum { HL_WV_SRC,       /* the vertices (LIST 0), or the array of pointers to them (LIST 1) */
       HL_WV_LIST,      /* 0: an array of W3D_Vertex; 1: an array of W3D_Vertex pointers */
       HL_WV_FIRST,     /* the index of vertex 0 */
       HL_WV_AT,        /* vertex j > 0 is index AT + j (a fan's later pieces restart at FIRST) */
       HL_WV_COUNT,     /* 1 .. HL_WV_MAX */
       HL_WV_OUT,       /* COUNT * 32 bytes, written in full */
       HL_WV_FLAGS,     /* HL_WV_GOURAUD, HL_WV_SPECULAR */
       HL_WV_CURRENT,   /* ARGB32: every vertex's colour without HL_WV_GOURAUD */
       HL_WV_KU,        /* u's scale: 16.16 = u * 2^KU, KU -16 .. 32, or HL_WV_UV_NONE (u = 0) */
       HL_WV_KV,        /* the same for v */
       HL_WV_LONGS };
#define HL_WV_BLOCK (4u * HL_WV_LONGS)
#define HL_WV_GOURAUD  1u
#define HL_WV_SPECULAR 2u
#define HL_WV_FLAGS_ALL 3u
#define HL_WV_UV_NONE ((uint32_t)-1000)
#define HL_WV_MAX 4096u
#define HL_WV_VERTEX 64u            /* sizeof (W3D_Vertex) on the 68k */
#define HL_WV_OVTX 32u              /* sizeof (struct ovtx) */
#define HL_WV_SPREAD (1u << 23)     /* the most a pointer list's vertices may be apart */

/* The result: COUNT when every vertex was written; 0 when the call does not
 * take these inputs (a count of 0 or over HL_WV_MAX, an unknown flag, a
 * scale out of range, an odd address, an output over a source, a pointer
 * list's vertices more than HL_WV_SPREAD apart), with nothing written: the
 * caller converts those vertices itself. */

#endif
