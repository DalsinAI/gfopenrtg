/* acvirgl says once when Mesa quietly gives a software renderer (AC-285).
 * Run by test_quiet_software.sh with test_virgl_stub.c as virglrenderer:
 *   test_quiet_software <times>
 * makes and frees <times> acvirgl contexts; the script reads stderr.
 * Copyright (c) 2026 AmiCradle Team. MIT. */
#include <stdio.h>
#include <stdlib.h>
#include "acvirgl.h"

int main(int argc, char **argv)
{
    int i, n = argc > 1 ? atoi(argv[1]) : 1;
    char err[320];
    for (i = 0; i < n; i++) {
        struct acvirgl *v = acvirgl_create("libvirglrenderer.so.1", err, (int)sizeof err);
        if (!v) { fprintf(stderr, "no context: %s\n", err); return 1; }
        printf("renderer %s\n", acvirgl_renderer(v));
        acvirgl_destroy(v);
    }
    return 0;
}
