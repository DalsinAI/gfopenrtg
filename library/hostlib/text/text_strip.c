/* SPDX-License-Identifier: MIT */
/* hostlib_text_strip: Text's glyphs into one 1-bit strip (text_strip.h has
 * the block). One source for gfopenrtg.library's 68k code and for
 * AmiCradle's host build of it (hostlib/hl.h). Copyright (c) 2026
 * AmiCradle Team. MIT.
 *
 * The places are those gfopenrtg's Text has drawn since 0.24: a character
 * moves the pen by its CharSpace (tf_XSize without one), its CharKern and
 * TxSpacing; its image lands at the pen plus its kerning; bold draws the
 * image again smear pixels right; italic moves row r right by
 * floor((baseline - r) / 2). A pixel two images set is set once; XORed (in
 * COMPLEMENT) it is inverted as often as images cover it. */
#include "text_strip.h"
#include "../hl_ids.h"

/* floor((baseline - r) / 2) */
static int32_t hl_ts_slant(int32_t baseline, int32_t r)
{
    int32_t d = baseline - r;
    return d >= 0 ? d / 2 : -((1 - d) / 2);
}

HL_TAG(hostlib_text_strip, HL_ID_OGFX_TEXT_STRIP)

HL_FN(hostlib_text_strip)
{
    hl_addr data, loc, space, kern, s, strip;
    int32_t mod, xsize, ysize, baseline, lo, hi, spacing, smear, italic, i, i0, j, pos, left, bpr, xor_, glyphs, r;
    HL_NEED_R(block, HL_TS_BLOCK);
    data = HL_ARG(HL_TS_DATA); mod = (int32_t)HL_ARG(HL_TS_MOD);
    loc = HL_ARG(HL_TS_LOC); space = HL_ARG(HL_TS_SPACE); kern = HL_ARG(HL_TS_KERN);
    xsize = (int32_t)HL_ARG(HL_TS_XSIZE); ysize = (int32_t)HL_ARG(HL_TS_YSIZE);
    baseline = (int32_t)HL_ARG(HL_TS_BASELINE);
    lo = (int32_t)HL_ARG(HL_TS_LO); hi = (int32_t)HL_ARG(HL_TS_HI);
    spacing = (int32_t)HL_ARG(HL_TS_SPACING); smear = (int32_t)HL_ARG(HL_TS_SMEAR);
    italic = (int32_t)HL_ARG(HL_TS_ITALIC);
    s = HL_ARG(HL_TS_S); i = i0 = (int32_t)HL_ARG(HL_TS_I); j = (int32_t)HL_ARG(HL_TS_J);
    pos = (int32_t)HL_ARG(HL_TS_POS); left = (int32_t)HL_ARG(HL_TS_LEFT);
    bpr = (int32_t)HL_ARG(HL_TS_BPR); strip = HL_ARG(HL_TS_STRIP); xor_ = (int32_t)HL_ARG(HL_TS_XOR);

    /* what it reads and writes; the host leaves odd fonts and sizes to the 68k code */
    glyphs = hi - lo + 2;                       /* tf_LoChar to tf_HiChar, and the one for the rest */
    if (glyphs < 1 || glyphs > 257 || ysize < 0 || bpr < 0 || smear < 0 || (ysize > 0 && mod <= 0)) HL_DECLINE();
    if (j > i) {
        if (i < 0) HL_DECLINE();
        HL_NEED_R(s + (uint32_t)i, (uint32_t)(j - i));
        HL_NEED_R(loc, 4u * (uint32_t)glyphs);
        if (space) HL_NEED_R(space, 2u * (uint32_t)glyphs);
        if (kern) HL_NEED_R(kern, 2u * (uint32_t)glyphs);
        HL_NEED_R(data, (uint64_t)(uint32_t)mod * (uint32_t)ysize);
    }
    HL_NEED_W(strip, (uint64_t)(uint32_t)bpr * (uint32_t)ysize);
    HL_DECLARED();

    for (r = 0; r < bpr * ysize; r++) HL_WR8(strip + (uint32_t)r, 0);
    for (; i < j; i++) {
        uint32_t c = HL_RD8(s + (uint32_t)i);
        int32_t k = ((int32_t)c >= lo && (int32_t)c <= hi) ? (int32_t)c - lo : hi - lo + 1;
        int32_t w = (int32_t)HL_RD16(loc + 4u * (uint32_t)k + 2), off = (int32_t)HL_RD16(loc + 4u * (uint32_t)k);
        int32_t kk = kern ? (int16_t)HL_RD16(kern + 2u * (uint32_t)k) : 0;
        int32_t at = pos + kk - left, b;
        if (w > 0)
            for (b = 0;; b = smear) {           /* the image, then bold's smear */
                for (r = 0; r < ysize; r++) {
                    /* w bits from bit off of the font's row r, ORed (or XORed) into the
                     * strip's row r from bit dbit; bit 0 is a byte's top bit */
                    hl_addr d = strip + (uint32_t)(r * bpr), src = data + (uint32_t)(r * mod);
                    int32_t dbit = at + b + (italic ? hl_ts_slant(baseline, r) : 0), sbit = off, n = w;
                    while (n > 0) {
                        int32_t cnt = n < 8 ? n : 8, sh = sbit & 7, dh = dbit & 7;
                        hl_addr sp = src + (uint32_t)(sbit >> 3), dp = d + (uint32_t)(dbit >> 3);
                        uint32_t v = HL_RD8(sp) << 8;
                        if (sh + cnt > 8) v |= HL_RD8(sp + 1);
                        v = ((v << sh) & (0xFF00u << (8 - cnt)) & 0xFF00u) >> dh;   /* the cnt bits, dh from the top of 16 */
                        if (xor_) {
                            HL_WR8(dp, HL_RD8(dp) ^ (v >> 8));
                            if (dh + cnt > 8) HL_WR8(dp + 1, HL_RD8(dp + 1) ^ (v & 0xFF));
                        } else {
                            HL_WR8(dp, HL_RD8(dp) | (v >> 8));
                            if (dh + cnt > 8) HL_WR8(dp + 1, HL_RD8(dp + 1) | (v & 0xFF));
                        }
                        sbit += cnt; dbit += cnt; n -= cnt;
                    }
                }
                if (b == smear) break;
            }
        pos += (space ? (int16_t)HL_RD16(space + 2u * (uint32_t)k) : xsize) + kk + spacing;
    }
    return j > i0 ? (uint32_t)(j - i0) : 0;
}
