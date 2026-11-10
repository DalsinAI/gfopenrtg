#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# Warp3D.library's integer float conversions against the float code they
# replace (tests/test_w3d_conv.c), 32-bit with SSE floats.
#   tests/run_w3d_conv.sh [count]
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT="$HERE/.."
OUT=${OUT:-$ROOT/build/tests}
mkdir -p "$OUT"
cc -m32 -msse2 -mfpmath=sse -std=gnu99 -I"$HERE/host-include" -I"$ROOT/include" -O1 -Wall -Wextra -Werror \
    -o "$OUT/test_w3d_conv" "$HERE/test_w3d_conv.c" "$ROOT/library/warp3d/w3d_conv.c"
"$OUT/test_w3d_conv" ${1:-2000000}
