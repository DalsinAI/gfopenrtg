#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# The Vulkan back end's choice of device (host/vulkan/ogpu_vk_pick.h):
# tests/test_vk_pick.c on the Team's machines' device lists (no Vulkan
# needed), then, where Vulkan's headers and loader are, the back end built
# as the runtime builds it (OGPU_WITH_3D) and asked on this machine which
# device it takes and why (vk_pick_here.c). It must not be a CPU device
# where a GPU is listed (vulkaninfo, when it is there, says what is).
#   sh tests/vulkan/test_vk_pick.sh
set -eu
here=$(cd "$(dirname "$0")" && pwd)
top=$here/../..
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
cc -std=c99 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=undefined \
    -o "$tmp/test_vk_pick" "$top/tests/test_vk_pick.c"
"$tmp/test_vk_pick"
# Vulkan's loader by its path (no libvulkan-dev needed) and its headers: VULKAN_INCLUDE,
# the system's, or those amicradle's scripts/vulkan_headers.sh unpacked into the build cache.
lib=$(ldconfig -p 2>/dev/null | awk '/libvulkan\.so\.1 /{ print $NF; exit }')
inc=
for d in "${VULKAN_INCLUDE:-}" /usr/include "${AC_BUILD_CACHE:-$HOME/.cache/amicradle}"/vulkan-headers-mesa-*/include; do
    if [ -n "$d" ] && [ -e "$d/vulkan/vulkan.h" ]; then inc=$d; break; fi
done
if [ -z "$lib" ] || [ -z "$inc" ]; then
    echo "no Vulkan loader (${lib:-none}) or headers (${inc:-none}): the run on this machine is skipped"
    exit 0
fi
echo "vulkan: headers in $inc, loader $lib"
ogpu=$top/library/opengpu
cc -std=c99 -O1 -g -Wall -Wextra -Werror -DOGPU_WITH_3D -idirafter "$inc" -o "$tmp/vk_pick_here" "$here/vk_pick_here.c" \
    "$top/host/vulkan/ogpu_vk.c" "$ogpu/ogpu_core.c" "$ogpu/ogpu_dwt.c" "$ogpu/ogpu_3d.c" "$lib" -lm
rc=0
"$tmp/vk_pick_here" | tee "$tmp/out" || rc=$?
[ "$rc" = 0 ] || { [ "$rc" = 2 ] && echo "no Vulkan device with compute here"; exit 0; }
if command -v vulkaninfo >/dev/null; then
    vulkaninfo --summary 2>/dev/null | grep -E "deviceName|deviceType" || true
    if vulkaninfo --summary 2>/dev/null | grep -q -E "PHYSICAL_DEVICE_TYPE_(DISCRETE|INTEGRATED)_GPU" \
       && grep -q "CPU device" "$tmp/out"; then
        echo "FAIL: a GPU is listed but the back end took a CPU device"
        exit 1
    fi
fi
echo "test_vk_pick.sh: passed"
