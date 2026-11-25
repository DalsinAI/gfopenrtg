/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * OGPU_Query's capability answers (gfopengpu.library 0.30, stage MQ of docs/design/OpenGPU-Shared-Pipe-Plan.md;
 * every op and every PARTIAL in docs/design/OGPU-Query.md). Plain C, no exec: the library holds one struct
 * ogpu_caps in its base and gives this file a way to run a virgl request block; tests/caps/test_caps.c gives it a
 * pretend host.
 *
 * Discovery is once per boot: the first capability question sends ACV_HELLO and ACV_CAPSET_INFO, then ACV_CAPS
 * into the struct itself, and every later question is answered from what came back. Only ogpu_caps_reset (the
 * host went away: OGPU_ERR_DEVICE) makes the next question look again. */
#ifndef OGPU_CAPS_CORE_H
#define OGPU_CAPS_CORE_H

#include <exec/types.h>

#define OGPU_CAPS_WORDS 192             /* virgl's caps set 2 up to its renderer string (bytes 696-759), and room */

/* Run one request block (in the Amiga's word order); 0 when it ran. */
typedef LONG (*ogpu_caps_run_fn)(void *ctx, ULONG *blk, ULONG bytes);

struct ogpu_caps {
    UBYTE looked;                       /* discovery done (whatever it found) */
    UBYTE virgl;                        /* the host answered HELLO with our protocol */
    UBYTE set;                          /* the caps set read: 2, 1, or 0 for none */
    UBYTE cpu3d;                        /* the 68k 3D core is in this build */
    ULONG host_flags;                   /* HELLO's flags (ACV_HOST_*) */
    ULONG words;                        /* caps words the host gave */
    ULONG discoveries;                  /* HELLOs sent since the library loaded: 1 per boot, or per host reset */
    ULONG blk[16];                      /* the request block (in the struct: it must be memory the host reaches) */
    ULONG caps[OGPU_CAPS_WORDS];        /* the host's caps, as it wrote them (VG_GETW reads them) */
};

void ogpu_caps_init(struct ogpu_caps *c, int cpu3d);
/* The host went away: the next question looks again. */
void ogpu_caps_reset(struct ogpu_caps *c);
/* Caps set 2's renderer string: 1 a graphics chip's, 0 a software renderer's, -1 none (ogpu_caps.c). */
int ogpu_caps_renderer_on_chip(const struct ogpu_caps *c);
/* Look, if not done yet. have_virgl: a back end carries OGPU_OP_VIRGL (run may be 0 when it doesn't).
 * caps_at: c->caps as the host reaches it, an Amiga address. The 68k library passes the pointer itself; a native
 * module (GF-91), whose pointers aren't Amiga addresses, passes what its gate maps there. */
void ogpu_caps_discover(struct ogpu_caps *c, int have_virgl, ogpu_caps_run_fn run, void *ctx, ULONG caps_at);

/* The answer word for a capability op (OGPU_OP_IS_CAP) from a back end (OGPU_DRV_*): OGPU_ANSWER, OGPU_BACKEND
 * (0 the CPU, 1 the driver) and OGPU_ONGPU_BIT, as OGPU_Query's. OGPU_DRV_ACRTG answers NONE: the driver's
 * stream route has no capability ops. */
ULONG ogpu_caps_answer(const struct ogpu_caps *c, ULONG drv, ULONG op, ULONG format);
/* A limit (OGPU_OP_MAX_*) from the best back end; 0 when not known. */
ULONG ogpu_caps_limit(const struct ogpu_caps *c, ULONG op);

#endif
