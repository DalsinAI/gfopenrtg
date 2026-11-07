/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * Which Vulkan device the back end takes (ogpu_vk.c's pick_device), apart
 * from Vulkan so tests/test_vk_pick.c can check it on any machine.
 *
 * Only a device with a compute queue is taken. A name asked for (the
 * config's device, else OGPU_VK_DEVICE) takes the first device whose name
 * contains it, a better kind before a worse one if several do. Otherwise the
 * kind decides: a discrete GPU, then an integrated one (an APU such as
 * origimagic's Vega 8), then a virtual one, then any other, and a CPU device
 * (llvmpipe, lavapipe, SwiftShader) only when nothing else is listed:
 * amigachromebuild and origimagic list llvmpipe beside their Radeon. Of two
 * of one kind, the first listed. `why` says which was taken and why, for the
 * runtime's log (ogpu_vk_device_why). */
#ifndef OGPU_VK_PICK_H
#define OGPU_VK_PICK_H

#include <stdio.h>
#include <string.h>

/* VkPhysicalDeviceType's values */
#define OGPU_VK_TYPE_OTHER      0
#define OGPU_VK_TYPE_INTEGRATED 1
#define OGPU_VK_TYPE_DISCRETE   2
#define OGPU_VK_TYPE_VIRTUAL    3
#define OGPU_VK_TYPE_CPU        4

struct ogpu_vk_cand {
    const char *name;
    int type;           /* OGPU_VK_TYPE_ (VkPhysicalDeviceType) */
    int compute;        /* it has a queue family with compute */
};

static const char *ogpu_vk_type_name(int type)
{
    switch (type) {
    case OGPU_VK_TYPE_DISCRETE: return "discrete GPU";
    case OGPU_VK_TYPE_INTEGRATED: return "integrated GPU";
    case OGPU_VK_TYPE_VIRTUAL: return "virtual GPU";
    case OGPU_VK_TYPE_CPU: return "CPU device, drawing in software";
    default: return "other device";
    }
}

/* Higher is better; a CPU device last. */
static int ogpu_vk_rank(int type)
{
    switch (type) {
    case OGPU_VK_TYPE_DISCRETE: return 4;
    case OGPU_VK_TYPE_INTEGRATED: return 3;
    case OGPU_VK_TYPE_VIRTUAL: return 2;
    case OGPU_VK_TYPE_CPU: return 0;
    default: return 1;
    }
}

/* The index in c[0..n) of the device to take, or -1 (none with compute, or
 * none by the name asked for); why gets the reason either way. */
static int ogpu_vk_pick(const struct ogpu_vk_cand *c, int n, const char *want, char *why, int whylen)
{
    int i, best = -1, others = 0, skipped = 0;
    size_t len;
    for (i = 0; i < n; i++) {
        if (!c[i].compute) { skipped++; continue; }
        if (want && *want && !strstr(c[i].name, want)) continue;
        if (best < 0 || ogpu_vk_rank(c[i].type) > ogpu_vk_rank(c[best].type)) best = i;
    }
    if (best < 0) {
        if (want && *want) snprintf(why, (size_t)whylen, "no Vulkan device with compute is named like \"%s\"", want);
        else snprintf(why, (size_t)whylen, "no Vulkan device with compute (%d listed, %d without)", n, skipped);
        return -1;
    }
    if (want && *want)
        snprintf(why, (size_t)whylen, "%s (%s): named by \"%s\"", c[best].name, ogpu_vk_type_name(c[best].type), want);
    else if (c[best].type == OGPU_VK_TYPE_CPU)
        snprintf(why, (size_t)whylen, "%s (%s): no GPU with compute is listed", c[best].name, ogpu_vk_type_name(c[best].type));
    else
        snprintf(why, (size_t)whylen, "%s (%s): the best kind listed", c[best].name, ogpu_vk_type_name(c[best].type));
    /* and what it was taken over */
    for (i = 0; i < n; i++) {
        if (i == best || !c[i].compute) continue;
        len = strlen(why);
        if ((int)len + 8 >= whylen) break;
        snprintf(why + len, (size_t)whylen - len, "%s%s (%s)", others++ ? ", " : "; over ", c[i].name,
                 c[i].type == OGPU_VK_TYPE_CPU ? "CPU" : ogpu_vk_type_name(c[i].type));
    }
    return best;
}

#endif
