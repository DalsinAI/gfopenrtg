/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * The device the Vulkan back end takes on this machine, and why: one
 * ogpu_vk_create with video RAM of its own, as the runtime makes it.
 * Exit 0 with a device, 2 with none (no Vulkan device with compute). */
#include <stdio.h>
#include <string.h>
#include "../../host/vulkan/ogpu_vk.h"

int main(void)
{
    struct ogpu_vk_config cfg;
    struct ogpu_vk *vk;
    char err[600];
    memset(&cfg, 0, sizeof cfg);
    cfg.vram_base = 0x40000000u;
    cfg.vram_size = 1u << 16;
    if (!(vk = ogpu_vk_create(&cfg, err, sizeof err))) {
        printf("vk_pick_here: no device: %s\n", err);
        return 2;
    }
    printf("vk_pick_here: %s\nvk_pick_here: why: %s\n", ogpu_vk_device(vk), ogpu_vk_device_why(vk));
    ogpu_vk_destroy(vk);
    return 0;
}
