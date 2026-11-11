/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 * The uaegfx monitor's mode table (gfopenrtg.library 0.26, library/uaegfx.c): with the caps
 * openrtg_lib.c gives a 16 MiB uaegfx board (3840 x 2160 at most, video RAM deciding, no Native
 * size, display not known), 1920 x 1080 32-bit is $610A1200 (GFOS's ScreenMode prefs), the
 * 64 MiB-only sizes are not offered, nothing is bigger than the video RAM, and there is no
 * preferred mode. */
#include <stdio.h>
#include <string.h>
#include "../library/modes.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

int main(void)
{
    static struct ortg_mode_table t;
    struct ortg_caps c;
    memset(&c, 0, sizeof c);
    c.vram_bytes = 16u << 20; c.max_width = 3840; c.max_height = 2160; c.formats = 7;
    CHECK(ortg_build_modes(&t, 1, &c, 0) == 45);
    const struct ortg_mode *m = ortg_find_mode(&t, 0x610A1200u);
    CHECK(m && m->width == 1920 && m->height == 1080 && m->depth == 32);
    CHECK(ortg_find_mode(&t, 0x610C1200u) == NULL);             /* 2560 x 1440: 64 MiB boards only */
    for (int i = 0; i < t.full_count; i++)
        CHECK((unsigned long)t.full[i].width * t.full[i].height * (t.full[i].depth / 8) <= c.vram_bytes);
    CHECK(ortg_preferred_mode(&t, ORTG_ARGB32) == 0);
    CHECK(ortg_best_mode(&t, 1920, 1080, 24) == 0x610A1200u);
    if (!fails) printf("test_uaegfx_modes: ok (%d modes listed, %d in All)\n", t.count, t.full_count);
    return fails != 0;
}
