/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * ogpu_image_size.c on the host: prints "W H ALPHA" for each file, or "-"
 * when the header doesn't say (tests/sdl2/test_image_size.py). With -p
 * first, each file is also read cut short at every length, each copy in
 * memory of exactly that length (ASan sees a read past it), and every
 * answer given for a cut copy that differs from the whole file's is
 * printed as "CUT n ...". */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../../library/modules/sdl2/satellites/module/ogpu_image_size.c"

int main(int argc, char **argv)
{
    int i, cuts = 0;
    if (argc > 1 && argv[1][0] == '-' && argv[1][1] == 'p') {
        cuts = 1;
    }
    for (i = 1 + cuts; i < argc; i++) {
        FILE *f = fopen(argv[i], "rb");
        unsigned char *b;
        long n;
        unsigned long w, h;
        int a;
        if (!f) return 2;
        fseek(f, 0, SEEK_END);
        n = ftell(f);
        fseek(f, 0, SEEK_SET);
        b = malloc(n ? (size_t)n : 1);
        if (!b || fread(b, 1, (size_t)n, f) != (size_t)n) return 2;
        fclose(f);
        if (ogpu_image_size(b, (size_t)n, &w, &h, &a)) printf("%lu %lu %d\n", w, h, a);
        else printf("-\n");
        if (cuts) {
            unsigned long fw = w, fh = h;
            int fa = a, whole = ogpu_image_size(b, (size_t)n, &w, &h, &a);
            long k;
            for (k = 0; k < n; k++) {
                unsigned char *c = malloc(k ? (size_t)k : 1);
                unsigned long cw, ch;
                int ca;
                if (!c) return 2;
                memcpy(c, b, (size_t)k);
                if (ogpu_image_size(c, (size_t)k, &cw, &ch, &ca) &&
                    (!whole || cw != fw || ch != fh || ca != fa)) {
                    printf("CUT %ld %lu %lu %d\n", k, cw, ch, ca);
                }
                free(c);
            }
        }
        free(b);
    }
    return 0;
}
