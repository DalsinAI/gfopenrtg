/* SPDX-License-Identifier: MIT */
#ifndef HOSTLIB_ZLIB_DEFLATE_ALL_H
#define HOSTLIB_ZLIB_DEFLATE_ALL_H
/* GFOpenZlib's deflate, a whole input in one call: raw deflate blocks
 * (RFC 1951), the last one marked, for every level and strategy z.library
 * has. Copyright (c) 2026 AmiCradle Team. MIT.
 *
 * A streaming deflate's state (window, hash chains, symbols: about 192 KB
 * at windowBits 15, memLevel 8) is over a locked call's 64 KB
 * (HOSTLIB-ABI.md 8.1, A2), so this call takes the whole input and fills
 * the output in one go, with its working memory in a write range it
 * clears first ("Larger state"). z.library uses it for compress2 and for
 * a first deflate(Z_FINISH) with room for all of the output; it writes
 * the zlib or gzip header and trailer itself.
 *
 * Not a locked call: nothing is kept between calls. A decline leaves
 * nothing behind the 68k body needs; it runs the call from the start. */
#include "../hl.h"

/* the block, longs */
enum { HL_ZDA_IN, HL_ZDA_IN_LEN, HL_ZDA_OUT, HL_ZDA_OUT_CAP, HL_ZDA_WORK, HL_ZDA_WORK_LEN,
       HL_ZDA_LEVEL, HL_ZDA_STRATEGY, HL_ZDA_WBITS, HL_ZDA_MEMLEVEL, HL_ZDA_LONGS };
#define HL_ZDA_BLOCK (4u * HL_ZDA_LONGS)

/* z.library's strategies (zlib.h's numbers) */
enum { HL_ZDA_DEFAULT, HL_ZDA_FILTERED, HL_ZDA_HUFFMAN_ONLY, HL_ZDA_RLE, HL_ZDA_FIXED };

/* The working memory's size for in_len bytes, windowBits 8..15 (8 is
 * taken as 9, as zlib does) and memLevel 1..9: the fixed part, the hash
 * heads, the chain links and the symbol buffer, each sized down for a
 * small input. HL_ZDA_WORK_LEN must be at least this. */
#define HL_ZDA_FIXED_BYTES 9600u
static inline uint32_t hl_zda_wlog(uint32_t in_len, uint32_t wbits)
{
    uint32_t w = wbits < 9 ? 9 : wbits;
    while (w > 8 && (1u << (w - 1)) >= in_len) w--;
    return w;
}
static inline uint32_t hl_zda_hbits(uint32_t wlog, uint32_t memlevel)
{
    uint32_t h = memlevel + 7;
    return h > wlog + 1 ? wlog + 1 : h;
}
static inline uint32_t hl_zda_cap(uint32_t in_len, uint32_t memlevel)
{
    uint32_t c = 1u << (memlevel + 6);
    return in_len + 1 < c ? (in_len + 1 > 256 ? in_len + 1 : 256) : c;
}
static inline uint32_t hl_zda_work(uint32_t in_len, uint32_t wbits, uint32_t memlevel)
{
    uint32_t wlog = hl_zda_wlog(in_len, wbits), cap = hl_zda_cap(in_len, memlevel);
    return HL_ZDA_FIXED_BYTES + 4u * (1u << hl_zda_hbits(wlog, memlevel)) + 4u * (1u << wlog) +
           ((3u * cap + 3u) & ~3u);
}

/* the working memory's first long, after the call: how many input bytes
 * the first block covers (z.library's data_type guess looks at those) */
#define HL_ZDA_FIRST_RAW 0u

#endif
