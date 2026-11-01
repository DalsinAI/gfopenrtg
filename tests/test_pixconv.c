/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * library/pixconv.h against the code it replaces in screens.c
 * (openrtg.library 0.24): the nearest pen and its memo, through palette
 * changes. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../library/pixconv.h"

static unsigned long long seed = 12345;
static ULONG rnd(void) { seed = seed * 6364136223846793005ULL + 1442695040888963407ULL; return (ULONG)(seed >> 33); }
static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; if (fails < 20) { printf(__VA_ARGS__); printf("\n"); } } } while (0)

/* ---- screens.c 0.24's code, as it was ---- */

static UBYTE old_px_pen(const ULONG *pal, UBYTE bpp, ULONG px)
{
    ULONG rgb, best = 0, dist = (ULONG)~0UL;
    if (bpp == 1) return (UBYTE)px;
    rgb = pc_decode(bpp, px);
    for (ULONG i = 0; i < 256; i++) {
        ULONG c = bpp == 2 ? pc_decode(bpp, pc_encode(bpp, pal[i])) : pal[i];
        LONG dr = (LONG)((c >> 16) & 255) - (LONG)((rgb >> 16) & 255), dg = (LONG)((c >> 8) & 255) - (LONG)((rgb >> 8) & 255), db = (LONG)(c & 255) - (LONG)(rgb & 255);
        ULONG d2 = (ULONG)(dr * dr + dg * dg + db * db);
        if (d2 < dist) { dist = d2; best = i; if (!d2) break; }
    }
    return (UBYTE)best;
}

/* ---- the tests ---- */

static ULONG pal[PC_MEMO_TABLES][256];

static void random_pal(ULONG *p)
{
    for (int i = 0; i < 256; i++) {
        switch (rnd() % 4) {
        case 0: p[i] = rnd() & 0xFFFFFF; break;
        case 1: p[i] = i ? p[rnd() % i] : 0; break;                         /* the same colour twice */
        case 2: p[i] = i ? (p[rnd() % i] ^ (rnd() & 0x030303)) & 0xFFFFFF : 0; break;   /* near another */
        default: p[i] = (rnd() & 0xFF000000) | (rnd() & 0x00F8FCF8); break;  /* top byte set, as 16-bit */
        }
    }
}

static void test_nearest_and_memo(void)
{
    static struct pc_memo memo;
    int t;
    for (t = 0; t < PC_MEMO_TABLES; t++) random_pal(pal[t]);
    for (int i = 0; i < 20000; i++) {
        UBYTE bpp = rnd() & 1 ? 2 : 4;
        ULONG px = rnd() % 3 ? rnd() : pal[rnd() % 16][rnd() % 256];
        t = rnd() % PC_MEMO_TABLES;
        if (bpp == 2) px &= 0xFFFF;
        CHECK(pc_nearest(pal[t], bpp, px) == old_px_pen(pal[t], bpp, px), "nearest %08x bpp %d", px, bpp);
    }
    /* the memo: random pixels, few enough to repeat, through palette changes */
    for (int i = 0; i < 200000; i++) {
        UBYTE bpp = rnd() & 1 ? 2 : 4;
        ULONG px = (rnd() % 600) * 0x10101 ^ (rnd() % 7) << 9;
        t = rnd() % PC_MEMO_TABLES;
        if (rnd() % 997 == 0) {
            pc_memo_forget(&memo);
            pal[rnd() % PC_MEMO_TABLES][rnd() % 256] = rnd() & 0xFFFFFF;
            pc_memo_forget(&memo);
        }
        if (i == 100000) memo.gen = PC_MEMO_GENS - 1;    /* round the generations */
        if (bpp == 2) px &= 0xFFFF;
        CHECK(pc_memo_pen(&memo, pal[t], bpp, (UBYTE)t, px) == old_px_pen(pal[t], bpp, px), "memo %08x bpp %d table %d at %d", px, bpp, t, i);
    }
    /* a table beyond the key's room goes uncached */
    CHECK(pc_memo_pen(&memo, pal[3], 4, 200, 0x123456) == old_px_pen(pal[3], 4, 0x123456), "memo table 200");
}

int main(void)
{
    test_nearest_and_memo();
    if (fails) { printf("test_pixconv: %d FAILED\n", fails); return 1; }
    printf("test_pixconv: the nearest pens and the memo are as before\n");
    return 0;
}
