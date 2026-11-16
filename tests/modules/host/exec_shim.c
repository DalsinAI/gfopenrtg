/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * Host stand-ins for the exec calls module_malloc.c makes. A task that
 * waits for a semaphore longer than WAIT_S seconds is taken as hung: the
 * test prints HANG and ends with 3. Allocate and Deallocate are exec's
 * first fit over a MemHeader's chunk list, in units of a MemChunk. */
#include <proto/exec.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define WAIT_S 2
#define UNIT ((ULONG)sizeof(struct MemChunk))

void InitSemaphore(struct SignalSemaphore *s)
{
    pthread_mutex_init(&s->m, NULL);
    pthread_cond_init(&s->c, NULL);
    s->nest = 0;
}

void ObtainSemaphore(struct SignalSemaphore *s)
{
    struct timespec until;
    pthread_t me = pthread_self();
    clock_gettime(CLOCK_REALTIME, &until);
    until.tv_sec += WAIT_S;
    pthread_mutex_lock(&s->m);
    while (s->nest > 0 && !pthread_equal(s->owner, me)) {
        if (pthread_cond_timedwait(&s->c, &s->m, &until) == ETIMEDOUT) {
            printf("HANG: a task waited %d s for the memory lock (held %d deep)\n", WAIT_S, s->nest);
            fflush(stdout);
            _exit(3);
        }
    }
    s->owner = me;
    s->nest++;
    pthread_mutex_unlock(&s->m);
}

void ReleaseSemaphore(struct SignalSemaphore *s)
{
    pthread_mutex_lock(&s->m);
    if (s->nest <= 0 || !pthread_equal(s->owner, pthread_self())) {
        printf("BAD: release by a task that doesn't hold the lock\n");
        _exit(4);
    }
    if (--s->nest == 0) pthread_cond_broadcast(&s->c);
    pthread_mutex_unlock(&s->m);
}

APTR AllocMem(ULONG bytes, ULONG flags)
{
    (void)flags;
    return aligned_alloc(64, (bytes + 63) & ~63UL);
}

void FreeMem(APTR p, ULONG bytes)
{
    (void)bytes;
    free(p);
}

static ULONG round_up(ULONG n) { return (n + UNIT - 1) & ~(UNIT - 1); }

APTR Allocate(struct MemHeader *mh, ULONG bytes)
{
    struct MemChunk **pp, *c;
    bytes = round_up(bytes);
    if (!bytes || bytes > mh->mh_Free) return NULL;
    for (pp = &mh->mh_First; (c = *pp) != NULL; pp = &c->mc_Next) {
        if (c->mc_Bytes >= bytes) {
            if (c->mc_Bytes == bytes) {
                *pp = c->mc_Next;
            } else {
                struct MemChunk *rest = (struct MemChunk *)((UBYTE *)c + bytes);
                rest->mc_Next = c->mc_Next;
                rest->mc_Bytes = c->mc_Bytes - bytes;
                *pp = rest;
            }
            mh->mh_Free -= bytes;
            return c;
        }
    }
    return NULL;
}

void Deallocate(struct MemHeader *mh, APTR p, ULONG bytes)
{
    struct MemChunk **pp, *c, *n = p, *prev = NULL;
    bytes = round_up(bytes);
    for (pp = &mh->mh_First; (c = *pp) != NULL && c < n; pp = &c->mc_Next) prev = c;
    n->mc_Bytes = bytes;
    n->mc_Next = c;
    *pp = n;
    if (c && (UBYTE *)n + n->mc_Bytes == (UBYTE *)c) {
        n->mc_Bytes += c->mc_Bytes;
        n->mc_Next = c->mc_Next;
    }
    if (prev && (UBYTE *)prev + prev->mc_Bytes == (UBYTE *)n) {
        prev->mc_Bytes += n->mc_Bytes;
        prev->mc_Next = n->mc_Next;
    }
    mh->mh_Free += bytes;
}

void AddHead(struct List *l, struct Node *n)
{
    n->ln_Succ = l->lh_Head;
    n->ln_Pred = (struct Node *)&l->lh_Head;
    l->lh_Head->ln_Pred = n;
    l->lh_Head = n;
}

void Remove(struct Node *n)
{
    n->ln_Pred->ln_Succ = n->ln_Succ;
    n->ln_Succ->ln_Pred = n->ln_Pred;
}
