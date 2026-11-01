/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * OpenGPUGradProbe: one linear gradient (three stops, the identity matrix)
 * filled by PATH_FILL on the CPU core, built as gfopengpu.library builds it.
 * Prints the pixels across row 40; they must step from 2040C0 through
 * green to yellow. Under qemu-m68k with gcc 13 at -O1/-O2 every pixel came
 * out as the first stop (the inverse matrix had a 0 for 65536). */
#include <stdio.h>
#include "../../../library/opengpu/ogpu_core.h"
#include "../../../include/gfopengpu/build.h"

static ogpu_u8 A[0x80000];
static ogpu_u8 *map(void *u, ogpu_u32 a, ogpu_u32 l) { (void)u; if (a > sizeof A || l > sizeof A - a) return 0; return A + a; }

int main(void)
{
    static const long ID[6] = { 0x10000, 0, 0, 0, 0x10000, 0 };
    static const unsigned long want[7] = { 0xff2040c0UL, 0xff205ca4UL, 0xff208f71UL, 0xff24c13fUL, 0xff71cd33UL, 0xffbdda26UL, 0 };
    long geo[6] = { 60L << 16, 0, 110L << 16, 0, 0, 0 };
    struct OGPUBatch B, P, G;
    static ogpu_u8 s[4096];
    struct ogpu_core c;
    int x, i, bad = 0;
    ogpu_batch_init(&P, A + 0x40000, 1000);
    ogpu_path_move(&P, 55 * 256, 10 * 256); ogpu_path_line(&P, 115 * 256, 10 * 256);
    ogpu_path_line(&P, 115 * 256, 70 * 256); ogpu_path_line(&P, 55 * 256, 70 * 256); ogpu_path_close(&P);
    ogpu_batch_init(&G, A + 0x50000, 100);
    ogpu_gradient(&G, OGPU_GRAD_LINEAR, 0, 3, ID, geo);
    ogpu_gradient_stop(&G, 0, 0xFF2040C0UL); ogpu_gradient_stop(&G, 0x8000, 0xFF20C040UL); ogpu_gradient_stop(&G, 0x10000, 0xFFE0E020UL);
    ogpu_batch_init(&B, s, 1024);
    ogpu_surface(&B, 0, 0, 160 * 4, 160, 100, OGPU_FMT_ARGB32);
    ogpu_target(&B, 0);
    ogpu_path_fill(&B, OGPU_PD_OVER | OGPU_PATH_GRADIENT, 0x50000, 0, 0, 0, 0, 0, 0x40000, (unsigned long)P.words, ID, 0x60000, 0x20000);
    ogpu_core_init(&c);
    c.map = map;
    ogpu_core_run(&c, s, B.words);
    printf("error %ld\n", (long)c.last_error);
    for (i = 0, x = 55; x < 116; x += 10, i++) {
        const ogpu_u8 *p = A + (40 * 160 + x) * 4;
        unsigned long v = (unsigned long)p[0] << 24 | (unsigned long)p[1] << 16 | (unsigned long)p[2] << 8 | p[3];
        printf("x %3d %08lx%s\n", x, v, v == want[i] ? "" : " WRONG");
        bad += v != want[i];
    }
    printf("OpenGPUGradProbe: %s\n", bad ? "FAIL" : "ok");
    return bad ? 10 : 0;
}
