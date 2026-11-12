/* SPDX-License-Identifier: MIT */
/* GFOpenLZMA's decoder, a whole stream in one call (lzma_dec.h). Written by
 * the Team from the published description of the formats (the range
 * coder; the literal, match and rep states; LZMA2's chunks), as the ROM
 * unpacker's decoder (common/openunpack/lzma_dec.c) is, with its input
 * bounded, LZMA2 and the end mark added, and its probabilities big-endian
 * in both builds. Copyright (c) 2026 AmiCradle Team. MIT. */
#include "lzma_dec.h"
#include "../hl_ids.h"

#define LZ_NBITS 11
#define LZ_ONE (1u << LZ_NBITS)
#define LZ_TOP (1u << 24)

/* the probabilities' layout (words) */
#define P_ISMATCH    0          /* [12 states][16 pos states] */
#define P_ISREP      192
#define P_ISREPG0    204
#define P_ISREPG1    216
#define P_ISREPG2    228
#define P_ISREP0LONG 240        /* [12][16] */
#define P_SLOT       432        /* [4 length states][64] */
#define P_SPEC       688        /* 114 */
#define P_ALIGN      802        /* 16 */
#define P_LEN        818        /* choice, choice2, low[16][8], mid[16][8], high[256] */
#define P_REPLEN     1332
#define P_LIT        1846       /* [0x300 << (lc + lp)] */

/* a probability: a word in the CPU's own order while the call runs. The
 * host's words are made big-endian once, as the call returns, so
 * the working memory ends as the 68k leaves it; a byte at a time in the
 * loop cost the host 25% on text. */
#ifdef HL_HOST
static inline uint32_t pg(const uint8_t *p, uint32_t i) { uint16_t v; memcpy(&v, p + 2 * i, 2); return v; }
static inline void ps(uint8_t *p, uint32_t i, uint32_t v) { uint16_t w = (uint16_t)v; memcpy(p + 2 * i, &w, 2); }
#else
static inline uint32_t pg(const uint8_t *p, uint32_t i) { return ((const uint16_t *)(const void *)p)[i]; }
static inline void ps(uint8_t *p, uint32_t i, uint32_t v) { ((uint16_t *)(void *)p)[i] = (uint16_t)v; }
#endif

struct lz {
    const uint8_t *in;
    uint32_t inpos, inend;      /* the range coder's bytes: in[inpos .. inend) */
    uint32_t range, code, over; /* over: it read past inend */
    uint8_t *probs;
    uint32_t lc, lp, pb;
    uint8_t *out;
    uint32_t pos, start;        /* the output so far; the dictionary's start (LZMA2's reset) */
    uint32_t st, r0, r1, r2, r3;
};

static inline uint32_t lz_byte(struct lz *z)
{
    if (z->inpos < z->inend) return z->in[z->inpos++];
    z->over = 1;
    return 0;
}

static void lz_reset_state(struct lz *z)
{
    uint32_t n = P_LIT + (0x300u << (z->lc + z->lp)), i;
    for (i = 0; i < n; i++) ps(z->probs, i, LZ_ONE / 2);
    z->st = z->r0 = z->r1 = z->r2 = z->r3 = 0;
}

/* the range coder's first five bytes; 0 if the first is not 0 */
static int lz_rc_init(struct lz *z)
{
    uint32_t i;
    z->range = 0xFFFFFFFFu;
    z->code = 0;
    if (lz_byte(z) != 0) return 0;
    for (i = 0; i < 4; i++) z->code = (z->code << 8) | lz_byte(z);
    return 1;
}

/* The decode loop keeps the range coder, the output position and the
 * reps in locals (registers on the 68k) and works on the probabilities
 * through pointers; the struct is written back when it returns. */
#ifdef HL_HOST
#define PG(q) pg((q), 0)
#define PS(q, v) ps((q), 0, (v))
#else
#define PG(q) ((uint32_t)*(const uint16_t *)(const void *)(q))
#define PS(q, v) (*(uint16_t *)(void *)(q) = (uint16_t)(v))
#endif
#define PR(i) (pr + 2 * (i))
/* the input running out inside a symbol ends the call: HL_LZD_SHORT */
#define NORM() do { if (rng < LZ_TOP) { \
        if (ip == ie) goto short_in; \
        rng <<= 8; \
        code = (code << 8) | *ip++; \
    } } while (0)
/* one bit at probability q: runs Z if it is 0, O if 1 */
#define BIT(q, Z, O) do { uint8_t *q_ = (q); uint32_t p_ = PG(q_), b_; NORM(); \
    b_ = (rng >> LZ_NBITS) * p_; \
    if (code < b_) { rng = b_; PS(q_, p_ + ((LZ_ONE - p_) >> 5)); Z; } \
    else { rng -= b_; code -= b_; PS(q_, p_ - (p_ >> 5)); O; } } while (0)
/* m = a tree's symbol (1 << n added) over n bits at base q */
#define TREE(q, n, m) do { uint8_t *t_ = (q); uint32_t k_ = (n); (m) = 1; \
    _Pragma("GCC unroll 8") \
    do { BIT(t_ + 2 * (m), (m) <<= 1, (m) = ((m) << 1) | 1); } while (--k_); } while (0)
#define LEN(L, l) do { uint8_t *L_ = (L); \
    BIT(L_, TREE(L_ + 4 + pst * 16, 3, l); l -= 8, \
        BIT(L_ + 2, TREE(L_ + 4 + 256 + pst * 16, 3, l), \
            TREE(L_ + 4 + 512, 8, l); l -= 256 - 16)); } while (0)

/* Decode up to end (output position). Answers HL_LZD_OK at end (or at the
 * end mark when mark is 1: *marked set), HL_LZD_DATA, HL_LZD_SHORT. A
 * match past end answers HL_LZD_DATA with z->r0 < z->pos - z->start (every
 * other HL_LZD_DATA has z->r0 at or past it): lz_lzma tells LZMA_EOS's too
 * little room by that, so the loop itself stays as it was. */
/* GCC's global CSE keeps probability addresses live across the loop and
 * runs the 68k out of registers (about 1.2% more instructions here) */
#if defined(__GNUC__) && !defined(__clang__)
__attribute__((optimize("no-gcse")))
#endif
static uint32_t lz_run(struct lz *z, uint32_t end, int mark_ok, int *marked)
{
    /* lpmask keeps the position's lp bits above the previous byte's lc
     * top bits in (pos << 8) + prev; shifted up by lc it is the literal
     * coder's number times 0x100 */
    const uint32_t pbm = (1u << z->pb) - 1, lc = z->lc, lpmask = (0x100u << z->lp) - (0x100u >> lc);
    uint8_t *const dst = z->out, *const pr = z->probs;
    const uint8_t *ip = z->in + z->inpos, *const ie = z->in + z->inend;
    uint32_t rng = z->range, code = z->code;
    uint32_t pos = z->pos, st = z->st, r0 = z->r0, r1 = z->r1, r2 = z->r2, r3 = z->r3;
    const uint32_t start = z->start;
    uint32_t ret = HL_LZD_OK;
    if (z->over) return HL_LZD_SHORT;
    while (pos < end) {
        uint32_t pst = pos & pbm, l;
        BIT(PR(P_ISMATCH + (st << 4) + pst), goto literal, (void)0);
        BIT(PR(P_ISREP + st), goto newmatch, (void)0);
        if (pos == start) { ret = HL_LZD_DATA; goto out; }
        BIT(PR(P_ISREPG0 + st),
            BIT(PR(P_ISREP0LONG + (st << 4) + pst), goto shortrep, (void)0),
            {
                uint32_t d;
                BIT(PR(P_ISREPG1 + st), d = r1,
                    { BIT(PR(P_ISREPG2 + st), d = r2, { d = r3; r3 = r2; }); r2 = r1; });
                r1 = r0;
                r0 = d;
            });
        LEN(PR(P_REPLEN), l);
        st = st < 7 ? 8 : 11;
        goto copy;
    newmatch: {
            uint32_t slot;
            r3 = r2;
            r2 = r1;
            r1 = r0;
            LEN(PR(P_LEN), l);
            st = st < 7 ? 7 : 10;
            TREE(PR(P_SLOT + ((l < 3 ? l : 3) << 6)), 6, slot);
            slot -= 64;
            if (slot < 4) {
                r0 = slot;
            } else {
                uint32_t nb = (slot >> 1) - 1, d = (2 | (slot & 1)) << nb, m = 1, i;
                if (slot < 14) {
                    uint8_t *q = PR(P_SPEC + d - slot - 1);
                    for (i = 0; i < nb; i++) BIT(q + 2 * m, m <<= 1, { m = (m << 1) | 1; d |= 1u << i; });
                } else {
                    uint32_t s = 1, lim = 1u << (nb - 4);   /* s: the bits under a leading 1 */
                    uint8_t *q = PR(P_ALIGN);
                    do {
                        uint32_t t;
                        NORM();
                        rng >>= 1;
                        code -= rng;
                        t = 0 - (code >> 31);           /* all ones: the bit is 0 */
                        code += rng & t;
                        s = (s << 1) + (t + 1);
                    } while (s < lim);
                    d += (s - lim) << 4;
                    _Pragma("GCC unroll 4")
                    for (i = 0; i < 4; i++) BIT(q + 2 * m, m <<= 1, { m = (m << 1) | 1; d |= 1u << i; });
                }
                r0 = d;
                if (d == 0xFFFFFFFFu) {             /* the end mark */
                    if (!mark_ok) { ret = HL_LZD_DATA; goto out; }
                    *marked = 1;
                    goto out;
                }
            }
        }
    copy:
        l += 2;
        if (r0 >= pos - start) { ret = HL_LZD_DATA; goto out; }    /* before the dictionary's start */
        if (l > end - pos) { ret = HL_LZD_DATA; goto out; }        /* past the end */
        {
            uint8_t *d = dst + pos, *f = d - r0 - 1;
            pos += l;
            do *d++ = *f++; while (--l);
        }
        continue;
    shortrep:                                       /* one byte at rep0 */
        st = st < 7 ? 9 : 11;
        if (r0 >= pos - start) { ret = HL_LZD_DATA; goto out; }
        dst[pos] = dst[pos - r0 - 1];
        pos++;
        continue;
    literal: {
            uint32_t prev = pos > start ? dst[pos - 1] : 0, s = 1;
            uint8_t *q = PR(P_LIT) + 6 * ((((pos << 8) + prev) & lpmask) << lc);
            if (st < 7) {                           /* the plain literal: eight bits, unrolled */
                uint32_t k = 8;
                _Pragma("GCC unroll 8")
                do BIT(q + 2 * s, s <<= 1, s = (s << 1) | 1); while (--k);
            } else {
                /* after a match: the byte at rep0 guides while its bits
                 * agree (offs 0x100), then the plain tree (offs 0) */
                uint32_t mb = dst[pos - r0 - 1], offs = 0x100, k = 8;
                _Pragma("GCC unroll 8")
                do {
                    uint32_t bit = offs;
                    mb <<= 1;
                    offs &= mb;
                    BIT(q + 2 * (offs + bit + s), { s <<= 1; offs ^= bit; }, s = (s << 1) | 1);
                } while (--k);
            }
            dst[pos++] = (uint8_t)s;
            st = st < 4 ? 0 : st < 10 ? st - 3 : st - 6;
        }
    }
    goto out;
short_in:                                       /* the input ran out inside a symbol */
    z->over = 1;
    ret = HL_LZD_SHORT;
out:
    z->inpos = (uint32_t)(ip - z->in);
    z->range = rng;
    z->code = code;
    z->pos = pos;
    z->st = st;
    z->r0 = r0; z->r1 = r1; z->r2 = r2; z->r3 = r3;
    return ret;
}

static int lz_props(struct lz *z, uint32_t props)
{
    if (props >= 9 * 5 * 5) return 0;
    z->lc = props % 9;
    z->lp = (props / 9) % 5;
    z->pb = props / 45;
    return 1;
}

static uint32_t lz_lzma(struct lz *z, uint32_t out_len, int eos)
{
    int marked = 0;
    uint32_t r;
    lz_reset_state(z);
    /* OUT_LEN bytes; an end mark may close the stream at OUT_LEN or before
     * it (then the output is short: HL_LZD_DATA, as the size was promised) */
    if (!lz_rc_init(z)) return z->over ? HL_LZD_SHORT : HL_LZD_DATA;
    r = lz_run(z, out_len, 1, &marked);
    /* LZMA_EOS: OUT_LEN is the room, not the size, so a match past it wants more room */
    if (r == HL_LZD_DATA && eos && z->r0 < z->pos - z->start) return HL_LZD_ROOM;
    if (r != HL_LZD_OK) return r;
    /* LZMA_EOS: only its end mark closes it */
    if (eos) return marked ? HL_LZD_OK : HL_LZD_ROOM;
    if (marked && z->pos != out_len) return HL_LZD_DATA;
    return HL_LZD_OK;
}

static uint32_t lz_lzma2(struct lz *z, uint32_t out_len)
{
    int need_dict = 1, need_props = 1, need_state = 1;
    for (;;) {
        uint32_t c, usize, psize, mode;
        int marked = 0;
        if (z->inpos >= z->inend) return HL_LZD_SHORT;
        c = z->in[z->inpos++];
        if (c == 0) return HL_LZD_OK;
        if (c == 1 || c == 2) {                     /* uncompressed, with or without a dictionary reset */
            uint32_t i;
            if (c == 1) {
                z->start = z->pos;
                need_dict = 0;
            } else if (need_dict) return HL_LZD_DATA;
            if (z->inend - z->inpos < 2) return HL_LZD_SHORT;
            usize = ((uint32_t)z->in[z->inpos] << 8 | z->in[z->inpos + 1]) + 1;
            z->inpos += 2;
            if (z->inend - z->inpos < usize) return HL_LZD_SHORT;
            if (out_len - z->pos < usize) return HL_LZD_ROOM;
            for (i = 0; i < usize; i++) z->out[z->pos + i] = z->in[z->inpos + i];
            z->pos += usize;
            z->inpos += usize;
            continue;
        }
        if (c < 0x80) return HL_LZD_DATA;
        mode = (c >> 5) & 3;
        if (z->inend - z->inpos < 4) return HL_LZD_SHORT;
        usize = ((c & 0x1F) << 16 | (uint32_t)z->in[z->inpos] << 8 | z->in[z->inpos + 1]) + 1;
        psize = ((uint32_t)z->in[z->inpos + 2] << 8 | z->in[z->inpos + 3]) + 1;
        z->inpos += 4;
        if (mode == 3) {
            z->start = z->pos;
            need_dict = 0;
        } else if (need_dict) return HL_LZD_DATA;
        if (mode >= 2) {
            if (z->inpos >= z->inend) return HL_LZD_SHORT;
            if (!lz_props(z, z->in[z->inpos++]) || z->lc + z->lp > 4) return HL_LZD_DATA;
            need_props = 0;
        } else if (need_props) return HL_LZD_DATA;
        if (mode >= 1) {
            lz_reset_state(z);
            need_state = 0;
        } else if (need_state) return HL_LZD_DATA;
        if (z->inend - z->inpos < psize) return HL_LZD_SHORT;
        if (out_len - z->pos < usize) return HL_LZD_ROOM;
        {
            uint32_t end = z->inend, cstart = z->inpos, r;
            z->inend = cstart + psize;
            if (!lz_rc_init(z)) return HL_LZD_DATA;
            r = lz_run(z, z->pos + usize, 0, &marked);
            if (r == HL_LZD_SHORT) r = HL_LZD_DATA;     /* a chunk's own bytes ran out */
            if (r != HL_LZD_OK) return r;
            z->inend = end;
            z->inpos = cstart + psize;
        }
    }
}

HL_TAG(hostlib_lzma_decode_all, HL_ID_LZMA_DECODE_ALL)
HL_FN(hostlib_lzma_decode_all)
{
    struct lz z;
    hl_addr ia, oa, wa, ra;
    uint32_t mode, props, in_len, out_len, wlen, need, r;
    uint8_t *res;
    HL_NEED_R(block, HL_LZD_BLOCK);
    mode = HL_ARG(HL_LZD_MODE);
    props = HL_ARG(HL_LZD_PROPS);
    ia = HL_ARG(HL_LZD_IN);
    in_len = HL_ARG(HL_LZD_IN_LEN);
    oa = HL_ARG(HL_LZD_OUT);
    out_len = HL_ARG(HL_LZD_OUT_LEN);
    wa = HL_ARG(HL_LZD_WORK);
    wlen = HL_ARG(HL_LZD_WORK_LEN);
    ra = HL_ARG(HL_LZD_RESULT);
    HL_NEED_W(ra, 8);
    if (in_len) HL_NEED_R(ia, in_len);
    if (out_len) HL_NEED_W(oa, out_len);
    if (mode == HL_LZD_LZMA || mode == HL_LZD_LZMA2 || mode == HL_LZD_LZMA_EOS) {
        need = hl_lzd_work(mode, props);
        if (wlen >= need) HL_NEED_W(wa, need);
    } else {
        need = 0xFFFFFFFFu;
    }
    HL_DECLARED();
    HL_PTR_W(res, ra, 8);
    z.in = 0;
    z.out = 0;
    z.probs = 0;
    if (in_len) HL_PTR_R(z.in, ia, in_len);
    if (out_len) HL_PTR_W(z.out, oa, out_len);
    z.inpos = 0;
    z.inend = in_len;
    z.over = 0;
    z.pos = z.start = 0;
    z.lc = z.lp = z.pb = 0;
    if (wlen < need || (mode != HL_LZD_LZMA2 && !lz_props(&z, props))) {
        r = HL_LZD_PARAM;
    } else {
        HL_PTR_W(z.probs, wa, need);
        /* LZMA2: the props come with its chunks; set lc + lp = 4 so every
         * word of the working memory is written first (rule 5) */
        if (mode == HL_LZD_LZMA2) {
            z.lc = 4;
            z.lp = 0;
            lz_reset_state(&z);
            z.lc = 0;
            r = lz_lzma2(&z, out_len);
        } else {
            r = lz_lzma(&z, out_len, mode == HL_LZD_LZMA_EOS);
        }
    }
#if defined(HL_HOST) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    /* every word the call wrote, big-endian as the 68k's (z.probs is set
     * only when the call ran, and then it wrote all need words first) */
    if (z.probs) {
        uint32_t i;
        for (i = 0; i < need; i += 2) { uint8_t t = z.probs[i]; z.probs[i] = z.probs[i + 1]; z.probs[i + 1] = t; }
    }
#endif
    res[0] = (uint8_t)(z.pos >> 24); res[1] = (uint8_t)(z.pos >> 16);
    res[2] = (uint8_t)(z.pos >> 8); res[3] = (uint8_t)z.pos;
    res[4] = (uint8_t)(z.inpos >> 24); res[5] = (uint8_t)(z.inpos >> 16);
    res[6] = (uint8_t)(z.inpos >> 8); res[7] = (uint8_t)z.inpos;
    return r;
}
