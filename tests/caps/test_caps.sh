#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# OGPU_Query's capabilities against a pretend host (stage MQ): built 32-bit with VG_HOST, under AddressSanitizer
# and UBSan, as the pipe's tests are. Needs gcc-multilib.
#   tests/caps/test_caps.sh
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT="$HERE/../.."
OUT=${OUT:-$ROOT/build/tests}
mkdir -p "$OUT"
cc -m32 -std=gnu99 -DVG_HOST -I"$ROOT/tests/host-include" -I"$ROOT/include" -g -O1 -Wall -Wextra -Werror -Wno-unused-parameter \
    -fsanitize=address,undefined -fno-sanitize-recover=undefined -o "$OUT/test_caps" \
    "$HERE/test_caps.c" "$ROOT/library/opengpu/ogpu_caps.c"
ASAN_OPTIONS=detect_leaks=1 "$OUT/test_caps"
