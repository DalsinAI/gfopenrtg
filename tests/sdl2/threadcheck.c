/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * threadcheck: SDL 2's threads on AmigaOS 3, one line each.
 *
 *   stack     the stack a thread gets: 256 KB when SDL is given no size
 *             (SDL_CreateThread without SDL_HINT_THREAD_STACK_SIZE, and
 *             SDL's own audio thread), the hint's size when it is set, and
 *             a thread that uses 128 KB of it
 *   join      SDL_WaitThread from a task other than the one that started
 *             the thread: while that task still runs, after it has ended,
 *             and after the thread itself has ended
 *   detach    threads detached before and after they end
 *   many      64 threads alive at once, then joined
 *   audio     the audio device opened in one thread and closed from main()
 *
 * A wait that does not end within 10 seconds is reported as FAIL (and the
 * program then goes on waiting: a thread SDL still runs can't be left).
 * Run on an Amiga with SDL2.module (threadcheck.m), or linked statically
 * (threadcheck). Arguments name the groups to run (stack join detach many
 * audio); none runs them all. Exit 0 when every check passes.
 * SDL_AUDIODRIVER=paula runs the audio checks on Paula. */
#include "SDL.h"
#include <stdio.h>
#include <string.h>
#include <proto/exec.h>
#include <exec/tasks.h>

static int failures = 0;
static int group_argc;
static char **group_argv;

/* Whether the group was asked for (all of them when none was named). */
static int wanted(const char *group)
{
    int i;
    if (group_argc < 2) {
        return 1;
    }
    for (i = 1; i < group_argc; i++) {
        if (strcmp(group_argv[i], group) == 0) {
            return 1;
        }
    }
    return 0;
}

static void report(const char *what, int ok, const char *detail)
{
    printf("%-14s %s  %s\n", what, ok ? "pass" : "FAIL", detail);
    fflush(stdout);
    if (!ok) {
        failures++;
    }
}

/* The calling task's stack, in bytes, as Exec set it up. */
static unsigned long stack_size(void)
{
    struct Task *me = FindTask(NULL);
    return (unsigned long)me->tc_SPUpper - (unsigned long)me->tc_SPLower;
}

/* ---- stack ---- */

static int SDLCALL stack_fn(void *data)
{
    *(unsigned long *)data = stack_size();
    return 0;
}

/* 128 KB of the stack, every page written and read back. */
static int SDLCALL stack_use_fn(void *data)
{
    volatile unsigned char big[128 * 1024];
    unsigned long sum = 0;
    int i;
    for (i = 0; i < (int)sizeof(big); i += 64) {
        big[i] = (unsigned char)i;
    }
    for (i = 0; i < (int)sizeof(big); i += 64) {
        sum += big[i];
    }
    *(unsigned long *)data = sum;
    return 0;
}

static unsigned long audio_stack;
static SDL_atomic_t callbacks;

static void SDLCALL audio_cb(void *userdata, Uint8 *stream, int len)
{
    SDL_memset(stream, 0, len);
    if (SDL_AtomicAdd(&callbacks, 1) == 0) {
        audio_stack = stack_size();
    }
}

static int open_audio(SDL_AudioDeviceID *dev)
{
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = 22050;
    want.format = AUDIO_S16MSB;
    want.channels = 2;
    want.samples = 1024;
    want.callback = audio_cb;
    *dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (*dev == 0) {
        return -1;
    }
    SDL_PauseAudioDevice(*dev, 0);
    return 0;
}

/* ---- join ---- */

/* The watchdog: started by main() and joined by main(), so it works on any
 * module; it says FAIL when the wait it watches has not ended in time. */
typedef struct {
    const char *what;
    SDL_atomic_t done;
    SDL_atomic_t timed_out;
} Watch;

static int SDLCALL watch_fn(void *data)
{
    Watch *w = (Watch *)data;
    int i;
    for (i = 0; i < 1000 && !SDL_AtomicGet(&w->done); i++) {
        SDL_Delay(10);
    }
    if (!SDL_AtomicGet(&w->done)) {
        SDL_AtomicSet(&w->timed_out, 1);
        report(w->what, 0, "the wait did not end in 10 s (still waiting)");
    }
    return 0;
}

static SDL_Thread *watch_start(Watch *w, const char *what)
{
    w->what = what;
    SDL_AtomicSet(&w->done, 0);
    SDL_AtomicSet(&w->timed_out, 0);
    return SDL_CreateThread(watch_fn, "watch", w);
}

static int watch_end(Watch *w, SDL_Thread *t)
{
    SDL_AtomicSet(&w->done, 1);
    SDL_WaitThread(t, NULL);
    return !SDL_AtomicGet(&w->timed_out);
}

static int SDLCALL child_fn(void *data)
{
    SDL_Delay((Uint32)(uintptr_t)data);
    return 11;
}

/* Starts a child and hands it back; stays alive until released when
 * keep is set. */
typedef struct {
    SDL_Thread *child;
    Uint32 child_ms;
    int keep;
    SDL_sem *started;
    SDL_sem *release;
} Starter;

static int SDLCALL starter_fn(void *data)
{
    Starter *s = (Starter *)data;
    s->child = SDL_CreateThread(child_fn, "child", (void *)(uintptr_t)s->child_ms);
    SDL_SemPost(s->started);
    if (s->keep) {
        SDL_SemWait(s->release);
    }
    return 5;
}

static void check_join(const char *what, Uint32 child_ms, int keep, Uint32 wait_after_ms)
{
    Starter s;
    SDL_Thread *starter, *watch;
    Watch w;
    char line[160];
    int rc = -1, starter_rc = -1, ok;

    SDL_zero(s);
    s.child_ms = child_ms;
    s.keep = keep;
    s.started = SDL_CreateSemaphore(0);
    s.release = SDL_CreateSemaphore(0);
    starter = SDL_CreateThread(starter_fn, "starter", &s);
    SDL_SemWait(s.started);
    if (!keep) {
        SDL_WaitThread(starter, &starter_rc);  /* main() started it */
        starter = NULL;
    }
    if (!s.child) {
        report(what, 0, SDL_GetError());
        return;
    }
    SDL_Delay(wait_after_ms);

    watch = watch_start(&w, what);
    SDL_WaitThread(s.child, &rc);
    ok = watch_end(&w, watch);

    if (keep) {
        SDL_SemPost(s.release);
        SDL_WaitThread(starter, &starter_rc);
    }
    SDL_snprintf(line, sizeof(line),
                 "main() waited for a thread another thread started (%s); it returned %d",
                 keep ? "that thread still running" : "that thread already ended", rc);
    if (ok) {
        report(what, rc == 11 && starter_rc == 5, line);
    }
    SDL_DestroySemaphore(s.started);
    SDL_DestroySemaphore(s.release);
}

/* ---- detach ---- */

static SDL_atomic_t detached_ran;

static int SDLCALL detached_fn(void *data)
{
    SDL_Delay((Uint32)(uintptr_t)data);
    SDL_AtomicAdd(&detached_ran, 1);
    return 0;
}

/* ---- many ---- */

#define MANY 64
static SDL_sem *many_go;

static int SDLCALL many_fn(void *data)
{
    SDL_SemWait(many_go);
    return (int)(uintptr_t)data;
}

/* ---- audio ---- */

static int SDLCALL audio_open_fn(void *data)
{
    return open_audio((SDL_AudioDeviceID *)data);
}

int main(int argc, char *argv[])
{
    char line[200];
    unsigned long got = 0;
    int i, rc;

    group_argc = argc;
    group_argv = argv;
    if (SDL_Init(SDL_INIT_TIMER | SDL_INIT_AUDIO) < 0) {
        printf("SDL_Init: %s\n", SDL_GetError());
        return 20;
    }
    printf("threadcheck: SDL %d.%d.%d, audio driver %s, main() stack %lu\n",
           SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL,
           SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "(none)",
           stack_size());

    /* ---- stack ---- */
    if (wanted("stack")) {
        SDL_Thread *t = SDL_CreateThread(stack_fn, "stack", &got);
        SDL_WaitThread(t, NULL);
        SDL_snprintf(line, sizeof(line), "no hint: the thread has %lu bytes (256 KB wanted)", got);
        report("stack-default", got >= 256 * 1024, line);
    }
    if (!wanted("stack")) {
        /* not asked for */
    } else if (got >= 256 * 1024) {
        unsigned long sum = 0, want = 0;
        SDL_Thread *t = SDL_CreateThread(stack_use_fn, "stackuse", &sum);
        SDL_WaitThread(t, NULL);
        for (i = 0; i < 128 * 1024; i += 64) {
            want += (unsigned char)i;
        }
        SDL_snprintf(line, sizeof(line), "a thread used 128 KB of its stack (sum %lu)", sum);
        report("stack-use", sum == want, line);
    } else {
        report("stack-use", 0, "not run: the stack is too small to use 128 KB of it");
    }
    if (wanted("stack")) {
        SDL_SetHint(SDL_HINT_THREAD_STACK_SIZE, "65536");
        SDL_Thread *t = SDL_CreateThread(stack_fn, "stack", &got);
        SDL_WaitThread(t, NULL);
        SDL_SetHint(SDL_HINT_THREAD_STACK_SIZE, NULL);
        SDL_snprintf(line, sizeof(line), "SDL_THREAD_STACK_SIZE 65536: the thread has %lu bytes", got);
        report("stack-hint", got >= 65536 && got < 128 * 1024, line);
    }
    if (wanted("stack")) {
        SDL_AudioDeviceID dev;
        if (open_audio(&dev) < 0) {
            report("stack-audio", 0, SDL_GetError());
        } else {
            for (i = 0; i < 200 && SDL_AtomicGet(&callbacks) == 0; i++) {
                SDL_Delay(10);
            }
            SDL_CloseAudioDevice(dev);
            SDL_snprintf(line, sizeof(line), "SDL's audio thread has %lu bytes (%d callbacks)",
                         audio_stack, SDL_AtomicGet(&callbacks));
            report("stack-audio", audio_stack >= 256 * 1024, line);
        }
    }

    /* ---- join ---- */
    if (wanted("join")) {
        check_join("join-live", 500, 1, 0);
        check_join("join-gone", 500, 0, 0);
        check_join("join-ended", 50, 0, 500);
    }

    /* ---- detach ---- */
    if (wanted("detach")) {
        SDL_Thread *late[8];
        Uint32 t0;
        SDL_AtomicSet(&detached_ran, 0);
        for (i = 0; i < 8; i++) {
            SDL_DetachThread(SDL_CreateThread(detached_fn, "early", (void *)(uintptr_t)100));
            late[i] = SDL_CreateThread(detached_fn, "late", (void *)(uintptr_t)0);
        }
        SDL_Delay(300);  /* the late ones have ended: detached afterwards */
        for (i = 0; i < 8; i++) {
            SDL_DetachThread(late[i]);
        }
        t0 = SDL_GetTicks();
        while (SDL_AtomicGet(&detached_ran) < 16 && SDL_GetTicks() - t0 < 5000) {
            SDL_Delay(10);
        }
        SDL_Delay(100);
        SDL_snprintf(line, sizeof(line), "%d of 16 detached threads ran (8 detached before ending, 8 after)",
                     SDL_AtomicGet(&detached_ran));
        report("detach", SDL_AtomicGet(&detached_ran) == 16, line);
    }

    /* ---- many ---- */
    if (wanted("many")) {
        SDL_Thread *t[MANY];
        int made = 0, sum = 0, want = 0;
        many_go = SDL_CreateSemaphore(0);
        for (i = 0; i < MANY; i++) {
            t[i] = SDL_CreateThread(many_fn, "many", (void *)(uintptr_t)(i + 1));
            if (t[i]) {
                made++;
            }
        }
        for (i = 0; i < MANY; i++) {
            SDL_SemPost(many_go);
        }
        for (i = 0; i < MANY; i++) {
            if (t[i]) {
                SDL_WaitThread(t[i], &rc);
                sum += rc;
                want += i + 1;
            }
        }
        SDL_DestroySemaphore(many_go);
        SDL_snprintf(line, sizeof(line), "%d of %d threads alive at once, all joined (%s)",
                     made, MANY, made == MANY ? "every one started" : SDL_GetError());
        report("many", made == MANY && sum == want, line);
    }

    /* ---- audio ---- */
    if (wanted("audio")) {
        SDL_AudioDeviceID dev = 0;
        SDL_Thread *opener, *watch;
        Watch w;
        int before;
        SDL_AtomicSet(&callbacks, 0);
        opener = SDL_CreateThread(audio_open_fn, "opener", &dev);
        SDL_WaitThread(opener, &rc);  /* the thread that opened it has ended */
        if (rc < 0) {
            report("audio-close", 0, SDL_GetError());
        } else {
            SDL_Delay(1000);
            before = SDL_AtomicGet(&callbacks);
            watch = watch_start(&w, "audio-close");
            SDL_CloseAudioDevice(dev);
            if (watch_end(&w, watch)) {
                SDL_snprintf(line, sizeof(line),
                             "opened in a thread (%s, %d callbacks in 1 s), closed from main()",
                             SDL_GetCurrentAudioDriver(), before);
                report("audio-close", before > 0, line);
            }
        }
    }

    SDL_Quit();
    printf("threadcheck: %s\n", failures ? "FAILED" : "all passed");
    return failures ? 10 : 0;
}
