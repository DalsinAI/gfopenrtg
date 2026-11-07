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
 * still fail the block.
 *
 * Then which driver a program gets (7 Nov 2026): ENV:GFOpenGPU/GLDriver gpu
 * refuses softpipe, cpu takes it without trying virgl, and a program that
 * lands on softpipe is told why in one line (on its output and in the
 * runtime's log: gla_notice_os3.c's, faked here), once. */
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
#include "gla/os3/gla_notice_os3.h"

struct gla_stats gla_counts;

/* gla_core.c's softpipe hook and its reason (gla_core.h), as the core keeps them */
static gla_softpipe_hook_fn hook;
static const char *noted;
void gla_set_softpipe_hook(gla_softpipe_hook_fn h) { hook = h; }
void gla_note_why(const char *why) { noted = why; }
const char *gla_last_why(void) { return noted; }

/* gla_notice_os3.c's three, faked: the environment, what was said, the board */
static const char *env_gldriver, *env_ogles3;
static char said[8][512];
static int nsaid;
static const char *board_says;
int gla_os3_getenv(const char *name, char *buf, int size)
{
    const char *v = !strcmp(name, "GFOpenGPU/GLDriver") ? env_gldriver : !strcmp(name, "OGLES3/Driver") ? env_ogles3 : NULL;
    if (!v) return -1;
    snprintf(buf, (size_t)size, "%s", v);
    return (int)strlen(buf);
}
void gla_os3_say(const char *line) { if (nsaid < 8) snprintf(said[nsaid], sizeof said[0], "%s", line); nsaid++; }
const char *gla_os3_board_why(void) { return board_says; }
static int virgl_full = 1;
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
ULONG OGPU_Query(ULONG op, ULONG format) { (void)format; return op == OGPU_OP_VIRGL && virgl_full ? (ULONG)OGPU_FULL | (1UL << 8) : 0; }
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

    /* ---- which driver, and the notice ---- */
    CHECK(hook != NULL, "the transport registered the softpipe hook");
    CHECK(gla_os3_driver_of("gpu") == GLA_OS3_DRIVER_GPU && gla_os3_driver_of(" VIRGL\n") == GLA_OS3_DRIVER_GPU &&
          gla_os3_driver_of("Cpu") == GLA_OS3_DRIVER_CPU && gla_os3_driver_of("softpipe") == GLA_OS3_DRIVER_CPU &&
          gla_os3_driver_of("auto") == GLA_OS3_DRIVER_AUTO && gla_os3_driver_of("") == GLA_OS3_DRIVER_AUTO &&
          gla_os3_driver_of("gpus") == GLA_OS3_DRIVER_AUTO && gla_os3_driver_of(NULL) == GLA_OS3_DRIVER_AUTO,
          "GLDriver: gpu or virgl, cpu or softpipe, in any case; anything else is auto");

    /* gpu, and no virgl: the transport says no, why is the board's, softpipe is refused and said so */
    env_gldriver = "gpu"; virgl_full = 0; board_says = "there's no ACRTG board"; noted = NULL; nsaid = 0;
    CHECK(!gla_os3_virgl_transport(&t), "gpu, no virgl: no transport");
    CHECK(noted && !strcmp(noted, board_says), "... the board's reason noted (%s)", noted ? noted : "none");
    CHECK(hook(noted) == 1, "... softpipe refused");
    CHECK(nsaid == 1 && strstr(said[0], "softpipe refused") && strstr(said[0], "GLDriver is gpu") && strstr(said[0], board_says),
          "... in one line naming why (%s)", said[0]);
    board_says = NULL;
    CHECK(!gla_os3_virgl_transport(&t) && noted && strstr(noted, "ACRTG.gpu"),
          "a board with virgl but a library without: the driver named (%s)", noted ? noted : "none");

    /* gpu, virgl there, but the program asks for softpipe (ogles3's OGLES3/Driver CPU, or none): refused, said why */
    virgl_full = 1; nsaid = 0; env_ogles3 = "CPU";
    CHECK(hook(NULL) == 1 && nsaid == 1 && strstr(said[0], "OGLES3/Driver is CPU"), "OGLES3/Driver CPU under gpu: refused (%s)", said[0]);
    env_ogles3 = NULL; nsaid = 0;
    CHECK(hook(NULL) == 1 && nsaid == 1 && strstr(said[0], "the program asked for softpipe"), "a program straight to softpipe: refused (%s)", said[0]);

    /* cpu: no virgl asked for, softpipe taken, said once */
    env_gldriver = "cpu"; nsaid = 0; noted = NULL;
    CHECK(!gla_os3_virgl_transport(&t) && noted && strstr(noted, "GLDriver is cpu"), "cpu: no transport, though virgl is there");
    CHECK(hook(noted) == 0, "... softpipe allowed");
    CHECK(nsaid == 1 && strstr(said[0], "on softpipe, not virgl: ENV:GFOpenGPU/GLDriver is cpu") && strstr(said[0], "68k"),
          "... in one line (%s)", said[0]);
    CHECK(hook(noted) == 0 && nsaid == 1, "... and only once a program (%d lines)", nsaid);

    /* no switch: virgl when it is there */
    env_gldriver = "whatever"; nsaid = 0;
    CHECK(gla_os3_virgl_transport(&t), "auto: the transport, when virgl is there");
    CHECK(nsaid == 0, "... and nothing said");
    printf("test_gla_virgl_os3: %d of %d checks passed\n", checks - fails, checks);
    return fails != 0;
}
