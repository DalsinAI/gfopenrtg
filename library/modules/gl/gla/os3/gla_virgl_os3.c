/* Copyright (c) 2026 AmiCradle Team. OpenGPU's GL module, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * virgl's transport on AmigaOS (gla_virgl_os3.h). The host reaches Fast RAM
 * by the Amiga's own addresses, so memory for it is AllocVec's, an address is
 * the pointer, and a request block is one OGPU_OP_VIRGL command in a batch,
 * done when OGPU_Wait returns. */
#include "gla_virgl_os3.h"

#include <exec/types.h>
#include <exec/memory.h>
#include <devices/timer.h>
#include <proto/exec.h>
#define TIMER_BASE_NAME gla_timer_base     /* not TimerBase: a program linking this may have its own */
#include <proto/timer.h>
#include <proto/gfopengpu.h>
#include <gfopengpu/build.h>
#include "gla_notice_os3.h"
#include "../gla_virgl.h"                  /* gla_virgl_batch_wanted */

struct Library *OpenGPUBase;

static void *os3_alloc(void *user, unsigned long bytes)
{
    (void)user;
    return AllocVec(bytes ? bytes : 4, MEMF_ANY | MEMF_CLEAR);
}

static void os3_free(void *user, void *p)
{
    (void)user;
    if (p) FreeVec(p);
}

static unsigned long os3_addr(void *user, const void *p)
{
    (void)user;
    return (unsigned long)p;
}

/* The E clock, to time the waits, when timer.device lends its base. */
static struct Device *gla_timer_base;
static struct timerequest timer_io;
static ULONG eclock_hz;

/* A block sent, not waited for (gla_virgl.h's gla_virgl_send): its fence
 * is the ticket. */
static int os3_send(void *user, void *block, unsigned long bytes, unsigned long *ticket)
{
    ULONG stream[4], fence = 0;
    struct OGPUBatch b;
    (void)user;
    ogpu_batch_init(&b, stream, 4);
    ogpu_virgl(&b, (unsigned long)block, bytes);
    if (OGPU_Submit(stream, (ULONG)b.words, &fence) != OGPU_OK) return -1;
    *ticket = fence;
    return 0;
}

/* ... and waited for (gla_virgl_wait); os3_run is both. */
static int os3_wait(void *user, unsigned long ticket)
{
    ULONG fence = (ULONG)ticket;
    struct EClockVal t0, t1;
    LONG r;
    (void)user;
    if (eclock_hz) ReadEClock(&t0);
    r = OGPU_Wait(fence);
    if (r == OGPU_ERR_EXPIRED) {
        /* Sixteen or more batches of other programs came between our submit
         * and this wait (three GL programs at once, the compositor), so the
         * library no longer keeps our batch's result: the batch is done, not
         * failed (gfopengpu.library waits for the newest; the driver may
         * not have, so wait for everything so far, which holds ours). Each
         * command's own result is in the block. Taken as a failure, a
         * resource the host had made was freed here and its handle (its
         * address) came again (the host's context then refused the stream:
         * "Illegal command buffer"), and a program starting beside others
         * fell back to softpipe, its shaders on the 68k. */
        r = OGPU_Wait(OGPU_FENCE_ALL);
        if (r == OGPU_ERR_EXPIRED) r = OGPU_OK;
    }
    gla_counts.fences++;
    if (eclock_hz) {
        ReadEClock(&t1);
        /* the low words suffice: a wait is far shorter than 2^32 ticks */
        gla_counts.fence_wait_us += (ULONG)(((unsigned long long)(t1.ev_lo - t0.ev_lo) * 1000000u) / eclock_hz);
    }
    return r == OGPU_OK ? 0 : -1;
}

static int os3_run(void *user, void *block, unsigned long bytes)
{
    unsigned long ticket;
    if (os3_send(user, block, bytes, &ticket)) return -1;
    return os3_wait(user, ticket);
}

/* ---- which driver, and saying so when it is softpipe (7 Nov 2026) ----
 * A program that couldn't have virgl used to drop to softpipe in silence,
 * its shaders then running on the 68k: GL at a frame a second where the
 * PC's graphics chip gives sixty, and nothing said why. Now softpipe starts
 * with one line, on the program's output and in the runtime's log
 * (gla_notice_os3.c), naming why virgl wasn't used. ENV:GFOpenGPU/GLDriver
 * chooses: "gpu" (or "virgl") refuses softpipe, so the program fails where
 * it would have run on the 68k; "cpu" (or "softpipe") takes softpipe
 * without trying virgl; anything else, or none, is virgl when it's there
 * and softpipe when not. ENV:OGLES3/Driver CPU still makes ogles3.library
 * take softpipe (it doesn't ask for virgl), unless GLDriver says gpu. */

static const char why_env_cpu[] = "ENV:GFOpenGPU/GLDriver is cpu";
static const char why_ogles3_cpu[] = "ENV:OGLES3/Driver is CPU";
static const char why_asked[] = "the program asked for softpipe";
static const char why_no_library[] = "gfopengpu.library didn't open";
static const char why_no_driver[] = "gfopengpu.library has no driver that carries virgl "
    "(LIBS:GFOpenGPU/ACRTG.gpu missing or too old, or the board stopped answering)";

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; }

/* "gpu" or "cpu" at the start of v, in any case; "virgl" and "softpipe" too. */
static int word_is(const char *v, const char *w)
{
    while (*v == ' ' || *v == '\t') v++;
    for (; *w; v++, w++)
        if (lower((unsigned char)*v) != *w) return 0;
    return !*v || *v == ' ' || *v == '\t' || *v == '\n' || *v == '\r';
}

int gla_os3_driver_of(const char *v)
{
    if (!v) return GLA_OS3_DRIVER_AUTO;
    if (word_is(v, "gpu") || word_is(v, "virgl")) return GLA_OS3_DRIVER_GPU;
    if (word_is(v, "cpu") || word_is(v, "softpipe")) return GLA_OS3_DRIVER_CPU;
    return GLA_OS3_DRIVER_AUTO;
}

int gla_os3_gl_driver(void)
{
    char v[16];
    return gla_os3_getenv("GFOpenGPU/GLDriver", v, sizeof v) > 0 ? gla_os3_driver_of(v) : GLA_OS3_DRIVER_AUTO;
}

/* Why gfopengpu.library offers no virgl; NULL when it does. */
static const char *library_why(void)
{
    const char *board;
    if (!OpenGPUBase && !(OpenGPUBase = OpenLibrary((CONST_STRPTR)OPENGPU_NAME, 0))) return why_no_library;
    if (OGPU_ANSWER(OGPU_Query(OGPU_OP_VIRGL, 0)) == OGPU_FULL) return NULL;
    board = gla_os3_board_why();        /* no board, no ring, no virglrenderer */
    return board ? board : why_no_driver;
}

static void cat(char *d, unsigned long size, const char *s)
{
    unsigned long n = 0;
    while (n < size - 1 && d[n]) n++;
    while (n < size - 1 && *s) d[n++] = *s++;
    d[n] = 0;
}

static int told;                        /* the notice is given once a program (each has its own data) */

/* gla_display_create's hook (gla_core.h): why, then refuse softpipe when
 * GLDriver says gpu. */
static int softpipe_hook(const char *why)
{
    char line[400];
    int drv = gla_os3_gl_driver();
    if (!why) {
        /* The program went to softpipe without asking for virgl, or asked
         * a GL.module that wasn't told why. */
        char v[8];
        if (drv == GLA_OS3_DRIVER_CPU) why = why_env_cpu;
        else if (gla_os3_getenv("OGLES3/Driver", v, sizeof v) > 0 && (v[0] == 'C' || v[0] == 'c')) why = why_ogles3_cpu;
        else if (!(why = library_why())) why = why_asked;
    }
    line[0] = 0;
#if !GLA_HAVE_SOFTPIPE
    /* GL_DRIVERS=virgl: no softpipe to fall back to, whatever GLDriver says. */
    cat(line, sizeof line, "GL: no virgl, and this GL.module has no softpipe (built GL_DRIVERS=virgl): ");
    cat(line, sizeof line, why);
    gla_os3_say(line);
    return 1;
#endif
    if (drv == GLA_OS3_DRIVER_GPU) {
        cat(line, sizeof line, "GL: softpipe refused, as ENV:GFOpenGPU/GLDriver is gpu; no virgl: ");
        cat(line, sizeof line, why);
        gla_os3_say(line);
        return 1;
    }
    if (!told) {
        told = 1;
        cat(line, sizeof line, "GL: on softpipe, not virgl: ");
        cat(line, sizeof line, why);
        cat(line, sizeof line, ". Shaders run on the 68k (ENV:GFOpenGPU/GLDriver gpu refuses softpipe).");
        gla_os3_say(line);
    }
    return 0;
}

void gla_os3_driver_init(void)
{
    gla_set_softpipe_hook(softpipe_hook);
}

int gla_os3_virgl_transport(struct gla_virgl_transport *t)
{
    const char *why;
    gla_os3_driver_init();
    if (gla_os3_gl_driver() == GLA_OS3_DRIVER_CPU) {
        gla_note_why(why_env_cpu);
        return 0;
    }
    if ((why = library_why()) != NULL) {
        gla_note_why(why);
        return 0;
    }
    {
        /* ENV:GFOpenGPU/GLBatch: 0 (or off) a block a command, 1 (or on)
         * batched whatever the runtime asks; unset as the runtime asks */
        char v[8];
        gla_virgl_batch_wanted = -1;
        if (gla_os3_getenv("GFOpenGPU/GLBatch", v, sizeof v) > 0) {
            if (v[0] == '0' || word_is(v, "off")) gla_virgl_batch_wanted = 0;
            else if (v[0] == '1' || word_is(v, "on")) gla_virgl_batch_wanted = 1;
        }
        /* ENV:GFOpenGPU/GLPipeline: the same for blocks sent without waiting */
        gla_virgl_pipeline_wanted = -1;
        if (gla_os3_getenv("GFOpenGPU/GLPipeline", v, sizeof v) > 0) {
            if (v[0] == '0' || word_is(v, "off")) gla_virgl_pipeline_wanted = 0;
            else if (v[0] == '1' || word_is(v, "on")) gla_virgl_pipeline_wanted = 1;
        }
        gla_virgl_send = os3_send;
        gla_virgl_wait = os3_wait;
    }
    if (!gla_timer_base && !OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_ECLOCK, (struct IORequest *)&timer_io, 0)) {
        struct EClockVal e;
        gla_timer_base = timer_io.tr_node.io_Device;   /* until gla_os3_virgl_close */
        eclock_hz = ReadEClock(&e);
    }
    t->alloc = os3_alloc;
    t->free = os3_free;
    t->addr = os3_addr;
    t->run = os3_run;
    t->user = NULL;
    return 1;
}

void gla_os3_virgl_close(void)
{
    if (gla_timer_base) CloseDevice((struct IORequest *)&timer_io);
    gla_timer_base = NULL;
    eclock_hz = 0;
}
