/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * The driver choice (library/opengpu/ogpu_driver_plan.h) against the shared
 * pipe plan's table (2.9): the matrix gate of M9. Every value of the front
 * end's variable and GFOpenGPU's (unset, auto, virgl, llvmpipe, cpu,
 * acrtg), with and without virgl, a graphics chip, ACV_PRESENT and the
 * stream route: 6 x 6 x 16 cases, each checked against the table's rule as
 * written out below, and the rules every answer keeps. Then the names.
 *   cc -I library/opengpu tests/test_driver_plan.c && ./a.out */
#include <stdio.h>
#include <string.h>
#include "ogpu_driver_plan.h"

static int checks, failures;
static void check(int ok, const char *what)
{
    checks++;
    if (!ok) { failures++; printf("FAIL %s\n", what); }
}

/* The table, row by row, as the plan has it */
static unsigned long table(int v, const struct ogd_have *h)
{
    int vc = h->virgl && h->chip;
    unsigned long next = h->acrtg ? OGPU_DRV_ACRTG : OGPU_DRV_CPU;     /* auto's next after virgl */
    switch (v) {
    case OGD_VIRGL:    return vc ? OGPU_DRV_VIRGL : next;
    case OGD_LLVMPIPE: return !h->virgl ? next : h->chip ? OGPU_DRV_VIRGL : OGPU_DRV_LLVMPIPE;
    case OGD_CPU:      return OGPU_DRV_CPU;
    case OGD_ACRTG:    if (h->acrtg) return OGPU_DRV_ACRTG; /* else auto */ break;
    }
    return vc && h->present ? OGPU_DRV_VIRGL : next;                   /* auto, unset */
}

static const char *const vname[] = { "unset", "auto", "virgl", "llvmpipe", "cpu", "acrtg" };
static const char *const dname[] = { "best", "virgl", "llvmpipe", "cpu", "acrtg" };

int main(void)
{
    int api, global, m;
    char what[200];
    for (api = OGD_UNSET; api <= OGD_ACRTG; api++)
        for (global = OGD_UNSET; global <= OGD_ACRTG; global++)
            for (m = 0; m < 16; m++) {
                struct ogd_have h;
                const char *why = NULL;
                unsigned long d, want;
                int v = api != OGD_UNSET ? api : global != OGD_UNSET ? global : OGD_AUTO;
                h.virgl = m & 1; h.chip = (m >> 1) & 1; h.present = (m >> 2) & 1; h.acrtg = (m >> 3) & 1;
                d = ogd_choose(api, global, &h, &why);
                want = table(v, &h);
                snprintf(what, sizeof what, "api %s, GFOpenGPU %s, virgl %d chip %d present %d stream %d: %s, want %s (%s)",
                         vname[api], vname[global], h.virgl, h.chip, h.present, h.acrtg,
                         d <= 4 ? dname[d] : "?", dname[want], why ? why : "no reason");
                check(d == want, what);
                /* what every answer keeps */
                check(why && *why, what);
                check(d != OGPU_DRV_BEST, what);
                check(d != OGPU_DRV_VIRGL || (h.virgl && h.chip), what);
                check(d != OGPU_DRV_LLVMPIPE || (h.virgl && !h.chip && v == OGD_LLVMPIPE), what);
                check(d != OGPU_DRV_ACRTG || h.acrtg, what);
                check(d != OGPU_DRV_CPU || v == OGD_CPU || (!h.acrtg && !(h.virgl && h.chip && (h.present || v == OGD_VIRGL))
                                                            && !(h.virgl && v == OGD_LLVMPIPE)), what);
                /* the front end's own variable wins over GFOpenGPU's */
                if (api != OGD_UNSET) check(d == ogd_choose(api, OGD_UNSET, &h, &why), what);
            }

    /* a few cases by name, as a reader checks the table */
    {
        struct ogd_have pc = { 1, 1, 1, 1 }, nochip = { 1, 0, 1, 1 }, nopresent = { 1, 1, 0, 1 }, bare = { 0, 0, 0, 0 };
        const char *why;
        check(ogd_choose(OGD_UNSET, OGD_UNSET, &pc, &why) == OGPU_DRV_VIRGL, "a PC with a GPU: virgl");
        check(ogd_choose(OGD_UNSET, OGD_UNSET, &nochip, &why) == OGPU_DRV_ACRTG, "virgl on llvmpipe: auto takes the stream route");
        check(ogd_choose(OGD_UNSET, OGD_LLVMPIPE, &nochip, &why) == OGPU_DRV_LLVMPIPE, "llvmpipe asked: virgl with no chip");
        check(ogd_choose(OGD_UNSET, OGD_UNSET, &nopresent, &why) == OGPU_DRV_ACRTG, "no ACV_PRESENT: auto takes the stream route");
        check(ogd_choose(OGD_UNSET, OGD_VIRGL, &nopresent, &why) == OGPU_DRV_VIRGL, "virgl asked, no ACV_PRESENT: virgl");
        check(ogd_choose(OGD_UNSET, OGD_UNSET, &bare, &why) == OGPU_DRV_CPU, "a bare Amiga: the 68k");
        check(ogd_choose(OGD_UNSET, OGD_ACRTG, &bare, &why) == OGPU_DRV_CPU, "acrtg asked with no stream route: auto");
        check(ogd_choose(OGD_CPU, OGD_VIRGL, &pc, &why) == OGPU_DRV_CPU, "Warp3D's CPU over GFOpenGPU's virgl");
        check(ogd_choose(OGD_UNSET, OGD_CPU, &pc, &why) == OGPU_DRV_CPU, "GFOpenGPU's cpu for every API");
    }

    /* the names */
    check(ogd_parse(NULL) == OGD_UNSET, "NULL: unset");
    check(ogd_parse("") == OGD_UNSET, "empty: unset");
    check(ogd_parse("\n") == OGD_UNSET, "a newline: unset");
    check(ogd_parse("auto") == OGD_AUTO && ogd_parse("Auto") == OGD_AUTO, "auto");
    check(ogd_parse("virgl") == OGD_VIRGL && ogd_parse("Virgl") == OGD_VIRGL && ogd_parse("VIRGL\n") == OGD_VIRGL, "virgl");
    check(ogd_parse("llvmpipe") == OGD_LLVMPIPE && ogd_parse("LLVMpipe") == OGD_LLVMPIPE, "llvmpipe");
    check(ogd_parse("cpu") == OGD_CPU && ogd_parse("CPU") == OGD_CPU && ogd_parse("CPU \n") == OGD_CPU, "cpu, and the old CPU");
    check(ogd_parse("acrtg") == OGD_ACRTG && ogd_parse("ACRTG") == OGD_ACRTG, "acrtg");
    check(ogd_parse("GFOpenGPU") == OGD_ACRTG && ogd_parse("OpenGPU") == OGD_ACRTG, "the old GFOpenGPU and OpenGPU: acrtg");
    check(ogd_parse("virglx") == OGD_AUTO && ogd_parse("cp") == OGD_AUTO && ogd_parse("fast") == OGD_AUTO, "anything else: auto");

    printf("test_driver_plan: %d checks, %d failed\n", checks, failures);
    return failures != 0;
}
