#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# Stage M5's differential test: the pipe's one fixed-function generator against MiniGL's and Warp3D's, text for text.
#   tests/pipe/test_ff.sh [random MiniGL vertex keys, default 200000]
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT="$HERE/../.."
OUT=${OUT:-$ROOT/build/tests}
mkdir -p "$OUT"
P="$ROOT/library/pipe"
cc -m32 -std=gnu99 -DMGL_HOST -DVG_HOST -DW3D_HOST -I"$ROOT/tests/host-include" -I"$ROOT/include" -g -O1 -Wall -Wextra -Werror \
    -Wno-unused-parameter -Wno-unused-function -fsanitize=address,undefined -fno-sanitize-recover=undefined \
    -o "$OUT/test_pipe_ff" "$HERE/test_ff.c" "$ROOT/library/minigl/mgl_vg_shader.c" "$HERE/ref/w3d_vg_shader.c" \
    "$P/pipe_screen.c" "$P/pipe_block.c" "$P/pipe_res.c" "$P/pipe_arena.c" "$P/pipe_state.c" "$P/pipe_ff.c" "$P/pipe_xport_host.c"
ASAN_OPTIONS=detect_leaks=1 "$OUT/test_pipe_ff" ${1:-}
