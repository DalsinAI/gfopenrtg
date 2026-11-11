/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT */
/* MGLCount: which minigl.library calls a program makes, how often, and how
 * much of the time goes inside each, so the busiest ones get host bodies
 * first (Team 6's MiniGL work, ranked from GLQuake).
 *
 *   MGLCount SECONDS/N,TO/K,TICK/N
 *   MGLCount SECONDS 40 TO RAM:mglcount.txt
 *
 * A program calls MiniGL through the table minigl.library's -30 hands out
 * (MiniGLDispatch->glBegin(...)), not through library vectors, so LibCount
 * and the runtime's AC_LVOCOUNT don't see these calls. MGLCount puts a stub
 * in each of the table's 205 entries:
 *
 *   ADDQ.L  #1,count[n]           the call
 *   MOVE.L  (SP)+,ret             the caller's return, kept
 *   MOVE.W  #n,cur                "inside entry n"
 *   JSR     original              the arguments are where they were, less the return
 *   MOVE.W  #-1,cur               back out
 *   MOVE.L  ret,-(SP)
 *   RTS
 *
 * D0, D1 and FP0 (the results) are left as the original left them. The time
 * comes from sampling: this task, at priority 21, wakes every TICK
 * microseconds (default 1000) from timer.device and adds one to the entry
 * cur names, or to "outside". So the time column is the share of samples
 * taken while the program was inside that entry, its waits for the GPU
 * included.
 *
 * Limits: one GL program at a time (ret and cur are single; two tasks
 * calling MiniGL at once would mix them up), and an entry that never
 * returns (longjmp) leaves cur set. The stubs go in under Forbid() with the
 * caches cleared; at the end each entry gets its original back only if it
 * still holds our stub, and the stubs' memory is never freed (a call may
 * still be inside one), about 7 KB. Ctrl-C ends the count early. */
#include <exec/execbase.h>
#include <exec/memory.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <string.h>

#include "mglcount_names.h"

static const char ver[] __attribute__((used)) = "$VER: MGLCount 1.0 (11.11.2026) GFOpenRTG";

#define TABLE_ABI     5          /* MGL_DISPATCH_ABI */
#define FIRST_ENTRY   20         /* entry 1's offset in the table */
#define STUB_WORDS    16         /* 32 bytes */

struct Shared {                   /* in public memory, with the stubs: never freed */
    ULONG count[MGLC_ENTRIES];
    ULONG samples[MGLC_ENTRIES + 1];   /* the last: outside every entry */
    APTR ret;
    UWORD cur;
    UWORD pad;
    UWORD exit[8];
    UWORD stubs[MGLC_ENTRIES][STUB_WORDS];
};

static void put32(UWORD *w, ULONG v) { w[0] = (UWORD)(v >> 16); w[1] = (UWORD)v; }

/* One stub: 16 words. Its JSR returns to the JMP, which goes on to the
 * shared exit (MOVE.W #-1,cur; MOVE.L ret,-(SP); RTS). */
static void make_stub(struct Shared *s, int n, APTR orig, const UWORD *exit)
{
    UWORD *w = s->stubs[n];
    w[0] = 0x52B9; put32(w + 1, (ULONG)&s->count[n]);    /* ADDQ.L #1,(abs).L */
    w[3] = 0x23DF; put32(w + 4, (ULONG)&s->ret);         /* MOVE.L (A7)+,(abs).L */
    w[6] = 0x33FC; w[7] = (UWORD)n; put32(w + 8, (ULONG)&s->cur);   /* MOVE.W #n,(abs).L */
    w[10] = 0x4EB9; put32(w + 11, (ULONG)orig);          /* JSR (abs).L */
    w[13] = 0x4EF9; put32(w + 14, (ULONG)exit);          /* JMP (abs).L */
}

static void make_exit(struct Shared *s)
{
    UWORD *w = s->exit;
    w[0] = 0x33FC; w[1] = 0xFFFF; put32(w + 2, (ULONG)&s->cur);   /* MOVE.W #-1,(abs).L */
    w[4] = 0x2F39; put32(w + 5, (ULONG)&s->ret);                  /* MOVE.L (abs).L,-(A7) */
    w[7] = 0x4E75;                                                 /* RTS */
}

static APTR get_table(struct Library *base)
{
    register APTR res __asm("d0");
    register struct Library *a6 __asm("a6") = base;
    __asm volatile ("jsr -30(%%a6)" : "=r"(res) : "r"(a6) : "d1", "a0", "a1", "memory", "cc");
    return res;
}

int main(void)
{
    LONG args[3] = { 0, 0, 0 };
    struct RDArgs *rd = ReadArgs((STRPTR)"SECONDS/N,TO/K,TICK/N", args, NULL);
    struct Library *mgl;
    ULONG *table;
    struct Shared *s;
    struct MsgPort *port;
    struct timerequest *tr;
    LONG seconds, tick, n, still = 0, oldpri;
    ULONG total = 0, inside = 0, calls = 0;
    struct DateStamp t0, t1;
    LONG ms;
    BPTR out;

    if (!rd) { PrintFault(IoErr(), (STRPTR)"MGLCount"); return RETURN_FAIL; }
    seconds = args[0] ? *(LONG *)args[0] : 30;
    tick = args[2] ? *(LONG *)args[2] : 1000;
    if (tick < 200) tick = 200;

    mgl = OpenLibrary((STRPTR)"minigl.library", 0);
    if (!mgl) { PutStr((STRPTR)"MGLCount: minigl.library isn't there\n"); FreeArgs(rd); return RETURN_FAIL; }
    table = (ULONG *)get_table(mgl);
    if (!table || table[0] != TABLE_ABI || table[1] < FIRST_ENTRY + 4 * MGLC_ENTRIES) {
        Printf((STRPTR)"MGLCount: the table isn't ABI %ld with %ld entries; nothing changed\n", (LONG)TABLE_ABI, (LONG)MGLC_ENTRIES);
        CloseLibrary(mgl); FreeArgs(rd); return RETURN_FAIL;
    }
    s = AllocMem(sizeof *s, MEMF_PUBLIC | MEMF_CLEAR);
    port = CreateMsgPort();
    tr = port ? (struct timerequest *)CreateIORequest(port, sizeof *tr) : NULL;
    if (!s || !tr || OpenDevice((STRPTR)TIMERNAME, UNIT_MICROHZ, (struct IORequest *)tr, 0)) {
        PutStr((STRPTR)"MGLCount: no memory or no timer.device\n");
        if (tr) DeleteIORequest((struct IORequest *)tr);
        if (port) DeleteMsgPort(port);
        if (s) FreeMem(s, sizeof *s);
        CloseLibrary(mgl); FreeArgs(rd); return RETURN_FAIL;
    }
    s->cur = 0xFFFF;
    make_exit(s);

    Forbid();
    for (n = 0; n < MGLC_ENTRIES; n++)
        if (table[FIRST_ENTRY / 4 + n]) make_stub(s, n, (APTR)table[FIRST_ENTRY / 4 + n], s->exit);
    CacheClearU();
    for (n = 0; n < MGLC_ENTRIES; n++)
        if (table[FIRST_ENTRY / 4 + n]) table[FIRST_ENTRY / 4 + n] = (ULONG)s->stubs[n];
    CacheClearU();
    Permit();
    Printf((STRPTR)"MGLCount: counting %ld entries for %ld s, a sample every %ld us\n", (LONG)MGLC_ENTRIES, seconds, tick);

    oldpri = SetTaskPri(FindTask(NULL), 21);
    DateStamp(&t0);
    for (;;) {
        tr->tr_node.io_Command = TR_ADDREQUEST;
        tr->tr_time.tv_secs = 0;
        tr->tr_time.tv_micro = (ULONG)tick;
        DoIO((struct IORequest *)tr);
        {
            UWORD c = s->cur;
            s->samples[c < MGLC_ENTRIES ? c : MGLC_ENTRIES]++;
        }
        if ((++total & 63) == 0) {
            if (SetSignal(0, 0) & SIGBREAKF_CTRL_C) { SetSignal(0, SIGBREAKF_CTRL_C); break; }
            DateStamp(&t1);
            ms = (t1.ds_Days - t0.ds_Days) * 86400000 + (t1.ds_Minute - t0.ds_Minute) * 60000 + (t1.ds_Tick - t0.ds_Tick) * 20;
            if (ms >= seconds * 1000) break;
        }
    }
    SetTaskPri(FindTask(NULL), oldpri);
    DateStamp(&t1);
    ms = (t1.ds_Days - t0.ds_Days) * 86400000 + (t1.ds_Minute - t0.ds_Minute) * 60000 + (t1.ds_Tick - t0.ds_Tick) * 20;

    Forbid();
    for (n = 0; n < MGLC_ENTRIES; n++) {
        ULONG *e = &table[FIRST_ENTRY / 4 + n];
        if (*e == (ULONG)s->stubs[n]) {
            /* the original is the JSR's target */
            UWORD *w = s->stubs[n];
            *e = (ULONG)w[11] << 16 | w[12];
        } else if (*e) still++;
    }
    CacheClearU();
    Permit();

    for (n = 0; n < MGLC_ENTRIES; n++) { calls += s->count[n]; inside += s->samples[n]; }
    out = args[1] ? Open((STRPTR)args[1], MODE_NEWFILE) : Output();
    if (out) {
        FPrintf(out, (STRPTR)"# MGLCount: %ld ms, %lu samples (%lu inside MiniGL), %lu calls\n", ms, total, inside, calls);
        FPrintf(out, (STRPTR)"# entry name calls samples\n");
        for (n = 0; n < MGLC_ENTRIES; n++)
            if (s->count[n] || s->samples[n])
                FPrintf(out, (STRPTR)"%ld %s %lu %lu\n", n + 1, (LONG)mglc_names[n], s->count[n], s->samples[n]);
        FPrintf(out, (STRPTR)"0 outside 0 %lu\n", s->samples[MGLC_ENTRIES]);
        if (args[1]) Close(out);
    }
    if (still) Printf((STRPTR)"MGLCount: %ld entries were changed after ours and keep their new value\n", still);
    CloseDevice((struct IORequest *)tr);
    DeleteIORequest((struct IORequest *)tr);
    DeleteMsgPort(port);
    CloseLibrary(mgl);
    FreeArgs(rd);
    return RETURN_OK;   /* the stubs, the shared block and the exit stay: a call may still be in one */
}
