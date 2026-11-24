/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * The driver choice (the shared pipe's plan, 2.9 and M9): which back end a
 * 3D front end draws on, from the variables and what the machine has. Only
 * OGPU_Driver (opengpu_lib.c) asks it; tests/test_driver_plan.c holds it to
 * the plan's table for every combination.
 *
 * The value: the front end's own variable (ENV:Warp3D/Driver,
 * ENV:MiniGL/Driver, ENV:TinyGL/Driver) when set, else ENV:GFOpenGPU/Driver,
 * else auto.
 *   auto      virgl when the host has it on a graphics chip with its direct present (ACV_HOST_PRESENT);
 *             else the stream route (ACRTG.gpu) while that back end is there;
 *             else the CPU;
 *   virgl     virgl on a graphics chip; without one, auto's next;
 *   llvmpipe  virgl even with no chip (the host's software renderer); without
 *             virgl, auto's next;
 *   cpu       the 68k core;
 *   acrtg     the stream route; without it, auto (it goes with M11).
 * The old names stay: GFOpenGPU and OpenGPU mean acrtg. Case doesn't matter,
 * and anything else means auto.
 *
 * No library calls, no writable data: the caller passes what it read. */
#ifndef OGPU_DRIVER_PLAN_H
#define OGPU_DRIVER_PLAN_H

#ifndef OGPU_DRV_BEST
#define OGPU_DRV_BEST              0
#define OGPU_DRV_VIRGL             1
#define OGPU_DRV_LLVMPIPE          2
#define OGPU_DRV_CPU               3
#define OGPU_DRV_ACRTG             4
#endif

/* What a variable says */
enum { OGD_UNSET, OGD_AUTO, OGD_VIRGL, OGD_LLVMPIPE, OGD_CPU, OGD_ACRTG };

/* What the machine has */
struct ogd_have {
    int virgl;          /* the host answered HELLO (OGPU_OP_VIRGL carried) */
    int chip;           /* ... drawing on a graphics chip (ACV_HOST_GPU) */
    int present;        /* ... and its direct present (ACV_HOST_PRESENT) */
    int acrtg;          /* a driver back end that draws 3D (not this 68k) */
};

static inline int ogd_lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

static inline int ogd_is(const char *v, const char *word)
{
    while (*word && ogd_lower((unsigned char)*v) == *word) { v++; word++; }
    /* a variable's file may end in a newline or spaces */
    return !*word && (!*v || *v == '\n' || *v == '\r' || *v == ' ' || *v == '\t');
}

/* A variable's text (NULL or empty: not set) */
static inline int ogd_parse(const char *v)
{
    if (!v || !*v || *v == '\n') return OGD_UNSET;
    if (ogd_is(v, "virgl")) return OGD_VIRGL;
    if (ogd_is(v, "llvmpipe")) return OGD_LLVMPIPE;
    if (ogd_is(v, "cpu")) return OGD_CPU;
    if (ogd_is(v, "acrtg") || ogd_is(v, "gfopengpu") || ogd_is(v, "opengpu")) return OGD_ACRTG;
    return OGD_AUTO;                                /* "auto", and what isn't known */
}

/* auto's choice, from the step after virgl when skip_virgl */
static inline unsigned long ogd_auto(const struct ogd_have *h, int skip_virgl, const char **why)
{
    if (!skip_virgl && h->virgl && h->chip && h->present) { *why = "virgl on the host's graphics chip"; return OGPU_DRV_VIRGL; }
    if (h->acrtg) {
        *why = !h->virgl ? "no virgl: the stream route" : !h->chip ? "virgl has no graphics chip: the stream route"
             : !h->present ? "the host has no direct present: the stream route" : "the stream route";
        return OGPU_DRV_ACRTG;
    }
    *why = !h->virgl ? "no virgl and no stream route: the 68k" : !h->chip ? "virgl has no graphics chip, no stream route: the 68k"
         : !h->present ? "the host has no direct present, no stream route: the 68k" : "the 68k";
    return OGPU_DRV_CPU;
}

/* api: the front end's variable; global: GFOpenGPU's. Returns an OGPU_DRV_ (never BEST); *why says why. */
static inline unsigned long ogd_choose(int api, int global, const struct ogd_have *h, const char **why)
{
    int v = api != OGD_UNSET ? api : global != OGD_UNSET ? global : OGD_AUTO;
    switch (v) {
    case OGD_VIRGL:
        if (h->virgl && h->chip) { *why = "asked: virgl on the host's graphics chip"; return OGPU_DRV_VIRGL; }
        return ogd_auto(h, 1, why);
    case OGD_LLVMPIPE:
        if (h->virgl) { *why = h->chip ? "asked: virgl (the host has a graphics chip)" : "asked: virgl, the host's software renderer";
                        return h->chip ? OGPU_DRV_VIRGL : OGPU_DRV_LLVMPIPE; }
        return ogd_auto(h, 1, why);
    case OGD_CPU:
        *why = "asked: the 68k"; return OGPU_DRV_CPU;
    case OGD_ACRTG:
        if (h->acrtg) { *why = "asked: the stream route"; return OGPU_DRV_ACRTG; }
        return ogd_auto(h, 0, why);
    }
    return ogd_auto(h, 0, why);
}

#endif
