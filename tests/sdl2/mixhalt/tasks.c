/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * tasks [NAME [LONGS]]: every task with its state, priority, the signals
 * it waits for and has, and its stack pointer; for tasks whose name holds
 * NAME, LONGS longwords from the stack pointer up and the hunks of the
 * process's program. A lab aid for hangs such as Amoebax's music change. */
#include <exec/execbase.h>
#include <exec/tasks.h>
#include <exec/semaphores.h>
#include <dos/dosextens.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern struct ExecBase *SysBase;

#define MAXT 96
struct row {
    struct Task *t;
    char name[40];
    UBYTE state;
    BYTE pri;
    ULONG wait, recvd;
    ULONG sp, lower, upper;
    ULONG stack[256];
    int nstack;
    ULONG hunks[16];
    int nhunks;
};
static struct row rows[MAXT];

static int has(const char *s, const char *pat) { return pat && *pat && strstr(s, pat); }

static void take(int *n, struct Task *t, const char *pat, int longs)
{
    struct row *r;
    if (*n >= MAXT) return;
    r = &rows[(*n)++];
    r->t = t;
    strncpy(r->name, t->tc_Node.ln_Name ? t->tc_Node.ln_Name : "?", sizeof r->name - 1);
    if (t->tc_Node.ln_Type == NT_PROCESS && ((struct Process *)t)->pr_CLI) {
        struct CommandLineInterface *c = BADDR(((struct Process *)t)->pr_CLI);
        UBYTE *b = BADDR(c->cli_CommandName);
        if (b && b[0]) {
            int k = b[0] < sizeof r->name - 2 ? b[0] : sizeof r->name - 2;
            r->name[0] = '>';
            memcpy(r->name + 1, b + 1, k);
            r->name[k + 1] = 0;
        }
    }
    r->state = t->tc_State;
    r->pri = t->tc_Node.ln_Pri;
    r->wait = t->tc_SigWait;
    r->recvd = t->tc_SigRecvd;
    r->sp = (ULONG)t->tc_SPReg;
    r->lower = (ULONG)t->tc_SPLower;
    r->upper = (ULONG)t->tc_SPUpper;
    r->nstack = 0;
    r->nhunks = 0;
    if (has(r->name, pat)) {
        ULONG *p = (ULONG *)r->sp;
        while (r->nstack < longs && (ULONG)p + 4 <= r->upper)
            r->stack[r->nstack++] = *p++;
        if (t->tc_Node.ln_Type == NT_PROCESS) {
            struct Process *pr = (struct Process *)t;
            struct CommandLineInterface *cli = BADDR(pr->pr_CLI);
            BPTR seg = cli ? cli->cli_Module : pr->pr_SegList;
            if (!cli && seg) seg = ((BPTR *)BADDR(seg))[3];
            while (seg && r->nhunks < 16) {
                ULONG *h = BADDR(seg);
                r->hunks[r->nhunks++] = (ULONG)(h + 1);
                seg = *h;
            }
        }
    }
}

/* gfopengpu.library's shared modules: its base (0.29) holds, at byte 2782,
   a MinList of { MinNode; BPTR seg; hdr; users; DateStamp; size; replaced;
   name[] }. */
#define MODULES_AT 2782
static void modules(void)
{
    struct Library *b;
    struct MinList *l;
    struct MinNode *n;
    int count = 0;
    Forbid();
    b = (struct Library *)FindName(&SysBase->LibList, (CONST_STRPTR)"gfopengpu.library");
    Permit();
    if (!b) { printf("no gfopengpu.library\n"); return; }
    printf("gfopengpu.library %u.%u at %08lx\n", b->lib_Version, b->lib_Revision, (unsigned long)b);
    l = (struct MinList *)((UBYTE *)b + MODULES_AT);
    if (l->mlh_Tail != NULL || !TypeOfMem(l->mlh_Head)) { printf("no module list there\n"); return; }
    for (n = l->mlh_Head; n->mln_Succ && count < 32; n = n->mln_Succ, count++) {
        UBYTE *u = (UBYTE *)n;
        BPTR seg = *(BPTR *)(u + 8);
        int k = 0;
        printf("module %.200s\n", (const char *)(u + 37));
        while (seg && k++ < 16) {
            ULONG *h = BADDR(seg);
            if (!TypeOfMem(h)) break;
            printf("   hunk at %08lx size %08lx\n", (unsigned long)(h + 1), (unsigned long)h[-1]);
            seg = *h;
        }
    }
}

/* The return addresses on a stack (a JSR or BSR just before) and the
   semaphores it points at, with their owners. */
static int is_code(ULONG v)
{
    return !(v & 1) && ((v >= 0x00f80010 && v < 0x01000000) || (v >= 0x00200010 && TypeOfMem((APTR)v) && TypeOfMem((APTR)(v - 8))));
}

static const char *call_before(ULONG v)
{
    UWORD *w = (UWORD *)v;
    if (w[-3] == 0x4eb9) return "jsr.l";
    if (w[-3] == 0x61ff) return "bsr.l";
    if ((w[-1] & 0xfff8) == 0x4e90) return "jsr(An)";
    if ((w[-2] & 0xfff8) == 0x4ea8 || w[-2] == 0x4eba) return "jsr d16";
    if ((w[-2] & 0xfff8) == 0x4eb0 || w[-2] == 0x4ebb) return "jsr idx";
    if (w[-2] == 0x6100) return "bsr.w";
    if ((w[-1] & 0xff00) == 0x6100 && (w[-1] & 0xff)) return "bsr.b";
    if ((w[-4] & 0xfff8) == 0x4eb0) return "jsr idx32";
    return NULL;
}

static void calls_and_sems(struct row *r)
{
    UBYTE *b = (UBYTE *)r->stack;
    int n = r->nstack * 4, i;
    for (i = 0; i + 4 <= n; i += 2) {
        ULONG v = ((ULONG)b[i] << 24) | ((ULONG)b[i + 1] << 16) | ((ULONG)b[i + 2] << 8) | b[i + 3];
        const char *c;
        if (is_code(v) && (c = call_before(v)) != NULL)
            printf("   +%03x ret %08lx (%s)\n", i, (unsigned long)v, c);
        else if (!(v & 1) && v >= 0x00200000 && TypeOfMem((APTR)v) && TypeOfMem((APTR)(v + 46))) {
            fflush(stdout);
            struct SignalSemaphore *ss = (struct SignalSemaphore *)v;
            if (ss->ss_Link.ln_Type == NT_SIGNALSEM) {
                struct MinNode *q;
                int k = 0;
                printf("   +%03x sem %08lx owner %08lx nest %d queue %d waiters",
                       i, (unsigned long)v, (unsigned long)ss->ss_Owner, ss->ss_NestCount, ss->ss_QueueCount);
                for (q = ss->ss_WaitQueue.mlh_Head; q && q->mln_Succ && TypeOfMem(q) && k < 8; q = q->mln_Succ, k++)
                    printf(" %08lx@%08lx", (unsigned long)((struct SemaphoreRequest *)q)->sr_Waiter, (unsigned long)q);
                if (k == 8) printf(" ...");
                printf("\n");
            }
        }
    }
}

int main(int argc, char **argv)
{
    const char *pat = argc > 1 ? argv[1] : NULL;
    if (pat && !strcmp(pat, "MODULES")) { modules(); return 0; }
    if (pat && !strcmp(pat, "PEEK") && argc > 3) {
        UBYTE *a = (UBYTE *)strtoul(argv[2], NULL, 16);
        int k, len = atoi(argv[3]);
        for (k = 0; k < len; k++)
            printf("%s%02x", k % 32 ? "" : (k ? "\n" : ""), a[k]);
        printf("\n");
        return 0;
    }
    int longs = argc > 2 ? atoi(argv[2]) : 64;
    int n = 0, i, j;
    struct Node *nd;
    if (longs > 256) longs = 256;
    Forbid();
    take(&n, FindTask(NULL), pat, longs);
    for (nd = SysBase->TaskReady.lh_Head; nd->ln_Succ; nd = nd->ln_Succ)
        take(&n, (struct Task *)nd, pat, longs);
    for (nd = SysBase->TaskWait.lh_Head; nd->ln_Succ; nd = nd->ln_Succ)
        take(&n, (struct Task *)nd, pat, longs);
    Permit();
    for (i = 0; i < n; i++) {
        struct row *r = &rows[i];
        printf("%08lx %-32s st%u pri%4d wait %08lx got %08lx sp %08lx [%08lx-%08lx]\n",
               (unsigned long)r->t, r->name, r->state, r->pri, (unsigned long)r->wait,
               (unsigned long)r->recvd, (unsigned long)r->sp, (unsigned long)r->lower,
               (unsigned long)r->upper);
        for (j = 0; j < r->nhunks; j++)
            printf("   hunk %d at %08lx\n", j, (unsigned long)r->hunks[j]);
        for (j = 0; j < r->nstack; j++)
            printf("%s%08lx", j % 8 ? " " : "\n   ", (unsigned long)r->stack[j]);
        if (r->nstack) printf("\n");
        if (r->nstack) calls_and_sems(r);
    }
    return 0;
}
