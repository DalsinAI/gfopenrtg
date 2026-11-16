/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * tasks [NAME [LONGS]]: every task with its state, priority, the signals
 * it waits for and has, and its stack pointer; for tasks whose name holds
 * NAME, LONGS longwords from the stack pointer up and the hunks of the
 * process's program. A lab aid for hangs such as Amoebax's music change. */
#include <exec/execbase.h>
#include <exec/tasks.h>
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

/* gfopengpu.library's shared modules: its base holds a MinList of
   { MinNode; BPTR seg; hdr; users; DateStamp; size; replaced; name[] }. */
static void modules(void)
{
    struct Library *b;
    UBYTE *base;
    int off;
    Forbid();
    b = (struct Library *)FindName(&SysBase->LibList, (CONST_STRPTR)"gfopengpu.library");
    Permit();
    if (!b) { printf("no gfopengpu.library\n"); return; }
    base = (UBYTE *)b;
    for (off = 0; off < 4096; off += 2) {
        struct MinList *l = (struct MinList *)(base + off);
        struct MinNode *n = l->mlh_Head;
        if (!n || ((ULONG)n & 1) || TypeOfMem(n) == 0) continue;
        if (l->mlh_Tail != NULL || (APTR)n == (APTR)&l->mlh_Tail) continue;
        for (; n && n->mln_Succ && TypeOfMem(n); n = n->mln_Succ) {
            UBYTE *u = (UBYTE *)n;
            BPTR seg = *(BPTR *)(u + 8);
            const char *name = (const char *)(u + 37);
            if (!strstr(name, "module")) break;
            printf("module %s\n", name);
            while (seg) {
                ULONG *h = BADDR(seg);
                printf("   hunk at %08lx size %08lx\n", (unsigned long)(h + 1), (unsigned long)h[-1]);
                seg = *h;
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
    }
    return 0;
}
