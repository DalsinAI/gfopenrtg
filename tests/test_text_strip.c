/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * Text's strip (openrtg.library 0.25, library/text_strip.h) against 0.24's
 * Text, which drew one glyph at a time: the same pixels, exactly.
 *
 * Each case draws one Text twice onto the same background, in a bitmap seen
 * through four visible pieces with gaps between them (a layer's ClipRects):
 *   before  0.24's text_patch: the JAM2 box, then each glyph (each smear,
 *           in italic each row) through the pieces as a TEMPLATE of its
 *           own, its rows copied as stage_copy copies them; the underline;
 *   after   0.25's: the same, but the glyphs from text_strip: as many
 *           characters as fit in `max` bytes built into one strip by
 *           ts_run and ts_build, one TEMPLATE for each piece of its box,
 *           read as ogpu_template_piece reads a staged strip; the rest one
 *           glyph at a time, as before, when the first is too big alone or
 *           there is no room.
 * The TEMPLATEs are carried out by OpenGPU's core (ogpu_core.c), in CLUT8,
 * RGB565 and ARGB32, and again by the CPU's tmpl_piece loop (with an 8-bit
 * pen mask, which keeps a piece off OpenGPU), and the two bitmaps compared
 * byte for byte. The fonts are made up: monospace, proportional with
 * CharSpace and CharKern (negative kerning, so images overlap), a 23-pixel
 * one with a smear of 2, a smear of 0 (bold draws the image twice in one
 * place), no underline row, a baseline of 0; widths of 0; characters the
 * font hasn't; TxSpacing 0, positive and negative; plain, bold, italic,
 * underlined and all three; JAM1, JAM2, COMPLEMENT and INVERSVID; text off
 * the bitmap's left edge and across the gaps; strips of 4096 bytes (the
 * library's STAGE_MAX) and smaller, so strings split into several strips,
 * and staging room running out partway.
 *   cc -std=c99 -Itests/host-include tests/test_text_strip.c \
 *       library/opengpu/ogpu_core.c library/opengpu/ogpu_build.c */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <exec/types.h>
#include "../library/text_strip.h"
#include "../library/opengpu/ogpu_core.h"
#include "../include/opengpu/build.h"

/* graphics/rastport.h and graphics/text.h */
#define JAM1 0
#define JAM2 1
#define COMPLEMENT 2
#define INVERSVID 4
#define FSF_UNDERLINED 1
#define FSF_BOLD 2
#define FSF_ITALIC 4

/* Amiga addresses are offsets into one arena. */
#define ARENA (1L << 21)
static ogpu_u8 arena[ARENA];
#define T0    0x001000L     /* the bitmap */
#define FONT  0x020000L     /* the font's glyphs */
#define STRIP 0x030000L     /* the staging block's strip */
#define STG   0x040000L     /* stage_copy's copies of glyph rows */
#define STG_END ARENA

static ogpu_u8 *map(void *user, ogpu_u32 address, ogpu_u32 length) {
    (void)user;
    if (address >= ARENA || length > (ogpu_u32)ARENA - address) return 0;
    return arena + address;
}

static int failures, cases;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; if (failures <= 20) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } } while (0)

static unsigned long rnd_state = 20261101;
static unsigned long rnd(void) { rnd_state = (rnd_state * 1103515245UL + 12345UL) & 0xFFFFFFFFUL; return (rnd_state >> 8) & 0xFFFFFFUL; }
static long rnd_in(long a, long b) { return a + (long)(rnd() % (unsigned long)(b - a + 1)); }

/* ---- the bitmap and its pieces ---------------------------------------------------- */

#define W 320
#define H 40
static int fmt, bpp;
static long bpr;
static ogpu_u8 before_px[(W * 4 + 4) * H], after_px[(W * 4 + 4) * H], background[(W * 4 + 4) * H];

/* the visible pieces: a ClipRect's box each (inclusive); the gaps between are hidden */
static const int clips[4][4] = { { 0, 0, 99, 39 }, { 100, 0, 180, 17 }, { 100, 24, 180, 39 }, { 190, 0, W - 1, 39 } };

typedef void (*piece_fn)(void *ctx, LONG x0, LONG y0, LONG x1, LONG y1);
static void pieces(LONG x0, LONG y0, LONG x1, LONG y1, piece_fn fn, void *ctx) {
    int i;
    if (x0 > x1 || y0 > y1) return;
    for (i = 0; i < 4; i++) {
        LONG a0 = x0 > clips[i][0] ? x0 : clips[i][0], b0 = y0 > clips[i][1] ? y0 : clips[i][1];
        LONG a1 = x1 < clips[i][2] ? x1 : clips[i][2], b1 = y1 < clips[i][3] ? y1 : clips[i][3];
        if (a0 <= a1 && b0 <= b1) fn(ctx, a0, b0, a1, b1);
    }
}

/* pens and ink, as screens.c's pens_of and ink_of */
static const unsigned long pen_rgb[8] = { 0x000000, 0xFFFFFF, 0x3366AA, 0xAA5522, 0x12E0F7, 0x808080, 0xFF00FF, 0x00FF00 };
struct ink { ULONG a, b, x; UBYTE mode, mask; };
static ULONG pen_px(int pen) {
    unsigned long c = pen_rgb[pen & 7];
    if (bpp == 1) return (ULONG)pen;
    if (bpp == 2) return (ULONG)(((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F));
    return (ULONG)c;                                    /* ortg_encode: the top byte 0 */
}
static void ink_of(int fg, int bg, int mode, int mask, struct ink *k) {
    if (mode & INVERSVID) { int t = fg; fg = bg; bg = t; }
    k->a = pen_px(fg); k->b = pen_px(bg); k->mode = (UBYTE)mode; k->mask = (UBYTE)mask;
    k->x = bpp == 1 ? (ULONG)mask : bpp == 2 ? 0xFFFF : 0xFFFFFF;
}

static ULONG get(LONG x, LONG y) {
    const ogpu_u8 *p = arena + T0 + y * bpr + x * bpp;
    return bpp == 1 ? p[0] : bpp == 2 ? (ULONG)(p[0] << 8 | p[1]) : (ULONG)p[0] << 24 | (ULONG)p[1] << 16 | (ULONG)p[2] << 8 | p[3];
}
static void put(LONG x, LONG y, ULONG v) {
    ogpu_u8 *p = arena + T0 + y * bpr + x * bpp;
    if (bpp == 1) p[0] = (ogpu_u8)v;
    else if (bpp == 2) { p[0] = (ogpu_u8)(v >> 8); p[1] = (ogpu_u8)v; }
    else { p[0] = (ogpu_u8)(v >> 24); p[1] = (ogpu_u8)(v >> 16); p[2] = (ogpu_u8)(v >> 8); p[3] = (ogpu_u8)v; }
}
/* screens.c's plot */
static void plot(LONG x, LONG y, const struct ink *k, int on) {
    ULONG d;
    if (!k->mask) return;
    if (k->mode & COMPLEMENT) { if (on) put(x, y, get(x, y) ^ k->x); return; }
    if (!on && !(k->mode & JAM2)) return;
    if (bpp != 1 || k->mask == 0xFF) { put(x, y, on ? k->a : k->b); return; }
    d = get(x, y);
    put(x, y, (d & ~(ULONG)k->mask) | ((on ? k->a : k->b) & k->mask));
}

/* a fill (the JAM2 box, the underline): fill_piece's CPU loop, the same in both */
static void fill_piece(void *c, LONG x0, LONG y0, LONG x1, LONG y1) {
    LONG x, y;
    for (y = y0; y <= y1; y++) for (x = x0; x <= x1; x++) plot(x, y, (const struct ink *)c, 1);
}

/* ---- the batch: TEMPLATEs through OpenGPU's core ----------------------------------- */

static unsigned char sbuf[1 << 16];
static struct OGPUBatch B;
static struct ogpu_core C;
static long stg_at;
static int use_cpu;                     /* the pieces go to tmpl_piece's CPU loop */
static long templates[2], strip_templates;   /* TEMPLATEs sent by 0.24's way and 0.25's; from strips */
static int way_now;                     /* 0: 0.24's, 1: 0.25's */

static void batch_begin(void) {
    ogpu_batch_init(&B, sbuf, sizeof sbuf / 4);
    ogpu_surface(&B, 0, T0, (unsigned long)bpr, W, H, fmt);
    ogpu_target(&B, 0);
}
/* ob_flush: what the batch holds, carried out; the staging block free again */
static void batch_flush(void) {
    if (B.words > 2 + 6) {
        ogpu_core_init(&C);
        C.map = map;
        ogpu_core_run(&C, sbuf, B.words);
        CHECK(C.last_error == OGPU_OK, "core error %d", C.last_error);
    }
    CHECK(!B.overflow, "batch overflow");
    batch_begin();
    stg_at = STG;
}

struct tmpl_ctx { struct ink k; long src; LONG src_x, mod, at_x, at_y; int staged; };

/* tmpl_piece: ogpu_template_piece's TEMPLATE, or the CPU's loop */
static void tmpl_piece(void *c, LONG x0, LONG y0, LONG x1, LONG y1) {
    struct tmpl_ctx *t = c;
    LONG row = y0 - t->at_y, first = t->src_x + (x0 - t->at_x), h = y1 - y0 + 1, x, y;
    if (!use_cpu && row >= 0 && first >= 0) {
        LONG bytes = ((first & 7) + (x1 - x0 + 1) + 7) >> 3, mod = t->mod, r;
        long src = t->src + row * t->mod, s;
        if (B.words + 9 + 8 > (long)(sizeof sbuf / 4)) batch_flush();
        if (t->staged) s = src + (first >> 3);
        else {                                      /* stage_copy */
            if (stg_at + bytes * h > STG_END) batch_flush();
            s = stg_at;
            for (r = 0; r < h; r++) memcpy(arena + s + r * bytes, arena + src + (first >> 3) + r * t->mod, (size_t)bytes);
            stg_at += (bytes * h + 3) & ~3L;
        }
        src = s; mod = t->staged ? t->mod : bytes; first &= 7;
        ogpu_template(&B, (unsigned long)src, (unsigned long)mod, (unsigned long)first, (int)x0, (int)y0, (int)(x1 - x0 + 1), (int)h,
                      t->k.a, t->k.b, t->k.mode & ~INVERSVID);
        templates[way_now]++;
        if (t->staged) strip_templates++;
        return;
    }
    batch_flush();
    for (y = y0; y <= y1; y++) {
        const ogpu_u8 *s = arena + t->src + (y - t->at_y) * t->mod;
        for (x = x0; x <= x1; x++) {
            LONG bit = t->src_x + (x - t->at_x);
            plot(x, y, &t->k, (s[bit >> 3] >> (7 - (bit & 7))) & 1);
        }
    }
}


/* ---- the fonts --------------------------------------------------------------------- */

#define MAXG 260
#define FONT_ROOM 0x2000L
struct font {
    long at;                    /* its glyphs in the arena */
    LONG ysize, baseline, xsize, lo, hi, smear, mod;
    UWORD loc[2 * MAXG];
    WORD space[MAXG], kern[MAXG];
    int has_space, has_kern;
};
#define NFONTS 6
static struct font fonts[NFONTS];
static int nfonts;

/* glyphs lo..hi and the one for characters the font hasn't, widths wmin..wmax,
 * packed with gaps of up to gap bits; CharSpace about the width; kerning kmin..kmax */
static void make_font(LONG ysize, LONG baseline, LONG lo, LONG hi, LONG wmin, LONG wmax, LONG gap, int has_space, int has_kern,
                      LONG kmin, LONG kmax, LONG xsize, LONG smear) {
    struct font *f = &fonts[nfonts];
    LONG k, n = hi - lo + 2, bit = 0, r, b;
    f->at = FONT + nfonts++ * FONT_ROOM;
    f->ysize = ysize; f->baseline = baseline; f->lo = lo; f->hi = hi; f->xsize = xsize; f->smear = smear;
    f->has_space = has_space; f->has_kern = has_kern;
    for (k = 0; k < n; k++) {
        LONG w = rnd_in(wmin, wmax);
        bit += gap ? rnd_in(0, gap) : 0;
        f->loc[2 * k] = (UWORD)bit; f->loc[2 * k + 1] = (UWORD)w;
        bit += w;
        f->space[k] = (WORD)(w + rnd_in(-1, 2));
        f->kern[k] = (WORD)rnd_in(kmin, kmax);
    }
    f->mod = ((bit + 15) >> 4) * 2 + (LONG)(rnd() & 1);   /* an odd modulo now and then */
    if (f->mod * ysize > FONT_ROOM) { printf("FAIL: font %d too big\n", nfonts - 1); exit(1); }
    memset(arena + f->at, 0, (size_t)FONT_ROOM);
    for (k = 0; k < n; k++)
        for (r = 0; r < ysize; r++)
            for (b = 0; b < f->loc[2 * k + 1]; b++)
                if (rnd() % 100 < 45) {
                    LONG at = f->loc[2 * k] + b;
                    arena[f->at + r * f->mod + (at >> 3)] |= (ogpu_u8)(0x80 >> (at & 7));
                }
}

/* ---- Text: 0.24's, and 0.25's ------------------------------------------------------- */

struct text {
    const struct font *f;
    const UBYTE *s;
    LONG n, cp_x, cp_y, spacing;
    int fg, bg, mode, mask, style;
};

/* screens.c's text_slant (0.24) */
static LONG slant24(const struct font *f, LONG r) {
    LONG d = f->baseline - r;
    return d >= 0 ? d / 2 : -((1 - d) / 2);
}

/* screens.c's text_underline (0.24), unchanged in 0.25 */
#define UL_CHUNK 1024
static void underline(const struct text *t, LONG x, LONG x0, LONG x1, LONG smear, int italic, const UBYTE *src, struct ink *u) {
    const struct font *f = t->f;
    const UWORD *loc = f->loc;
    const WORD *space = f->has_space ? f->space : NULL, *kern = f->has_kern ? f->kern : NULL;
    LONG r = f->baseline + 1, sl = italic ? slant24(f, r) : 0, y = t->cp_y + 1, cx;
    const UBYTE *row = src + r * f->mod;
    UBYTE buf[(UL_CHUNK + 2 + 7) / 8 + 1];
    for (cx = x0; cx <= x1; cx += UL_CHUNK) {
        LONG i, pos = 0, c, run = -1, last = cx + UL_CHUNK - 1 > x1 ? x1 : cx + UL_CHUNK - 1;
        for (i = 0; i < (LONG)sizeof buf; i++) buf[i] = 0;
        for (i = 0; i < t->n; i++) {
            UBYTE ch = t->s[i];
            int k = (ch >= f->lo && ch <= f->hi) ? ch - f->lo : f->hi - f->lo + 1;
            LONG w = loc[2 * k + 1], off = loc[2 * k], left = pos + (kern ? kern[k] : 0) + sl, b, j;
            pos += (space ? space[k] : f->xsize) + (kern ? kern[k] : 0) + t->spacing;
            for (b = 0;; b = smear) {
                for (j = 0; j < w; j++) {
                    LONG col = left + b + j - (cx - 1), bit = off + j;
                    if (col < 0 || col > UL_CHUNK + 1) continue;
                    if (row[bit >> 3] >> (7 - (bit & 7)) & 1) buf[col >> 3] |= (UBYTE)(0x80 >> (col & 7));
                }
                if (b == smear) break;
            }
        }
#define UL_INK(c) (buf[(c) >> 3] >> (7 - ((c) & 7)) & 1)
        for (c = 1; c <= last - cx + 1; c++) {
            int on = !UL_INK(c - 1) && !UL_INK(c) && !UL_INK(c + 1);
            if (on && run < 0) run = c;
            if (!on && run >= 0) { pieces(x + cx - 1 + run, y, x + cx - 1 + c - 1, y, fill_piece, u); run = -1; }
        }
        if (run >= 0) pieces(x + cx - 1 + run, y, x + last, y, fill_piece, u);
#undef UL_INK
    }
}

/* screens.c's text_strip (0.25), the staging block's room counted by rooms (-1: always room) */
static LONG text_strip(const struct ts_font *tf, const UBYTE *s, LONG i, LONG n, LONG x, LONG top, LONG *pos,
                       const struct tmpl_ctx *t, LONG max, int *rooms) {
    LONG left, width, end, bpr, j = ts_run(tf, s, i, n, *pos, max, &left, &width, &end), k;
    struct tmpl_ctx st = *t;
    if (j == i) return i;
    CHECK(j > i && j <= n, "ts_run gave %ld from %ld of %ld", (long)j, (long)i, (long)n);
    if (width) {
        bpr = (width + 7) >> 3;
        CHECK(bpr * tf->ysize <= max, "a strip of %ld bytes, max %ld", (long)(bpr * tf->ysize), (long)max);
        if (!*rooms) return -1;
        if (*rooms > 0) (*rooms)--;
        memset(arena + STRIP, 0xA5, (size_t)(bpr * tf->ysize + 64));
        ts_build(tf, s, i, j, *pos, left, bpr, arena + STRIP, (t->k.mode & COMPLEMENT) != 0);
        for (k = 0; k < 64; k++) CHECK(arena[STRIP + bpr * tf->ysize + k] == 0xA5, "ts_build wrote past its strip");
        st.src = STRIP; st.src_x = 0; st.mod = bpr; st.at_x = x + left; st.at_y = top; st.staged = 1;
        pieces(x + left, top, x + left + width - 1, top + tf->ysize - 1, tmpl_piece, &st);
        batch_flush();                              /* ob_flush */
    }
    *pos = end;
    return j;
}

/* text_patch: 0.24's (strips 0) or 0.25's */
static void text(const struct text *t, int strips, LONG max, int rooms) {
    const struct font *f = t->f;
    const UWORD *loc = f->loc;
    const WORD *space = f->has_space ? f->space : NULL, *kern = f->has_kern ? f->kern : NULL;
    LONG x = t->cp_x, top = t->cp_y - f->baseline, pos = 0, lo = 0, hi = 0, x0, x1, i, n = t->n;
    int any = 0;
    struct tmpl_ctx tc;
    struct ts_font tf;
    LONG smear = (t->style & FSF_BOLD) ? f->smear : 0;
    int italic = (t->style & FSF_ITALIC) != 0;
    ink_of(t->fg, t->bg, t->mode, t->mask, &tc.k);
    way_now = strips;
    batch_begin();
    stg_at = STG;
    for (i = 0; i < n; i++) {
        UBYTE c = t->s[i];
        int k = (c >= f->lo && c <= f->hi) ? c - f->lo : f->hi - f->lo + 1;
        LONG left = pos + (kern ? kern[k] : 0), w = loc[2 * k + 1];
        if (w > 0) {
            if (!any || left < lo) lo = left;
            if (!any || left + w - 1 > hi) hi = left + w - 1;
            any = 1;
        }
        pos += (space ? space[k] : f->xsize) + (kern ? kern[k] : 0) + t->spacing;
    }
    x0 = any && lo < 0 ? lo : 0;
    x1 = any ? hi : -0x7FFFFFFF;
    if (pos - 1 > x1) x1 = pos - 1;
    x1 += smear;
    if (italic) {
        x1 += f->baseline / 2;
        x0 -= (f->ysize - f->baseline + 1) / 2;
    }
    if (n > 0 && tc.k.mode & JAM2 && !(tc.k.mode & COMPLEMENT)) {
        struct ink bg = tc.k;
        bg.a = tc.k.b; bg.mode = JAM1;
        pieces(x + x0, top, x + x1, top + f->ysize - 1, fill_piece, &bg);
    }
    tc.k.mode &= ~JAM2;
    tc.src = f->at; tc.mod = f->mod; tc.staged = 0;
    tf.data = arena + f->at; tf.mod = f->mod; tf.loc = loc; tf.space = space; tf.kern = kern;
    tf.xsize = f->xsize; tf.ysize = f->ysize; tf.baseline = f->baseline; tf.lo = f->lo; tf.hi = f->hi;
    tf.spacing = t->spacing; tf.smear = smear; tf.italic = italic;
    pos = 0;
    for (i = 0; i < n; i++) {
        UBYTE c = t->s[i];
        int k = (c >= f->lo && c <= f->hi) ? c - f->lo : f->hi - f->lo + 1;
        LONG gx = x + pos + (kern ? kern[k] : 0), w = loc[2 * k + 1], b, j;
        if (strips && (j = text_strip(&tf, t->s, i, n, x, top, &pos, &tc, max, &rooms)) != i) {
            if (j > i) { i = j - 1; continue; }
            strips = 0;
        }
        if (w > 0) {
            tc.src_x = loc[2 * k]; tc.at_y = top;
            for (b = 0;; b = smear) {
                LONG ax = gx + b;
                if (!italic) {
                    tc.at_x = ax;
                    pieces(ax, top, ax + w - 1, top + f->ysize - 1, tmpl_piece, &tc);
                } else {
                    LONG r;
                    for (r = 0; r < f->ysize; r++) {
                        tc.at_x = ax + slant24(f, r);
                        pieces(tc.at_x, top + r, tc.at_x + w - 1, top + r, tmpl_piece, &tc);
                    }
                }
                if (b == smear) break;
            }
        }
        pos += (space ? space[k] : f->xsize) + (kern ? kern[k] : 0) + t->spacing;
    }
    batch_flush();
    if (n > 0 && t->style & FSF_UNDERLINED && f->baseline + 1 < f->ysize) {
        struct ink u = tc.k;
        underline(t, x, x0, x1, smear, italic, arena + f->at, &u);
    }
}

/* ---- ts_bits alone, in buffers of just the size (ASan sees a byte too far) ---------- */

static void test_bits(void) {
    int round;
    for (round = 0; round < 20000; round++) {
        LONG sbit = rnd_in(0, 40), dbit = rnd_in(0, 40), w = rnd_in(1, 70), j;
        size_t sbytes = (size_t)((sbit + w + 7) >> 3) - (size_t)(sbit >> 3), dbytes = (size_t)((dbit + w + 7) >> 3) - (size_t)(dbit >> 3);
        UBYTE *s = malloc(sbytes), *d = malloc(dbytes), *ref = malloc(dbytes);
        int xor = (int)(rnd() & 1);
        for (j = 0; j < (LONG)sbytes; j++) s[j] = (UBYTE)rnd();
        for (j = 0; j < (LONG)dbytes; j++) d[j] = ref[j] = (UBYTE)rnd();
        for (j = 0; j < w; j++) {
            LONG sb = (sbit & 7) + j, db = (dbit & 7) + j;
            if (s[sb >> 3] >> (7 - (sb & 7)) & 1) {
                if (xor) ref[db >> 3] ^= (UBYTE)(0x80 >> (db & 7));
                else ref[db >> 3] |= (UBYTE)(0x80 >> (db & 7));
            }
        }
        /* the buffers start at the bytes the bits are in */
        ts_bits(d - (dbit >> 3), dbit, s - (sbit >> 3), sbit, w, xor);
        cases++;
        CHECK(!memcmp(d, ref, dbytes), "ts_bits %ld bits from %ld to %ld (xor %d)", (long)w, (long)sbit, (long)dbit, xor);
        free(s); free(d); free(ref);
    }
}

/* ---- the cases ---------------------------------------------------------------------- */

static const char *const fixed[] = {
    "drawn after the writes", "PokePull frame 00001", "Ram Disk", "Workbench AmigaChrome 3.2",
    "iiiWWW|j_gy,Qq", "11111111", "\x7f\xff\x80 A", "The quick brown fox jumps over the lazy", "W"
};
#define NFIXED (int)(sizeof fixed / sizeof fixed[0])
static const int styles[] = { 0, FSF_BOLD, FSF_ITALIC, FSF_UNDERLINED, FSF_BOLD | FSF_ITALIC, FSF_BOLD | FSF_ITALIC | FSF_UNDERLINED };
static const int modes[] = { JAM1, JAM2, COMPLEMENT, JAM1 | INVERSVID, JAM2 | INVERSVID, COMPLEMENT | JAM2 };
static const char *const fmt_names[] = { "", "CLUT8", "RGB565", "ARGB32" };

/* the ways the pieces are drawn: OpenGPU's core in each format, the CPU's loop */
struct way { int fmt, cpu, mask; };
static const struct way ways[] = { { OGPU_FMT_CLUT8, 0, 0xFF }, { OGPU_FMT_RGB565, 0, 0xFF }, { OGPU_FMT_ARGB32, 0, 0xFF },
                                   { OGPU_FMT_CLUT8, 1, 0x0F }, { OGPU_FMT_RGB565, 1, 0xFF } };
#define NWAYS (int)(sizeof ways / sizeof ways[0])

static void set_way(const struct way *w) {
    long i;
    fmt = w->fmt; use_cpu = w->cpu;
    bpp = fmt == OGPU_FMT_CLUT8 ? 1 : fmt == OGPU_FMT_RGB565 ? 2 : 4;
    bpr = W * bpp + 4;
    for (i = 0; i < bpr * H; i++) background[i] = (ogpu_u8)rnd();
    if (bpp == 4) for (i = 0; i < bpr * H; i += 4) background[i] = 0;      /* the top byte 0, as OpenRTG keeps it */
}

static int one(const struct way *w, struct text *t, LONG max, int rooms) {
    size_t bytes = (size_t)(bpr * H);
    t->mask = w->mask;
    memcpy(arena + T0, background, bytes);
    text(t, 0, max, rooms);
    memcpy(before_px, arena + T0, bytes);
    memcpy(arena + T0, background, bytes);
    text(t, 1, max, rooms);
    memcpy(after_px, arena + T0, bytes);
    cases++;
    if (!memcmp(before_px, after_px, bytes)) return 1;
    {
        size_t i;
        for (i = 0; i < bytes && before_px[i] == after_px[i]; i++) ;
        CHECK(0, "%s%s font %d \"%.20s\" (%ld) at %ld,%ld spacing %ld style %d mode %d max %ld rooms %d: differ from byte %lu (x %lu, y %lu)",
              fmt_names[w->fmt], w->cpu ? " CPU" : "", (int)(t->f - fonts), (const char *)t->s, (long)t->n, (long)t->cp_x, (long)t->cp_y,
              (long)t->spacing, t->style, t->mode, (long)max, rooms, (unsigned long)i, (unsigned long)((i % (size_t)bpr) / (size_t)bpp),
              (unsigned long)(i / (size_t)bpr));
    }
    return 0;
}

int main(void) {
    static UBYTE str[400];
    static const LONG spacings[] = { 0, 2, -3 };
    static const LONG xs[] = { -9, 3, 97, 183 };
    int wi, fi, si, mi, pi, xi, round, before_cases;
    make_font(8, 6, 32, 255, 8, 8, 0, 0, 0, 0, 0, 8, 1);            /* monospace, as topaz */
    make_font(11, 8, 32, 126, 0, 9, 3, 1, 1, -2, 1, 7, 1);          /* proportional, kerned, widths of 0 */
    make_font(23, 18, 32, 126, 3, 17, 2, 1, 1, -3, 2, 12, 2);       /* Workbench's size, smear 2 */
    make_font(5, 4, 65, 90, 1, 12, 5, 0, 1, -4, 3, 7, 0);           /* smear 0, no underline row, A-Z only */
    make_font(9, 0, 33, 120, 2, 10, 1, 1, 0, 0, 0, 6, 1);           /* baseline 0: italic leans left */
    make_font(16, 12, 32, 100, 10, 40, 7, 1, 1, -6, 2, 20, 3);      /* wide glyphs */

    test_bits();
    before_cases = cases;

    /* every font, style, mode, way and spacing, with the fixed strings */
    for (wi = 0; wi < NWAYS; wi++) {
        set_way(&ways[wi]);
        for (fi = 0; fi < nfonts; fi++)
            for (si = 0; si < (int)(sizeof styles / sizeof styles[0]); si++)
                for (mi = 0; mi < (int)(sizeof modes / sizeof modes[0]); mi++)
                    for (pi = 0; pi < 3; pi++)
                        for (xi = 0; xi < 2; xi++) {
                            struct text t;
                            const char *s = fixed[(fi + si + mi + pi + xi) % NFIXED];
                            t.f = &fonts[fi]; t.s = (const UBYTE *)s; t.n = (LONG)strlen(s);
                            t.cp_x = xs[(si + mi + xi * 2) % 4]; t.cp_y = fonts[fi].baseline + (xi ? 3 : H - fonts[fi].ysize - 1);
                            t.spacing = spacings[pi]; t.fg = 1 + (mi & 3); t.bg = 5 + (si & 1); t.mode = modes[mi]; t.style = styles[si];
                            one(&ways[wi], &t, 4096, -1);
                        }
    }
    printf("text strip: TEMPLATEs for the fixed strings on OpenGPU's ways: %ld by 0.24's, %ld by 0.25's\n", templates[0], templates[1]);
    /* at random: strings of any characters, long ones, small strips, room running out */
    for (round = 0; round < 6000; round++) {
        struct text t;
        LONG j, max;
        int rooms;
        const struct way *w = &ways[round % NWAYS];
        if (round % NWAYS == 0) set_way(w);
        t.f = &fonts[rnd() % (unsigned long)nfonts];
        t.n = round % 50 == 0 ? rnd_in(200, 399) : rnd_in(0, 60);
        for (j = 0; j < t.n; j++) str[j] = (UBYTE)(rnd() % 4 ? rnd_in(32, 126) : rnd_in(0, 255));
        str[t.n] = 0;
        t.s = str;
        t.cp_x = rnd_in(-40, W - 20); t.cp_y = t.f->baseline + rnd_in(-4, H - t.f->ysize + 4);
        t.spacing = rnd_in(-4, 4); t.fg = (int)rnd_in(0, 7); t.bg = (int)rnd_in(0, 7);
        t.mode = modes[rnd() % 6]; t.style = (int)(rnd() & 7);
        max = rnd() % 3 == 0 ? 4096 : rnd_in(1, 200);
        rooms = rnd() % 4 == 0 ? (int)rnd_in(0, 3) : -1;
        one(w, &t, max, rooms);
    }
    printf("text strip: %d ts_bits checks, %d Text cases drawn 0.24's way and 0.25's, %d differ\n",
           before_cases, cases - before_cases, failures);
    printf("text strip: TEMPLATEs in all: %ld by 0.24's, %ld by 0.25's (%ld of them strips)\n",
           templates[0], templates[1], strip_templates);
    return failures != 0;
}
