/* Copyright (c) 2026 AmiCradle Team. OpenGPU's GL module, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * The Amiga's side of the softpipe notice (gla_notice_os3.h): the
 * environment, the program's output, the runtime's log, and what the ACRTG
 * board says it has. */
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/ports.h>
#include <exec/io.h>
#include <exec/tasks.h>
#include <dos/dos.h>
#include <dos/var.h>
#include <libraries/configvars.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/expansion.h>
#include "../../../sdl2/satellites/include/devices/gfopenservice.h"
#include "gla_notice_os3.h"

/* The ACRTG board (amicradle-guest common/protocol/acrtg.h), as
 * library/openrtg_lib.c finds it. */
#define AMICRADLE_MFR       0xDA15
#define AMICRADLE_MFR_OLD   2011                /* before 1 October 2026 */
#define ACRTG_PAGE          0x10000UL           /* the register page: the board's last 64 KiB */
#define R_VERSION           0x04
#define R_GPU_INFO          0xD0
#define GPU_INFO_RING       0x1UL
#define GPU_INFO_VIRGL      0x4UL

#define LOG_SERVICE "log/1"                     /* the runtime's log (gfosmotherboards' services card) */
#define LOG_OP_LINE 1
#define LOG_TICKS   100                         /* 2 s at most for the line, then it is given up */

static unsigned long len(const char *s)
{
    unsigned long n = 0;
    while (s[n]) n++;
    return n;
}

int gla_os3_getenv(const char *name, char *buf, int size)
{
    if (!DOSBase) return -1;
    return GetVar((CONST_STRPTR)name, (STRPTR)buf, size, 0);
}

/* One request on the device, waited for at most LOG_TICKS; its status. */
static LONG run(struct OSRequest *io)
{
    ULONG ticks = 0;
    io->os_Status = OSERR_LOST;
    SendIO((struct IORequest *)io);
    while (!CheckIO((struct IORequest *)io) && ticks++ < LOG_TICKS) Delay(1);
    if (!CheckIO((struct IORequest *)io)) AbortIO((struct IORequest *)io);
    WaitIO((struct IORequest *)io);
    return io->os_Status;
}

static void clear(struct OSRequest *io)
{
    int i;
    io->os_Flags = 0;
    io->os_Arg = 0;
    for (i = 0; i < 4; i++) {
        io->os_Buf[i].ob_Data = NULL;
        io->os_Buf[i].ob_Length = 0;
        io->os_Extra[i] = 0;
    }
}

/* The line into the runtime's log through the services card's log/1. A
 * machine without gfopenservice.device, or a runtime without log/1 (before
 * 7 November 2026), says no, and the line is only on the output. */
static void runtime_log(const char *text)
{
    struct MsgPort *port = CreateMsgPort();
    struct OSRequest *io = port ? (struct OSRequest *)CreateIORequest(port, sizeof *io) : NULL;
    if (io && OpenDevice((CONST_STRPTR)OPENSERVICE_NAME, 0, (struct IORequest *)io, 0) == 0) {
        clear(io);
        io->os_Req.io_Command = OSCMD_OPEN;
        io->os_Buf[0].ob_Data = (APTR)LOG_SERVICE;
        io->os_Buf[0].ob_Length = sizeof LOG_SERVICE - 1;
        if (run(io) == OSERR_OK) {
            UWORD handle = (UWORD)io->os_Result;
            clear(io);
            io->os_Req.io_Command = OSCMD_CALL;
            io->os_Service = handle;
            io->os_Op = LOG_OP_LINE;
            io->os_Buf[0].ob_Data = (APTR)text;
            io->os_Buf[0].ob_Length = len(text);
            (void)run(io);
            clear(io);
            io->os_Req.io_Command = OSCMD_CLOSE;
            io->os_Service = handle;
            (void)run(io);
        }
        CloseDevice((struct IORequest *)io);
    }
    if (io) DeleteIORequest((struct IORequest *)io);
    if (port) DeleteMsgPort(port);
}

void gla_os3_say(const char *line)
{
    char text[480];
    unsigned long n = 0, k;
    BPTR out;
    if (!DOSBase) return;
    if ((out = Output()) != 0) {
        Write(out, (APTR)line, (LONG)len(line));
        Write(out, (APTR)"\n", 1);
    }
    /* "Program: GL: ..." in the log, so a log of several programs says whose */
    text[0] = 0;
    if (!GetProgramName((STRPTR)text, 64) || !text[0]) {
        struct Task *me = FindTask(NULL);
        const char *nm = me->tc_Node.ln_Name ? me->tc_Node.ln_Name : "?";
        for (k = 0; nm[k] && k < 63; k++) text[k] = nm[k];
        text[k] = 0;
    }
    n = len(text);
    text[n++] = ':';
    text[n++] = ' ';
    for (k = 0; line[k] && n < sizeof text - 1; k++) text[n++] = line[k];
    text[n] = 0;
    runtime_log(text);
}

const char *gla_os3_board_why(void)
{
    static const ULONG makers[2] = { AMICRADLE_MFR, AMICRADLE_MFR_OLD };
    static const ULONG products[3] = { 9, 15, 16 };
    static const ULONG sizes[3] = { 0x01000000UL, 0x04000000UL, 0x10000000UL };
    struct Library *ExpansionBase = OpenLibrary((CONST_STRPTR)"expansion.library", 37);
    struct ConfigDev *cd = NULL;
    ULONG size = 0, version, info;
    volatile ULONG *regs;
    int k, p;
    if (ExpansionBase) {
        for (k = 0; k < 2 && !cd; k++)
            for (p = 0; p < 3 && !cd; p++)
                if ((cd = FindConfigDev(NULL, makers[k], products[p])) != NULL && cd->cd_BoardAddr) size = sizes[p];
                else cd = NULL;
        CloseLibrary(ExpansionBase);
    }
    if (!cd) return "there's no ACRTG board (a real Amiga, or a machine without AmiCradle's RTG board)";
    regs = (volatile ULONG *)((UBYTE *)cd->cd_BoardAddr + size - ACRTG_PAGE);
    version = regs[R_VERSION / 4];
    if (version < 3) return "the runtime's ACRTG board is older than acrtg-v3 and has no command ring";
    info = regs[R_GPU_INFO / 4];
    if (!(info & GPU_INFO_RING)) return "the ACRTG board's command ring isn't there";
    if (!(info & GPU_INFO_VIRGL)) return "the runtime has no virglrenderer (the ACRTG board doesn't offer virgl; the runtime's log says why)";
    return NULL;
}
