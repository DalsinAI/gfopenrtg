/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * A module's malloc and free, in place of libnix's (module_rt.c includes
 * this file; tests/modules/test_module_malloc.py builds it on the host).
 *
 * libnix's free() takes its memory lock and, when the pointer is in none of
 * its blocks of memory, returns without giving the lock back. In a program
 * of one task that does no harm (the lock nests), but a module's memory is
 * used by every task of the program that opened it: SDL2.module's SDL_free
 * is called by the program's main task and its malloc by SDL's audio
 * thread. Once the main task has freed one pointer that wasn't the module's
 * (a free that libnix lets pass), the next malloc in the audio thread waits
 * for ever, with SDL's audio lock held; the main task's next Mix_LockAudio
 * (Mix_HaltMusic, Mix_PlayMusic) then waits for the audio thread: Amoebax
 * stops at "Ready?" (lab t3-wbgap-i32, 16 Nov 2026).
 *
 * The same layout as libnix's, so its realloc and calloc work on top: each
 * allocation is a long holding its size (with that long) and then the
 * caller's bytes, carved with exec's Allocate from blocks of at least
 * MODULE_MALLOC_BLOCK bytes, each a MemHeader on this module's list. A free
 * of memory that isn't in a block is let pass, as libnix does, and every
 * way out of malloc and free gives the lock back. A block whose memory is
 * all free again goes back to exec.
 *
 * module_malloc_init before anything allocates, module_malloc_end after the
 * last free (both from module_rt.c's open and close). */
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/lists.h>
#include <exec/semaphores.h>
#include <proto/exec.h>
#include <stddef.h>

#define MODULE_MALLOC_BLOCK 16384UL     /* libnix's own step */
#define MODULE_MALLOC_ROUND 4096UL

static struct SignalSemaphore malloc_lock;
static struct MinList malloc_blocks;        /* struct MemHeader */

void module_malloc_init(void)
{
    InitSemaphore(&malloc_lock);
    malloc_blocks.mlh_Head = (struct MinNode *)&malloc_blocks.mlh_Tail;
    malloc_blocks.mlh_Tail = NULL;
    malloc_blocks.mlh_TailPred = (struct MinNode *)&malloc_blocks.mlh_Head;
}

static ULONG block_size(const struct MemHeader *mh)
{
    /* AllocMem'd a whole number of MODULE_MALLOC_ROUND; mh_Upper may stop
     * short of it by less than a MemChunk */
    ULONG n = (ULONG)((const UBYTE *)mh->mh_Upper - (const UBYTE *)mh);
    return (n + MODULE_MALLOC_ROUND - 1) & ~(MODULE_MALLOC_ROUND - 1);
}

/* Every block back to exec: the module is closing, nothing is in use. */
void module_malloc_end(void)
{
    struct MemHeader *mh;
    while ((mh = (struct MemHeader *)malloc_blocks.mlh_Head)->mh_Node.ln_Succ) {
        Remove(&mh->mh_Node);
        FreeMem(mh, block_size(mh));
    }
}

void *malloc(size_t size)
{
    struct MemHeader *mh;
    ULONG need, bytes;
    ULONG *a = NULL;

    if ((long)size < 0) {
        return NULL;
    }
    need = ((ULONG)size + 3) & ~3UL;
    need += sizeof(ULONG);
    ObtainSemaphore(&malloc_lock);
    for (mh = (struct MemHeader *)malloc_blocks.mlh_Head; mh->mh_Node.ln_Succ;
         mh = (struct MemHeader *)mh->mh_Node.ln_Succ) {
        if ((a = Allocate(mh, need)) != NULL) {
            break;
        }
    }
    if (!a) {
        bytes = need + sizeof(struct MemHeader) + sizeof(struct MemChunk);
        if (bytes < MODULE_MALLOC_BLOCK) {
            bytes = MODULE_MALLOC_BLOCK;
        }
        bytes = (bytes + MODULE_MALLOC_ROUND - 1) & ~(MODULE_MALLOC_ROUND - 1);
        mh = AllocMem(bytes, MEMF_ANY);
        if (mh) {
            /* the first chunk on a MemChunk boundary, its size a whole
             * number of them (both already so on the Amiga) */
            ULONG start = ((ULONG)sizeof(struct MemHeader) + sizeof(struct MemChunk) - 1)
                          & ~((ULONG)sizeof(struct MemChunk) - 1);
            struct MemChunk *mc = (struct MemChunk *)((UBYTE *)mh + start);
            mh->mh_Node.ln_Type = NT_MEMORY;
            mh->mh_Node.ln_Pri = 0;
            mh->mh_Node.ln_Name = NULL;
            mh->mh_Attributes = 0;
            mh->mh_First = mc;
            mh->mh_Lower = mc;
            mh->mh_Upper = (UBYTE *)mh + start + ((bytes - start) & ~((ULONG)sizeof(struct MemChunk) - 1));
            mh->mh_Free = (bytes - start) & ~((ULONG)sizeof(struct MemChunk) - 1);
            mc->mc_Next = NULL;
            mc->mc_Bytes = mh->mh_Free;
            AddHead((struct List *)&malloc_blocks, &mh->mh_Node);
            a = Allocate(mh, need);
        }
    }
    if (a) {
        *a++ = need;
    }
    ReleaseSemaphore(&malloc_lock);
    return a;
}

void free(void *ptr)
{
    struct MemHeader *mh;
    ULONG *a;

    if (!ptr) {
        return;
    }
    a = (ULONG *)ptr - 1;
    ObtainSemaphore(&malloc_lock);
    for (mh = (struct MemHeader *)malloc_blocks.mlh_Head; mh->mh_Node.ln_Succ;
         mh = (struct MemHeader *)mh->mh_Node.ln_Succ) {
        if ((APTR)a >= mh->mh_Lower && (APTR)a < mh->mh_Upper) {
            Deallocate(mh, a, *a);
            if (mh->mh_Free == (ULONG)((UBYTE *)mh->mh_Upper - (UBYTE *)mh->mh_Lower)) {
                Remove(&mh->mh_Node);
                FreeMem(mh, block_size(mh));
            }
            break;
        }
    }
    /* not in a block: not this module's memory, let pass (as libnix) */
    ReleaseSemaphore(&malloc_lock);
}
