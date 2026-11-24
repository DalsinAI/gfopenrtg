#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# The shared pipe's resources, blocks and arenas against a pretend host (stage M3): built 32-bit with VG_HOST, under
# AddressSanitizer and UBSan, as the pipe's other tests. Needs gcc-multilib.
#   tests/pipe/test_res.sh
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT="$HERE/../.."
OUT=${OUT:-$ROOT/build/tests}
mkdir -p "$OUT"
P="$ROOT/library/pipe"
cc -m32 -std=gnu99 -DVG_HOST -I"$ROOT/tests/host-include" -I"$ROOT/include" -g -O1 -Wall -Wextra -Werror -Wno-unused-parameter \
    -fsanitize=address,undefined -fno-sanitize-recover=undefined -o "$OUT/test_pipe_res" \
    "$HERE/test_res.c" "$P/pipe_screen.c" "$P/pipe_block.c" "$P/pipe_res.c" "$P/pipe_arena.c" "$P/pipe_state.c" "$P/pipe_frame.c" "$P/pipe_ff.c"
ASAN_OPTIONS=detect_leaks=1 "$OUT/test_pipe_res"
