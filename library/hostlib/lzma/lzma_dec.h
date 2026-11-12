/* SPDX-License-Identifier: MIT */
#ifndef HOSTLIB_LZMA_DEC_H
#define HOSTLIB_LZMA_DEC_H
/* GFOpenLZMA's decoder, a whole stream in one call: LZMA (7z's method
 * 030101, a raw stream with its size known, an end mark allowed) or LZMA2
 * (7z's 21, xz's: chunks, uncompressed or LZMA, with their resets). The
 * shared decoder for 7z and PhysicsFS in place of each one's copy of the
 * LZMA SDK. Copyright (c) 2026 AmiCradle Team. MIT.
 *
 * The output is the dictionary: the call decodes all of it at once, so
 * nothing is kept between calls (HOSTLIB-ABI.md 8.1, "Larger state"). The
 * probabilities live in a write range the call sets first, big-endian
 * words in both builds. */
#include "../hl.h"

/* the block, longs */
enum { HL_LZD_MODE, HL_LZD_PROPS, HL_LZD_IN, HL_LZD_IN_LEN, HL_LZD_OUT, HL_LZD_OUT_LEN,
       HL_LZD_WORK, HL_LZD_WORK_LEN, HL_LZD_RESULT, HL_LZD_LONGS };
#define HL_LZD_BLOCK (4u * HL_LZD_LONGS)

/* MODE: LZMA with PROPS its lc/lp/pb byte (lc + 9 (lp + 5 pb), at most
 * 224) and OUT_LEN bytes out exactly (an end mark may close it, or end it
 * at OUT_LEN); or LZMA2, its chunks to their end mark, at most OUT_LEN
 * bytes out (PROPS, the dictionary size, is not needed). */
enum { HL_LZD_LZMA = 1, HL_LZD_LZMA2 = 2, HL_LZD_LZMA_EOS = 3 };
/* LZMA_EOS: LZMA as above whose size is not known (an .lzma file's size
 * of -1): the stream must close with its end mark, at most OUT_LEN bytes
 * out (HL_LZD_ROOM when it does not close by then: the mark is read only
 * while there is room, so give one byte more than the largest output). */

/* RESULT is a write range of two longs: the bytes written, the bytes
 * read. The call answers one of these (never 0, a decline's): */
enum { HL_LZD_OK = 1,          /* the stream decoded whole */
       HL_LZD_DATA,            /* the data is damaged: a bad code, a distance before the start */
       HL_LZD_SHORT,           /* the input ended first */
       HL_LZD_ROOM,            /* LZMA2 or LZMA_EOS: more output than OUT_LEN */
       HL_LZD_PARAM };         /* a mode, props or working memory the call does not take */

/* the working memory for LZMA2 (lc + lp at most 4) or for an LZMA props byte */
#define HL_LZD_PROBS_FIXED 1846u
static inline uint32_t hl_lzd_work(uint32_t mode, uint32_t props)
{
    uint32_t lclp = mode == HL_LZD_LZMA2 ? 4u : props % 9 + (props / 9) % 5;
    return 2u * (HL_LZD_PROBS_FIXED + (0x300u << lclp));
}

#endif
