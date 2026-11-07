/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * The Vulkan back end's choice of device (host/vulkan/ogpu_vk_pick.h), on
 * the device lists the Team's machines give: no Vulkan needed. */
#include <stdio.h>
#include <string.h>
#include "../host/vulkan/ogpu_vk_pick.h"

#define RX460 "AMD Radeon RX 460 Graphics (RADV POLARIS11)"
#define HD7750 "AMD Radeon HD 7700 Series (RADV VERDE)"
#define VEGA8 "AMD Radeon Vega 8 Graphics (RADV RAVEN2)"
#define LLVMPIPE "llvmpipe (LLVM 20.1.8, 256 bits)"

static int fails;

static void check(const char *what, const struct ogpu_vk_cand *c, int n, const char *want, int expect, const char *why_has)
{
    char why[512];
    int got = ogpu_vk_pick(c, n, want, why, sizeof why);
    if (got != expect || (why_has && !strstr(why, why_has))) {
        printf("FAIL %s: took %d (%s), wanted %d with \"%s\"\n", what, got, why, expect, why_has ? why_has : "");
        fails++;
    } else
        printf("ok   %s: %s\n", what, why);
}

int main(void)
{
    /* amigachromebuild and origimagic: llvmpipe listed first, then the Radeon */
    const struct ogpu_vk_cand build[] = { { LLVMPIPE, OGPU_VK_TYPE_CPU, 1 }, { HD7750, OGPU_VK_TYPE_DISCRETE, 1 } };
    const struct ogpu_vk_cand apu[] = { { LLVMPIPE, OGPU_VK_TYPE_CPU, 1 }, { VEGA8, OGPU_VK_TYPE_INTEGRATED, 1 } };
    /* daletop: the RX 460, and llvmpipe after it */
    const struct ogpu_vk_cand dale[] = { { RX460, OGPU_VK_TYPE_DISCRETE, 1 }, { LLVMPIPE, OGPU_VK_TYPE_CPU, 1 } };
    /* a discrete card and an APU: the card */
    const struct ogpu_vk_cand both[] = { { VEGA8, OGPU_VK_TYPE_INTEGRATED, 1 }, { LLVMPIPE, OGPU_VK_TYPE_CPU, 1 },
                                         { RX460, OGPU_VK_TYPE_DISCRETE, 1 } };
    /* a virtual GPU (a VM's virtio-gpu, venus) before llvmpipe; an "other" one before a CPU too */
    const struct ogpu_vk_cand vm[] = { { LLVMPIPE, OGPU_VK_TYPE_CPU, 1 }, { "Virtio-GPU Venus", OGPU_VK_TYPE_VIRTUAL, 1 } };
    const struct ogpu_vk_cand other[] = { { LLVMPIPE, OGPU_VK_TYPE_CPU, 1 }, { "odd", OGPU_VK_TYPE_OTHER, 1 } };
    const struct ogpu_vk_cand vother[] = { { "odd", OGPU_VK_TYPE_OTHER, 1 }, { "Virtio-GPU Venus", OGPU_VK_TYPE_VIRTUAL, 1 } };
    /* nothing but software: taken, and said */
    const struct ogpu_vk_cand soft[] = { { LLVMPIPE, OGPU_VK_TYPE_CPU, 1 } };
    /* a GPU with no compute queue isn't taken */
    const struct ogpu_vk_cand nocomp[] = { { RX460, OGPU_VK_TYPE_DISCRETE, 0 }, { LLVMPIPE, OGPU_VK_TYPE_CPU, 1 } };
    const struct ogpu_vk_cand none[] = { { RX460, OGPU_VK_TYPE_DISCRETE, 0 } };
    /* two of a kind: the first */
    const struct ogpu_vk_cand two[] = { { RX460, OGPU_VK_TYPE_DISCRETE, 1 }, { HD7750, OGPU_VK_TYPE_DISCRETE, 1 } };

    check("amigachromebuild", build, 2, NULL, 1, "(discrete GPU): the best kind listed; over " LLVMPIPE " (CPU)");
    check("origimagic", apu, 2, NULL, 1, "(integrated GPU): the best kind listed");
    check("daletop", dale, 2, NULL, 0, RX460 " (discrete GPU)");
    check("discrete over integrated", both, 3, NULL, 2, "over " VEGA8 " (integrated GPU), " LLVMPIPE " (CPU)");
    check("integrated over virtual", (const struct ogpu_vk_cand[]){ { "Virtio-GPU Venus", OGPU_VK_TYPE_VIRTUAL, 1 },
                                                                    { VEGA8, OGPU_VK_TYPE_INTEGRATED, 1 } }, 2, NULL, 1, VEGA8);
    check("virtual over CPU", vm, 2, NULL, 1, "virtual GPU");
    check("other over CPU", other, 2, NULL, 1, "other device");
    check("virtual over other", vother, 2, NULL, 1, "virtual GPU");
    check("only software", soft, 1, NULL, 0, "no GPU with compute is listed");
    check("no compute queue", nocomp, 2, NULL, 1, "no GPU with compute is listed");
    check("nothing with compute", none, 1, NULL, -1, "no Vulkan device with compute");
    check("first of a kind", two, 2, NULL, 0, RX460);
    /* OGPU_VK_DEVICE (or the config's device) still decides */
    check("named llvmpipe", build, 2, "llvmpipe", 0, "named by \"llvmpipe\"");
    check("named RADV", both, 3, "RADV", 2, "named by \"RADV\"");
    check("named, none such", build, 2, "NVIDIA", -1, "named like \"NVIDIA\"");
    check("empty name: by kind", build, 2, "", 1, "the best kind listed");
    {   /* a reason that doesn't fit is cut, not overrun */
        char small[24];
        int got = ogpu_vk_pick(both, 3, NULL, small, sizeof small);
        if (got != 2 || strlen(small) >= sizeof small) { printf("FAIL small buffer: %d %s\n", got, small); fails++; }
        else printf("ok   small buffer: %s\n", small);
    }
    printf(fails ? "test_vk_pick: %d failed\n" : "test_vk_pick: all passed\n", fails);
    return fails != 0;
}
