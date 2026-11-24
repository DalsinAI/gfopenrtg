#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# The GL module's virgl transport on OS 3.2 (library/modules/gl/gla/os3/gla_virgl_os3.c) on a fake gfopengpu.library,
# by itself (tests/run.sh runs it among the rest): which driver a program gets, ENV:GFOpenGPU/GLBatch, and a block
# run through the transport (sent, and its fence waited for).
#   tests/gl/test_gla_virgl_os3.sh
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
T="$HERE/.."
OUT=${OUT:-$T/../build/tests}
mkdir -p "$OUT"
cc -std=gnu99 -O1 -g -Wall -Wextra -Werror -Wno-unused-parameter -fsanitize=address,undefined -fno-sanitize-recover=undefined \
    -I"$T/gla_virgl-include" -I"$T/gla_present-include" -I"$T/../include" -I"$T/../library/modules/gl" \
    -o "$OUT/test_gla_virgl_os3" "$T/test_gla_virgl_os3.c" "$T/../library/modules/gl/gla/os3/gla_virgl_os3.c" \
    "$T/../library/opengpu/ogpu_build.c"
"$OUT/test_gla_virgl_os3"
