/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 * A SignalSemaphore as exec's behaves for this test: one owner task at a
 * time, nesting for its owner, others wait. */
#ifndef HOST_EXEC_SEMAPHORES_H
#define HOST_EXEC_SEMAPHORES_H
#include <pthread.h>
#include <exec/types.h>
struct SignalSemaphore {
    pthread_mutex_t m;
    pthread_cond_t c;
    pthread_t owner;
    int nest;
};
#endif
