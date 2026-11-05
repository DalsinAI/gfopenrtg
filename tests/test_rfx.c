/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * Host test for stream v1.10's RemoteFX Progressive ops (RLGR_DEC,
 * RFX_TILE, RFX_YCBCR) and DWT_INV's v1.10 byte order: every result must be
 * bit-for-bit what GFAmigaRDP's CPU path gives. The reference below is
 * GFAmigaRDP's src/codec/progressive.c (DalsinAI/gfamigardp d8ee7cd; same
 * authors, MIT, clean-room from [MS-RDPRFX]) copied as it is, with its
 * render() dequantize and YCbCr loops. Inputs are random (fixed seeds):
 * RLGR streams of random bytes, coefficient tiles, quant factors 6..15
 * (and 0..5 for the right shift), and both DWT layouts. Buffers in the
 * arena are big-endian, as on the Amiga, whatever this host is. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../library/opengpu/ogpu_core.h"
#include "../include/gfopengpu/build.h"

#define ARENA (1L << 20)
static ogpu_u8 arena[ARENA];

static ogpu_u8 *map(void *user, ogpu_u32 address, ogpu_u32 length) {
    (void)user;
    if (address >= ARENA || length > (ogpu_u32)ARENA - address) return 0;
    return arena + address;
}

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* ======== reference: GFAmigaRDP progressive.c (verbatim but for names) ======== */
/* Bands in linearization order ([MS-RDPRFX] 3.1.8.1.6). */
enum { B_HL1, B_LH1, B_HH1, B_HL2, B_LH2, B_HH2, B_HL3, B_LH3, B_HH3, B_LL3, NBANDS };
/* Level sizes: low and high counts per level (1..3). Reduce-Extrapolate
 * (3.2.8.1.2.2): 33/31, 17/16, 9/8; the original method: 32/32, 16/16, 8/8. */
static void level_sizes(int re, int level, int* nl, int* nh)
{
	static const uint8_t lo_re[3] = { 33, 17, 9 }, hi_re[3] = { 31, 16, 8 };
	static const uint8_t lo[3] = { 32, 16, 8 }, hi[3] = { 32, 16, 8 };
	*nl = re ? lo_re[level - 1] : lo[level - 1];
	*nh = re ? hi_re[level - 1] : hi[level - 1];
}

/* HLk is high horizontally and low vertically (the top-right of [MS-RDPRFX]
 * figure 7): width nh, height nl. LHk: width nl, height nh. */
/* Precomputed once: band offset/w/h for both DWT layouts. Hot tiles call
 * this dozens of times; a table keeps the 68040 off the size loops. */
static struct { int16_t off, w, h; } s_band[2][NBANDS];
static uint8_t s_band_ready;

static void band_init(void)
{
	int re, band;
	if (s_band_ready)
		return;
	for (re = 0; re < 2; re++)
	{
		int o = 0;
		for (band = 0; band < NBANDS; band++)
		{
			int level = band == B_LL3 ? 3 : band / 3 + 1, nl, nh, bw, bh;
			level_sizes(re, level, &nl, &nh);
			switch (band == B_LL3 ? 3 : band % 3)
			{
			case 0: bw = nh; bh = nl; break;
			case 1: bw = nl; bh = nh; break;
			case 2: bw = nh; bh = nh; break;
			default: bw = nl; bh = nl; break;
			}
			s_band[re][band].off = (int16_t)o;
			s_band[re][band].w = (int16_t)bw;
			s_band[re][band].h = (int16_t)bh;
			o += bw * bh;
		}
	}
	s_band_ready = 1;
}

void ref_band_layout(int re, int band, int* off, int* w, int* h)
{
	band_init();
	re = re ? 1 : 0;
	if (band < 0 || band >= NBANDS)
	{
		*off = *w = *h = 0;
		return;
	}
	*off = s_band[re][band].off;
	*w = s_band[re][band].w;
	*h = s_band[re][band].h;
}
/* ---- bit reading, most significant bit first ([MS-RDPRFX] 3.1.8.1.7.3.1,
 * [MS-RDPEGFX] 3.2.8.1.5.2.1). A 32-bit accumulator avoids a shift per bit
 * on the 68040 hot path (RLGR / SRL). ---- */
struct bits
{
	const uint8_t* p;
	const uint8_t* end;
	uint32_t buf;     /* next bits in the high end */
	int nbuf;         /* how many bits are valid in buf */
	size_t pos;       /* bits consumed (for bits_over) */
	size_t lim;       /* len * 8 */
};

static void bits_init(struct bits* b, const uint8_t* p, size_t len)
{
	b->p = p;
	b->end = p + len;
	b->buf = 0;
	b->nbuf = 0;
	b->pos = 0;
	b->lim = len * 8;
}

static void bits_fill(struct bits* b)
{
	while (b->nbuf <= 24 && b->p < b->end)
	{
		b->buf |= (uint32_t)(*b->p++) << (24 - b->nbuf);
		b->nbuf += 8;
	}
}

static uint32_t get_bits(struct bits* b, int n)
{
	uint32_t v;
	if (n <= 0)
		return 0;
	if (b->nbuf < n)
		bits_fill(b);
	if (b->nbuf < n)
	{
		/* past the end: zeros, same as reading off the old bit stream */
		v = b->nbuf ? (b->buf >> (32 - b->nbuf)) << (n - b->nbuf) : 0;
		b->pos += (size_t)n;
		b->buf = 0;
		b->nbuf = 0;
		return v;
	}
	v = b->buf >> (32 - n);
	b->buf <<= n;
	b->nbuf -= n;
	b->pos += (size_t)n;
	return v;
}

static int bits_over(const struct bits* b)
{
	return b->pos > b->lim + 64;   /* far past the end: garbage */
}

/* ---- RLGR1 ([MS-RDPRFX] 3.1.8.1.7.3.1) ---- */
#define KPMAX 80
#define LSGR 3
#define UP_GR 4
#define DN_GR 6
#define UQ_GR 3
#define DQ_GR 3

static int update_param(int* param, int delta)
{
	*param += delta;
	if (*param > KPMAX) *param = KPMAX;
	if (*param < 0) *param = 0;
	return *param >> LSGR;
}

static uint32_t gr_code(struct bits* b, int* krp, int* kr)
{
	uint32_t vk = 0, mag;
	while (get_bits(b, 1) == 1)
	{
		vk++;
		if (bits_over(b))
			return 0;
	}
	mag = (vk << *kr) | get_bits(b, *kr);
	if (!vk)
		*kr = update_param(krp, -2);
	else if (vk != 1)
		*kr = update_param(krp, (int)vk);
	return mag;
}

int ref_rlgr1_decode(const uint8_t* p, size_t len, int16_t* out, int n)
{
	struct bits b;
	int k = 1, kp = 1 << LSGR, kr = 1, krp = 1 << LSGR, o = 0;
	bits_init(&b, p, len);
	while (o < n)
	{
		if (bits_over(&b))
			return (-1);
		if (k)
		{
			uint32_t run, sign;
			int mag;
			while (get_bits(&b, 1) == 0)
			{
				int z = 1 << k;
				if (z > n - o) z = n - o;
				if (z > 0)
				{
					memset(out + o, 0, (size_t)z * sizeof(int16_t));
					o += z;
				}
				k = update_param(&kp, UP_GR);
				if (o >= n || bits_over(&b))
					break;
			}
			if (o >= n)
				break;
			run = get_bits(&b, k);
			if (run > (uint32_t)(n - o))
				run = (uint32_t)(n - o);
			if (run)
			{
				memset(out + o, 0, (size_t)run * sizeof(int16_t));
				o += (int)run;
			}
			if (o >= n)
				break;
			sign = get_bits(&b, 1);
			mag = (int)gr_code(&b, &krp, &kr) + 1;
			out[o++] = (int16_t)(sign ? -mag : mag);
			k = update_param(&kp, -DN_GR);
		}
		else
		{
			uint32_t mag = gr_code(&b, &krp, &kr);   /* 2 * magnitude - sign */
			if (!mag)
			{
				out[o++] = 0;
				k = update_param(&kp, UQ_GR);
			}
			else
			{
				out[o++] = (int16_t)((mag & 1) ? -(int)((mag + 1) >> 1) : (int)(mag >> 1));
				k = update_param(&kp, -DQ_GR);
			}
		}
	}
	return 0;
}
/* ---- inverse DWT ([MS-RDPRFX] 3.1.8.2.4, [MS-RDPEGFX] 3.3.8.2.2) ---- */

/* nl low, nh high (stride ls/hs between elements); the high band is
 * padded with zeros to nhe = nl - 1 or nl; nout of the nl + nhe outputs. */
static void idwt1(const int32_t* L, int ls, const int32_t* H, int hs, int nl, int nh, int nhe,
                  int32_t* X, int xs, int nout, int32_t* t)
{
	int n, total = nl + nhe;
	/* Even: L − ((H[n-1] + H[n] + 1) >> 1), H padded with 0 past nh. */
	for (n = 0; n < nl; n++)
	{
		int i0 = n > 0 ? n - 1 : 0, i1 = n < nhe ? n : (nhe > 0 ? nhe - 1 : 0);
		int32_t hp = i0 < nh ? H[i0 * hs] : 0;
		int32_t hn = i1 < nh ? H[i1 * hs] : 0;
		t[2 * n] = L[n * ls] - ((hp + hn + 1) >> 1);
	}
	for (n = 0; n < nhe; n++)
	{
		int32_t a = t[2 * n], b = 2 * n + 2 < total ? t[2 * n + 2] : t[2 * n];
		int32_t hn = n < nh ? H[n * hs] : 0;
		t[2 * n + 1] = 2 * hn + ((a + b) >> 1);
	}
	/* Contiguous output (horizontal pass): one memcpy beats n stores. */
	if (xs == 1)
		memcpy(X, t, (size_t)nout * sizeof(int32_t));
	else
		for (n = 0; n < nout; n++)
			X[n * xs] = t[n];
}

/* One 2D level: LL (a x a, stride las), HL (b wide, a high), LH (a wide,
 * b high), HH (b x b), all packed; out n x n (stride n), n = a + b, or 64
 * for the Reduce-Extrapolate first level. */
static void idwt2(const int32_t* LL, int las, const int32_t* HL, const int32_t* LH, const int32_t* HH,
                  int a, int b, int be, int n, int32_t* out, int32_t* lp, int32_t* hp, int32_t* t)
{
	int y, x;
	/* horizontal: rows of LL with HL, rows of LH with HH */
	for (y = 0; y < a; y++)
		idwt1(LL + y * las, 1, HL + y * b, 1, a, b, be, lp + y * n, 1, n, t);
	for (y = 0; y < b; y++)
		idwt1(LH + y * a, 1, HH + y * b, 1, a, b, be, hp + y * n, 1, n, t);
	/* vertical */
	for (x = 0; x < n; x++)
		idwt1(lp + x, n, hp + x, n, a, b, be, out + x, n, n, t);
}

/* buf: 4096 dequantized coefficients in band order; out: the 64 x 64
 * component written back into buf. tmp: RDP_PROG_TMP ints. */
#define RDP_PROG_TMP (33 * 33 + 2 * 33 * 64 + 2 * 66 + 64 * 64)
void ref_idwt(int32_t* buf, int re, int32_t* tmp)
{
	/* tmp layout: ll (33*33) | lp (33*64) | hp (33*64) | t (2*66) | mid (64*64).
	 * mid holds the level-3/2 intermediate; the final 64x64 lands in buf. */
	int32_t *ll = tmp, *lp = ll + 33 * 33, *hp = lp + 33 * 64, *t = hp + 33 * 64, *mid = t + 2 * 66;
	int off[NBANDS], i, l3, h3, l2, h2, l1, h1, n2, n1, rr;
	band_init();
	rr = re ? 1 : 0;
	for (i = 0; i < NBANDS; i++)
		off[i] = s_band[rr][i].off;
	level_sizes(re, 3, &l3, &h3);
	level_sizes(re, 2, &l2, &h2);
	level_sizes(re, 1, &l1, &h1);
	n2 = l3 + h3;   /* 17 or 16 = l2 */
	n1 = l2 + h2;   /* 33 or 32 = l1 */
	/* level 3 -> LL2 in mid */
	idwt2(buf + off[B_LL3], l3, buf + off[B_HL3], buf + off[B_LH3], buf + off[B_HH3], l3, h3,
	      re ? l3 - 1 : l3, n2, mid, lp, hp, t);
	/* level 2 -> LL1 in ll */
	idwt2(mid, n2, buf + off[B_HL2], buf + off[B_LH2], buf + off[B_HH2], l2, h2,
	      re ? l2 - 1 : l2, n1, ll, lp, hp, t);
	/* level 1 -> 64 x 64 written straight into buf. Horizontal has already
	 * copied HL1/LH1/HH1 into lp/hp, so overwriting the band layout is safe
	 * and saves a 16 KiB memcpy per component (48 KiB per tile). */
	idwt2(ll, n1, buf + off[B_HL1], buf + off[B_LH1], buf + off[B_HH1], l1, h1,
	      re ? l1 - 1 : l1, 64, buf, lp, hp, t);
}

/* render()'s dequantize (progressive.c) */
static void ref_dequant(const int16_t* co, const uint8_t* q10, int re, int32_t* w)
{
	int b, i;
	band_init();
	for (b = 0; b < NBANDS; b++)
	{
		int sh = (int)q10[b] - 6;
		int off = s_band[re][b].off, n = (int)s_band[re][b].w * s_band[re][b].h;
		if (sh >= 0)
		{
			int32_t m = 1 << sh;
			for (i = off; i < off + n; i++)
				w[i] = (int32_t)co[i] * m;
		}
		else
		{
			int rs = -sh;
			for (i = off; i < off + n; i++)
				w[i] = (int32_t)co[i] >> rs;
		}
	}
}

/* render()'s YCbCr loop for one pixel (progressive.c, [MS-RDPRFX] 3.1.8.2.5).
 * The products are written as 32-bit wrapping (unsigned) arithmetic, as
 * GFAmigaRDP's loop now is: the same bits a 68040 gives for int32, without
 * C's signed-overflow UB for the random far-out-of-range tiles here. */
static uint32_t ref_px(int32_t Y, int32_t Cb, int32_t Cr)
{
	int32_t yy = Y + 128;
	uint32_t cb = (uint32_t)Cb, cr = (uint32_t)Cr;
	int32_t R = yy + ((int32_t)(cr * 91916u + 32768u) >> 16);
	int32_t G = yy - ((int32_t)(cb * 22527u + cr * 46819u + 32768u) >> 16);
	int32_t B = yy + ((int32_t)(cb * 115993u + 32768u) >> 16);
	uint32_t r = (uint32_t)R, g = (uint32_t)G, b = (uint32_t)B;
	if (r > 255u) r = (uint32_t)(R < 0 ? 0 : 255);
	if (g > 255u) g = (uint32_t)(G < 0 ? 0 : 255);
	if (b > 255u) b = (uint32_t)(B < 0 ? 0 : 255);
	return 0xFF000000u | (r << 16) | (g << 8) | b;
}

/* ======== the test ======== */

static uint32_t rng = 1;
static uint32_t rnd(void) { rng = rng * 1103515245u + 12345u; return rng >> 8; }

static void put16(ogpu_u32 at, int v) { arena[at] = (ogpu_u8)((unsigned)v >> 8); arena[at + 1] = (ogpu_u8)v; }
static int get16(ogpu_u32 at) { return (int16_t)(uint16_t)((arena[at] << 8) | arena[at + 1]); }
static void put32(ogpu_u32 at, uint32_t v) { arena[at] = (ogpu_u8)(v >> 24); arena[at + 1] = (ogpu_u8)(v >> 16); arena[at + 2] = (ogpu_u8)(v >> 8); arena[at + 3] = (ogpu_u8)v; }
static uint32_t get32(ogpu_u32 at) { return ((uint32_t)arena[at] << 24) | ((uint32_t)arena[at + 1] << 16) | ((uint32_t)arena[at + 2] << 8) | arena[at + 3]; }

#define SRC_A   0x01000UL   /* RLGR bytes (up to 3 x 8 KB) */
#define COEF_A  0x08000UL   /* 3 x 4096 int16 */
#define QUANT_A 0x0E000UL   /* 30 bytes */
#define YCC_A   0x10000UL   /* 3 x 4096 int32 */
#define WORK_A  0x20000UL
#define PX_A    0x40000UL   /* 80 x 70 ARGB32 surface */
#define PX_W    80

static int run(unsigned char *sbuf, long words) {
    struct ogpu_core C;
    ogpu_core_init(&C);
    C.map = map;
    ogpu_core_run(&C, sbuf, words);
    return C.last_error;
}

static void test_rlgr(int rounds) {
    static int16_t ref[4096];
    static uint8_t bytes[6000];
    unsigned char sbuf[64];
    struct OGPUBatch B;
    int r, i, same = 0, proto = 0;
    for (r = 0; r < rounds; r++) {
        int len = 1 + (int)(rnd() % 5000), flags = (int)(rnd() % 4), rc_ref, err, bad = 0;
        int dens = (int)(rnd() % 4);
        for (i = 0; i < len; i++) {
            uint32_t v = rnd() & 255;
            if (dens == 1) v &= rnd() & 255;          /* sparser: more zero runs */
            if (dens == 2) v |= rnd() & 255;          /* denser: long magnitudes */
            bytes[i] = (uint8_t)v;
        }
        rc_ref = ref_rlgr1_decode(bytes, (size_t)len, ref, 4096);
        if (rc_ref == 0 && (flags & 2)) {
            int off, bw, bh;
            ref_band_layout(flags & 1, B_LL3, &off, &bw, &bh);
            for (i = off + 1; i < off + bw * bh; i++)
                ref[i] = (int16_t)(ref[i] + ref[i - 1]);
        }
        memcpy(arena + SRC_A, bytes, (size_t)len);
        ogpu_batch_init(&B, sbuf, sizeof sbuf / 4);
        ogpu_rlgr_dec(&B, SRC_A, (unsigned long)len, COEF_A, 4096, (unsigned long)flags);
        err = run(sbuf, B.words);
        if (rc_ref < 0) {
            proto++;
            CHECK(err == OGPU_ERR_DATA, "round %d: reference PROTO, op %d", r, err);
            continue;
        }
        CHECK(err == OGPU_OK, "round %d: op error %d", r, err);
        for (i = 0; i < 4096; i++)
            if (get16(COEF_A + i * 2) != ref[i]) { bad = 1; break; }
        CHECK(!bad, "round %d (len %d flags %d): value %d differs: %d vs %d", r, len, flags, i, get16(COEF_A + i * 2), ref[i]);
        same += !bad;
    }
    /* Past its bytes RLGR1 reads zeros, which fill 4096 values in well under
     * the 64 spare bits, so neither side ever reports a malformed stream on
     * these inputs; the op's OGPU_ERR_DATA mirrors the reference's PROTO. */
    printf("rlgr: %d rounds, %d bit-exact, %d malformed (both)\n", rounds, same, proto);
    CHECK(same + proto == rounds, "%d + %d of %d", same, proto, rounds);
}

static void test_tile_ycbcr(int rounds) {
    static int16_t co[3][4096];
    static int32_t ref[3][4096], tmp[OGPU_DWT_RFX_WORK_INTS];
    static uint32_t want[PX_W * 70];
    uint8_t q[3][10];
    unsigned char sbuf[512];
    struct OGPUBatch B;
    int r, i, comp, bad;
    for (r = 0; r < rounds; r++) {
        int re = (int)(rnd() & 1), mag = (int)(rnd() % 4), nrect = 1 + (int)(rnd() % 4), k, err;
        int rx[4], ry[4], rw[4], rh[4];
        for (comp = 0; comp < 3; comp++) {
            for (i = 0; i < 10; i++)
                q[comp][i] = (uint8_t)(r % 7 == 3 ? rnd() % 16 : 6 + rnd() % 10);
            for (i = 0; i < 4096; i++) {
                int v = (int)(rnd() % 65536) - 32768;
                if (mag == 0) v >>= 12; else if (mag == 1) v >>= 8; else if (mag == 2) v = (rnd() % 8) ? 0 : v >> 6;
                co[comp][i] = (int16_t)v;
                put16(COEF_A + (ogpu_u32)(comp * 8192 + i * 2), v);
            }
            for (i = 0; i < 10; i++) arena[QUANT_A + comp * 10 + i] = q[comp][i];
            ref_dequant(co[comp], q[comp], re, ref[comp]);
            ref_idwt(ref[comp], re, tmp);
        }
        for (i = 0; i < PX_W * 70; i++) { want[i] = 0x12345678u; put32(PX_A + (ogpu_u32)i * 4, 0x12345678u); }
        ogpu_batch_init(&B, sbuf, sizeof sbuf / 4);
        ogpu_rfx_tile(&B, COEF_A, QUANT_A, re ? OGPU_RFX_RE : 0, YCC_A, WORK_A, OGPU_RFX_TILE_WORK_BYTES);
        for (k = 0; k < nrect; k++) {
            int x, y;
            rx[k] = (int)(rnd() % 64); ry[k] = (int)(rnd() % 64);
            rw[k] = 1 + (int)(rnd() % (unsigned)(64 - rx[k])); rh[k] = 1 + (int)(rnd() % (unsigned)(64 - ry[k]));
            if (k == 0 && r % 5 == 0) { rx[k] = ry[k] = 0; rw[k] = rh[k] = 64; }
            /* the tile sits at (8, 3) in the surface */
            ogpu_rfx_ycbcr(&B, YCC_A, rx[k], ry[k], rw[k], rh[k],
                           PX_A + (ogpu_u32)(((3 + ry[k]) * PX_W + 8 + rx[k]) * 4), PX_W * 4);
            for (y = ry[k]; y < ry[k] + rh[k]; y++)
                for (x = rx[k]; x < rx[k] + rw[k]; x++)
                    want[(3 + y) * PX_W + 8 + x] = ref_px(ref[0][y * 64 + x], ref[1][y * 64 + x], ref[2][y * 64 + x]);
        }
        err = run(sbuf, B.words);
        CHECK(err == OGPU_OK, "round %d: error %d", r, err);
        bad = 0;
        for (comp = 0; comp < 3 && !bad; comp++)
            for (i = 0; i < 4096; i++)
                if ((int32_t)get32(YCC_A + (ogpu_u32)(comp * 16384 + i * 4)) != ref[comp][i]) {
                    CHECK(0, "round %d re %d comp %d coef %d: %d vs %d", r, re, comp, i,
                          (int32_t)get32(YCC_A + (ogpu_u32)(comp * 16384 + i * 4)), ref[comp][i]);
                    bad = 1;
                    break;
                }
        for (i = 0; i < PX_W * 70 && !bad; i++)
            if (get32(PX_A + (ogpu_u32)i * 4) != want[i]) {
                CHECK(0, "round %d pixel %d,%d: %08x vs %08x", r, i % PX_W, i / PX_W,
                      (unsigned)get32(PX_A + (ogpu_u32)i * 4), (unsigned)want[i]);
                bad = 1;
            }
    }
    printf("rfx_tile + rfx_ycbcr: %d tiles bit-exact with the CPU path\n", rounds);
}

/* DWT_INV alone against the reference, out of place, both layouts */
static void test_dwt_exact(void) {
    static int32_t ref[4096], tmp[OGPU_DWT_RFX_WORK_INTS];
    unsigned char sbuf[64];
    struct OGPUBatch B;
    int re, i, bad;
    for (re = 0; re < 2; re++) {
        for (i = 0; i < 4096; i++) {
            ref[i] = (int32_t)(rnd() % 4001) - 2000;
            put32(COEF_A + (ogpu_u32)i * 4, (uint32_t)ref[i]);
        }
        ref_idwt(ref, re, tmp);
        ogpu_batch_init(&B, sbuf, sizeof sbuf / 4);
        ogpu_dwt_inv(&B, COEF_A, YCC_A, re ? OGPU_DWT_RFX_RE : 0, WORK_A, OGPU_DWT_RFX_WORK_BYTES);
        CHECK(run(sbuf, B.words) == OGPU_OK, "dwt run");
        for (i = 0, bad = 0; i < 4096; i++)
            if ((int32_t)get32(YCC_A + (ogpu_u32)i * 4) != ref[i]) { bad = 1; break; }
        CHECK(!bad, "dwt re %d out of place: coef %d", re, i);
    }
    printf("dwt_inv: bit-exact (both layouts, out of place, big-endian buffers)\n");
}

static void test_errors(void) {
    unsigned char sbuf[64];
    struct OGPUBatch B;
    int i;
    /* RLGR: reserved flag bits, odd out, too many values */
    ogpu_batch_init(&B, sbuf, sizeof sbuf / 4);
    ogpu_rlgr_dec(&B, SRC_A, 16, COEF_A, 4096, OGPU_RLGR_MODE3);
    CHECK(run(sbuf, B.words) == OGPU_ERR_UNSUPPORTED, "RLGR3 reserved");
    ogpu_batch_init(&B, sbuf, sizeof sbuf / 4);
    ogpu_rlgr_dec(&B, SRC_A, 16, COEF_A + 1, 4096, 0);
    CHECK(run(sbuf, B.words) == OGPU_ERR_UNSUPPORTED, "odd out");
    ogpu_batch_init(&B, sbuf, sizeof sbuf / 4);
    ogpu_rlgr_dec(&B, SRC_A, 16, COEF_A, 4097, 0);
    CHECK(run(sbuf, B.words) == OGPU_ERR_UNSUPPORTED, "count > 4096");
    /* no source bytes at all: zeros, as the reference gives */
    ogpu_batch_init(&B, sbuf, sizeof sbuf / 4);
    ogpu_rlgr_dec(&B, SRC_A, 0, COEF_A, 4096, OGPU_RLGR_LL3_DELTA);
    CHECK(run(sbuf, B.words) == OGPU_OK && get16(COEF_A + 4095 * 2) == 0, "empty RLGR");
    ogpu_batch_init(&B, sbuf, sizeof sbuf / 4);
    ogpu_rlgr_dec(&B, SRC_A, 16, ARENA - 100, 4096, 0);
    CHECK(run(sbuf, B.words) == OGPU_ERR_NOMAP, "unmapped out");
    /* TILE: quant over 15, short work, misaligned out */
    for (i = 0; i < 30; i++) arena[QUANT_A + i] = 6;
    arena[QUANT_A + 17] = 16;
    ogpu_batch_init(&B, sbuf, sizeof sbuf / 4);
    ogpu_rfx_tile(&B, COEF_A, QUANT_A, 0, YCC_A, WORK_A, OGPU_RFX_TILE_WORK_BYTES);
    CHECK(run(sbuf, B.words) == OGPU_ERR_DATA, "quant 16 is DATA");
    arena[QUANT_A + 17] = 6;
    ogpu_batch_init(&B, sbuf, sizeof sbuf / 4);
    ogpu_rfx_tile(&B, COEF_A, QUANT_A, 0, YCC_A, WORK_A, 64);
    CHECK(run(sbuf, B.words) == OGPU_ERR_UNSUPPORTED, "short work");
    ogpu_batch_init(&B, sbuf, sizeof sbuf / 4);
    ogpu_rfx_tile(&B, COEF_A, QUANT_A, 0, YCC_A + 2, WORK_A, OGPU_RFX_TILE_WORK_BYTES);
    CHECK(run(sbuf, B.words) == OGPU_ERR_UNSUPPORTED, "misaligned out");
    /* YCBCR: past the tile, short rows; empty is fine */
    ogpu_batch_init(&B, sbuf, sizeof sbuf / 4);
    ogpu_rfx_ycbcr(&B, YCC_A, 10, 0, 55, 4, PX_A, PX_W * 4);
    CHECK(run(sbuf, B.words) == OGPU_ERR_UNSUPPORTED, "x + w > 64");
    ogpu_batch_init(&B, sbuf, sizeof sbuf / 4);
    ogpu_rfx_ycbcr(&B, YCC_A, 0, 0, 8, 4, PX_A, 16);
    CHECK(run(sbuf, B.words) == OGPU_ERR_UNSUPPORTED, "bpr < w * 4");
    ogpu_batch_init(&B, sbuf, sizeof sbuf / 4);
    ogpu_rfx_ycbcr(&B, YCC_A, 0, 0, 0, 4, PX_A, PX_W * 4);
    CHECK(run(sbuf, B.words) == OGPU_OK, "empty rect");
    /* no TARGET needed, any format answers FULL */
    CHECK(ogpu_core_supports(OGPU_OP_RLGR_DEC, OGPU_FMT_ARGB32) == OGPU_FULL, "RLGR_DEC FULL");
    CHECK(ogpu_core_supports(OGPU_OP_RFX_TILE, OGPU_FMT_ARGB32) == OGPU_FULL, "RFX_TILE FULL");
    CHECK(ogpu_core_supports(OGPU_OP_RFX_YCBCR, OGPU_FMT_CLUT8) == OGPU_FULL, "RFX_YCBCR FULL");
}

int main(int argc, char **argv) {
    int rounds = argc > 1 ? atoi(argv[1]) : 400;
    test_dwt_exact();
    test_rlgr(rounds);
    test_tile_ycbcr(rounds / 4 > 0 ? rounds / 4 : 1);
    test_errors();
    printf(failures ? "FAILED %d\n" : "all ok\n", failures);
    return failures != 0;
}
