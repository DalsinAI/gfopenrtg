/* Copyright (c) 2026 AmiCradle Team. OpenGPU's GL module, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * Clean room: cybergraphics.library's calls gla_present_os3.c makes, through
 * OpenNDK's inline/macros.h, with the offsets and registers of the Team's own
 * cybergraphics.library (compat/cybergraphics_lib.c's lib_vectors). */
#ifndef PROTO_CYBERGRAPHICS_H
#define PROTO_CYBERGRAPHICS_H

#include <exec/types.h>
#include <utility/tagitem.h>
#include <inline/macros.h>
#include <cybergraphx/cybergraphics.h>

struct BitMap;
struct RastPort;

#ifndef __NOLIBBASE__
extern struct Library *CyberGfxBase;
#endif
#ifndef CYBERGRAPHICS_BASE_NAME
#define CYBERGRAPHICS_BASE_NAME CyberGfxBase
#endif

#define GetCyberMapAttr(bm, attr) \
    LP2(0x60, ULONG, GetCyberMapAttr, struct BitMap *, (bm), a0, ULONG, (attr), d0, \
        , CYBERGRAPHICS_BASE_NAME)

#define WritePixelArray(src, sx, sy, smod, rp, dx, dy, w, h, fmt) \
    LP10(0x7e, ULONG, WritePixelArray, APTR, (src), a0, UWORD, (sx), d0, UWORD, (sy), d1, \
         UWORD, (smod), d2, struct RastPort *, (rp), a1, UWORD, (dx), d3, UWORD, (dy), d4, \
         UWORD, (w), d5, UWORD, (h), d6, UBYTE, (fmt), d7, , CYBERGRAPHICS_BASE_NAME)

#define LockBitMapTagList(bm, tags) \
    LP2(0xa8, APTR, LockBitMapTagList, struct BitMap *, (bm), a0, struct TagItem *, (tags), a1, \
        , CYBERGRAPHICS_BASE_NAME)

#define UnLockBitMap(handle) \
    LP1NR(0xae, UnLockBitMap, APTR, (handle), a0, , CYBERGRAPHICS_BASE_NAME)

/* The varargs form: the tags on the stack, as a tag list. */
#define LockBitMapTags(bm, ...) \
    ({ ULONG _cgx_tags[] = { __VA_ARGS__ }; LockBitMapTagList((bm), (struct TagItem *)_cgx_tags); })

#endif
