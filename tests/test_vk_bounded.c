/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * The Vulkan back end's bounded waits (host/vulkan/ogpu_vk.h, 7 Nov 2026):
 * with a setup deadline too short for any device, ogpu_vk_create gives up
 * at once with "the GPU didn't answer", before the caller's video RAM is
 * touched, and leaves the device to finish and be freed on its own thread;
 * with the usual deadline it starts and draws. Skips when there is no
 * Vulkan device. */
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../host/vulkan/ogpu_vk.h"

#define VBASE 0x40000000u
#define VSIZE (1u << 20)

static int fails;
static ogpu_u8 *mem;

static ogpu_u8 *map(void *user, ogpu_u32 a, ogpu_u32 len) {
    (void)user;
    return a >= VBASE && a - VBASE <= VSIZE && len <= VSIZE - (a - VBASE) ? mem + (a - VBASE) : 0;
}
static void fence(void *user, ogpu_u32 id) { (void)user; (void)id; }

static double ms_now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

static struct ogpu_vk *make(const char *setup_ms, char *err, int errlen, double *took) {
    struct ogpu_vk_config cfg;
    double t0 = ms_now();
    struct ogpu_vk *v;
    memset(&cfg, 0, sizeof cfg);
    cfg.vram_base = VBASE;
    cfg.vram_size = VSIZE;
    cfg.arena_size = 1u << 20;
    cfg.map = map;
    cfg.fence = fence;
    cfg.host_vram = mem;
    cfg.route = OGPU_VK_ROUTE_GPU;
    if (setup_ms) setenv("OGPU_VK_SETUP_MS", setup_ms, 1); else unsetenv("OGPU_VK_SETUP_MS");
    v = ogpu_vk_create(&cfg, err, errlen);
    *took = ms_now() - t0;
    return v;
}

int main(void) {
    char err[200];
    double took;
    struct ogpu_vk *v;
    int i, gave_up = 0;
    void *h = 0;
    if (posix_memalign(&h, 65536, VSIZE)) { printf("FAIL no memory\n"); return 1; }
    mem = h;
    memset(mem, 0, VSIZE);
    mem[VSIZE - 1] = 0x5A;
    /* the usual deadline: it starts (or there is no device at all) */
    v = make(NULL, err, sizeof err, &took);
    if (!v) {
        int none = !strcmp(err, "vkCreateInstance (VkResult -9)") || !strcmp(err, "no Vulkan device (VkResult 0)")
                   || !strncmp(err, "no device with compute", 22);
        printf("vk bounded: %s (%s)\n", none ? "no device; skipped" : "FAIL to start", err);
        return none ? 0 : 1;
    }
    if (ogpu_vk_lost(v)) { printf("FAIL lost after a normal start\n"); fails++; }
    else printf("ok   starts within the usual deadline (%.0f ms), mark kept %d\n", took, mem[VSIZE - 1] == 0x5A);
    ogpu_vk_destroy(v);
    /* a deadline no device meets: it gives up in about that time, says why,
     * and the caller's video RAM is as it was. A device quick enough to
     * answer within it anyway starts; a few tries, one must give up. */
    for (i = 0; i < 5 && !gave_up; i++) {
        mem[VSIZE - 1] = 0x5A;
        v = make("0.000001", err, sizeof err, &took);
        if (v) { ogpu_vk_destroy(v); continue; }
        gave_up = 1;
        if (!strstr(err, "didn't answer")) { printf("FAIL gave up, but said: %s\n", err); fails++; }
        else if (took > 1500) { printf("FAIL gave up after %.0f ms\n", took); fails++; }
        else if (mem[VSIZE - 1] != 0x5A) { printf("FAIL video RAM changed\n"); fails++; }
        else printf("ok   a device that doesn't answer in time: given up after %.1f ms (%s)\n", took, err);
    }
    if (!gave_up) printf("ok   the device answered within a microsecond five times: nothing to give up (not tested)\n");
    /* and the next start, with its abandoned predecessor perhaps still being freed, is a normal one */
    v = make(NULL, err, sizeof err, &took);
    if (!v || ogpu_vk_lost(v)) { printf("FAIL to start again: %s\n", v ? "lost" : err); fails++; }
    else printf("ok   starts again afterwards (%.0f ms)\n", took);
    if (v) ogpu_vk_destroy(v);
    {   /* the abandoned device's thread frees it once its work is done */
        struct timespec ts = { 0, 200000000 };
        nanosleep(&ts, NULL);
    }
    printf("vk bounded: %s\n", fails ? "FAILED" : "all passed");
    return fails ? 1 : 0;
}
