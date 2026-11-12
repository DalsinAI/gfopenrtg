/* SPDX-License-Identifier: MIT */
/* GFOpenZlib's inflate, the codes of a block (inflate_codes.h). Written from
 * RFC 1951 as zlib_core.cpp's M_LEN to M_MATCH are, clean-room: the same
 * steps, in C, on the 68k's layout, so the 68k body and the host build
 * leave the same registers, window and output. Copyright (c) 2026 AmiCradle
 * Team. MIT. */
#include "inflate_codes.h"
#include "../hl_ids.h"

#if defined(HL_HOST) && defined(HOSTLIB_TEST_STOP)
/* The host test sets this to decline after that many output bytes, or to
 * all-ones to decline before the first. 0 leaves the call alone. */
extern uint32_t hostlib_zic_stop;
#endif

static const uint16_t zic_len_base[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                                           35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const uint8_t zic_len_extra[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
                                           3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const uint16_t zic_dist_base[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
                                            257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
                                            8193, 12289, 16385, 24577 };
static const uint8_t zic_dist_extra[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
                                            7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

/* The code at the bottom of hold, not taken: 1 (sym and len), 0 (more bits
 * needed), -1 (no such code). zlib_core.cpp's huff_peek. */
static int zic_peek(const struct hl_zic_huff *h, uint32_t hold, uint32_t bits, uint32_t *sym, uint32_t *len)
{
    int32_t code = 0, first = 0, index = 0;
    uint32_t l;
    if (bits >= HL_ZIC_FAST) {
        uint32_t e = h->fast[hold & ((1u << HL_ZIC_FAST) - 1)];
        if (e) {
            *sym = e & 511;
            *len = e >> 9;
            return 1;
        }
    }
    for (l = 1; l < 16; l++) {
        int32_t c;
        if (l > bits) return 0;
        code |= (int32_t)((hold >> (l - 1)) & 1);
        c = h->count[l];
        if (code - first < c) {
            /* a table the library built never reaches past sym[287]; one
             * that is not the library's stops here in both builds */
            if (index + code - first >= 288) return -1;
            *sym = h->sym[index + code - first];
            *len = l;
            return 1;
        }
        index += c;
        first = (first + c) << 1;
        code <<= 1;
    }
    return -1;
}

/* the registers, as locals while the call runs */
struct zic {
    uint32_t mode, hold, bits, back, emitted, length, dist, dsym, lit, last, wnext, whave, total;
    const uint8_t *in;
    uint32_t avail_in, in0;
    uint8_t *out;
    uint32_t avail_out, out0;
    uint8_t *win;
    uint32_t wmask, wsize;
};

static inline void zic_drop(struct zic *z, uint32_t n)
{
    z->hold = n >= 32 ? 0 : z->hold >> n;
    z->bits -= n;
    z->back += n;
}

static inline int zic_need(struct zic *z, uint32_t n)
{
    while (z->bits < n) {
        if (!z->avail_in) return 0;
        z->avail_in--;
        z->hold |= (uint32_t)*z->in++ << z->bits;
        z->bits += 8;
    }
    return 1;
}

static int zic_decode(struct zic *z, const struct hl_zic_huff *h, uint32_t *sym, uint32_t *len)
{
    for (;;) {
        int r = zic_peek(h, z->hold, z->bits, sym, len);
        if (r) return r;
        if (!z->avail_in) return 0;
        z->avail_in--;
        z->hold |= (uint32_t)*z->in++ << z->bits;
        z->bits += 8;
    }
}

static inline __attribute__((always_inline)) void zic_put(struct zic *z, uint8_t b)
{
    *z->out++ = b;
    z->avail_out--;
    z->win[z->wnext] = b;
    z->wnext = (z->wnext + 1) & z->wmask;
    if (z->whave < z->wsize) z->whave++;
    z->total++;
}

/* n bytes of a match (n at most the output left): zic_put n times, with
 * the window's place in a register */
static inline __attribute__((always_inline)) void zic_copy(struct zic *z, uint32_t n)
{
    uint8_t *o = z->out, *w = z->win;
    const uint32_t m = z->wmask, d = z->dist;
    uint32_t wn = z->wnext, i;
    for (i = 0; i < n; i++) {
        const uint8_t b = w[(wn - d) & m];
        *o++ = b;
        w[wn] = b;
        wn = (wn + 1) & m;
    }
    z->out = o;
    z->avail_out -= n;
    z->wnext = wn;
    z->whave = z->wsize - z->whave > n ? z->whave + n : z->wsize;
    z->total += n;
}

/* The steps; the answer (HL_ZIC_MORE ...). */
static uint32_t zic_run(struct zic *z, const struct hl_zic_huff *lc, const struct hl_zic_huff *dc)
{
    uint32_t sym, len;
    for (;;) {
        switch (z->mode) {
        case HL_ZIC_LEN: {
            uint32_t li, xb;
            int r;
            z->back = 0;
            z->emitted = 0;
            r = zic_decode(z, lc, &sym, &len);
            if (r < 0) return HL_ZIC_BAD_LIT;
            if (r == 0) return HL_ZIC_MORE;
            if (sym < 256) {
                zic_drop(z, len);
                if (z->avail_out) {
                    zic_put(z, (uint8_t)sym);
                    break;
                }
                z->lit = sym;
                z->mode = HL_ZIC_LIT;
                return HL_ZIC_MORE;
            }
            if (sym == 256) {
                zic_drop(z, len);
                return HL_ZIC_END;
            }
            if (sym > 285) return HL_ZIC_BAD_LIT;
            li = sym - 257;
            xb = zic_len_extra[li];
            if (!zic_need(z, len + xb)) return HL_ZIC_MORE;
            zic_drop(z, len);
            z->length = zic_len_base[li] + (z->hold & ((1u << xb) - 1));
            zic_drop(z, xb);
            z->mode = HL_ZIC_DIST;
        }
            /* fallthrough */
        case HL_ZIC_DIST: {
            int r = zic_decode(z, dc, &sym, &len);
            if (r < 0) return HL_ZIC_BAD_DIST;
            if (r == 0) return HL_ZIC_MORE;
            if (sym >= 30) return HL_ZIC_BAD_DIST;
            zic_drop(z, len);
            z->dsym = sym;
            z->mode = HL_ZIC_DISTEXT;
        }
            /* fallthrough */
        case HL_ZIC_DISTEXT: {
            uint32_t xb = zic_dist_extra[z->dsym < 30 ? z->dsym : 0];
            if (!zic_need(z, xb)) return HL_ZIC_MORE;
            z->dist = zic_dist_base[z->dsym < 30 ? z->dsym : 0] + (z->hold & ((1u << xb) - 1));
            zic_drop(z, xb);
            /* the window bounds a distance, even when this call's own output
             * would reach further back (zlib_core.cpp's ABI test) */
            if (z->dist > z->whave) return HL_ZIC_FAR;
            z->mode = HL_ZIC_MATCH;
        }
            /* fallthrough */
        case HL_ZIC_MATCH:
            while (z->length) {
                uint32_t n = z->length < z->avail_out ? z->length : z->avail_out;
                if (!n) return HL_ZIC_MORE;
#if defined(HL_HOST) && defined(HOSTLIB_TEST_STOP)
                if (hostlib_zic_stop && n > hostlib_zic_stop - (z->out0 - z->avail_out))
                    n = hostlib_zic_stop - (z->out0 - z->avail_out);
#endif
                zic_copy(z, n);
                z->length -= n;
                z->emitted += n;
#if defined(HL_HOST) && defined(HOSTLIB_TEST_STOP)
                if (hostlib_zic_stop && z->out0 - z->avail_out >= hostlib_zic_stop) return 0;
#endif
            }
            z->mode = HL_ZIC_LEN;
            break;
        case HL_ZIC_LIT:
            if (!z->avail_out) return HL_ZIC_MORE;
            zic_put(z, (uint8_t)z->lit);
            z->mode = HL_ZIC_LEN;
            break;
        default:
            return HL_ZIC_BAD_LIT;          /* not a mode of the codes: the library never asks */
        }
#if defined(HL_HOST) && defined(HOSTLIB_TEST_STOP)
        if (hostlib_zic_stop && z->out0 - z->avail_out >= hostlib_zic_stop) return 0;
#endif
    }
}

HL_TAG_LOCKED(hostlib_zlib_inflate_codes, HL_ID_ZLIB_INFLATE_CODES)
HL_FN(hostlib_zlib_inflate_codes)
{
    struct hl_zic_state *st;
    const struct hl_zic_huff *lc, *dc;
    hl_addr sa, wa, la, da, ia, oa;
    uint32_t wsize, ain, aout, r;
    struct zic z;
    HL_NEED_R(block, HL_ZIC_BLOCK);
    sa = HL_ARG(HL_ZIC_STATE);
    wa = HL_ARG(HL_ZIC_WINDOW);
    wsize = HL_ARG(HL_ZIC_WSIZE);
    la = HL_ARG(HL_ZIC_LCODE);
    da = HL_ARG(HL_ZIC_DCODE);
    ia = HL_ARG(HL_ZIC_IN);
    ain = HL_ARG(HL_ZIC_AVAIL_IN);
    oa = HL_ARG(HL_ZIC_OUT);
    aout = HL_ARG(HL_ZIC_AVAIL_OUT);
    /* a window of 256 bytes to 32K, a power of two (the library's) */
    if (wsize < 256 || wsize > 32768 || (wsize & (wsize - 1))) return HL_ZIC_BAD_LIT;
    HL_NEED_S(sa, sizeof(struct hl_zic_state));
    HL_NEED_S(wa, wsize);
    HL_NEED_R(la, sizeof(struct hl_zic_huff));
    HL_NEED_R(da, sizeof(struct hl_zic_huff));
    HL_NEED_R(ia, ain);
    HL_NEED_W(oa, aout);
    HL_DECLARED();
#if defined(HL_HOST) && defined(HOSTLIB_TEST_STOP)
    if (hostlib_zic_stop == 0xffffffffu) HL_DECLINE();
#endif
    HL_STRUCT_W(st, sa);
    HL_STRUCT_R(lc, la);
    HL_STRUCT_R(dc, da);
    HL_PTR_W(z.win, wa, wsize);
    if (ain) HL_PTR_R(z.in, ia, ain); else z.in = 0;
    if (aout) HL_PTR_W(z.out, oa, aout); else z.out = 0;
    z.mode = st->mode; z.hold = st->hold; z.bits = st->bits; z.back = st->back; z.emitted = st->emitted;
    z.length = st->length; z.dist = st->dist; z.dsym = st->dsym; z.lit = st->lit; z.last = st->last;
    z.wnext = st->wnext & (wsize - 1); z.whave = st->whave; z.total = st->total;
    z.avail_in = z.in0 = ain;
    z.avail_out = z.out0 = aout;
    z.wsize = wsize;
    z.wmask = wsize - 1;
    r = zic_run(&z, lc, dc);
#if defined(HL_HOST) && defined(HOSTLIB_TEST_STOP)
    if (!r) HL_DECLINE();
#endif
    st->mode = z.mode; st->hold = z.hold; st->bits = z.bits; st->back = z.back; st->emitted = z.emitted;
    st->length = z.length; st->dist = z.dist; st->dsym = z.dsym; st->lit = z.lit; st->last = z.last;
    st->wnext = z.wnext; st->whave = z.whave; st->total = z.total;
    st->in_used = z.in0 - z.avail_in;
    st->out_used = z.out0 - z.avail_out;
    return r;
}
