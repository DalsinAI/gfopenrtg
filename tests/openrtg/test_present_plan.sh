#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# ORTG_Present's plan (library/present_plan.h, gfopenrtg.library 0.30): routes, and the prepared present's command
# word for word as MiniGL's and Warp3D's own presents. Also part of tests/run.sh.
#   tests/openrtg/test_present_plan.sh
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
OUT=${OUT:-$ROOT/build/tests}
mkdir -p "$OUT"
cc -std=c99 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=undefined \
    -o "$OUT/test_present_plan" "$ROOT/tests/test_present_plan.c"
"$OUT/test_present_plan"
