/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 * For tests/test_gla_present.c on the PC: just what gla_present_os3.c uses. */
#include <exec/types.h>
APTR LockBitMapTags(struct BitMap *bm, ...);
void UnLockBitMap(APTR lock);
ULONG GetCyberMapAttr(struct BitMap *bm, ULONG attr);
ULONG WritePixelArray(APTR src, int sx, int sy, int stride, struct RastPort *rp, int x, int y, int w, int h, int fmt);
