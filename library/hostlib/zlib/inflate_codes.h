/* SPDX-License-Identifier: MIT */
#ifndef HOSTLIB_ZLIB_INFLATE_CODES_H
#define HOSTLIB_ZLIB_INFLATE_CODES_H
/* GFOpenZlib's inflate, the codes of a block (RFC 1951 3.2.5): literals,
 * lengths and distances, until the block's end, the input or the output runs
 * out, or a code is bad. Copyright (c) 2026 AmiCradle Team. MIT.
 *
 * z.library's inflate (libraries/openzlib/zlib_core.cpp) does the rest:
 * headers, stored blocks, the code tables, the checks. It hands this call
 * the decoder's registers (struct hl_zic_state, copied in and out by the
 * library), the window (1 << wlog bytes), the two code tables as the 68k
 * lays them out (struct hl_zic_huff), the input and the output. Every
 * output byte also goes into the window, as the library's own put() does.
 *
 * State (HOSTLIB-ABI.md 8.1): the registers and the window, read and then
 * changed. A decline puts both back and the 68k body runs the call; a
 * stream may go on either way at every call. The tables and the input are
 * read ranges, the output a write range. */
#include "../hl.h"

/* the block, longs */
enum { HL_ZIC_STATE, HL_ZIC_WINDOW, HL_ZIC_WSIZE, HL_ZIC_LCODE, HL_ZIC_DCODE,
       HL_ZIC_IN, HL_ZIC_AVAIL_IN, HL_ZIC_OUT, HL_ZIC_AVAIL_OUT, HL_ZIC_LONGS };
#define HL_ZIC_BLOCK (4u * HL_ZIC_LONGS)

/* where the decoder is: inflate's modes M_LEN to M_MATCH, in their order */
enum { HL_ZIC_LEN, HL_ZIC_LIT, HL_ZIC_DIST, HL_ZIC_DISTEXT, HL_ZIC_MATCH };

/* what the call answers (never 0: that is only a decline's) */
enum { HL_ZIC_MORE = 1,       /* the input or the output ran out: the registers say where */
       HL_ZIC_END,            /* the end-of-block code, taken */
       HL_ZIC_BAD_LIT,        /* invalid literal/length code */
       HL_ZIC_BAD_DIST,       /* invalid distance code */
       HL_ZIC_FAR };          /* invalid distance too far back */

#define HL_ZIC_FAST 8         /* zlib_core.cpp's FAST: the short-code table's bits */

/* The host reads these structs in the 68k's byte order through HL_BE,
 * which only GCC has (scalar_storage_order). Without it the host would
 * read z.library's state little-endian with no other warning. */
#if defined(HL_HOST) && !defined(HL_BE)
#error "inflate_codes.h needs HL_BE (GCC's scalar_storage_order): build the host side with GCC"
#endif

/* zlib_core.cpp's Huff, as the 68k lays it out */
struct hl_zic_huff {
    uint16_t count[16];
    uint16_t sym[288];
    uint16_t fast[1 << HL_ZIC_FAST];   /* len << 9 | symbol, 0: not a short code */
}
#ifdef HL_BE
HL_BE
#endif
;
HL_SIZE(struct hl_zic_huff, 1120);

/* the decoder's registers: zlib_core.cpp's Inflate fields of the same names */
struct hl_zic_state {
    uint32_t mode, hold, bits, back, emitted;
    uint32_t length, dist, dsym, lit, last;
    uint32_t wnext, whave, total;
    uint32_t in_used, out_used;        /* answers: input taken and output given this call */
}
#ifdef HL_BE
HL_BE
#endif
;
HL_SIZE(struct hl_zic_state, 60);
HL_LAYOUT(struct hl_zic_state, total, 48);

#endif
