/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * Text's glyphs (1 Nov 2026) gathered into one 1-bit strip, as OS Text gathers a
 * string in its TmpRas, so screens.c's text_patch sends one TEMPLATE for
 * each visible piece of the strip, not one for each glyph in each piece.
 * Plain C on exec/types.h's names, so tests/test_text_strip.c builds it on
 * the PC and checks it draws the pixels 0.24's glyph-at-a-time Text drew.
 *
 * The places are 0.24's (screens.c, text_patch): a character moves the pen
 * by its CharSpace (tf_XSize without one), its CharKern and TxSpacing; its
 * image lands at the pen plus its kerning; bold draws the image again
 * tf_BoldSmear pixels right; italic moves row r right by
 * floor((tf_Baseline - r) / 2). A pixel set by two images (kerning, a
 * negative TxSpacing, the smear) is set once in the strip; in COMPLEMENT,
 * where 0.24 inverted it once for each image, the images are XORed in, so
 * it is inverted as often as before (an even count: not at all). */
#ifndef ORTG_TEXT_STRIP_H
#define ORTG_TEXT_STRIP_H

#include <exec/types.h>
#include "../include/opengpu/stream.h"

/* What the glyphs of one Text call need of its TextFont and RastPort. */
struct ts_font {
    const UBYTE *data;          /* tf_CharData */
    LONG mod;                   /* tf_Modulo */
    const UWORD *loc;           /* tf_CharLoc: each glyph's first bit and width */
    const WORD *space, *kern;   /* tf_CharSpace and tf_CharKern, or NULL */
    LONG xsize, ysize, baseline, lo, hi;
    LONG spacing;               /* the RastPort's TxSpacing */
    LONG smear;                 /* bold: tf_BoldSmear; else 0 */
    int italic;
};

/* A character's glyph: the one after tf_HiChar when the font hasn't it. */
static int ts_glyph(const struct ts_font *f, UBYTE c)
{
    return (c >= f->lo && c <= f->hi) ? c - f->lo : f->hi - f->lo + 1;
}

/* How far glyph k moves the pen. */
static LONG ts_advance(const struct ts_font *f, int k)
{
    return (f->space ? f->space[k] : f->xsize) + (f->kern ? f->kern[k] : 0) + f->spacing;
}

/* Italic's move of row r: floor((baseline - r) / 2). */
static LONG ts_slant(LONG baseline, LONG r)
{
    LONG d = baseline - r;
    return d >= 0 ? d / 2 : -((1 - d) / 2);
}

/* w bits from bit sbit of s ORed (xor: XORed) into d from bit dbit; bit 0
 * is a byte's top bit, as in a template. Reads and writes only the bytes
 * those bits are in. */
static void ts_bits(UBYTE *d, LONG dbit, const UBYTE *s, LONG sbit, LONG w, int xor)
{
    while (w > 0) {
        LONG c = w < 8 ? w : 8, sh = sbit & 7, dh = dbit & 7;
        const UBYTE *sp = s + (sbit >> 3);
        UBYTE *dp = d + (dbit >> 3);
        ULONG v = (ULONG)sp[0] << 8;
        if (sh + c > 8) v |= sp[1];
        v = ((v << sh) & (0xFF00UL << (8 - c)) & 0xFF00UL) >> dh;   /* the c bits, dh from the top of 16 */
        if (xor) {
            dp[0] ^= (UBYTE)(v >> 8);
            if (dh + c > 8) dp[1] ^= (UBYTE)v;
        } else {
            dp[0] |= (UBYTE)(v >> 8);
            if (dh + c > 8) dp[1] |= (UBYTE)v;
        }
        sbit += c; dbit += c; w -= c;
    }
}

/* The characters from i on (of n) whose strip, (*width + 7) / 8 bytes a
 * row for ysize rows, fits in max bytes, the first's pen at pos (pixels
 * from the Text's own). Returns the one after the last (i when the first
 * alone has too big an image); *left is the strip's first column from the
 * Text's pen, *width its columns (0: no image in them), *end the pen after
 * them. */
static LONG ts_run(const struct ts_font *f, const UBYTE *s, LONG i, LONG n, LONG pos, LONG max,
                   LONG *left, LONG *width, LONG *end)
{
    LONG lo = 0, hi = -1, sl0 = 0, sl1 = 0;
    int any = 0;
    if (f->italic) { sl0 = ts_slant(f->baseline, f->ysize - 1); sl1 = ts_slant(f->baseline, 0); }
    for (; i < n; i++) {
        int k = ts_glyph(f, s[i]);
        LONG w = f->loc[2 * k + 1];
        if (w > 0) {
            LONG at = pos + (f->kern ? f->kern[k] : 0), a = at + sl0, b = at + w - 1 + f->smear + sl1;
            if (any && lo < a) a = lo;
            if (any && hi > b) b = hi;
            if (b - a >= max * 8 || ((b - a + 8) >> 3) * f->ysize > max) break;
            lo = a; hi = b; any = 1;
        }
        pos += ts_advance(f, k);
    }
    *left = lo; *width = any ? hi - lo + 1 : 0; *end = pos;
    return i;
}

/* Characters i to j - 1 (the first's pen at pos), drawn into strip: bpr
 * bytes a row, ysize rows, its column 0 at left from the Text's pen (as
 * ts_run gave). Cleared first; the images ORed in, or XORed (xor). */
static void ts_build(const struct ts_font *f, const UBYTE *s, LONG i, LONG j, LONG pos, LONG left, LONG bpr,
                     UBYTE *strip, int xor)
{
    LONG r, n = bpr * f->ysize;
    for (r = 0; r < n; r++) strip[r] = 0;
    for (; i < j; i++) {
        int k = ts_glyph(f, s[i]);
        LONG w = f->loc[2 * k + 1], off = f->loc[2 * k], at = pos + (f->kern ? f->kern[k] : 0) - left, b;
        if (w > 0)
            for (b = 0;; b = f->smear) {                /* the image, then bold's smear */
                for (r = 0; r < f->ysize; r++)
                    ts_bits(strip + r * bpr, at + b + (f->italic ? ts_slant(f->baseline, r) : 0),
                            f->data + r * f->mod, off, w, xor);
                if (b == f->smear) break;
            }
        pos += ts_advance(f, k);
    }
}

/* A TEMPLATE in Text's gathered batch, read back from the stream. target:
 * how many SURFACEs came before it, less one (Text names each bitmap it
 * draws on in a SURFACE of its own, in slot 0, and draws there). */
struct ts_tmpl {
    ULONG src, bpr, first, fg, bg;
    LONG x, y, w, h;
    int mode, target;
};

static ULONG ts_word(const UBYTE *p)
{
    return (ULONG)p[0] << 24 | (ULONG)p[1] << 16 | (ULONG)p[2] << 8 | (ULONG)p[3];
}

/* Each TEMPLATE in a batch of words words, as ogpu_build.c writes them
 * (high byte first), to fn in order. When the back end refuses the batch,
 * screens.c's ob_flush draws them on the CPU with these. */
static void ts_templates(const UBYTE *buf, LONG words, void (*fn)(void *, const struct ts_tmpl *), void *ctx)
{
    LONG at = 0, n;
    int target = -1;
    for (; at < words; at += n) {
        ULONG h = ts_word(buf + at * 4);
        n = (LONG)OGPU_HDR_WORDS(h);
        if (!n || at + n > words) return;
        if (OGPU_HDR_OP(h) == OGPU_OP_SURFACE) target++;
        else if (OGPU_HDR_OP(h) == OGPU_OP_TEMPLATE && n == 9) {
            const UBYTE *a = buf + at * 4 + 4;
            ULONG xy = ts_word(a + 12), wh = ts_word(a + 16);
            struct ts_tmpl t;
            t.src = ts_word(a); t.bpr = ts_word(a + 4); t.first = ts_word(a + 8);
            t.x = (WORD)(UWORD)(xy >> 16); t.y = (WORD)(UWORD)xy;
            t.w = (LONG)(wh >> 16); t.h = (LONG)(wh & 0xFFFF);
            t.fg = ts_word(a + 20); t.bg = ts_word(a + 24); t.mode = (int)ts_word(a + 28);
            t.target = target;
            fn(ctx, &t);
        }
    }
}

#endif
