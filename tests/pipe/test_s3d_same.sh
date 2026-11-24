#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# The shared pipe's stream3d emitter against the builder the front ends had (stage M7, part 1): the same calls to
# both, the streams compared word for word. Built 32-bit under AddressSanitizer and UBSan, as the pipe's other
# tests. Needs gcc-multilib.
#   tests/pipe/test_s3d_same.sh
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT="$HERE/../.."
OUT=${OUT:-$ROOT/build/tests}
mkdir -p "$OUT"
O="$ROOT/library/opengpu"
cc -m32 -std=gnu99 -I"$ROOT/tests/host-include" -I"$ROOT/include" -g -O1 -Wall -Wextra -Werror -Wno-unused-parameter \
    -fsanitize=address,undefined -fno-sanitize-recover=undefined -o "$OUT/test_pipe_s3d_same" \
    "$HERE/test_s3d_same.c" "$ROOT/library/pipe/pipe_emit_s3d.c" "$O/ogpu_build.c" "$O/ogpu_build3d.c"
ASAN_OPTIONS=detect_leaks=1 "$OUT/test_pipe_s3d_same"
