#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# ORTG_Present's RETAINED route (library/present_retained.c, gfopenrtg.library 0.31) and the plan that picks it
# (library/present_plan.h: only with GFOpenRTG's screens on). Also part of tests/run.sh.
#   tests/openrtg/test_present_retained.sh
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
OUT=${OUT:-$ROOT/build/tests}
OGPU="$ROOT/library/opengpu"
mkdir -p "$OUT"
sh "$HERE/test_present_plan.sh"
cc -std=gnu99 -O1 -g -Wall -Wextra -Werror -Wno-unused-parameter -fsanitize=address,undefined -fno-sanitize-recover=undefined \
    -I"$ROOT/tests/gla_present-include" -I"$ROOT/include" \
    -o "$OUT/test_present_retained" "$ROOT/tests/test_present_retained.c" "$ROOT/library/present_retained.c" \
    "$OGPU/ogpu_retained.c" "$OGPU/ogpu_core.c" "$OGPU/ogpu_dwt.c"
"$OUT/test_present_retained"
