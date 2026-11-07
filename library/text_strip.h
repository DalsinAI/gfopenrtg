/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
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
#include "../include/gfopengpu/stream.h"

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
 * ts_run gave). Cleared first; the images ORed in, or XORed (xor).
 *
 * 7 Nov 2026: the work is hostlib's text strip (library/hostlib, vendored
 * from amicradle-guest by its hostlib/sync.sh; HOSTLIB-ABI.md,
 * OGFX_TEXT_STRIP): one C source, which this library runs as its own 68k
 * code and AmiCradle's JIT may run as host code in its place (about a third
 * of the emulation thread's time went here in GFOpenBenchmark's Text). The
 * pixels are 0.24's and 1 Nov's, as before (amicradle-guest's
 * tests/m68k/test_hostlib.sh checks it against the C that was here). */
#define HL_VENDORED 1
#if defined(__m68k__) || defined(__mc68000__)
#include "hostlib/text/text_strip.c"

static void ts_build(const struct ts_font *f, const UBYTE *s, LONG i, LONG j, LONG pos, LONG left, LONG bpr,
                     UBYTE *strip, int xor)
{
    ULONG b[HL_TS_LONGS];
    b[HL_TS_DATA] = (ULONG)f->data; b[HL_TS_MOD] = (ULONG)f->mod; b[HL_TS_LOC] = (ULONG)f->loc;
    b[HL_TS_SPACE] = (ULONG)f->space; b[HL_TS_KERN] = (ULONG)f->kern; b[HL_TS_XSIZE] = (ULONG)f->xsize;
    b[HL_TS_YSIZE] = (ULONG)f->ysize; b[HL_TS_BASELINE] = (ULONG)f->baseline; b[HL_TS_LO] = (ULONG)f->lo;
    b[HL_TS_HI] = (ULONG)f->hi; b[HL_TS_SPACING] = (ULONG)f->spacing; b[HL_TS_SMEAR] = (ULONG)f->smear;
    b[HL_TS_ITALIC] = (ULONG)f->italic; b[HL_TS_S] = (ULONG)s; b[HL_TS_I] = (ULONG)i; b[HL_TS_J] = (ULONG)j;
    b[HL_TS_POS] = (ULONG)pos; b[HL_TS_LEFT] = (ULONG)left; b[HL_TS_BPR] = (ULONG)bpr; b[HL_TS_STRIP] = (ULONG)strip;
    b[HL_TS_XOR] = (ULONG)xor;
    (void)hostlib_text_strip((hl_addr)b);
}
#else
/* Elsewhere (the PC's tests): hostlib's host build of the same source, on a
 * scratch arena holding big-endian copies of what it reads, the strip copied
 * back after. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define HL_HOST 1
static uint8_t *ts_arena;
static uint32_t ts_arena_n;
static uint8_t *hl_host_mem(void) { return ts_arena; }
static int hl_host_plain(uint32_t at, uint32_t n, int write) { (void)write; return (uint64_t)at + n <= ts_arena_n; }
static int hl_host_aliased(uint32_t at, uint32_t n) { (void)at; (void)n; return 0; }
static void hl_host_written(uint32_t at, uint32_t n) { (void)at; (void)n; }
#include "hostlib/hl.h"
#include "hostlib/text/text_strip.c"

static void ts_put32(uint32_t at, ULONG v)
{
    ts_arena[at] = (uint8_t)(v >> 24); ts_arena[at + 1] = (uint8_t)(v >> 16);
    ts_arena[at + 2] = (uint8_t)(v >> 8); ts_arena[at + 3] = (uint8_t)v;
}

static void ts_build(const struct ts_font *f, const UBYTE *s, LONG i, LONG j, LONG pos, LONG left, LONG bpr,
                     UBYTE *strip, int xor)
{
    uint32_t glyphs = (uint32_t)(f->hi - f->lo + 2), data = 16, loc = data + (uint32_t)(f->mod * f->ysize), k;
    uint32_t space = loc + 4 * glyphs, kern = space + 2 * glyphs, str = kern + 2 * glyphs;
    uint32_t st = str + (uint32_t)j, blk = (st + (uint32_t)(bpr * f->ysize) + 3) & ~3u;
    struct hl_ctx hl;
    ts_arena_n = blk + HL_TS_BLOCK;
    ts_arena = calloc(1, ts_arena_n);
    if (!ts_arena) abort();
    memcpy(ts_arena + data, f->data, (size_t)(f->mod * f->ysize));
    for (k = 0; k < glyphs; k++) {
        ts_arena[loc + 4 * k] = (uint8_t)(f->loc[2 * k] >> 8); ts_arena[loc + 4 * k + 1] = (uint8_t)f->loc[2 * k];
        ts_arena[loc + 4 * k + 2] = (uint8_t)(f->loc[2 * k + 1] >> 8); ts_arena[loc + 4 * k + 3] = (uint8_t)f->loc[2 * k + 1];
        if (f->space) { ts_arena[space + 2 * k] = (uint8_t)((UWORD)f->space[k] >> 8); ts_arena[space + 2 * k + 1] = (uint8_t)f->space[k]; }
        if (f->kern) { ts_arena[kern + 2 * k] = (uint8_t)((UWORD)f->kern[k] >> 8); ts_arena[kern + 2 * k + 1] = (uint8_t)f->kern[k]; }
    }
    memcpy(ts_arena + str, s, (size_t)j);
    {
        ULONG v[HL_TS_LONGS] = { data, (ULONG)f->mod, loc, f->space ? space : 0, f->kern ? kern : 0, (ULONG)f->xsize,
                                 (ULONG)f->ysize, (ULONG)f->baseline, (ULONG)f->lo, (ULONG)f->hi, (ULONG)f->spacing,
                                 (ULONG)f->smear, (ULONG)f->italic, str, (ULONG)i, (ULONG)j, (ULONG)pos, (ULONG)left,
                                 (ULONG)bpr, st, (ULONG)xor };
        for (k = 0; k < HL_TS_LONGS; k++) ts_put32(blk + 4 * k, v[k]);
    }
    hl_init(&hl, 0);
    (void)hostlib_text_strip_host(&hl, blk);
    if (hl.why) abort();                        /* the tests' fonts are all ones the host build takes */
    memcpy(strip, ts_arena + st, (size_t)(bpr * f->ysize));
    free(ts_arena);
}
#endif

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
