/* SPDX-License-Identifier: MIT */
/* GFOpenZlib's deflate, a whole input in one call (deflate_all.h). Written
 * from RFC 1951 as zlib_core.cpp's deflate is, clean-room, the same
 * choices (levels, lazy matching, the shortest of stored, fixed and
 * dynamic blocks), in C, so the 68k body and the host build write the
 * same bytes, working memory included. Copyright (c) 2026 AmiCradle Team.
 * MIT. */
#include "deflate_all.h"
#include "../hl_ids.h"

#define ZDA_MIN_MATCH 3u
#define ZDA_MAX_MATCH 258u
#define ZDA_MIN_LOOKAHEAD (ZDA_MAX_MATCH + ZDA_MIN_MATCH + 1u)
#define ZDA_TOO_FAR 4096u

static const uint16_t zda_len_base[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                                           35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const uint8_t zda_len_extra[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
                                           3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const uint16_t zda_dist_base[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
                                            257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
                                            8193, 12289, 16385, 24577 };
static const uint8_t zda_dist_extra[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
                                            7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };
static const uint8_t zda_cl_order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };

/* zlib_core.cpp's LEVELS: good, lazy, nice, chain, kind, max_insert */
enum { ZDA_STORE, ZDA_GREEDY, ZDA_LAZY };
static const uint16_t zda_levels[10][6] = {
    {  0,   0,   0,    0, ZDA_STORE,  0 },
    {  0,   0,   8,    2, ZDA_GREEDY, 4 },
    {  0,   0,  16,    6, ZDA_GREEDY, 6 },
    {  0,   0,  32,   16, ZDA_GREEDY, 16 },
    {  6,   6,  24,   12, ZDA_LAZY,   0 },
    {  8,  12,  48,   24, ZDA_LAZY,   0 },
    { 10,  20,  96,   96, ZDA_LAZY,   0 },
    { 12,  40, 160,  200, ZDA_LAZY,   0 },
    { 24, 100, 258,  800, ZDA_LAZY,   0 },
    { 40, 258, 258, 3000, ZDA_LAZY,   0 },
};

/* The working memory: big-endian numbers in both builds, so the two leave
 * the same bytes. The fixed part's offsets: */
enum {
    ZF_FIRST_RAW = 0, ZF_LFREQ = 4, ZF_DFREQ = 1148, ZF_CLFREQ = 1268, ZF_HW = 1344,
    ZF_HPARENT = 3648, ZF_HDEPTH = 4800, ZF_HSORTED = 5952, ZF_LCODE = 6528, ZF_DCODE = 7104,
    ZF_CLCODE = 7164, ZF_FCODE = 7202, ZF_FDCODE = 7778, ZF_LLEN = 7838, ZF_DLEN = 8126,
    ZF_CLLEN = 8156, ZF_LEN_CODE = 8176, ZF_DIST_CODE = 8432, ZF_RLE_SYM = 8944, ZF_RLE_EXT = 9264,
    ZF_END = 9584
};
_Static_assert(ZF_END <= HL_ZDA_FIXED_BYTES, "deflate_all.h's HL_ZDA_FIXED_BYTES is too small");

/* a big-endian long or word at p + 4i or 2i: the 68k's own loads, the
 * host's byte by byte (its compiler makes a load and a swap of them) */
#ifdef HL_HOST
static inline uint32_t g32(const uint8_t *p, uint32_t i)
{
    p += 4 * i;
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
static inline void s32(uint8_t *p, uint32_t i, uint32_t v)
{
    p += 4 * i;
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}
static inline uint32_t g16(const uint8_t *p, uint32_t i)
{
    p += 2 * i;
    return (uint32_t)p[0] << 8 | p[1];
}
static inline void s16(uint8_t *p, uint32_t i, uint32_t v)
{
    p += 2 * i;
    p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v;
}
#else
static inline uint32_t g32(const uint8_t *p, uint32_t i) { return ((const uint32_t *)(const void *)p)[i]; }
static inline void s32(uint8_t *p, uint32_t i, uint32_t v) { ((uint32_t *)(void *)p)[i] = v; }
static inline uint32_t g16(const uint8_t *p, uint32_t i) { return ((const uint16_t *)(const void *)p)[i]; }
static inline void s16(uint8_t *p, uint32_t i, uint32_t v) { ((uint16_t *)(void *)p)[i] = (uint16_t)v; }
#endif

struct zda {
    uint8_t *f;                         /* the fixed part */
    uint8_t *head, *prev;               /* longs */
    uint8_t *sym_lc;
    uint8_t *sym_dist;                  /* words */
    const uint8_t *in;
    uint8_t *out;
    uint32_t in_len, out_n, out_cap, over;
    uint32_t bitbuf, bitcnt;
    uint32_t wmask, maxdist, hmask, hshift, sym_n, sym_cap;
    uint32_t strstart, lookahead, block_start, match_start, typed;
    uint32_t pending_lit, plen, pdist;
    uint32_t level, strategy, good, lazy, nice, chain, max_insert, kind;
};

static uint32_t zda_reverse(uint32_t code, uint32_t n)
{
    uint32_t r = 0;
    while (n--) {
        r = r << 1 | (code & 1);
        code >>= 1;
    }
    return r;
}

static uint32_t zda_fixed_len(uint32_t sym)
{
    return sym < 144 ? 8 : sym < 256 ? 9 : sym < 280 ? 7 : 8;
}

/* ---- bits out ---------------------------------------------------------- */

static inline void zda_byte(struct zda *d, uint32_t b)
{
    if (d->out_n < d->out_cap) d->out[d->out_n++] = (uint8_t)b;
    else d->over = 1;
}

static inline void zda_bits(struct zda *d, uint32_t value, uint32_t n)
{
    d->bitbuf |= value << d->bitcnt;
    d->bitcnt += n;
    while (d->bitcnt >= 8) {
        zda_byte(d, d->bitbuf);
        d->bitbuf >>= 8;
        d->bitcnt -= 8;
    }
}

static void zda_align(struct zda *d)
{
    if (d->bitcnt) zda_byte(d, d->bitbuf);
    d->bitbuf = d->bitcnt = 0;
}

/* ---- Huffman codes (zlib_core.cpp's build_lengths and make_codes) ------ */

/* lengths of at most maxbits for the n longs at freq, into the n bytes at len */
static void zda_lengths(struct zda *d, const uint8_t *freq, uint32_t n, uint32_t maxbits, uint8_t *len)
{
    uint8_t *w = d->f + ZF_HW, *nw = w + 4 * n;
    uint8_t *srt = d->f + ZF_HSORTED, *par = d->f + ZF_HPARENT, *dep = d->f + ZF_HDEPTH;
    uint32_t m = 0, i, k = 0, li, ii, next, cap, kraft;
    int over = 0;
    for (i = 0; i < n; i++) {
        s32(w, i, g32(freq, i));
        len[i] = 0;
    }
    for (i = 0; i < n; i++)
        if (g32(w, i)) m++;
    for (i = 0; i < n && m < 2; i++)
        if (!g32(w, i)) {
            s32(w, i, 1);
            m++;
        }
    for (i = 0; i < n; i++) {
        uint32_t j, wi = g32(w, i);
        if (!wi) continue;
        j = k++;
        while (j && g32(w, g16(srt, j - 1)) > wi) {
            s16(srt, j, g16(srt, j - 1));
            j--;
        }
        s16(srt, j, i);
    }
    li = 0;
    ii = m;
    for (next = m; next < 2 * m - 1; next++) {
        uint32_t pick[2], t, wa, wb;
        for (t = 0; t < 2; t++) {
            if (li < m && (ii >= next || g32(w, g16(srt, li)) <= g32(nw, ii - m))) pick[t] = li++;
            else pick[t] = ii++;
        }
        wa = pick[0] < m ? g32(w, g16(srt, pick[0])) : g32(nw, pick[0] - m);
        wb = pick[1] < m ? g32(w, g16(srt, pick[1])) : g32(nw, pick[1] - m);
        s32(nw, next - m, wa + wb);
        s16(par, pick[0], next);
        s16(par, pick[1], next);
    }
    s16(dep, 2 * m - 2, 0);
    for (i = 2 * m - 2; i-- > 0;) s16(dep, i, g16(dep, g16(par, i)) + 1);
    for (i = 0; i < m; i++) {
        uint32_t l = g16(dep, i);
        if (l > maxbits) {
            l = maxbits;
            over = 1;
        }
        len[g16(srt, i)] = (uint8_t)l;
    }
    if (!over) return;
    cap = 1u << maxbits;
    kraft = 0;
    for (i = 0; i < n; i++)
        if (len[i]) kraft += 1u << (maxbits - len[i]);
    while (kraft > cap) {
        uint32_t best = n;
        for (i = 0; i < n; i++)
            if (len[i] && len[i] < maxbits && (best == n || len[i] > len[best])) best = i;
        len[best]++;
        kraft -= 1u << (maxbits - len[best]);
    }
    while (kraft < cap) {
        uint32_t best = n;
        for (i = 0; i < n; i++)
            if (len[i] && (best == n || len[i] > len[best])) best = i;
        kraft += 1u << (maxbits - len[best]);
        len[best]--;
    }
}

/* RFC 1951 3.2.2's canonical codes, bit-reversed, as words at code */
static void zda_codes(const uint8_t *len, uint32_t n, uint8_t *code)
{
    uint32_t count[16], next[16], i, b, c = 0;
    for (i = 0; i < 16; i++) count[i] = 0;
    for (i = 0; i < n; i++) count[len[i]]++;
    count[0] = 0;
    next[0] = 0;
    for (b = 1; b < 16; b++) {
        c = (c + count[b - 1]) << 1;
        next[b] = c;
    }
    for (i = 0; i < n; i++) s16(code, i, len[i] ? zda_reverse(next[len[i]]++, len[i]) : 0);
}

/* ---- blocks (zlib_core.cpp's emit_block) -------------------------------- */

static inline uint32_t zda_dcode(const struct zda *d, uint32_t dist)
{
    const uint8_t *dc = d->f + ZF_DIST_CODE;
    return dist <= 256 ? dc[dist - 1] : dc[256 + ((dist - 1) >> 7)];
}

static uint32_t zda_stored_bits(uint32_t bitcnt, uint32_t raw)
{
    uint32_t bits = 0;
    do {
        uint32_t n = raw < 65535 ? raw : 65535;
        raw -= n;
        bitcnt += 3;
        bits += 3 + ((8 - (bitcnt & 7)) & 7) + 32 + 8 * n;
        bitcnt = 0;
    } while (raw);
    return bits;
}

static uint32_t zda_cl_at(const struct zda *d, uint32_t k, uint32_t hlit)
{
    return k < hlit ? d->f[ZF_LLEN + k] : d->f[ZF_DLEN + k - hlit];
}

static void zda_cl_emit(struct zda *d, uint32_t *rn, uint32_t sym, uint32_t ext)
{
    d->f[ZF_RLE_SYM + *rn] = (uint8_t)sym;
    d->f[ZF_RLE_EXT + (*rn)++] = (uint8_t)ext;
    s32(d->f + ZF_CLFREQ, sym, g32(d->f + ZF_CLFREQ, sym) + 1);
}

static uint32_t zda_rle(struct zda *d, uint32_t hlit, uint32_t hdist)
{
    uint32_t total = hlit + hdist, i = 0, j, rn = 0;
    for (j = 0; j < 19; j++) s32(d->f + ZF_CLFREQ, j, 0);
    while (i < total) {
        uint32_t v = zda_cl_at(d, i, hlit), run = 1;
        while (i + run < total && zda_cl_at(d, i + run, hlit) == v) run++;
        i += run;
        if (v == 0) {
            while (run >= 11) {
                uint32_t r = run < 138 ? run : 138;
                zda_cl_emit(d, &rn, 18, r - 11);
                run -= r;
            }
            if (run >= 3) {
                zda_cl_emit(d, &rn, 17, run - 3);
                run = 0;
            }
            while (run--) zda_cl_emit(d, &rn, 0, 0);
        } else {
            zda_cl_emit(d, &rn, v, 0);
            run--;
            while (run >= 3) {
                uint32_t r = run < 6 ? run : 6;
                zda_cl_emit(d, &rn, 16, r - 3);
                run -= r;
            }
            while (run--) zda_cl_emit(d, &rn, v, 0);
        }
    }
    return rn;
}

/* the symbols with codes lc (words) and lengths ll (bytes; 0: the fixed
 * code's), distances dc and dl (0: five bits) */
static void zda_symbols(struct zda *d, const uint8_t *lc, const uint8_t *ll, const uint8_t *dc, const uint8_t *dl)
{
    const uint8_t *lcode = d->f + ZF_LEN_CODE;
    uint32_t k;
    for (k = 0; k < d->sym_n; k++) {
        uint32_t dist = g16(d->sym_dist, k), c = d->sym_lc[k], li, s, di;
        if (!dist) {
            zda_bits(d, g16(lc, c), ll ? ll[c] : zda_fixed_len(c));
            continue;
        }
        li = lcode[c];
        s = 257 + li;
        zda_bits(d, g16(lc, s), ll ? ll[s] : zda_fixed_len(s));
        if (zda_len_extra[li]) zda_bits(d, c + 3 - zda_len_base[li], zda_len_extra[li]);
        di = zda_dcode(d, dist);
        zda_bits(d, g16(dc, di), dl ? dl[di] : 5);
        if (zda_dist_extra[di]) zda_bits(d, dist - zda_dist_base[di], zda_dist_extra[di]);
    }
    zda_bits(d, g16(lc, 256), ll ? ll[256] : zda_fixed_len(256));
}

static void zda_block(struct zda *d, int last)
{
    uint8_t *f = d->f, *lf = f + ZF_LFREQ, *df = f + ZF_DFREQ, *cf = f + ZF_CLFREQ;
    uint8_t *llen = f + ZF_LLEN, *dlen = f + ZF_DLEN, *cllen = f + ZF_CLLEN;
    uint32_t end = d->strstart - (d->pending_lit ? 1 : 0);
    uint32_t raw = end - d->block_start, sbits, i, hlit = 0, hdist = 0, hclen = 0, rn = 0;
    const uint8_t *rawp = d->in + d->block_start;
    int type = 0;
    if (!last && raw == 0 && d->sym_n == 0) return;
    if (!d->typed) {
        s32(f, 0, raw);
        d->typed = 1;
    }
    s32(lf, 256, 1);
    sbits = zda_stored_bits(d->bitcnt, raw);
    if (d->kind != ZDA_STORE || raw == 0) {
        uint32_t fbits = 3, best;
        for (i = 0; i < 286; i++) fbits += g32(lf, i) * zda_fixed_len(i);
        for (i = 0; i < 29; i++) fbits += g32(lf, 257 + i) * zda_len_extra[i];
        for (i = 0; i < 30; i++) fbits += g32(df, i) * (5 + zda_dist_extra[i]);
        best = fbits;
        type = 1;
        if (d->strategy != HL_ZDA_FIXED) {
            uint32_t dbits;
            zda_lengths(d, lf, 286, 15, llen);
            llen[286] = llen[287] = 0;
            zda_lengths(d, df, 30, 15, dlen);
            hlit = 286;
            hdist = 30;
            while (hlit > 257 && !llen[hlit - 1]) hlit--;
            while (hdist > 1 && !dlen[hdist - 1]) hdist--;
            rn = zda_rle(d, hlit, hdist);
            zda_lengths(d, cf, 19, 7, cllen);
            hclen = 19;
            while (hclen > 4 && !cllen[zda_cl_order[hclen - 1]]) hclen--;
            dbits = 3 + 14 + 3 * hclen + g32(cf, 16) * 2 + g32(cf, 17) * 3 + g32(cf, 18) * 7;
            for (i = 0; i < 19; i++) dbits += g32(cf, i) * cllen[i];
            for (i = 0; i < 286; i++) dbits += g32(lf, i) * llen[i];
            for (i = 0; i < 29; i++) dbits += g32(lf, 257 + i) * zda_len_extra[i];
            for (i = 0; i < 30; i++) dbits += g32(df, i) * (dlen[i] + zda_dist_extra[i]);
            if (dbits < best) {
                best = dbits;
                type = 2;
                zda_codes(llen, 288, f + ZF_LCODE);
                zda_codes(dlen, 30, f + ZF_DCODE);
                zda_codes(cllen, 19, f + ZF_CLCODE);
            }
        }
        if (sbits < best) type = 0;
    }
    if (type == 0) {
        uint32_t left = raw;
        do {
            uint32_t n = left < 65535 ? left : 65535;
            left -= n;
            zda_bits(d, last && !left ? 1 : 0, 3);
            zda_align(d);
            zda_byte(d, n & 0xff);
            zda_byte(d, n >> 8);
            zda_byte(d, ~n & 0xff);
            zda_byte(d, (~n >> 8) & 0xff);
            if (d->over || d->out_cap - d->out_n < n) {
                d->over = 1;
                return;
            }
            for (i = 0; i < n; i++) d->out[d->out_n + i] = rawp[i];
            d->out_n += n;
            rawp += n;
        } while (left);
    } else if (type == 1) {
        zda_bits(d, last ? 1 : 0, 1);
        zda_bits(d, 1, 2);
        zda_symbols(d, f + ZF_FCODE, 0, f + ZF_FDCODE, 0);
    } else {
        zda_bits(d, last ? 1 : 0, 1);
        zda_bits(d, 2, 2);
        zda_bits(d, hlit - 257, 5);
        zda_bits(d, hdist - 1, 5);
        zda_bits(d, hclen - 4, 4);
        for (i = 0; i < hclen; i++) zda_bits(d, cllen[zda_cl_order[i]], 3);
        for (i = 0; i < rn; i++) {
            uint32_t s = f[ZF_RLE_SYM + i], e = f[ZF_RLE_EXT + i];
            zda_bits(d, g16(f + ZF_CLCODE, s), cllen[s]);
            if (s == 16) zda_bits(d, e, 2);
            else if (s == 17) zda_bits(d, e, 3);
            else if (s == 18) zda_bits(d, e, 7);
        }
        zda_symbols(d, f + ZF_LCODE, llen, f + ZF_DCODE, dlen);
    }
    if (last) zda_align(d);
    for (i = 0; i < 286; i++) s32(lf, i, 0);
    for (i = 0; i < 30; i++) s32(df, i, 0);
    d->sym_n = 0;
    d->block_start = end;
}

/* ---- matches (zlib_core.cpp's insert, longest_match, step) -------------- */

static inline uint32_t zda_insert(struct zda *d, uint32_t p)
{
    const uint8_t *w = d->in + p;
    uint32_t h = ((uint32_t)w[0] << (2 * d->hshift) ^ (uint32_t)w[1] << d->hshift ^ w[2]) & d->hmask;
    uint32_t old = g32(d->head, h);
    s32(d->prev, p & d->wmask, old);
    s32(d->head, h, p);
    return old;
}

static uint32_t zda_longest(struct zda *d, uint32_t cur, uint32_t best, uint32_t prev_len)
{
    uint32_t chain = d->chain, maxlen, nice, limit;
    const uint8_t *scan;
    if (prev_len >= d->good && d->good) chain >>= 2;
    if (!chain) chain = 1;
    maxlen = d->lookahead < ZDA_MAX_MATCH ? d->lookahead : ZDA_MAX_MATCH;
    nice = d->nice ? d->nice : ZDA_MAX_MATCH;
    if (nice > maxlen) nice = maxlen;
    limit = d->strstart > d->maxdist ? d->strstart - d->maxdist : 0;
    if (best >= maxlen) return best;
    scan = d->in + d->strstart;
    while (cur && cur >= limit && cur < d->strstart) {
        const uint8_t *m = d->in + cur;
        uint32_t nx;
        if (m[best] == scan[best] && m[0] == scan[0] && m[1] == scan[1]) {
            uint32_t l = 2;
            while (l < maxlen && m[l] == scan[l]) l++;
            if (l > best) {
                best = l;
                d->match_start = cur;
                if (l >= nice) break;
            }
        }
        if (--chain == 0) break;
        nx = g32(d->prev, cur & d->wmask);
        if (nx >= cur) break;
        cur = nx;
    }
    return best;
}

static inline void zda_lit(struct zda *d, uint32_t c)
{
    d->sym_lc[d->sym_n] = (uint8_t)c;
    s16(d->sym_dist, d->sym_n++, 0);
    s32(d->f + ZF_LFREQ, c, g32(d->f + ZF_LFREQ, c) + 1);
}

static inline void zda_match(struct zda *d, uint32_t dist, uint32_t len)
{
    uint32_t lc = 257 + d->f[ZF_LEN_CODE + len - ZDA_MIN_MATCH], dc = zda_dcode(d, dist);
    d->sym_lc[d->sym_n] = (uint8_t)(len - ZDA_MIN_MATCH);
    s16(d->sym_dist, d->sym_n++, dist);
    s32(d->f + ZF_LFREQ, lc, g32(d->f + ZF_LFREQ, lc) + 1);
    s32(d->f + ZF_DFREQ, dc, g32(d->f + ZF_DFREQ, dc) + 1);
}

static void zda_insert_run(struct zda *d, uint32_t from, uint32_t to, uint32_t end)
{
    uint32_t p;
    for (p = from; p < to && p + ZDA_MIN_MATCH <= end; p++) zda_insert(d, p);
}

static uint32_t zda_find(struct zda *d, uint32_t prev_len, uint32_t *dist)
{
    uint32_t hh, len;
    if (d->lookahead < ZDA_MIN_MATCH) return 0;
    hh = zda_insert(d, d->strstart);
    if (!hh || d->strstart - hh > d->maxdist) return 0;
    len = zda_longest(d, hh, ZDA_MIN_MATCH - 1, prev_len);
    if (len < ZDA_MIN_MATCH) return 0;
    *dist = d->strstart - d->match_start;
    if (len <= 5 && (d->strategy == HL_ZDA_FILTERED || (len == ZDA_MIN_MATCH && *dist > ZDA_TOO_FAR))) return 0;
    return len;
}

static void zda_step(struct zda *d)
{
    uint32_t s = d->strstart, end = s + d->lookahead, dist = 0, len = 0;
    if (d->kind == ZDA_STORE) {
        d->strstart = end;
        d->lookahead = 0;
        return;
    }
    if (d->strategy == HL_ZDA_HUFFMAN_ONLY) {
        zda_lit(d, d->in[s]);
        d->strstart++;
        d->lookahead--;
        return;
    }
    if (d->strategy == HL_ZDA_RLE) {
        if (s > 0 && d->lookahead >= ZDA_MIN_MATCH) {
            uint32_t maxlen = d->lookahead < ZDA_MAX_MATCH ? d->lookahead : ZDA_MAX_MATCH;
            uint8_t c = d->in[s - 1];
            while (len < maxlen && d->in[s + len] == c) len++;
        }
        if (len >= ZDA_MIN_MATCH) {
            zda_match(d, 1, len);
            d->strstart += len;
            d->lookahead -= len;
        } else {
            zda_lit(d, d->in[s]);
            d->strstart++;
            d->lookahead--;
        }
        return;
    }
    if (d->kind == ZDA_GREEDY) {
        len = zda_find(d, 0, &dist);
        if (len) {
            zda_match(d, dist, len);
            if (len <= d->max_insert) zda_insert_run(d, s + 1, s + len, end);
            d->strstart += len;
            d->lookahead -= len;
        } else {
            zda_lit(d, d->in[s]);
            d->strstart++;
            d->lookahead--;
        }
        return;
    }
    if (!d->pending_lit || d->plen < d->lazy) len = zda_find(d, d->pending_lit ? d->plen : 0, &dist);
    else if (d->lookahead >= ZDA_MIN_MATCH) zda_insert(d, s);
    if (d->pending_lit && d->plen >= ZDA_MIN_MATCH && len <= d->plen) {
        zda_match(d, d->pdist, d->plen);
        zda_insert_run(d, s + 1, s - 1 + d->plen, end);
        d->strstart = s - 1 + d->plen;
        d->lookahead -= d->plen - 1;
        d->pending_lit = 0;
        return;
    }
    if (d->pending_lit) zda_lit(d, d->in[s - 1]);
    d->pending_lit = 1;
    d->plen = len;
    d->pdist = dist;
    d->strstart++;
    d->lookahead--;
}

/* ---- the call ------------------------------------------------------------ */

HL_TAG(hostlib_zlib_deflate_all, HL_ID_ZLIB_DEFLATE_ALL)
HL_FN(hostlib_zlib_deflate_all)
{
    struct zda d;
    hl_addr ia, oa, wa;
    uint32_t wlen, wbits, memlevel, wlog, hbits, need, i, c, v;
    uint8_t *work, *f;
    HL_NEED_R(block, HL_ZDA_BLOCK);
    ia = HL_ARG(HL_ZDA_IN);
    d.in_len = HL_ARG(HL_ZDA_IN_LEN);
    oa = HL_ARG(HL_ZDA_OUT);
    d.out_cap = HL_ARG(HL_ZDA_OUT_CAP);
    wa = HL_ARG(HL_ZDA_WORK);
    wlen = HL_ARG(HL_ZDA_WORK_LEN);
    d.level = HL_ARG(HL_ZDA_LEVEL);
    d.strategy = HL_ARG(HL_ZDA_STRATEGY);
    wbits = HL_ARG(HL_ZDA_WBITS);
    memlevel = HL_ARG(HL_ZDA_MEMLEVEL);
    /* what z.library never asks: an answer of 0 in both builds */
    if (d.level > 9 || d.strategy > HL_ZDA_FIXED || wbits < 8 || wbits > 15 || memlevel < 1 || memlevel > 9 ||
        d.in_len > (1u << 30) || !d.out_cap)
        return 0;
    need = hl_zda_work(d.in_len, wbits, memlevel);
    if (wlen < need) return 0;
    if (d.in_len) HL_NEED_R(ia, d.in_len);
    HL_NEED_W(oa, d.out_cap);
    HL_NEED_W(wa, need);
    HL_DECLARED();
    if (d.in_len) HL_PTR_R(d.in, ia, d.in_len); else d.in = 0;
    HL_PTR_W(d.out, oa, d.out_cap);
    HL_PTR_W(work, wa, need);
    /* the working memory, all of it, cleared first (rule 5) */
    for (i = 0; i < need; i += 4) s32(work, i >> 2, 0);   /* need is a multiple of 4 */
    wlog = hl_zda_wlog(d.in_len, wbits);
    hbits = hl_zda_hbits(wlog, memlevel);
    f = work;
    d.f = f;
    d.head = work + HL_ZDA_FIXED_BYTES;
    d.prev = d.head + 4u * (1u << hbits);
    d.sym_cap = hl_zda_cap(d.in_len, memlevel);
    d.sym_dist = d.prev + 4u * (1u << wlog);
    d.sym_lc = d.sym_dist + 2u * d.sym_cap;
    d.wmask = (1u << wlog) - 1;
    d.maxdist = (1u << (wbits < 9 ? 9 : wbits)) - ZDA_MIN_LOOKAHEAD;
    d.hmask = (1u << hbits) - 1;
    d.hshift = (hbits + ZDA_MIN_MATCH - 1) / ZDA_MIN_MATCH;
    d.good = zda_levels[d.level][0];
    d.lazy = zda_levels[d.level][1];
    d.nice = zda_levels[d.level][2];
    d.chain = zda_levels[d.level][3];
    d.kind = zda_levels[d.level][4];
    d.max_insert = zda_levels[d.level][5];
    d.out_n = d.over = d.bitbuf = d.bitcnt = d.sym_n = 0;
    d.strstart = d.block_start = d.match_start = d.typed = 0;
    d.pending_lit = d.plen = d.pdist = 0;
    d.lookahead = d.in_len;
    for (c = 0; c < 29; c++)
        for (v = zda_len_base[c]; v < zda_len_base[c] + (1u << zda_len_extra[c]) && v <= ZDA_MAX_MATCH; v++)
            f[ZF_LEN_CODE + v - ZDA_MIN_MATCH] = (uint8_t)c;
    f[ZF_LEN_CODE + ZDA_MAX_MATCH - ZDA_MIN_MATCH] = 28;
    for (c = 0; c < 30; c++)
        for (v = zda_dist_base[c]; v < zda_dist_base[c] + (1u << zda_dist_extra[c]); v++) {
            if (v <= 256) f[ZF_DIST_CODE + v - 1] = (uint8_t)c;
            else f[ZF_DIST_CODE + 256 + ((v - 1) >> 7)] = (uint8_t)c;
        }
    for (i = 0; i < 288; i++) f[ZF_LLEN + i] = (uint8_t)zda_fixed_len(i);
    zda_codes(f + ZF_LLEN, 288, f + ZF_FCODE);
    for (i = 0; i < 288; i++) f[ZF_LLEN + i] = 0;
    for (i = 0; i < 30; i++) s16(f + ZF_FDCODE, i, zda_reverse(i, 5));
    while (d.lookahead && !d.over) {
        zda_step(&d);
        if (d.sym_n >= d.sym_cap) zda_block(&d, 0);
    }
    if (d.pending_lit) {
        zda_lit(&d, d.in[d.strstart - 1]);
        d.pending_lit = 0;
        if (d.sym_n >= d.sym_cap) zda_block(&d, 0);
    }
    if (!d.over) zda_block(&d, 1);
    return d.over ? 0 : d.out_n;
}
