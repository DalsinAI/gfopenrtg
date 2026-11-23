#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# The shared pipe's screen against a pretend host (stage M2): built 32-bit with VG_HOST, under AddressSanitizer
# and UBSan, as the libraries' PC tests are. Needs gcc-multilib.
#   tests/pipe/test_screen.sh
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT="$HERE/../.."
OUT=${OUT:-$ROOT/build/tests}
mkdir -p "$OUT"
cc -m32 -std=gnu99 -DVG_HOST -I"$ROOT/tests/host-include" -I"$ROOT/include" -g -O1 -Wall -Wextra -Werror -Wno-unused-parameter \
    -fsanitize=address,undefined -fno-sanitize-recover=undefined -o "$OUT/test_pipe_screen" \
    "$HERE/test_screen.c" "$ROOT/library/pipe/pipe_screen.c"
ASAN_OPTIONS=detect_leaks=1 "$OUT/test_pipe_screen"
