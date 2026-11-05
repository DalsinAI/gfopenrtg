/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * RemoteFX compute kernels for the OpenGPU core: the inverse 5/3 DWT of
 * OGPU_OP_DWT_INV (stream v1.9), and v1.10's RLGR1 decode (RLGR_DEC),
 * dequantize + DWT of a tile (RFX_TILE) and YCbCr to ARGB32 (RFX_YCBCR).
 * Clean-room from [MS-RDPRFX] 3.1.8.1.6, 3.1.8.1.7.3, 3.1.8.2.4, 3.1.8.2.5
 * and [MS-RDPEGFX] 3.3.8.2, and the AmigaChrome OpenGPU patterns; no
 * FreeRDP. Plain C, no libc, no float, no writable statics
 * (gfopengpu.library is ROM-clean: hunk_rw_check). The file keeps its v1.9
 * name so every build that already lists it (and the Cradle runtime, which
 * builds ogpu_core.c with it) picks the new ops up unchanged. */
#include "ogpu_dwt.h"

enum {
	B_HL1, B_LH1, B_HH1, B_HL2, B_LH2, B_HH2, B_HL3, B_LH3, B_HH3, B_LL3, NBANDS
};

static void level_sizes(int re, int level, int *nl, int *nh)
{
	/* const tables only — reside in the code hunk. */
	static const unsigned char lo_re[3] = { 33, 17, 9 }, hi_re[3] = { 31, 16, 8 };
	static const unsigned char lo[3] = { 32, 16, 8 }, hi[3] = { 32, 16, 8 };
	*nl = re ? lo_re[level - 1] : lo[level - 1];
	*nh = re ? hi_re[level - 1] : hi[level - 1];
}

static void band_offs(int re, int off[NBANDS])
{
	int band, o = 0;
	for (band = 0; band < NBANDS; band++) {
		int level = band == B_LL3 ? 3 : band / 3 + 1, nl, nh, bw, bh;
		level_sizes(re, level, &nl, &nh);
		switch (band == B_LL3 ? 3 : band % 3) {
		case 0: bw = nh; bh = nl; break;
		case 1: bw = nl; bh = nh; break;
		case 2: bw = nh; bh = nh; break;
		default: bw = nl; bh = nl; break;
		}
		off[band] = o;
		o += bw * bh;
	}
}

/* nl low, nh high; high band padded with zeros to nhe. */
void ogpu_rfx_band(int re, int band, int *off, int *w, int *h)
{
	int o[NBANDS], nl, nh, level;
	if (band < 0 || band >= NBANDS) {
		*off = *w = *h = 0;
		return;
	}
	band_offs(re ? 1 : 0, o);
	level = band == B_LL3 ? 3 : band / 3 + 1;
	level_sizes(re, level, &nl, &nh);
	switch (band == B_LL3 ? 3 : band % 3) {
	case 0: *w = nh; *h = nl; break;
	case 1: *w = nl; *h = nh; break;
	case 2: *w = nh; *h = nh; break;
	default: *w = nl; *h = nl; break;
	}
	*off = o[band];
}

static void idwt1(const ogpu_rfx_s32 *L, int ls, const ogpu_rfx_s32 *H, int hs, int nl, int nh, int nhe,
                  ogpu_rfx_s32 *X, int xs, int nout, ogpu_rfx_s32 *t)
{
	int n, total = nl + nhe;
	for (n = 0; n < nl; n++) {
		int i0 = n > 0 ? n - 1 : 0, i1 = n < nhe ? n : (nhe > 0 ? nhe - 1 : 0);
		ogpu_rfx_s32 hp = i0 < nh ? H[i0 * hs] : 0;
		ogpu_rfx_s32 hn = i1 < nh ? H[i1 * hs] : 0;
		t[2 * n] = L[n * ls] - ((hp + hn + 1) >> 1);
	}
	for (n = 0; n < nhe; n++) {
		ogpu_rfx_s32 a = t[2 * n], b = 2 * n + 2 < total ? t[2 * n + 2] : t[2 * n];
		ogpu_rfx_s32 hn = n < nh ? H[n * hs] : 0;
		t[2 * n + 1] = 2 * hn + ((a + b) >> 1);
	}
	if (xs == 1) {
		for (n = 0; n < nout; n++)
			X[n] = t[n];
	} else {
		for (n = 0; n < nout; n++)
			X[n * xs] = t[n];
	}
}

static void idwt2(const ogpu_rfx_s32 *LL, int las, const ogpu_rfx_s32 *HL, const ogpu_rfx_s32 *LH, const ogpu_rfx_s32 *HH,
                  int a, int b, int be, int n, ogpu_rfx_s32 *out, ogpu_rfx_s32 *lp, ogpu_rfx_s32 *hp, ogpu_rfx_s32 *t)
{
	int y, x;
	for (y = 0; y < a; y++)
		idwt1(LL + y * las, 1, HL + y * b, 1, a, b, be, lp + y * n, 1, n, t);
	for (y = 0; y < b; y++)
		idwt1(LH + y * a, 1, HH + y * b, 1, a, b, be, hp + y * n, 1, n, t);
	for (x = 0; x < n; x++)
		idwt1(lp + x, n, hp + x, n, a, b, be, out + x, n, n, t);
}

void ogpu_rfx_idwt(ogpu_rfx_s32 *buf, int re, ogpu_rfx_s32 *tmp)
{
	ogpu_rfx_s32 *ll = tmp, *lp = ll + 33 * 33, *hp = lp + 33 * 64, *t = hp + 33 * 64, *mid = t + 2 * 66;
	int off[NBANDS], l3, h3, l2, h2, l1, h1, n2, n1;
	band_offs(re ? 1 : 0, off);
	level_sizes(re, 3, &l3, &h3);
	level_sizes(re, 2, &l2, &h2);
	level_sizes(re, 1, &l1, &h1);
	n2 = l3 + h3;
	n1 = l2 + h2;
	idwt2(buf + off[B_LL3], l3, buf + off[B_HL3], buf + off[B_LH3], buf + off[B_HH3], l3, h3,
	      re ? l3 - 1 : l3, n2, mid, lp, hp, t);
	idwt2(mid, n2, buf + off[B_HL2], buf + off[B_LH2], buf + off[B_HH2], l2, h2,
	      re ? l2 - 1 : l2, n1, ll, lp, hp, t);
	idwt2(ll, n1, buf + off[B_HL1], buf + off[B_LH1], buf + off[B_HH1], l1, h1,
	      re ? l1 - 1 : l1, 64, buf, lp, hp, t);
}

/* ---- v1.10: the Amiga's order in memory ------------------------------------------ */

/* The buffers v1.10's ops read and write are big-endian, as the 68k keeps
 * them; a little-endian host core (the Cradle's runtime, the PiStorm's ARM)
 * swaps at the edges, and the 68k reads them as they are. Compilers that
 * do not say the byte order (MSVC, vbcc, SAS/C without a define) must not
 * silently assume big-endian on a little-endian host: refuse the build, or
 * set RFX_FORCE_LE / RFX_FORCE_BE. */
#if defined(RFX_FORCE_LE)
#define RFX_LE 1
#elif defined(RFX_FORCE_BE)
#define RFX_LE 0
#elif defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define RFX_LE 1
#elif defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define RFX_LE 0
#elif defined(__LITTLE_ENDIAN__) && !defined(__BIG_ENDIAN__)
#define RFX_LE 1
#elif defined(__BIG_ENDIAN__) && !defined(__LITTLE_ENDIAN__)
#define RFX_LE 0
#else
#error "ogpu_dwt.c: unknown host byte order; define RFX_FORCE_LE or RFX_FORCE_BE"
#endif

static ogpu_rfx_s32 ld32(const unsigned char *p)
{
#if RFX_LE
	return (ogpu_rfx_s32)(((ogpu_rfx_u32)p[0] << 24) | ((ogpu_rfx_u32)p[1] << 16) |
	                      ((ogpu_rfx_u32)p[2] << 8) | (ogpu_rfx_u32)p[3]);
#else
	return *(const ogpu_rfx_s32 *)(const void *)p;
#endif
}

static ogpu_rfx_s32 ld16(const unsigned char *p)
{
#if RFX_LE
	return (ogpu_rfx_s16)(unsigned short)(((unsigned)p[0] << 8) | p[1]);
#else
	return *(const ogpu_rfx_s16 *)(const void *)p;
#endif
}

static void st16(unsigned char *p, ogpu_rfx_s32 v)
{
#if RFX_LE
	p[0] = (unsigned char)((ogpu_rfx_u32)v >> 8);
	p[1] = (unsigned char)v;
#else
	*(ogpu_rfx_s16 *)(void *)p = (ogpu_rfx_s16)v;
#endif
}

static void st32(unsigned char *p, ogpu_rfx_u32 v)
{
#if RFX_LE
	p[0] = (unsigned char)(v >> 24);
	p[1] = (unsigned char)(v >> 16);
	p[2] = (unsigned char)(v >> 8);
	p[3] = (unsigned char)v;
#else
	*(ogpu_rfx_u32 *)(void *)p = v;
#endif
}

void ogpu_rfx_order32(ogpu_rfx_s32 *p, int n)
{
#if RFX_LE
	int i;
	for (i = 0; i < n; i++) {
		ogpu_rfx_u32 v = (ogpu_rfx_u32)p[i];
		p[i] = (ogpu_rfx_s32)((v >> 24) | ((v >> 8) & 0xFF00u) | ((v << 8) & 0xFF0000u) | (v << 24));
	}
#else
	(void)p; (void)n;
#endif
}

/* ---- v1.10: RLGR1 ([MS-RDPRFX] 3.1.8.1.7.3) ---------------------------------------- */

/* Most significant bit first, through a 32-bit accumulator. Past the end
 * the stream reads as zeros; far past it (64 bits) the decode gives up. */
struct rfx_bits {
	const unsigned char *p, *end;
	ogpu_rfx_u32 buf;
	int nbuf;
	unsigned long pos, lim;
};

static void bits_init(struct rfx_bits *b, const unsigned char *p, unsigned long len)
{
	b->p = p;
	b->end = p + len;
	b->buf = 0;
	b->nbuf = 0;
	b->pos = 0;
	b->lim = len * 8UL;
}

static ogpu_rfx_u32 get_bits(struct rfx_bits *b, int n)
{
	ogpu_rfx_u32 v;
	if (n <= 0)
		return 0;
	if (b->nbuf < n) {
		while (b->nbuf <= 24 && b->p < b->end) {
			b->buf |= (ogpu_rfx_u32)(*b->p++) << (24 - b->nbuf);
			b->nbuf += 8;
		}
	}
	if (b->nbuf < n) {
		v = b->nbuf ? (b->buf >> (32 - b->nbuf)) << (n - b->nbuf) : 0;
		b->pos += (unsigned long)n;
		b->buf = 0;
		b->nbuf = 0;
		return v;
	}
	v = b->buf >> (32 - n);
	b->buf <<= n;
	b->nbuf -= n;
	b->pos += (unsigned long)n;
	return v;
}

static int bits_over(const struct rfx_bits *b)
{
	return b->pos > b->lim + 64;
}

#define KPMAX 80
#define LSGR 3
#define UP_GR 4
#define DN_GR 6
#define UQ_GR 3
#define DQ_GR 3

static int update_param(int *param, int delta)
{
	*param += delta;
	if (*param > KPMAX) *param = KPMAX;
	if (*param < 0) *param = 0;
	return *param >> LSGR;
}

static ogpu_rfx_u32 gr_code(struct rfx_bits *b, int *krp, int *kr)
{
	ogpu_rfx_u32 vk = 0, mag;
	while (get_bits(b, 1) == 1) {
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

static void zeros16(unsigned char *out, int from, int n)
{
	int i;
	for (i = 0; i < n; i++)
		st16(out + (from + i) * 2, 0);
}

int ogpu_rfxk_rlgr1(const unsigned char *p, unsigned long len, unsigned char *out, int n, int flags)
{
	struct rfx_bits b;
	int k = 1, kp = 1 << LSGR, kr = 1, krp = 1 << LSGR, o = 0;
	bits_init(&b, p, len);
	while (o < n) {
		if (bits_over(&b))
			return -1;
		if (k) {
			ogpu_rfx_u32 run, sign;
			int mag;
			while (get_bits(&b, 1) == 0) {
				int z = 1 << k;
				if (z > n - o) z = n - o;
				if (z > 0) {
					zeros16(out, o, z);
					o += z;
				}
				k = update_param(&kp, UP_GR);
				if (o >= n || bits_over(&b))
					break;
			}
			if (o >= n)
				break;
			run = get_bits(&b, k);
			if (run > (ogpu_rfx_u32)(n - o))
				run = (ogpu_rfx_u32)(n - o);
			if (run) {
				zeros16(out, o, (int)run);
				o += (int)run;
			}
			if (o >= n)
				break;
			sign = get_bits(&b, 1);
			mag = (int)gr_code(&b, &krp, &kr) + 1;
			st16(out + o * 2, sign ? -mag : mag);
			o++;
			k = update_param(&kp, -DN_GR);
		} else {
			ogpu_rfx_u32 mag = gr_code(&b, &krp, &kr);   /* 2 * magnitude - sign */
			if (!mag) {
				st16(out + o * 2, 0);
				o++;
				k = update_param(&kp, UQ_GR);
			} else {
				st16(out + o * 2, (mag & 1) ? -(int)((mag + 1) >> 1) : (int)(mag >> 1));
				o++;
				k = update_param(&kp, -DQ_GR);
			}
		}
	}
	/* [MS-RDPEGFX] 3.3.8.2.1.1: LL3 comes as deltas, summed in int16. */
	if (flags & 2) {
		int off, bw, bh, i;
		ogpu_rfx_band(flags & 1, 9, &off, &bw, &bh);
		if (off + bw * bh <= n)
			for (i = off + 1; i < off + bw * bh; i++)
				st16(out + i * 2, (ogpu_rfx_s16)(ld16(out + i * 2) + ld16(out + (i - 1) * 2)));
	}
	return 0;
}

/* ---- v1.10: a tile's dequantize + DWT, and YCbCr ([MS-RDPRFX] 3.1.8.2.3-5) --------- */

int ogpu_rfxk_tile(const unsigned char *coef, const unsigned char *quant, int re,
                  unsigned char *out, ogpu_rfx_s32 *work)
{
	int comp, band, i;
	for (i = 0; i < 30; i++)
		if (quant[i] > 15)
			return -1;
	for (comp = 0; comp < 3; comp++) {
		const unsigned char *co = coef + comp * 4096 * 2;
		ogpu_rfx_s32 *w = (ogpu_rfx_s32 *)(void *)(out + comp * 4096 * 4);
		for (band = 0; band < NBANDS; band++) {
			int sh = (int)quant[comp * 10 + band] - 6, off, bw, bh, end;
			ogpu_rfx_band(re, band, &off, &bw, &bh);
			end = off + bw * bh;
			if (sh >= 0) {
				ogpu_rfx_s32 m = (ogpu_rfx_s32)1 << sh;
				for (i = off; i < end; i++)
					w[i] = ld16(co + i * 2) * m;
			} else {
				int rs = -sh;
				for (i = off; i < end; i++)
					w[i] = ld16(co + i * 2) >> rs;
			}
		}
		ogpu_rfx_idwt(w, re, work);
		ogpu_rfx_order32(w, 4096);
	}
	return 0;
}

static ogpu_rfx_u32 clamp8(ogpu_rfx_s32 v)
{
	ogpu_rfx_u32 u = (ogpu_rfx_u32)v;
	if (u > 255u)
		u = v < 0 ? 0u : 255u;
	return u;
}

void ogpu_rfxk_ycbcr(const unsigned char *ycc, int x, int y, int w, int h,
                    unsigned char *dst, long bpr)
{
	int row, i;
	for (row = 0; row < h; row++) {
		const unsigned char *Y = ycc + ((y + row) * 64 + x) * 4;
		const unsigned char *Cb = Y + 4096 * 4, *Cr = Cb + 4096 * 4;
		unsigned char *d = dst + (long)row * bpr;
		for (i = 0; i < w; i++) {
			ogpu_rfx_s32 yy = ld32(Y + i * 4) + 128, cb = ld32(Cb + i * 4), cr = ld32(Cr + i * 4);
			ogpu_rfx_u32 ucb = (ogpu_rfx_u32)cb, ucr = (ogpu_rfx_u32)cr;
			/* 32-bit wrapping products, then an arithmetic shift: the same
			 * bits a 68040's muls.l and asr.l give the C of the reference */
			ogpu_rfx_s32 R = yy + ((ogpu_rfx_s32)(ucr * 91916u + 32768u) >> 16);
			ogpu_rfx_s32 G = yy - ((ogpu_rfx_s32)(ucb * 22527u + ucr * 46819u + 32768u) >> 16);
			ogpu_rfx_s32 B = yy + ((ogpu_rfx_s32)(ucb * 115993u + 32768u) >> 16);
			st32(d + i * 4, 0xFF000000u | (clamp8(R) << 16) | (clamp8(G) << 8) | clamp8(B));
		}
	}
}
