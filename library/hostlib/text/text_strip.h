/* SPDX-License-Identifier: MIT */
#ifndef HOSTLIB_TEXT_STRIP_H
#define HOSTLIB_TEXT_STRIP_H
/* hostlib_text_strip (HOSTLIB-ABI.md: OGFX_TEXT_STRIP, $00010105, version 1):
 * Text's glyphs gathered into one 1-bit strip, as gfopenrtg's
 * library/text_strip.h ts_build did it in its own C until 7 Nov 2026 (and
 * OS Text gathers a string in its TmpRas). Copyright (c) 2026 AmiCradle
 * Team. MIT.
 *
 * The block, big-endian longs (HL_TS_* are their numbers):
 *   0 data      tf_CharData
 *   1 mod       tf_Modulo
 *   2 loc       tf_CharLoc: each glyph's first bit and width, UWORDs
 *   3 space     tf_CharSpace (WORDs), or 0
 *   4 kern      tf_CharKern (WORDs), or 0
 *   5 xsize     tf_XSize, the advance without tf_CharSpace
 *   6 ysize     tf_YSize, the strip's rows
 *   7 baseline  tf_Baseline
 *   8 lo, 9 hi  tf_LoChar, tf_HiChar
 *  10 spacing   the RastPort's TxSpacing
 *  11 smear     bold: tf_BoldSmear, else 0
 *  12 italic    non-zero: row r moves right by floor((baseline - r) / 2)
 *  13 s         the string
 *  14 i, 15 j   characters i to j - 1 of it
 *  16 pos       character i's pen, in pixels from the Text's own
 *  17 left      the strip's column 0, in pixels from the Text's pen
 *  18 bpr       the strip's bytes a row
 *  19 strip     the strip: bpr x ysize bytes, cleared, then each image ORed
 *               in (bit 0 of a byte is its top bit), or XORed (20 non-zero)
 *  20 xor
 * Returns j - i (0 when j <= i). */
#include "../hl.h"

enum {
    HL_TS_DATA, HL_TS_MOD, HL_TS_LOC, HL_TS_SPACE, HL_TS_KERN, HL_TS_XSIZE, HL_TS_YSIZE, HL_TS_BASELINE,
    HL_TS_LO, HL_TS_HI, HL_TS_SPACING, HL_TS_SMEAR, HL_TS_ITALIC, HL_TS_S, HL_TS_I, HL_TS_J, HL_TS_POS,
    HL_TS_LEFT, HL_TS_BPR, HL_TS_STRIP, HL_TS_XOR, HL_TS_LONGS
};
#define HL_TS_BLOCK (4u * HL_TS_LONGS)

#ifndef HL_HOST
uint32_t hostlib_text_strip(hl_addr block);
#else
uint32_t hostlib_text_strip_host(struct hl_ctx *hl_, hl_addr block);
#endif

#endif
