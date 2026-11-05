/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * MS-RDPRFX inverse 5/3 DWT for OGPU_OP_DWT_INV (stream v1.9). Clean-room
 * from [MS-RDPRFX] 3.1.8.2.4 / 3.1.8.1.6 and the AmigaChrome OpenGPU
 * patterns; no FreeRDP. Plain C, no libc, no float — same file builds for
 * the 68k CPU back end, Cradle host, and PiStorm ARM. */
#include "ogpu_dwt.h"

enum {
	B_HL1, B_LH1, B_HH1, B_HL2, B_LH2, B_HH2, B_HL3, B_LH3, B_HH3, B_LL3, NBANDS
};

static void level_sizes(int re, int level, int *nl, int *nh)
{
	static const unsigned char lo_re[3] = { 33, 17, 9 }, hi_re[3] = { 31, 16, 8 };
	static const unsigned char lo[3] = { 32, 16, 8 }, hi[3] = { 32, 16, 8 };
	*nl = re ? lo_re[level - 1] : lo[level - 1];
	*nh = re ? hi_re[level - 1] : hi[level - 1];
}

/* Band offset/w/h for both layouts; filled once. */
static struct { short off, w, h; } s_band[2][NBANDS];
static unsigned char s_band_ready;

static void band_init(void)
{
	int re, band;
	if (s_band_ready)
		return;
	for (re = 0; re < 2; re++) {
		int o = 0;
		for (band = 0; band < NBANDS; band++) {
			int level = band == B_LL3 ? 3 : band / 3 + 1, nl, nh, bw, bh;
			level_sizes(re, level, &nl, &nh);
			switch (band == B_LL3 ? 3 : band % 3) {
			case 0: bw = nh; bh = nl; break;
			case 1: bw = nl; bh = nh; break;
			case 2: bw = nh; bh = nh; break;
			default: bw = nl; bh = nl; break;
			}
			s_band[re][band].off = (short)o;
			s_band[re][band].w = (short)bw;
			s_band[re][band].h = (short)bh;
			o += bw * bh;
		}
	}
	s_band_ready = 1;
}

/* nl low, nh high; high band padded with zeros to nhe. */
static void idwt1(const long *L, int ls, const long *H, int hs, int nl, int nh, int nhe,
                  long *X, int xs, int nout, long *t)
{
	int n, total = nl + nhe;
	for (n = 0; n < nl; n++) {
		int i0 = n > 0 ? n - 1 : 0, i1 = n < nhe ? n : (nhe > 0 ? nhe - 1 : 0);
		long hp = i0 < nh ? H[i0 * hs] : 0;
		long hn = i1 < nh ? H[i1 * hs] : 0;
		t[2 * n] = L[n * ls] - ((hp + hn + 1) >> 1);
	}
	for (n = 0; n < nhe; n++) {
		long a = t[2 * n], b = 2 * n + 2 < total ? t[2 * n + 2] : t[2 * n];
		long hn = n < nh ? H[n * hs] : 0;
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

static void idwt2(const long *LL, int las, const long *HL, const long *LH, const long *HH,
                  int a, int b, int be, int n, long *out, long *lp, long *hp, long *t)
{
	int y, x;
	for (y = 0; y < a; y++)
		idwt1(LL + y * las, 1, HL + y * b, 1, a, b, be, lp + y * n, 1, n, t);
	for (y = 0; y < b; y++)
		idwt1(LH + y * a, 1, HH + y * b, 1, a, b, be, hp + y * n, 1, n, t);
	for (x = 0; x < n; x++)
		idwt1(lp + x, n, hp + x, n, a, b, be, out + x, n, n, t);
}

void ogpu_rfx_idwt(long *buf, int re, long *tmp)
{
	long *ll = tmp, *lp = ll + 33 * 33, *hp = lp + 33 * 64, *t = hp + 33 * 64, *mid = t + 2 * 66;
	int off[NBANDS], i, l3, h3, l2, h2, l1, h1, n2, n1, rr;
	band_init();
	rr = re ? 1 : 0;
	for (i = 0; i < NBANDS; i++)
		off[i] = s_band[rr][i].off;
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
