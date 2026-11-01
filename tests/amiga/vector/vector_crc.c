/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * OpenGPUVectorCRC: tests/vector_same.c's random batches (paths and
 * composites with every paint, mask, operator and format) run once on this
 * build's CPU core, with a CRC-32 of the compared memory after each round,
 * one line a round, and the CRC of them all. The same program built for the
 * host and for the Amiga must print the same lines: the core's pixels are
 * then the same whatever compiler built it, and a test that fails on one
 * of them fails in its reference, not the core.
 *   OpenGPUVectorCRC [ROUNDS [SEED [MIX]]]   (vector_same.c's arguments) */
#include <stdio.h>
#define main vector_same_main
#include "../../vector_same.c"
#undef main

static unsigned long crc_table[256], crc_all = 0xFFFFFFFFUL, crc_rounds;

static unsigned long crc32_of(const ogpu_u8 *p, unsigned long n, unsigned long c) {
    unsigned long i;
    for (i = 0; i < n; i++) c = crc_table[(c ^ p[i]) & 255] ^ (c >> 8);
    return c;
}

/* "the earlier core" is this one: each round's first run, whose memory is
 * then CRC'd (the second run, the same core again, is only compared) */
void old_ogpu_core_init(struct ogpu_core *c) { ogpu_core_init(c); }
long old_ogpu_core_run(struct ogpu_core *c, const ogpu_u8 *stream, long words) {
    long r = ogpu_core_run(c, stream, words);
    unsigned long v = crc32_of(cur, CMP_END, 0xFFFFFFFFUL) ^ 0xFFFFFFFFUL;
    printf("%lu %08lx %d\n", crc_rounds++, v, (int)c->last_error);
    crc_all = crc32_of((const ogpu_u8 *)&v, sizeof v, crc_all);
    return r;
}

int main(int argc, char **argv) {
    unsigned long k, c;
    int i, r;
    for (k = 0; k < 256; k++) {
        for (c = k, i = 0; i < 8; i++) c = c & 1 ? 0xEDB88320UL ^ (c >> 1) : c >> 1;
        crc_table[k] = c;
    }
    r = vector_same_main(argc, argv);
    printf("OpenGPUVectorCRC: %lu rounds, all %08lx\n", crc_rounds, crc_all ^ 0xFFFFFFFFUL);
    return r;
}
