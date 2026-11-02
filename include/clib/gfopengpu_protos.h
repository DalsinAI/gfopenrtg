/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 * gfopengpu.library's calls, from library/opengpu/gfopengpu_lib.sfd (for
 * compilers that take prototypes and pragmas or stubs; GCC's inline calls
 * are <inline/gfopengpu.h>, through <proto/gfopengpu.h>). */
#ifndef CLIB_GFOPENGPU_PROTOS_H
#define CLIB_GFOPENGPU_PROTOS_H

#include <exec/types.h>
#include <gfopengpu/gfopengpu.h>
#include <gfopengpu/module.h>
#include <gfopengpu/gfx.h>
#include <gfopengpu/retained.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -30 .. -60 */
ULONG OGPU_Query(ULONG op, ULONG format);
STRPTR OGPU_BackEndName(ULONG index);
LONG OGPU_Submit(APTR stream, ULONG words, ULONG *fence);
LONG OGPU_Wait(ULONG fence);
APTR OGPU_ModuleOpen(CONST_STRPTR name, ULONG version, APTR *table);
VOID OGPU_ModuleClose(APTR handle);
/* -66 .. -144: GFOpenGfx (0.6) */
ULONG OGFX_Version(VOID);
LONG OGFX_InstallPatches(VOID);
LONG OGFX_SetEnabled(ULONG enabled);
ULONG OGFX_Status(VOID);
LONG OGFX_RegisterProvider(struct OGFXProviderV1 *provider);
LONG OGFX_UnregisterProvider(APTR owner);
LONG OGFX_Text(struct RastPort *rp, STRPTR text, ULONG length);
WORD OGFX_TextLength(struct RastPort *rp, STRPTR text, ULONG length);
VOID OGFX_TextExtent(struct RastPort *rp, STRPTR text, ULONG length, struct TextExtent *extent);
ULONG OGFX_TextFit(struct RastPort *rp, STRPTR text, ULONG length, struct TextExtent *extent, struct TextExtent *constraining,
                   LONG direction, ULONG bitWidth, ULONG bitHeight);
VOID OGFX_RectFill(struct RastPort *rp, LONG x0, LONG y0, LONG x1, LONG y1);
LONG OGFX_BltBitMap(struct BitMap *src, LONG sx, LONG sy, struct BitMap *dst, LONG dx, LONG dy, LONG width, LONG height,
                    ULONG minterm, ULONG mask, PLANEPTR temp);
VOID OGFX_BltTemplate(PLANEPTR source, LONG sx, LONG modulo, struct RastPort *rp, LONG dx, LONG dy, LONG width, LONG height);
VOID OGFX_ScrollRaster(struct RastPort *rp, LONG dx, LONG dy, LONG x0, LONG y0, LONG x1, LONG y1);
/* -150, -156: the look hook (0.7) */
LONG OGFX_RegisterLook(struct OGFXLookV1 *look);
LONG OGFX_UnregisterLook(APTR owner);
/* -162 .. -192: retained surfaces (0.25) */
struct OGPURetained *OGPU_RetainedCreate(ULONG width, ULONG height, ULONG format, ULONG flags);
VOID OGPU_RetainedPalette(struct OGPURetained *rs, ULONG first, ULONG count, CONST ULONG *argb);
VOID OGPU_RetainedMark(struct OGPURetained *rs, LONG x, LONG y, LONG width, LONG height);
LONG OGPU_RetainedPresent(struct OGPURetained *rs, struct RastPort *rp, LONG x, LONG y, LONG width, LONG height);
ULONG OGPU_RetainedStats(struct OGPURetained *rs, struct OGPURetainedStats *stats);
VOID OGPU_RetainedDestroy(struct OGPURetained *rs);

#ifdef __cplusplus
}
#endif

#endif
