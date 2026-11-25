#!/bin/sh
# TRIANGLES on the GPU's own graphics pipeline (host/vulkan/ogpu3d_gfx.c),
# built and run on its own, as tests/run.sh does, for a kitchen run
# (repo_test) or a card: the bench scenes within tolerance of the core,
# against the exact picture, random scenes, and routing by cost.
# OGPU_VK_DEVICE picks a device; ROUNDS the random scenes (default 300).
# A shared machine gets a minute for setup and two for a submit: this checks
# pictures, not latency.
set -e
HERE=$(cd "$(dirname "$0")/.." && pwd)
OGPU=$HERE/../library/opengpu
OUT=${OUT:-$(mktemp -d)}
CC=${CC:-cc}
SAN="-fsanitize=address,undefined -fno-sanitize-recover=undefined"
[ -n "$NO_SAN" ] && SAN=
$CC -std=c99 -O1 -g -Wall -Wextra -Werror $SAN -DOGPU_WITH_3D \
    -o "$OUT/test_ogpu3d_gfx" "$HERE/test_ogpu3d_gfx.c" "$HERE/scenes3d.c" "$HERE/../host/vulkan/ogpu_vk.c" \
    "$OGPU/ogpu_core.c" "$OGPU/ogpu_dwt.c" "$OGPU/ogpu_3d.c" "$OGPU/ogpu_build.c" "$OGPU/ogpu_build3d.c" -lvulkan -lm
export OGPU_VK_SETUP_MS=${OGPU_VK_SETUP_MS:-60000} OGPU_VK_WAIT_MS=${OGPU_VK_WAIT_MS:-120000} ASAN_OPTIONS=detect_leaks=0
"$OUT/test_ogpu3d_gfx" "${ROUNDS:-300}"
# and with the 2D commands on the core (OGPU_VK_2D=core, as on a V3D)
OGPU_VK_2D=core "$OUT/test_ogpu3d_gfx" 50
