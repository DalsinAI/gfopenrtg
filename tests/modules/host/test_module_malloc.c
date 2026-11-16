/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * library/modules/common/module_malloc.c on the host, against exec_shim.c.
 * Built twice by tests/modules/test_module_malloc.py:
 *   -DLIBNIX_FREE  free is libnix's (as the modules had it): it keeps the
 *                  lock when the pointer is not its own;
 *   (plain)        free is module_malloc.c's.
 *
 * test "audio": what SDL2.module saw under Amoebax. The main task frees a
 * pointer that isn't the module's (allowed: let pass), then an "audio
 * thread" mallocs. libnix's free leaves the lock held by the main task, so
 * the audio thread waits for ever (exec_shim says HANG, exit 3).
 * test "blocks": allocations of many sizes, freed in another order; each
 * keeps its size long (libnix's realloc reads it); a block all free again
 * goes back; module_malloc_end leaves nothing. */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define malloc mm_malloc
#define free mm_free
#include "../../../library/modules/common/module_malloc.c"
#undef malloc
#undef free

#ifdef LIBNIX_FREE
/* libnix's free, as SDL2.module's disassembles (lab, 16 Nov 2026): the
 * search's two ways out when no block holds the pointer skip the release. */
static void nix_free(void *ptr)
{
    struct MemHeader *mh;
    ULONG *a;
    if (!ptr) return;
    a = (ULONG *)ptr - 1;
    ObtainSemaphore(&malloc_lock);
    for (mh = (struct MemHeader *)malloc_blocks.mlh_Head; mh->mh_Node.ln_Succ;
         mh = (struct MemHeader *)mh->mh_Node.ln_Succ) {
        if ((APTR)a >= mh->mh_Lower && (APTR)a < mh->mh_Upper) {
            Deallocate(mh, a, *a);
            ReleaseSemaphore(&malloc_lock);
            return;
        }
    }
    /* not found: returns with the lock still held */
}
#define FREE nix_free
#else
#define FREE mm_free
#endif

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static int blocks(void)
{
    int n = 0;
    struct MinNode *m;
    for (m = malloc_blocks.mlh_Head; m->mln_Succ; m = m->mln_Succ) n++;
    return n;
}

static void *audio_thread(void *arg)
{
    void *p = mm_malloc(4096);      /* music_pcm_getaudio's SDL_stack_alloc */
    (void)arg;
    if (p) FREE(p);
    return p;
}

static int test_audio(void)
{
    pthread_t t;
    void *got = NULL;
    char foreign[64];               /* the program's memory, not the module's */
    void *mine = mm_malloc(100);
    CHECK(mine != NULL);
    FREE(foreign + 8);              /* let pass */
    FREE(mine);
    pthread_create(&t, NULL, audio_thread, NULL);
    pthread_join(t, &got);
    CHECK(got != NULL);
    printf("audio: the audio thread's malloc came back\n");
    return 0;
}

static int test_blocks(void)
{
    enum { N = 600 };
    static void *p[N];
    static size_t sz[N];
    int i;
    for (i = 0; i < N; i++) {
        sz[i] = (size_t)((i * 7919) % 3000);
        p[i] = mm_malloc(sz[i]);
        CHECK(p[i] != NULL);
        if (!p[i]) return 1;
        CHECK(((ULONG *)p[i])[-1] == (((ULONG)sz[i] + 3) & ~3UL) + sizeof(ULONG));
        memset(p[i], i & 0xff, sz[i]);
    }
    CHECK(blocks() > 1);
    for (i = 0; i < N; i += 2) mm_free(p[i]);
    for (i = 1; i < N; i += 2) {
        size_t k;
        for (k = 0; k < sz[i]; k++) {
            if (((unsigned char *)p[i])[k] != (unsigned char)(i & 0xff)) { CHECK(0); break; }
        }
    }
    for (i = N - 1; i > 0; i -= 2) mm_free(p[i]);
    CHECK(blocks() == 0);           /* every block went back when all free */
    p[0] = mm_malloc(20000);        /* bigger than a block: one of its own */
    CHECK(p[0] != NULL);
    CHECK(blocks() == 1);
    mm_free(p[0]);
    CHECK(blocks() == 0);
    p[0] = mm_malloc(10);
    module_malloc_end();
    CHECK(blocks() == 0);
    CHECK(mm_malloc((size_t)-1) == NULL);
    printf("blocks: %d checks failed\n", fails);
    return 0;
}

int main(int argc, char **argv)
{
    module_malloc_init();
    if (argc > 1 && !strcmp(argv[1], "audio")) test_audio();
    else test_blocks();
    if (fails) return 1;
    printf("OK\n");
    return 0;
}
