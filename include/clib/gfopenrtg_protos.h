/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 * gfopenrtg.library's calls, from library/gfopenrtg_lib.sfd (for
 * compilers that take prototypes and pragmas or stubs; GCC's inline calls
 * are <inline/gfopenrtg.h>, through <proto/gfopenrtg.h>). Each call's LVO
 * is in its comment. */
#ifndef CLIB_GFOPENRTG_PROTOS_H
#define CLIB_GFOPENRTG_PROTOS_H

#include <exec/types.h>
#include <graphics/rastport.h>
#include <gfopenrtg/gfopenrtg.h>

#ifdef __cplusplus
extern "C" {
#endif

ULONG ORTG_MonitorCount(VOID);   /* -30 */
ULONG ORTG_NextMode(ULONG monitor, ULONG previous);   /* -36 */
BOOL ORTG_GetMode(ULONG modeID, struct OpenRTGMode *mode);   /* -42 */
ULONG ORTG_BestMode(ULONG monitor, ULONG width, ULONG height, ULONG depth);   /* -48 */
LONG ORTG_SetModeList(ULONG monitor, ULONG all);   /* -54 */
APTR ORTG_BoardAddress(ULONG monitor);   /* -60 */
LONG ORTG_DisplayDatabase(ULONG on);   /* -66 */
LONG ORTG_Screens(ULONG on);   /* -72 */
LONG ORTG_WritePixels(struct RastPort *rp, LONG x, LONG y, const struct OpenRTGPixels *pixels);   /* -78 */
LONG ORTG_ReadPixels(struct RastPort *rp, LONG x, LONG y, struct OpenRTGPixels *pixels);   /* -84 */
LONG ORTG_FillPixels(struct RastPort *rp, LONG x, LONG y, LONG width, LONG height, ULONG argb);   /* -90 */
LONG ORTG_InvertPixels(struct RastPort *rp, LONG x, LONG y, LONG width, LONG height);   /* -96 */
BOOL ORTG_BitMapInfo(struct BitMap *bitmap, struct OpenRTGBitMapInfo *info);   /* -102 */
ULONG ORTG_DrawStats(ULONG *counts, ULONG kinds, ULONG flags);   /* -108 */
/* -114 */
LONG ORTG_WritePixelsAlpha(struct RastPort *rp, LONG x, LONG y, const struct OpenRTGPixels *pixels, ULONG alpha);
LONG ORTG_PresentVirgl(struct RastPort *rp, LONG x, LONG y, const struct OpenRTGVirglFrame *frame);   /* -120 */
BOOL ORTG_NativeSize(ULONG monitor, struct OpenRTGNativeSize *size);   /* -126 */
ULONG ORTG_WrittenSpans(struct BitMap *bitmap, struct OpenRTGSpan *buf, ULONG max);   /* -132 */
LONG ORTG_ShowInstead(struct BitMap *screen, struct BitMap *picture);   /* -138 */
LONG ORTG_ShownNow(struct BitMap *screen, struct BitMap *picture);   /* -144 */
LONG ORTG_Present(struct ORTG_PresentInfo *info);   /* -150 */
VOID ORTG_PresentDone(APTR token, const ULONG *blk, ULONG at);   /* -156 */

#ifdef __cplusplus
}
#endif

#endif
