/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * acvirgl_block_scan (host/virgl/acvirgl.h): what a request block writes
 * besides its own words, so the runtime knows whether it may run on its
 * own thread while the 68k goes on. No virglrenderer needed. */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include "../host/virgl/acvirgl.h"
#include "../include/gfopengpu/stream.h"
#include "../include/gfopengpu/virgl.h"

#define BASE 0x01000000u
#define SIZE 0x10000u
#define VRAM 0x01008000u
#define VLEN 0x4000u

static uint8_t mem[SIZE];
static int fails;

static uint8_t *map(void *u, uint32_t a, uint32_t n) {
    (void)u;
    return a >= BASE && a - BASE <= SIZE && n <= SIZE - (a - BASE) ? mem + (a - BASE) : NULL;
}
static void put(uint32_t a, uint32_t v) {
    uint8_t *p = mem + (a - BASE);
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}
static uint32_t w;      /* words of the block at BASE */
static void cmd(unsigned c, unsigned n, const uint32_t *args) {
    unsigned i;
    put(BASE + w * 4, (uint32_t)ACV_HDR(c, n + 2)); w++;
    put(BASE + w * 4, 0); w++;
    for (i = 0; i < n; i++) { put(BASE + w * 4, args[i]); w++; }
}

static uint32_t got_a[8], got_n[8];
static int got;
static void present(void *u, uint32_t a, uint32_t n) { (void)u; if (got < 8) { got_a[got] = a; got_n[got] = n; } got++; }

static void check(const char *what, int want, int want_presents) {
    int r;
    got = 0;
    r = acvirgl_block_scan(map, NULL, BASE, w * 4, VRAM, VLEN, present, NULL);
    if (r != want || got != want_presents) { printf("FAIL %s: %d with %d presents, wanted %d with %d\n", what, r, got, want, want_presents); fails++; }
    else printf("ok   %s\n", what);
}

int main(void) {
    uint32_t a[16];
    memset(a, 0, sizeof a);
    /* SUBMIT of commands in Fast RAM, ATTACH, RES_CREATE: their words only */
    w = 0;
    a[0] = 7; a[1] = BASE + 0x1000; a[2] = 16; cmd(ACV_SUBMIT, 3, a);
    a[0] = 7; a[1] = 9; cmd(ACV_ATTACH, 2, a);
    memset(a, 0, sizeof a); cmd(ACV_RES_CREATE, 12, a);
    check("SUBMIT, ATTACH, RES_CREATE: only the block", 0, 0);
    /* a SUBMIT whose commands are in video RAM: the 68k may be writing there */
    w = 0;
    a[0] = 7; a[1] = VRAM + 0x100; a[2] = 16; cmd(ACV_SUBMIT, 3, a);
    check("SUBMIT reading video RAM: with the 68k stopped", 1, 0);
    /* a PUT from video RAM, and one from Fast RAM */
    w = 0;
    memset(a, 0, sizeof a); a[11] = BASE + 0x2000; a[12] = 64; a[13] = ACV_SWAP_NONE; cmd(ACV_PUT, 14, a);
    check("PUT from Fast RAM", 0, 0);
    w = 0;
    memset(a, 0, sizeof a); a[11] = VRAM - 32; a[12] = 64; a[13] = ACV_SWAP_NONE; cmd(ACV_PUT, 14, a);
    check("PUT reaching into video RAM", 1, 0);
    /* GET and CAPS write the guest's memory */
    w = 0;
    memset(a, 0, sizeof a); a[11] = BASE + 0x2000; a[12] = 64; cmd(ACV_GET, 14, a);
    check("GET", 1, 0);
    w = 0;
    a[0] = 2; a[1] = 0; a[2] = BASE + 0x3000; a[3] = 256; cmd(ACV_CAPS, 4, a);
    check("CAPS", 1, 0);
    /* PRESENT: its target told, 2 rectangles; a 16-bit one too */
    w = 0;
    memset(a, 0, sizeof a);
    a[0] = 7; a[1] = 9; a[4] = VRAM + 0x200; a[5] = 400; a[6] = OGPU_FMT_XRGB32; a[7] = 100u << 16 | 20; a[9] = 2;
    a[10] = 0; a[11] = 10u << 16 | 10; a[12] = 0; a[13] = 10u << 16 | 10;
    cmd(ACV_PRESENT, 14, a);
    a[6] = OGPU_FMT_RGB565; a[9] = 0;
    cmd(ACV_PRESENT, 10, a);
    check("two PRESENTs: their targets told", 0, 2);
    if (got_a[0] != VRAM + 0x200 || got_n[0] != 400u * 19 + 100 * 4 || got_n[1] != 400u * 19 + 100 * 2) {
        printf("FAIL PRESENT's span: %08x %u, %u\n", (unsigned)got_a[0], (unsigned)got_n[0], (unsigned)got_n[1]); fails++;
    }
    /* a bad length: acvirgl_run stops there, so nothing after it counts */
    w = 0;
    a[0] = 7; a[1] = 9; cmd(ACV_ATTACH, 2, a);
    put(BASE + w * 4, (uint32_t)ACV_HDR(ACV_GET, 3)); w++; put(BASE + w * 4, 0); w++; put(BASE + w * 4, 0); w++;
    check("a GET too short: the rest skipped, as acvirgl_run does", 0, 0);
    /* a block that can't be reached, or in video RAM itself */
    got = 0;
    if (acvirgl_block_scan(map, NULL, 0x100, 64, VRAM, VLEN, present, NULL) != 1) { printf("FAIL an unreachable block\n"); fails++; }
    else printf("ok   an unreachable block: with the 68k stopped\n");
    if (acvirgl_block_scan(map, NULL, VRAM, 64, VRAM, VLEN, present, NULL) != 1) { printf("FAIL a block in video RAM\n"); fails++; }
    else printf("ok   a block in video RAM: with the 68k stopped\n");
    printf("acvirgl scan: %s\n", fails ? "FAILED" : "all passed");
    return fails != 0;
}
