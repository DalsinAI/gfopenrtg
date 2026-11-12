#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# Warp3D.library's virgl route against the CPU core, on the PC: the route
# built 32-bit (W3D_HOST) as the child of minigl_vg_bridge (64-bit), which
# carries its request blocks to virglrenderer. ACVIRGL_LIB names
# libvirglrenderer.so.1.
#   tests/run_w3d_vg.sh [rounds]
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT="$HERE/.."
OUT=${OUT:-$ROOT/build/tests}
W="$ROOT/library/warp3d"
O="$ROOT/library/opengpu"
mkdir -p "$OUT"
if [ -z "${ACVIRGL_LIB:-}" ]; then echo "no ACVIRGL_LIB; Warp3D's virgl route is not run"; exit 0; fi
cc -m32 -std=gnu99 -DW3D_HOST -I"$HERE/host-include" -I"$ROOT/include" -g -O1 -Wall -Wextra -Werror -Wno-unused-parameter \
    -o "$OUT/test_w3d_vg" "$HERE/test_w3d_vg.c" "$HERE/scenes3d.c" "$W/w3d_vg.c" "$W/w3d_vg_shader.c" \
    "$O/ogpu_core.c" "$O/ogpu_dwt.c" "$O/ogpu_3d.c" "$O/ogpu_build.c" "$O/ogpu_build3d.c" -lm
cc -std=gnu11 -O1 -g -Wall -Wextra -Werror -o "$OUT/minigl_vg_bridge" "$HERE/minigl_vg_bridge.c" "$ROOT/host/virgl/acvirgl.c" -ldl
"$OUT/minigl_vg_bridge" "$OUT/test_w3d_vg" ${1:-300} -o "$OUT"
