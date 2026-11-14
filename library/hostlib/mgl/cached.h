/* SPDX-License-Identifier: MIT */
#ifndef HOSTLIB_MGL_CACHED_H
#define HOSTLIB_MGL_CACHED_H
/* cached(): find the virgl object made for a state key (a blend, DSA or
 * rasterizer key of 1 to 5 longs) in a context's small table, or where a new
 * one goes. Only reads: the table, its handles and the key. Copyright (c)
 * 2026 AmiCradle Team. MIT. */
#include "../hl.h"

/* keys: slots x keyw longs (minigl's struct vg_cache key[][]); handles: slots
 * 16-bit handles, 0 for an empty slot (its handle[]); key: n longs. slots is
 * a power of two (1 to 1024), keyw 1 to 16, n 1 to keyw, probes 1 to slots. */
enum { HL_MK_KEYS, HL_MK_HANDLES, HL_MK_SLOTS, HL_MK_KEYW, HL_MK_KEY, HL_MK_N, HL_MK_PROBES,
       HL_MK_LONGS };
#define HL_MK_BLOCK (4u * HL_MK_LONGS)

/* The result: the kind in bits 16-17, the handle or the slot below. 0 is a
 * rejected block (both builds), and the caller looks the key up itself. */
#define HL_MK_FOUND 0x00010000u         /* | the handle */
#define HL_MK_FREE  0x00020000u         /* | the empty slot a new object goes in */
#define HL_MK_FULL  0x00030000u         /* | the slot whose object a new one replaces */
#define HL_MK_KIND(r) ((r) & 0x00030000u)
#define HL_MK_VALUE(r) ((r) & 0xffffu)

#endif
