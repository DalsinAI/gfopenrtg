/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * threadcheck: SDL 2's threads on AmigaOS 3, one line each.
 *
 *   stack     the stack a thread gets: 256 KB when SDL is given no size
 *             (SDL_CreateThread without SDL_HINT_THREAD_STACK_SIZE, and
 *             SDL's own audio thread), the hint's size when it is set, and
 *             a thread that uses 128 KB of it
 *
 * Run on an Amiga with SDL2.module (threadcheck.m), or linked statically
 * (threadcheck). Arguments name the groups to run (stack); none runs
 * them all. Exit 0 when every check passes. SDL_AUDIODRIVER=paula runs the
 * audio check on Paula. */
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

int main(int argc, char *argv[])
{
    char line[200];
    unsigned long got = 0;
    int i;

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

    SDL_Quit();
    printf("threadcheck: %s\n", failures ? "FAILED" : "all passed");
    return failures ? 10 : 0;
}
