/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * The GL module's virgl transport on OS 3.2 (library/modules/gl/gla/os3/
 * gla_virgl_os3.c) on the PC, with stub AmigaOS headers
 * (tests/gla_virgl-include, then tests/gla_present-include) and a fake
 * gfopengpu.library. A request block is done when OGPU_Wait gives OK, and
 * also when it gives OGPU_ERR_EXPIRED: sixteen or more other programs'
 * batches came between the submit and the wait, and the library no longer
 * keeps the result. GL.module took that as a failed block (7 Nov 2026: a
 * program starting beside two others fell back to softpipe, and resources
 * the host had made were freed and their handles used again). Other errors
 * still fail the block. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <exec/types.h>
#include <exec/memory.h>
#include <devices/timer.h>
#include <gfopengpu/gfopengpu.h>
#include <gfopengpu/stream.h>
#include "gla/gla_core.h"
#include "gla/os3/gla_virgl_os3.h"

struct gla_stats gla_counts;
static struct Library ogpu_lib = { 0, 27 };

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* the fake library: what the next waits answer, and what was waited for */
static LONG answers[4];
static int nanswers, nwaits, waited_all, batches_done;
static ULONG next_fence = 100;

struct Library *OpenLibrary(CONST_STRPTR name, ULONG version) { (void)version; return !strcmp(name, OPENGPU_NAME) ? &ogpu_lib : NULL; }
void CloseLibrary(struct Library *lib) { (void)lib; }
APTR AllocVec(ULONG bytes, ULONG flags) { return (flags & MEMF_CLEAR) ? calloc(1, bytes) : malloc(bytes); }
void FreeVec(APTR p) { free(p); }
LONG OpenDevice(CONST_STRPTR name, ULONG unit, struct IORequest *io, ULONG flags) { (void)name; (void)unit; (void)io; (void)flags; return 1; }
void CloseDevice(struct IORequest *io) { (void)io; }
ULONG ReadEClock(struct EClockVal *e) { e->ev_hi = e->ev_lo = 0; return 709379; }
ULONG OGPU_Query(ULONG op, ULONG format) { (void)format; return op == OGPU_OP_VIRGL ? (ULONG)OGPU_FULL | (1UL << 8) : 0; }
LONG OGPU_Submit(APTR stream, ULONG words, ULONG *fence)
{
    (void)stream; (void)words;
    *fence = ++next_fence;
    return OGPU_OK;
}
LONG OGPU_Wait(ULONG fence)
{
    nwaits++;
    if (fence == OGPU_FENCE_ALL) { waited_all++; batches_done = 1; return OGPU_OK; }
    if (nanswers) {
        LONG a = answers[0];
        memmove(answers, answers + 1, sizeof answers - sizeof answers[0]);
        nanswers--;
        if (a == OGPU_ERR_EXPIRED) batches_done = 0;   /* as ACRTG.gpu's expired driver fence: not waited for */
        else batches_done = 1;
        return a;
    }
    batches_done = 1;
    return OGPU_OK;
}

int main(void)
{
    struct gla_virgl_transport t;
    char block[64];
    int r;
    CHECK(gla_os3_virgl_transport(&t), "the transport is there when the library says VIRGL is full");

    r = t.run(t.user, block, sizeof block);
    CHECK(r == 0 && nwaits == 1 && !waited_all, "a block waited for: done (%d, %d waits)", r, nwaits);

    nwaits = 0; answers[0] = OGPU_ERR_EXPIRED; nanswers = 1;
    r = t.run(t.user, block, sizeof block);
    CHECK(r == 0, "an expired fence is a block done, not failed (%d)", r);
    CHECK(waited_all == 1 && batches_done, "... after waiting for every batch so far (%d waits, %d for all)", nwaits, waited_all);

    nwaits = 0; waited_all = 0; answers[0] = OGPU_ERR_BADOP; nanswers = 1;
    r = t.run(t.user, block, sizeof block);
    CHECK(r != 0 && !waited_all, "any other error still fails the block (%d)", r);

    nwaits = 0; waited_all = 0; answers[0] = OGPU_ERR_DEVICE; nanswers = 1;
    r = t.run(t.user, block, sizeof block);
    CHECK(r != 0, "the device gone still fails it (%d)", r);

    CHECK(gla_counts.fences == 4, "every block counted (%lu)", gla_counts.fences);
    gla_os3_virgl_close();
    printf("test_gla_virgl_os3: %d of %d checks passed\n", checks - fails, checks);
    return fails != 0;
}
